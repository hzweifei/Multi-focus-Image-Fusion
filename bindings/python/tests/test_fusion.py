"""Python 接口回归测试，可直接运行或由 unittest 发现。

运行前需准备可导入的 mif 扩展和 NumPy。覆盖独立配准与融合的配置边界、
精度、内存归属、切片输入、组合等价性和异常；算法质量由 C++ 测试进一步验证。
"""
import gc
import os
from pathlib import Path
import unittest

# 仅用于构建目录测试：由测试环境传入运行库目录，并保留 Windows 目录句柄。
_dll_handles = []
if os.name == "nt":
    for folder in os.environ.get("MIF_TEST_DLL_DIRS", "").split(os.pathsep):
        if folder and Path(folder).is_dir():
            _dll_handles.append(os.add_dll_directory(folder))

import numpy as np
import mif

ALL_METHODS = (mif.GuidedFilterFusionOptions, mif.LaplacianPyramidFusionOptions,
               mif.BlockVarianceFusionOptions, mif.DtcwtFusionOptions, mif.GfgFgfFusionOptions)


def alignment_texture():
    """只用 NumPy 生成可复现的纹理和几何图案，测试不额外依赖 Python OpenCV。"""
    rng = np.random.default_rng(20260928)
    image = rng.integers(20, 235, size=(240, 320)).astype(np.float32)
    # 轻度平滑抑制像素噪声，同时留下 SIFT 和 ECC 都能利用的局部纹理。
    image = (4 * image + np.roll(image, 1, 0) + np.roll(image, -1, 0)
             + np.roll(image, 1, 1) + np.roll(image, -1, 1)) / 8
    yy, xx = np.indices(image.shape)
    for x, y, radius, value in ((60, 60, 18, 245), (235, 55, 25, 30), (170, 175, 30, 220)):
        image[(xx - x) ** 2 + (yy - y) ** 2 <= radius ** 2] = value
    image[160:178, 30:95] = 35
    image[95:115, 210:290] = 240
    return image.astype(np.uint8)


class FusionTests(unittest.TestCase):
    """检查 Python 包装层与 C++ 扩展之间的数据约定。"""

    def test_float_endpoints(self):
        """[0, 1] 两端均为合法浮点输入，恒定图像融合后应保持不变。"""
        for value in (0.0, 1.0):
            image = np.full((5, 7, 3), value, np.float32)
            np.testing.assert_allclose(mif.fuse([image, image]), image, atol=1e-6)

    def test_precision_and_ownership(self):
        """检查各精度、通道和算法组合，并确认返回数组不与输入共享可写存储。"""
        for dtype in (np.uint8, np.uint16, np.float32):
            for shape in ((23, 31), (23, 31, 3)):
                image = np.random.default_rng(42).random(shape).astype(np.float32)
                if dtype != np.float32:
                    image = (image * np.iinfo(dtype).max).astype(dtype)
                for method in ALL_METHODS:
                    options = method()
                    original = image.copy()
                    result = mif.fuse([image, image], options)
                    # 主动回收临时 Python 对象，返回数组仍应能读取和写入。
                    gc.collect()
                    self.assertEqual(result.dtype, dtype)
                    np.testing.assert_allclose(result, image, atol=2e-6 if dtype == np.float32 else 1, rtol=0)
                    result[:] = 0
                    np.testing.assert_array_equal(image, original)

    def test_strided_readonly(self):
        """包装层应接受非连续的只读切片，并在进入核心之前整理内存布局。"""
        image = np.arange(40 * 60, dtype=np.uint16).reshape(40, 60)[:, ::2]
        image.flags.writeable = False
        np.testing.assert_allclose(mif.fuse([image, image]), image, atol=1, rtol=0)

    def test_new_method_options_and_lifetime(self):
        """具体参数直接传给统一入口，错误参数转为 ValueError，结果独立持有存储。"""
        cases = ((mif.BlockVarianceFusionOptions, "block_size", 8, 4, 1),
                 (mif.DtcwtFusionOptions, "max_levels", 4, 2, 0),
                 (mif.GfgFgfFusionOptions, "guided_radius", 5, 3, 0),
                 (mif.GfgFgfFusionOptions, "guided_subsample_factor", 4, 2, 0))
        image = np.arange(21 * 31, dtype=np.uint16).reshape(21, 31) * 53
        for option_type, attribute, default, changed, invalid in cases:
            with self.subTest(method=option_type, field=attribute):
                options = option_type()
                self.assertIsInstance(options, mif.FusionOptionsBase)
                self.assertEqual(getattr(options, attribute), default)
                setattr(options, attribute, changed)
                result = mif.fuse([image, image], options)
                setattr(options, attribute, invalid)
                with self.assertRaises(ValueError):
                    mif.fuse([image, image], options)
                del options
                gc.collect()
                np.testing.assert_allclose(result, image, atol=1, rtol=0)

    def test_gfgfgf_subsample_options(self):
        """快速滤波倍数完整传入核心；默认保留全部帧，合法端点可处理平坦输入。"""
        options = mif.GfgFgfFusionOptions()
        self.assertEqual(options.selection_ratio, 0)
        self.assertEqual(options.guided_subsample_factor, 4)
        images = [np.full((23, 35), value, np.uint16) for value in (10000, 50000)]
        for factor in (1, 4, 16):
            options.guided_subsample_factor = factor
            self.assertEqual(options.guided_subsample_factor, factor)
            np.testing.assert_allclose(mif.fuse(images, options), 30000, atol=1, rtol=0)
        for factor in (0, 17):
            options.guided_subsample_factor = factor
            with self.assertRaises(ValueError):
                mif.fuse(images, options)

    def test_all_method_diagnostics_and_pipeline(self):
        """五种方法都能经过独立入口与组合入口；DTCWT 明确返回空诊断。"""
        images = [np.full((23, 35), value, np.uint16) for value in (10000, 50000)]
        for method in ALL_METHODS:
            with self.subTest(method=method):
                options = method()
                options.include_weight_maps = True
                direct = mif.fuse_detailed(images, options)
                combined = mif.register_and_fuse(images, fusion_options=options)
                gc.collect()
                np.testing.assert_allclose(direct["image"], 30000, atol=1, rtol=0)
                np.testing.assert_array_equal(combined["image"], direct["image"])
                self.assertEqual(combined["crop_region"], (0, 0, 35, 23))
                if method == mif.DtcwtFusionOptions:
                    self.assertIsNone(direct["source_index_map"])
                    self.assertIsNone(combined["source_index_map"])
                    self.assertEqual(direct["weight_maps"], [])
                else:
                    self.assertEqual(direct["source_index_map"].dtype, np.int32)
                    self.assertEqual(direct["source_index_map"].shape, images[0].shape)
                    self.assertEqual(len(direct["weight_maps"]), len(images))
                    np.testing.assert_allclose(sum(direct["weight_maps"]), 1, atol=1e-6)
                    for weight in direct["weight_maps"]:
                        self.assertTrue(np.isfinite(weight).all() and (weight >= 0).all())

    def test_guided_epsilon_numerical_range(self):
        """官方 float32 实现的正则项上下限应通过 Python 异常和最小值结果可见。"""
        image = np.full((25, 37), 0.5, np.float32)
        for option_type, attribute in (
                (mif.GuidedFilterFusionOptions, "base_epsilon"),
                (mif.GuidedFilterFusionOptions, "detail_epsilon"),
                (mif.LaplacianPyramidFusionOptions, "detail_epsilon"),
                (mif.GfgFgfFusionOptions, "guided_epsilon")):
            options = option_type()
            for value in (1e-100, 1e-8, 1e100):
                setattr(options, attribute, value)
                with self.assertRaises(ValueError):
                    mif.fuse([image, image], options)
            setattr(options, attribute, 1e-6)
            np.testing.assert_allclose(mif.fuse([image, image], options), image, atol=2e-5, rtol=0)

    def test_diagnostics(self):
        """检查诊断结果字段、索引精度和归一化权重；无纹理平局应等权平均。"""
        options = mif.GuidedFilterFusionOptions()
        options.include_weight_maps = True
        result = mif.fuse_detailed([np.full((21, 33), 10000, np.uint16), np.full((21, 33), 50000, np.uint16)], options)
        gc.collect()
        self.assertEqual(set(result), {"image", "source_index_map", "weight_maps"})
        self.assertEqual(result["source_index_map"].dtype, np.int32)
        np.testing.assert_allclose(sum(result["weight_maps"]), 1, atol=1e-6)
        np.testing.assert_allclose(result["image"], 30000, atol=1)

    def test_fusion_option_defaults_and_isolation(self):
        """方法各自持有参数，修改一个对象不会改变其他方法或同类的新实例。"""
        guided = mif.GuidedFilterFusionOptions()
        pyramid = mif.LaplacianPyramidFusionOptions()
        for config in (guided, pyramid):
            self.assertIsInstance(config, mif.FusionOptionsBase)
            self.assertFalse(config.include_weight_maps)
            self.assertEqual(config.focus.measure, mif.FocusMeasure.MODIFIED_LAPLACIAN)
            self.assertEqual(config.focus.window_size, 9)
            self.assertEqual(config.detail_radius, 3)
            self.assertEqual(config.detail_epsilon, 0.0001)
        self.assertEqual(mif.FocusMeasureOptions().window_size, 9)
        self.assertEqual(guided.base_radius, 15)
        self.assertEqual(guided.base_epsilon, 0.01)
        self.assertEqual(pyramid.max_levels, 5)
        self.assertFalse(hasattr(pyramid, "base_radius"))
        self.assertFalse(hasattr(guided, "max_levels"))
        for config in (guided, pyramid):
            for removed in ("method", "guided_filter", "laplacian_pyramid", "keep_weight_maps"):
                self.assertFalse(hasattr(config, removed))
                with self.assertRaises(AttributeError):
                    setattr(config, removed, 1)
            with self.assertRaises(TypeError):
                config.focus = mif.BlockVarianceFusionOptions()
        guided.focus.window_size = 7
        guided.focus.measure = mif.FocusMeasure.TENENGRAD
        guided.detail_radius = 4
        self.assertEqual(pyramid.focus.window_size, 9)
        self.assertEqual(pyramid.focus.measure, mif.FocusMeasure.MODIFIED_LAPLACIAN)
        self.assertEqual(pyramid.detail_radius, 3)
        pyramid.focus.window_size = 11
        pyramid.detail_radius = 6
        self.assertEqual(guided.focus.window_size, 7)
        self.assertEqual(guided.detail_radius, 4)
        self.assertEqual(mif.GuidedFilterFusionOptions().focus.window_size, 9)

    def test_nested_option_value_assignment_and_lifetime(self):
        """focus 读取是内部引用，整体赋值复制数值，子对象能保留父配置的生命周期。"""
        for option_type in (mif.GuidedFilterFusionOptions, mif.LaplacianPyramidFusionOptions):
            with self.subTest(method=option_type):
                options = option_type()
                focus = options.focus
                replacement = mif.FocusMeasureOptions()
                replacement.window_size = 5
                options.focus = replacement
                self.assertEqual(focus.window_size, 5)
                replacement.window_size = 13
                self.assertEqual(options.focus.window_size, 5)
                focus.window_size = 11
                self.assertEqual(options.focus.window_size, 11)
                self.assertEqual(replacement.window_size, 13)
                del options, replacement
                gc.collect()
                # 仅保留子对象时，其父配置必须继续有效，不能访问已经释放的存储。
                focus.window_size = 7
                retained = option_type()
                retained.focus = focus
                focus.window_size = 3
                self.assertEqual(retained.focus.window_size, 7)
                self.assertEqual(focus.window_size, 3)

    def test_nested_fusion_settings_affect_results(self):
        """真实融合确认嵌套编辑和具体参数快照进入计算，而不是修改临时副本。"""
        random = np.random.default_rng(54)
        images = [random.random((53, 71), dtype=np.float32) for _ in range(2)]
        for option_type in (mif.GuidedFilterFusionOptions, mif.LaplacianPyramidFusionOptions):
            with self.subTest(method=option_type):
                options = option_type()
                baseline = mif.fuse_detailed(images, options)
                options.focus.measure = mif.FocusMeasure.TENENGRAD
                options.focus.window_size = 1
                options.detail_radius = 5
                options.detail_epsilon = 0.02
                if option_type is mif.GuidedFilterFusionOptions:
                    options.base_radius = 7
                    options.base_epsilon = 0.03
                else:
                    options.max_levels = 2
                changed = mif.fuse_detailed(images, options)
                self.assertTrue(np.any(changed["source_index_map"] != baseline["source_index_map"]))
                self.assertGreater(float(np.max(np.abs(changed["image"] - baseline["image"]))), 1e-5)
                copied = option_type()
                copied.focus = options.focus
                copied.detail_radius = options.detail_radius
                copied.detail_epsilon = options.detail_epsilon
                if option_type is mif.GuidedFilterFusionOptions:
                    copied.base_radius = options.base_radius
                    copied.base_epsilon = options.base_epsilon
                else:
                    copied.max_levels = options.max_levels
                np.testing.assert_array_equal(mif.fuse(images, copied), changed["image"])

    def test_only_selected_fusion_configuration_is_validated(self):
        """其他参数对象中的非法设置不影响当前方法，传入该对象时才执行其校验。"""
        random = np.random.default_rng(61)
        images = [random.random((29, 37), dtype=np.float32) for _ in range(2)]
        for active_type, inactive_type in (
                (mif.GuidedFilterFusionOptions, mif.LaplacianPyramidFusionOptions),
                (mif.LaplacianPyramidFusionOptions, mif.GuidedFilterFusionOptions)):
            with self.subTest(active=active_type):
                options = active_type()
                expected = mif.fuse(images, options)
                invalid = inactive_type()
                invalid.focus.window_size = 2
                invalid.detail_radius = 0
                invalid.detail_epsilon = np.nan
                if inactive_type is mif.GuidedFilterFusionOptions:
                    invalid.base_radius = 0
                    invalid.base_epsilon = np.nan
                else:
                    invalid.max_levels = 0
                np.testing.assert_array_equal(mif.fuse(images, options), expected)
                np.testing.assert_array_equal(mif.register_and_fuse(images, fusion_options=options)["image"], expected)
                with self.assertRaises(ValueError):
                    mif.fuse(images, invalid)
                with self.assertRaises(ValueError):
                    mif.register_and_fuse(images, fusion_options=invalid)
                np.testing.assert_array_equal(mif.fuse(images, options), expected)

    def test_registration_options(self):
        """配准参数由类型选择方法，ECC 和 SIFT 各自公开有关的数值设置。"""
        ecc = mif.EccRegistrationOptions()
        sift = mif.SiftRegistrationOptions()
        for options in (mif.NoRegistrationOptions(), ecc, sift):
            self.assertIsInstance(options, mif.RegistrationOptionsBase)
            self.assertEqual(options.max_working_dimension, 1200)
            options.max_working_dimension = 600
            self.assertEqual(options.max_working_dimension, 600)
        self.assertEqual(ecc.motion_model, mif.MotionModel.TRANSLATION)
        for model in (mif.MotionModel.TRANSLATION, mif.MotionModel.AFFINE, mif.MotionModel.HOMOGRAPHY):
            ecc.motion_model = model
            self.assertEqual(ecc.motion_model, model)
        for options, fields in (
                (ecc, (("max_iterations", 150, 200), ("convergence_tolerance", 1e-5, 1e-6))),
                (sift, (("max_features", 4000, 5000), ("match_ratio_threshold", 0.75, 0.8),
                        ("ransac_reprojection_threshold", 3.0, 2.5), ("min_inlier_ratio", 0.25, 0.5)))):
            for field, default, changed in fields:
                self.assertEqual(getattr(options, field), default)
                setattr(options, field, changed)
                self.assertEqual(getattr(options, field), changed)

    def test_independent_option_types(self):
        """旧聚合配置明确移除，参数类型和枚举类型在 Python 边界严格区分。"""
        fusion = mif.GuidedFilterFusionOptions()
        registration = mif.EccRegistrationOptions()
        image = np.full((20, 30), 100, np.uint8)
        for old_type in ("Alignment", "FusionMethod", "RegistrationMethod", "FusionOptions",
                         "RegistrationOptions", "FocusOptions", "DctOptions", "GfgfgfOptions"):
            self.assertFalse(hasattr(mif, old_type))
            self.assertFalse(hasattr(mif._mif, old_type))
        for base in (mif.FusionOptionsBase, mif.RegistrationOptionsBase):
            with self.assertRaises(TypeError):
                base()
        for value in (mif.FocusMeasure.TENENGRAD, 99):
            with self.assertRaises(TypeError):
                registration.motion_model = value
        with self.assertRaises(TypeError):
            fusion.focus.measure = mif.MotionModel.TRANSLATION
        with self.assertRaises(ValueError):
            mif.MotionModel(99)
        for options, forbidden in (
                (fusion, ("method", "alignment", "keep_weight_maps", "max_working_dimension")),
                (registration, ("method", "epsilon", "iterations", "max_size", "max_features", "focus")),
                (mif.SiftRegistrationOptions(), ("motion_model", "max_iterations", "match_ratio"))):
            for field in forbidden:
                self.assertFalse(hasattr(options, field))
                with self.assertRaises(AttributeError):
                    setattr(options, field, 1)
        with self.assertRaises(TypeError):
            mif.fuse([image, image], registration)
        with self.assertRaises(TypeError):
            mif.register_images([image, image], fusion)
        with self.assertRaises(TypeError):
            mif.register_and_fuse([image, image], fusion, registration)

    def test_registration_none_ownership(self):
        """NONE 也生成独立图像，保留全部 dtype/通道，并支持非连续只读切片。"""
        for dtype in (np.uint8, np.uint16, np.float32):
            for shape in ((23, 62), (23, 62, 3)):
                with self.subTest(dtype=dtype, shape=shape):
                    backing = np.random.default_rng(42).random(shape).astype(np.float32)
                    if dtype != np.float32:
                        backing = (backing * np.iinfo(dtype).max).astype(dtype)
                    image = backing[:, ::2]
                    image.flags.writeable = False
                    original = image.copy()
                    result = mif.register_images([image, image])
                    self.assertEqual(set(result), {"images", "crop_region", "transforms"})
                    self.assertEqual(result["crop_region"], (0, 0, 31, 23))
                    aligned = result["images"]
                    transforms = result["transforms"]
                    del result
                    gc.collect()
                    for output in aligned:
                        self.assertEqual(output.dtype, dtype)
                        self.assertFalse(np.shares_memory(output, image))
                        np.testing.assert_array_equal(output, original)
                    self.assertFalse(np.shares_memory(aligned[0], aligned[1]))
                    for transform in transforms:
                        self.assertEqual(transform.dtype, np.float32)
                        np.testing.assert_array_equal(transform, np.eye(2, 3))
                    aligned[0][:] = 0
                    transforms[0][:] = 0
                    np.testing.assert_array_equal(aligned[1], original)
                    np.testing.assert_array_equal(image, original)
                    np.testing.assert_array_equal(transforms[1], np.eye(2, 3))

    def test_homography_diagnostics_and_ownership(self):
        """两种单应性独立返回配准图和 3×3 变换，支持只读切片并可继续纯融合。"""
        for method in (mif.SiftRegistrationOptions, mif.EccRegistrationOptions):
            with self.subTest(method=method):
                backing = np.repeat(alignment_texture(), 2, axis=1)
                image = backing[:, ::2]
                images = [image, image]
                originals = [source.copy() for source in images]
                for source in images:
                    source.flags.writeable = False
                options = method()
                if method is mif.EccRegistrationOptions:
                    options.motion_model = mif.MotionModel.HOMOGRAPHY
                result = mif.register_images(images, options)
                aligned = result["images"]
                transforms = result["transforms"]
                x, y, width, height = result["crop_region"]
                # 清除字典、参数和临时对象，验证各返回数组自行维持所需的底层存储。
                del result, options
                gc.collect()
                for output in aligned:
                    self.assertEqual(output.shape, (height, width))
                    self.assertEqual(output.dtype, np.uint8)
                    np.testing.assert_allclose(output, image[y:y + height, x:x + width], atol=1)
                self.assertGreater(width * height, image.size * 0.9)
                self.assertEqual(len(transforms), 2)
                for transform in transforms:
                    self.assertEqual(transform.shape, (3, 3))
                    self.assertEqual(transform.dtype, np.float32)
                    self.assertTrue(np.isfinite(transform).all())
                    np.testing.assert_allclose(transform, np.eye(3), atol=0.01, rtol=0)
                for source, original in zip(images, originals):
                    for output in aligned:
                        self.assertFalse(np.shares_memory(source, output))
                    np.testing.assert_array_equal(source, original)
                fused = mif.fuse_detailed(aligned)
                np.testing.assert_allclose(fused["image"], image[y:y + height, x:x + width], atol=1)
                self.assertEqual(set(fused), {"image", "source_index_map", "weight_maps"})
                # 可写输出之间也不应错误复用矩阵存储，第一张变换的修改不能污染第二张。
                transforms[0][:] = 0
                np.testing.assert_allclose(transforms[1], np.eye(3), atol=0.01, rtol=0)
                second_aligned = aligned[1].copy()
                aligned[0][:] = 0
                np.testing.assert_array_equal(aligned[1], second_aligned)
                for source, original in zip(images, originals):
                    np.testing.assert_array_equal(source, original)

    def test_motion_model_belongs_only_to_ecc(self):
        """SIFT 与跳过配准的配置没有运动模型字段，各自返回约定的变换。"""
        reference = alignment_texture()
        images = [reference, np.roll(reference, (1, 2), axis=(0, 1))]
        for option_type in (mif.NoRegistrationOptions, mif.SiftRegistrationOptions):
            options = option_type()
            self.assertFalse(hasattr(options, "motion_model"))
            for model in (mif.MotionModel.TRANSLATION, mif.MotionModel.AFFINE, mif.MotionModel.HOMOGRAPHY):
                with self.assertRaises(AttributeError):
                    options.motion_model = model
            result = mif.register_images(images, options)
            if option_type is mif.NoRegistrationOptions:
                self.assertEqual(result["crop_region"], (0, 0, reference.shape[1], reference.shape[0]))
                for original, output, transform in zip(images, result["images"], result["transforms"]):
                    np.testing.assert_array_equal(output, original)
                    np.testing.assert_array_equal(transform, np.eye(2, 3))
            else:
                self.assertEqual(len(result["transforms"]), len(images))
                for transform in result["transforms"]:
                    self.assertEqual(transform.shape, (3, 3))

    def test_registration_parameter_validation(self):
        """具体配准参数由本方法校验，非法配准参数不妨碍单独调用纯融合。"""
        image = np.zeros((20, 30), np.uint8)
        cases = (
            (mif.NoRegistrationOptions, "max_working_dimension", (15, 8193)),
            (mif.EccRegistrationOptions, "max_iterations", (0, 10001)),
            (mif.EccRegistrationOptions, "convergence_tolerance", (0.0, -1.0, np.nan, np.inf)),
            (mif.EccRegistrationOptions, "max_working_dimension", (15, 8193)),
            (mif.SiftRegistrationOptions, "max_working_dimension", (15, 8193)),
            (mif.SiftRegistrationOptions, "max_features", (63, 100001)),
            (mif.SiftRegistrationOptions, "match_ratio_threshold", (0.0, 1.0, -0.1, np.nan, np.inf)),
            (mif.SiftRegistrationOptions, "ransac_reprojection_threshold", (0.0, -1.0, np.nan, np.inf)),
            (mif.SiftRegistrationOptions, "min_inlier_ratio", (0.0, -0.1, 1.1, np.nan, np.inf)),
        )
        for option_type, field, values in cases:
            for value in values:
                with self.subTest(method=option_type, field=field, value=value):
                    options = option_type()
                    setattr(options, field, value)
                    with self.assertRaises(ValueError):
                        mif.register_images([image, image], options)
                    np.testing.assert_array_equal(mif.fuse([image, image]), image)

    def test_fusion_validation_does_not_affect_registration(self):
        """融合参数错误只影响融合及组合流程，不参与单独配准的校验。"""
        image = np.full((20, 30), 100, np.uint8)
        for option_type, specific in (
                (mif.GuidedFilterFusionOptions, (("base_radius", 0), ("base_epsilon", np.nan))),
                (mif.LaplacianPyramidFusionOptions, (("max_levels", 0),))):
            for name, value in (("window_size", 2), ("detail_radius", 0), ("detail_epsilon", 0.0)) + specific:
                with self.subTest(method=option_type, field=name):
                    options = option_type()
                    setattr(options.focus if name == "window_size" else options, name, value)
                    with self.assertRaises(ValueError):
                        mif.fuse([image, image], options)
                    with self.assertRaises(ValueError):
                        mif.register_and_fuse([image, image], fusion_options=options)
                    registered = mif.register_images([image, image])
                    np.testing.assert_array_equal(registered["images"][0], image)

    def test_registration_image_validation_and_failure(self):
        """独立配准检查输入并暴露求解失败，不因拆分接口而静默接受无效图像。"""
        good = np.zeros((20, 30), np.uint8)
        for images in ([], [good], [good, good[:, :-1]], [good, good.astype(np.uint16)],
                       [good.astype(np.float64)] * 2, [np.zeros((20, 30, 4), np.uint8)] * 2,
                       [np.full((20, 30), np.nan, np.float32)] * 2,
                       [np.full((20, 30), 1.1, np.float32)] * 2):
            with self.assertRaises((ValueError, TypeError)):
                mif.register_images(images)
        for method in (mif.EccRegistrationOptions, mif.SiftRegistrationOptions):
            options = method()
            with self.assertRaises(RuntimeError):
                mif.register_images([good, good], options)

    def test_explicit_stages_match_pipeline(self):
        """真实位移输入在各精度和方法下，显式两步与组合入口具有相同结果和元数据。"""
        texture = alignment_texture()
        shifted = np.roll(texture, (1, 2), axis=(0, 1))
        configurations = (
            (mif.NoRegistrationOptions, None),
            (mif.EccRegistrationOptions, mif.MotionModel.TRANSLATION),
            (mif.EccRegistrationOptions, mif.MotionModel.AFFINE),
            (mif.EccRegistrationOptions, mif.MotionModel.HOMOGRAPHY),
            (mif.SiftRegistrationOptions, None),
        )
        for dtype in (np.uint8, np.uint16, np.float32):
            scale = 257 if dtype == np.uint16 else 1 / 255 if dtype == np.float32 else 1
            images = [(image.astype(np.float32) * scale).astype(dtype) for image in (texture, shifted)]
            for registration_type, model in configurations:
                registration_options = registration_type()
                if model is not None:
                    registration_options.motion_model = model
                # 低于原图最长边，验证共有估计分辨率配置能通过多态快照进入两个入口。
                registration_options.max_working_dimension = 240
                registered = mif.register_images(images, registration_options)
                expected_shape = (3, 3) if registration_type is mif.SiftRegistrationOptions or (
                    model == mif.MotionModel.HOMOGRAPHY) else (2, 3)
                for transform in registered["transforms"]:
                    self.assertEqual(transform.shape, expected_shape)
                for fusion_type in (mif.GuidedFilterFusionOptions, mif.LaplacianPyramidFusionOptions):
                    with self.subTest(dtype=dtype, registration=registration_type, model=model, fusion=fusion_type):
                        fusion_options = fusion_type()
                        fusion_options.include_weight_maps = True
                        explicit = mif.fuse_detailed(registered["images"], fusion_options)
                        combined = mif.register_and_fuse(images, registration_options, fusion_options)
                        self.assertEqual(set(combined), {"image", "source_index_map", "weight_maps", "crop_region", "transforms"})
                        self.assertEqual(combined["image"].dtype, dtype)
                        self.assertEqual(combined["crop_region"], registered["crop_region"])
                        np.testing.assert_array_equal(combined["image"], explicit["image"])
                        np.testing.assert_array_equal(combined["source_index_map"], explicit["source_index_map"])
                        self.assertEqual(len(combined["weight_maps"]), len(images))
                        self.assertEqual(len(combined["transforms"]), len(images))
                        for actual, expected in zip(combined["weight_maps"], explicit["weight_maps"]):
                            np.testing.assert_array_equal(actual, expected)
                        for actual, expected in zip(combined["transforms"], registered["transforms"]):
                            np.testing.assert_array_equal(actual, expected)

    def test_pipeline_parameter_validation(self):
        """组合入口按独立参数类型调用两个阶段，不能遗漏任一阶段的非法配置。"""
        image = np.zeros((20, 30), np.uint8)
        registration_options = mif.EccRegistrationOptions()
        registration_options.convergence_tolerance = np.nan
        with self.assertRaises(ValueError):
            mif.register_and_fuse([image, image], registration_options=registration_options)

    def test_validation(self):
        """非法数量、形状、精度、值域与参数应转换为明确的 Python 异常。"""
        good = np.zeros((20, 30), np.uint8)
        for images in ([], [good], [good, good[:, :-1]], [good, good.astype(np.uint16)],
                       [good.astype(np.float64)] * 2, [np.zeros((20, 30, 4), np.uint8)] * 2,
                       [np.full((20, 30), np.nan, np.float32)] * 2,
                       [np.full((20, 30), 1.1, np.float32)] * 2):
            with self.assertRaises((ValueError, TypeError)):
                mif.fuse(images)
        options = mif.GuidedFilterFusionOptions()
        options.focus.window_size = 2
        with self.assertRaises(ValueError):
            mif.fuse([good, good], options)


if __name__ == "__main__":
    unittest.main()
