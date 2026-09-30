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

ALL_METHODS = (mif.FusionMethod.GUIDED_FILTER, mif.FusionMethod.LAPLACIAN_PYRAMID,
               mif.FusionMethod.DCT, mif.FusionMethod.DTCWT, mif.FusionMethod.GFGFGF)


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
                    options = mif.FusionOptions()
                    options.method = method
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
        """新增嵌套配置以值复制赋值，借用成员保留父对象，错误参数正确转为 ValueError。"""
        cases = ((mif.FusionMethod.DCT, "dct", mif.DctOptions, "block_size", 8, 4, 1),
                 (mif.FusionMethod.DTCWT, "dtcwt", mif.DtcwtOptions, "levels", 4, 2, 0),
                 (mif.FusionMethod.GFGFGF, "gfgfgf", mif.GfgfgfOptions, "guided_radius", 5, 3, 0),
                 (mif.FusionMethod.GFGFGF, "gfgfgf", mif.GfgfgfOptions, "guided_subsample", 4, 2, 0))
        image = np.arange(21 * 31, dtype=np.uint16).reshape(21, 31) * 53
        for method, field, option_type, attribute, default, changed, invalid in cases:
            with self.subTest(method=method):
                options = mif.FusionOptions()
                member = getattr(options, field)
                self.assertEqual(getattr(member, attribute), default)
                source = option_type()
                setattr(source, attribute, changed)
                setattr(options, field, source)
                setattr(source, attribute, default)
                self.assertEqual(getattr(member, attribute), changed)
                setattr(member, attribute, invalid)
                # 未选中的非法设置不妨碍默认 GFF；切换选中后须拒绝。
                np.testing.assert_allclose(mif.fuse([image, image], options), image, atol=1, rtol=0)
                options.method = method
                with self.assertRaises(ValueError):
                    mif.fuse([image, image], options)
                del options
                gc.collect()
                setattr(member, attribute, changed)
                self.assertEqual(getattr(member, attribute), changed)

    def test_gfgfgf_subsample_options(self):
        """快速滤波倍数完整传入核心；默认保留全部帧，合法端点可处理平坦输入。"""
        options = mif.FusionOptions()
        options.method = mif.FusionMethod.GFGFGF
        self.assertEqual(options.gfgfgf.selection_ratio, 0)
        self.assertEqual(options.gfgfgf.guided_subsample, 4)
        images = [np.full((23, 35), value, np.uint16) for value in (10000, 50000)]
        for factor in (1, 4, 16):
            options.gfgfgf.guided_subsample = factor
            self.assertEqual(options.gfgfgf.guided_subsample, factor)
            np.testing.assert_allclose(mif.fuse(images, options), 30000, atol=1, rtol=0)
        for factor in (0, 17):
            options.gfgfgf.guided_subsample = factor
            with self.assertRaises(ValueError):
                mif.fuse(images, options)

    def test_all_method_diagnostics_and_pipeline(self):
        """五种方法都能经过独立入口与组合入口；DTCWT 明确返回空诊断。"""
        images = [np.full((23, 35), value, np.uint16) for value in (10000, 50000)]
        for method in ALL_METHODS:
            with self.subTest(method=method):
                options = mif.FusionOptions()
                options.method = method
                options.keep_weight_maps = True
                direct = mif.fuse_detailed(images, options)
                combined = mif.register_and_fuse(images, fusion_options=options)
                gc.collect()
                np.testing.assert_allclose(direct["image"], 30000, atol=1, rtol=0)
                np.testing.assert_array_equal(combined["image"], direct["image"])
                self.assertEqual(combined["crop"], (0, 0, 35, 23))
                if method == mif.FusionMethod.DTCWT:
                    self.assertIsNone(direct["focus_indices"])
                    self.assertIsNone(combined["focus_indices"])
                    self.assertEqual(direct["weights"], [])
                else:
                    self.assertEqual(direct["focus_indices"].dtype, np.int32)
                    self.assertEqual(direct["focus_indices"].shape, images[0].shape)
                    self.assertEqual(len(direct["weights"]), len(images))
                    np.testing.assert_allclose(sum(direct["weights"]), 1, atol=1e-6)
                    for weight in direct["weights"]:
                        self.assertTrue(np.isfinite(weight).all() and (weight >= 0).all())

    def test_guided_epsilon_numerical_range(self):
        """官方 float32 实现的正则项上下限应通过 Python 异常和最小值结果可见。"""
        image = np.full((25, 37), 0.5, np.float32)
        for method, field, attribute in (
                (mif.FusionMethod.GUIDED_FILTER, "guided_filter", "base_epsilon"),
                (mif.FusionMethod.GUIDED_FILTER, "guided_filter", "detail_epsilon"),
                (mif.FusionMethod.LAPLACIAN_PYRAMID, "laplacian_pyramid", "detail_epsilon"),
                (mif.FusionMethod.GFGFGF, "gfgfgf", "guided_epsilon")):
            options = mif.FusionOptions()
            options.method = method
            for value in (1e-100, 1e-8, 1e100):
                setattr(getattr(options, field), attribute, value)
                with self.assertRaises(ValueError):
                    mif.fuse([image, image], options)
            setattr(getattr(options, field), attribute, 1e-6)
            np.testing.assert_allclose(mif.fuse([image, image], options), image, atol=2e-5, rtol=0)

    def test_diagnostics(self):
        """检查诊断结果字段、索引精度和归一化权重；无纹理平局应等权平均。"""
        options = mif.FusionOptions()
        options.keep_weight_maps = True
        result = mif.fuse_detailed([np.full((21, 33), 10000, np.uint16), np.full((21, 33), 50000, np.uint16)], options)
        gc.collect()
        self.assertEqual(set(result), {"image", "focus_indices", "weights"})
        self.assertEqual(result["focus_indices"].dtype, np.int32)
        np.testing.assert_allclose(sum(result["weights"]), 1, atol=1e-6)
        np.testing.assert_allclose(result["image"], 30000, atol=1)

    def test_fusion_option_defaults_and_isolation(self):
        """方法各自拥有默认配置；修改与切换不会把清晰度或滤波参数串到另一方法。"""
        options = mif.FusionOptions()
        self.assertEqual(options.method, mif.FusionMethod.GUIDED_FILTER)
        self.assertFalse(options.keep_weight_maps)
        for config in (mif.GuidedFilterOptions(), mif.LaplacianPyramidOptions(),
                       options.guided_filter, options.laplacian_pyramid):
            self.assertEqual(config.focus.measure, mif.FocusMeasure.MODIFIED_LAPLACIAN)
            self.assertEqual(config.focus.window, 9)
            self.assertEqual(config.detail_radius, 3)
            self.assertEqual(config.detail_epsilon, 0.0001)
        self.assertEqual(mif.FocusOptions().window, 9)
        self.assertEqual(options.guided_filter.base_radius, 15)
        self.assertEqual(options.guided_filter.base_epsilon, 0.01)
        self.assertEqual(options.laplacian_pyramid.levels, 5)
        self.assertFalse(hasattr(options.laplacian_pyramid, "base_radius"))
        self.assertFalse(hasattr(options.guided_filter, "levels"))
        for old_field in ("focus_measure", "focus_window", "base_radius", "detail_radius",
                          "base_epsilon", "detail_epsilon", "pyramid_levels"):
            self.assertFalse(hasattr(options, old_field))
            with self.assertRaises(AttributeError):
                setattr(options, old_field, 1)
        with self.assertRaises(TypeError):
            options.guided_filter = mif.LaplacianPyramidOptions()
        with self.assertRaises(TypeError):
            options.laplacian_pyramid.focus = mif.GuidedFilterOptions()

        options.guided_filter.focus.window = 7
        options.guided_filter.focus.measure = mif.FocusMeasure.TENENGRAD
        options.guided_filter.detail_radius = 4
        options.method = mif.FusionMethod.LAPLACIAN_PYRAMID
        self.assertEqual(options.laplacian_pyramid.focus.window, 9)
        self.assertEqual(options.laplacian_pyramid.focus.measure, mif.FocusMeasure.MODIFIED_LAPLACIAN)
        self.assertEqual(options.laplacian_pyramid.detail_radius, 3)
        options.laplacian_pyramid.focus.window = 11
        options.laplacian_pyramid.detail_radius = 6
        options.method = mif.FusionMethod.GUIDED_FILTER
        self.assertEqual(options.guided_filter.focus.window, 7)
        self.assertEqual(options.guided_filter.focus.measure, mif.FocusMeasure.TENENGRAD)
        self.assertEqual(options.guided_filter.detail_radius, 4)
        self.assertEqual(mif.FusionOptions().guided_filter.focus.window, 9)

    def test_nested_option_value_assignment_and_lifetime(self):
        """嵌套读取是内部引用，整体赋值复制数值，子对象能保留父配置的生命周期。"""
        for field, option_type in (("guided_filter", mif.GuidedFilterOptions),
                                   ("laplacian_pyramid", mif.LaplacianPyramidOptions)):
            with self.subTest(field=field):
                options = mif.FusionOptions()
                nested = getattr(options, field)
                focus = nested.focus
                replacement = option_type()
                replacement.focus.window = 5
                replacement.detail_radius = 7
                setattr(options, field, replacement)
                # 替换写入原结构体，已取得的子/孙对象引用仍指向同一个内部成员。
                self.assertEqual(nested.detail_radius, 7)
                self.assertEqual(focus.window, 5)
                replacement.focus.window = 13
                replacement.detail_radius = 9
                self.assertEqual(getattr(options, field).focus.window, 5)
                self.assertEqual(nested.detail_radius, 7)
                nested.detail_radius = 4
                self.assertEqual(getattr(options, field).detail_radius, 4)
                self.assertEqual(replacement.detail_radius, 9)

                source_focus = mif.FocusOptions()
                source_focus.window = 11
                nested.focus = source_focus
                source_focus.window = 15
                self.assertEqual(focus.window, 11)
                del options, replacement, source_focus
                gc.collect()
                nested.detail_radius = 6
                self.assertEqual(nested.detail_radius, 6)
                del nested
                gc.collect()
                # 此时仅保留孙对象；修改和再次复制仍应安全，不能访问已经释放的父存储。
                focus.window = 7
                retained = option_type()
                retained.focus = focus
                focus.window = 3
                self.assertEqual(retained.focus.window, 7)
                self.assertEqual(focus.window, 3)

    def test_nested_fusion_settings_affect_results(self):
        """通过真实融合确认嵌套编辑进入计算，而不是只改变 Python 返回的临时副本。"""
        random = np.random.default_rng(54)
        images = [random.random((53, 71), dtype=np.float32) for _ in range(2)]
        for method, field in ((mif.FusionMethod.GUIDED_FILTER, "guided_filter"),
                              (mif.FusionMethod.LAPLACIAN_PYRAMID, "laplacian_pyramid")):
            with self.subTest(method=method):
                options = mif.FusionOptions()
                options.method = method
                baseline = mif.fuse_detailed(images, options)
                config = getattr(options, field)
                config.focus.measure = mif.FocusMeasure.TENENGRAD
                config.focus.window = 1
                config.detail_radius = 5
                config.detail_epsilon = 0.02
                if method == mif.FusionMethod.GUIDED_FILTER:
                    config.base_radius = 7
                    config.base_epsilon = 0.03
                else:
                    config.levels = 2
                changed = mif.fuse_detailed(images, options)
                self.assertTrue(np.any(changed["focus_indices"] != baseline["focus_indices"]))
                self.assertGreater(float(np.max(np.abs(changed["image"] - baseline["image"]))), 1e-5)

                # 将完整配置复制到另一个顶层对象，结果应与逐项修改的配置一致。
                copied = mif.FusionOptions()
                copied.method = method
                setattr(copied, field, config)
                np.testing.assert_array_equal(mif.fuse(images, copied), changed["image"])

    def test_only_selected_fusion_configuration_is_validated(self):
        """未选中方法的非法值不参与计算；切换到该方法后，纯融合和组合入口都拒绝。"""
        random = np.random.default_rng(61)
        images = [random.random((29, 37), dtype=np.float32) for _ in range(2)]
        for active, inactive, invalid_field in (
                (mif.FusionMethod.GUIDED_FILTER, mif.FusionMethod.LAPLACIAN_PYRAMID, "laplacian_pyramid"),
                (mif.FusionMethod.LAPLACIAN_PYRAMID, mif.FusionMethod.GUIDED_FILTER, "guided_filter")):
            with self.subTest(active=active):
                options = mif.FusionOptions()
                options.method = active
                expected = mif.fuse(images, options)
                invalid = getattr(options, invalid_field)
                invalid.focus.window = 2
                invalid.detail_radius = 0
                invalid.detail_epsilon = np.nan
                if invalid_field == "guided_filter":
                    invalid.base_radius = 0
                    invalid.base_epsilon = np.nan
                else:
                    invalid.levels = 0
                np.testing.assert_array_equal(mif.fuse(images, options), expected)
                np.testing.assert_array_equal(mif.register_and_fuse(images, fusion_options=options)["image"], expected)
                options.method = inactive
                with self.assertRaises(ValueError):
                    mif.fuse(images, options)
                with self.assertRaises(ValueError):
                    mif.register_and_fuse(images, fusion_options=options)
                options.method = active
                np.testing.assert_array_equal(mif.fuse(images, options), expected)

    def test_registration_options(self):
        """配准算法与 ECC 运动模型分开配置，数值参数默认值仍由核心提供。"""
        options = mif.RegistrationOptions()
        self.assertEqual(options.method, mif.RegistrationMethod.NONE)
        self.assertEqual(options.motion_model, mif.MotionModel.TRANSLATION)
        for method in (mif.RegistrationMethod.NONE, mif.RegistrationMethod.ECC, mif.RegistrationMethod.SIFT):
            options.method = method
            self.assertEqual(options.method, method)
        for model in (mif.MotionModel.TRANSLATION, mif.MotionModel.AFFINE, mif.MotionModel.HOMOGRAPHY):
            options.motion_model = model
            self.assertEqual(options.motion_model, model)
        for field, default, changed in (
                ("iterations", 150, 200), ("epsilon", 1e-5, 1e-6), ("max_size", 1200, 600),
                ("max_features", 4000, 5000), ("match_ratio", 0.75, 0.8),
                ("ransac_threshold", 3.0, 2.5), ("min_inlier_ratio", 0.25, 0.5)):
            self.assertEqual(getattr(options, field), default)
            setattr(options, field, changed)
            self.assertEqual(getattr(options, field), changed)

    def test_independent_option_types(self):
        """旧枚举和混合配置属性明确移除，各种枚举与阶段参数不能互相替代。"""
        fusion = mif.FusionOptions()
        registration = mif.RegistrationOptions()
        image = np.full((20, 30), 100, np.uint8)
        self.assertFalse(hasattr(mif, "Alignment"))
        self.assertFalse(hasattr(mif._mif, "Alignment"))
        for old_member in ("TRANSLATION", "AFFINE", "FEATURE_HOMOGRAPHY", "ECC_HOMOGRAPHY"):
            self.assertFalse(hasattr(mif.RegistrationMethod, old_member))
        # 即使底层枚举值相同，算法、模型和融合算法也不能交叉赋值。
        for field, values in (
                ("method", (mif.MotionModel.TRANSLATION, mif.FusionMethod.GUIDED_FILTER, 99)),
                ("motion_model", (mif.RegistrationMethod.ECC, mif.FusionMethod.GUIDED_FILTER, 99))):
            for value in values:
                with self.subTest(field=field, value=value), self.assertRaises(TypeError):
                    setattr(registration, field, value)
        for enum in (mif.RegistrationMethod, mif.MotionModel):
            with self.assertRaises(ValueError):
                enum(99)
        for old_field in ("alignment", "alignment_iterations", "alignment_epsilon", "alignment_max_size",
                          "alignment_max_features", "alignment_match_ratio", "alignment_ransac_threshold",
                          "alignment_min_inlier_ratio"):
            self.assertFalse(hasattr(fusion, old_field))
            with self.assertRaises(AttributeError):
                setattr(fusion, old_field, 1)
        self.assertFalse(hasattr(registration, "focus_window"))
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
                    self.assertEqual(set(result), {"images", "crop", "transforms"})
                    self.assertEqual(result["crop"], (0, 0, 31, 23))
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
        for method in (mif.RegistrationMethod.SIFT, mif.RegistrationMethod.ECC):
            with self.subTest(method=method):
                backing = np.repeat(alignment_texture(), 2, axis=1)
                image = backing[:, ::2]
                images = [image, image]
                originals = [source.copy() for source in images]
                for source in images:
                    source.flags.writeable = False
                options = mif.RegistrationOptions()
                options.method = method
                options.motion_model = mif.MotionModel.HOMOGRAPHY
                result = mif.register_images(images, options)
                aligned = result["images"]
                transforms = result["transforms"]
                x, y, width, height = result["crop"]
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
                self.assertEqual(set(fused), {"image", "focus_indices", "weights"})
                # 可写输出之间也不应错误复用矩阵存储，第一张变换的修改不能污染第二张。
                transforms[0][:] = 0
                np.testing.assert_allclose(transforms[1], np.eye(3), atol=0.01, rtol=0)
                second_aligned = aligned[1].copy()
                aligned[0][:] = 0
                np.testing.assert_array_equal(aligned[1], second_aligned)
                for source, original in zip(images, originals):
                    np.testing.assert_array_equal(source, original)

    def test_motion_model_ignored_outside_ecc(self):
        """SIFT 对三种合法模型都求解相同单应性，NONE 则始终返回 2×3 单位变换。"""
        reference = alignment_texture()
        images = [reference, np.roll(reference, (1, 2), axis=(0, 1))]
        baseline = None
        for model in (mif.MotionModel.TRANSLATION, mif.MotionModel.AFFINE, mif.MotionModel.HOMOGRAPHY):
            with self.subTest(model=model):
                options = mif.RegistrationOptions()
                options.motion_model = model
                unregistered = mif.register_images(images, options)
                self.assertEqual(unregistered["crop"], (0, 0, reference.shape[1], reference.shape[0]))
                for original, output, transform in zip(images, unregistered["images"], unregistered["transforms"]):
                    np.testing.assert_array_equal(output, original)
                    np.testing.assert_array_equal(transform, np.eye(2, 3))

                options.method = mif.RegistrationMethod.SIFT
                registered = mif.register_images(images, options)
                self.assertEqual(len(registered["transforms"]), len(images))
                for transform in registered["transforms"]:
                    self.assertEqual(transform.shape, (3, 3))
                if baseline is None:
                    baseline = registered
                else:
                    self.assertEqual(registered["crop"], baseline["crop"])
                    for key in ("images", "transforms"):
                        self.assertEqual(len(registered[key]), len(baseline[key]))
                        for actual, expected in zip(registered[key], baseline[key]):
                            np.testing.assert_array_equal(actual, expected)

    def test_registration_parameter_validation(self):
        """配准参数在配准入口被拒绝；非法配准配置不妨碍单独调用纯融合。"""
        image = np.zeros((20, 30), np.uint8)
        for field, values in (
                ("iterations", (0, 10001)), ("epsilon", (0.0, -1.0, np.nan, np.inf)),
                ("max_size", (15, 8193)), ("max_features", (63, 100001)),
                ("match_ratio", (0.0, 1.0, -0.1, np.nan, np.inf)),
                ("ransac_threshold", (0.0, -1.0, np.nan, np.inf)),
                ("min_inlier_ratio", (0.0, -0.1, 1.1, np.nan, np.inf))):
            for value in values:
                with self.subTest(field=field, value=value):
                    options = mif.RegistrationOptions()
                    setattr(options, field, value)
                    with self.assertRaises(ValueError):
                        mif.register_images([image, image], options)
                    np.testing.assert_array_equal(mif.fuse([image, image]), image)

    def test_fusion_validation_does_not_affect_registration(self):
        """融合参数错误只影响融合及组合流程，不参与单独配准的校验。"""
        image = np.full((20, 30), 100, np.uint8)
        for method, field, specific in (
                (mif.FusionMethod.GUIDED_FILTER, "guided_filter", (("base_radius", 0), ("base_epsilon", np.nan))),
                (mif.FusionMethod.LAPLACIAN_PYRAMID, "laplacian_pyramid", (("levels", 0),))):
            for name, value in (("window", 2), ("detail_radius", 0), ("detail_epsilon", 0.0)) + specific:
                with self.subTest(method=method, field=name):
                    options = mif.FusionOptions()
                    options.method = method
                    config = getattr(options, field)
                    setattr(config.focus if name == "window" else config, name, value)
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
        for method in (mif.RegistrationMethod.ECC, mif.RegistrationMethod.SIFT):
            options = mif.RegistrationOptions()
            options.method = method
            with self.assertRaises(RuntimeError):
                mif.register_images([good, good], options)

    def test_explicit_stages_match_pipeline(self):
        """真实位移输入在各精度和方法下，显式两步与组合入口具有相同结果和元数据。"""
        texture = alignment_texture()
        shifted = np.roll(texture, (1, 2), axis=(0, 1))
        configurations = (
            (mif.RegistrationMethod.NONE, mif.MotionModel.TRANSLATION),
            (mif.RegistrationMethod.ECC, mif.MotionModel.TRANSLATION),
            (mif.RegistrationMethod.ECC, mif.MotionModel.AFFINE),
            (mif.RegistrationMethod.ECC, mif.MotionModel.HOMOGRAPHY),
            (mif.RegistrationMethod.SIFT, mif.MotionModel.TRANSLATION),
        )
        for dtype in (np.uint8, np.uint16, np.float32):
            scale = 257 if dtype == np.uint16 else 1 / 255 if dtype == np.float32 else 1
            images = [(image.astype(np.float32) * scale).astype(dtype) for image in (texture, shifted)]
            for method, model in configurations:
                registration_options = mif.RegistrationOptions()
                registration_options.method = method
                registration_options.motion_model = model
                registered = mif.register_images(images, registration_options)
                expected_shape = (3, 3) if method == mif.RegistrationMethod.SIFT or (
                    method == mif.RegistrationMethod.ECC and model == mif.MotionModel.HOMOGRAPHY) else (2, 3)
                for transform in registered["transforms"]:
                    self.assertEqual(transform.shape, expected_shape)
                for fusion_method in (mif.FusionMethod.GUIDED_FILTER, mif.FusionMethod.LAPLACIAN_PYRAMID):
                    with self.subTest(dtype=dtype, registration=method, model=model, fusion=fusion_method):
                        fusion_options = mif.FusionOptions()
                        fusion_options.method = fusion_method
                        fusion_options.keep_weight_maps = True
                        explicit = mif.fuse_detailed(registered["images"], fusion_options)
                        combined = mif.register_and_fuse(images, registration_options, fusion_options)
                        self.assertEqual(set(combined), {"image", "focus_indices", "weights", "crop", "transforms"})
                        self.assertEqual(combined["image"].dtype, dtype)
                        self.assertEqual(combined["crop"], registered["crop"])
                        np.testing.assert_array_equal(combined["image"], explicit["image"])
                        np.testing.assert_array_equal(combined["focus_indices"], explicit["focus_indices"])
                        self.assertEqual(len(combined["weights"]), len(images))
                        self.assertEqual(len(combined["transforms"]), len(images))
                        for actual, expected in zip(combined["weights"], explicit["weights"]):
                            np.testing.assert_array_equal(actual, expected)
                        for actual, expected in zip(combined["transforms"], registered["transforms"]):
                            np.testing.assert_array_equal(actual, expected)

    def test_pipeline_parameter_validation(self):
        """组合入口按独立参数类型调用两个阶段，不能遗漏任一阶段的非法配置。"""
        image = np.zeros((20, 30), np.uint8)
        registration_options = mif.RegistrationOptions()
        registration_options.epsilon = np.nan
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
        options = mif.FusionOptions()
        options.guided_filter.focus.window = 2
        with self.assertRaises(ValueError):
            mif.fuse([good, good], options)


if __name__ == "__main__":
    unittest.main()
