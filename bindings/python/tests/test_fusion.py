"""Python 接口回归测试，可直接运行或由 unittest 发现。

运行前需准备可导入的 mif 扩展和 NumPy。覆盖精度、内存归属、切片输入、
诊断字段及异常映射；具体图像融合质量由 C++ 测试验证。
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
                for method in (mif.FusionMethod.GUIDED_FILTER, mif.FusionMethod.LAPLACIAN_PYRAMID):
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

    def test_diagnostics(self):
        """检查诊断结果字段、索引精度和归一化权重；无纹理平局应等权平均。"""
        options = mif.FusionOptions()
        options.keep_weight_maps = True
        result = mif.fuse_detailed([np.full((21, 33), 10000, np.uint16), np.full((21, 33), 50000, np.uint16)], options)
        gc.collect()
        self.assertEqual(result["crop"], (0, 0, 33, 21))
        self.assertEqual(result["focus_indices"].dtype, np.int32)
        self.assertEqual(len(result["transforms"]), 2)
        # 新增单应性模式时，未配准的旧接口仍保持 2×3 矩阵，避免破坏已有调用。
        for transform in result["transforms"]:
            self.assertEqual(transform.shape, (2, 3))
            self.assertEqual(transform.dtype, np.float32)
        np.testing.assert_allclose(sum(result["weights"]), 1, atol=1e-6)
        np.testing.assert_allclose(result["image"], 30000, atol=1)

    def test_homography_options(self):
        """两种单应性枚举可赋值，新增参数的默认值和 Python 可写属性保持一致。"""
        options = mif.FusionOptions()
        self.assertNotEqual(mif.Alignment.FEATURE_HOMOGRAPHY, mif.Alignment.ECC_HOMOGRAPHY)
        for alignment in (mif.Alignment.FEATURE_HOMOGRAPHY, mif.Alignment.ECC_HOMOGRAPHY):
            options.alignment = alignment
            self.assertEqual(options.alignment, alignment)
        for field, default, changed in (
                ("alignment_max_features", 4000, 5000),
                ("alignment_match_ratio", 0.75, 0.8),
                ("alignment_ransac_threshold", 3.0, 2.5),
                ("alignment_min_inlier_ratio", 0.25, 0.5)):
            self.assertEqual(getattr(options, field), default)
            setattr(options, field, changed)
            self.assertEqual(getattr(options, field), changed)

    def test_homography_diagnostics_and_ownership(self):
        """两种模式返回 3×3 变换；结果离开 C++ 后有效，写结果不能修改输入。"""
        for alignment in (mif.Alignment.FEATURE_HOMOGRAPHY, mif.Alignment.ECC_HOMOGRAPHY):
            with self.subTest(alignment=alignment):
                image = alignment_texture()
                images = [image.copy(), image.copy()]
                originals = [source.copy() for source in images]
                for source in images:
                    source.flags.writeable = False
                options = mif.FusionOptions()
                options.alignment = alignment
                options.keep_weight_maps = True
                result = mif.fuse_detailed(images, options)
                output = result["image"]
                transforms = result["transforms"]
                weights = result["weights"]
                indices = result["focus_indices"]
                x, y, width, height = result["crop"]
                # 清除字典、参数和临时对象，验证各返回数组自行维持所需的底层存储。
                del result, options
                gc.collect()
                self.assertEqual(output.shape, (height, width))
                self.assertEqual(output.dtype, np.uint8)
                self.assertGreater(width * height, image.size * 0.9)
                self.assertEqual(len(transforms), 2)
                for transform in transforms:
                    self.assertEqual(transform.shape, (3, 3))
                    self.assertEqual(transform.dtype, np.float32)
                    self.assertTrue(np.isfinite(transform).all())
                    np.testing.assert_allclose(transform, np.eye(3), atol=0.01, rtol=0)
                np.testing.assert_allclose(output, image[y:y + height, x:x + width], atol=1)
                np.testing.assert_allclose(sum(weights), 1, atol=1e-6)
                self.assertEqual(indices.shape, output.shape)
                self.assertEqual(indices.dtype, np.int32)
                for source, original in zip(images, originals):
                    self.assertFalse(np.shares_memory(source, output))
                    np.testing.assert_array_equal(source, original)
                # 可写输出之间也不应错误复用矩阵存储，第一张变换的修改不能污染第二张。
                transforms[0][:] = 0
                np.testing.assert_allclose(transforms[1], np.eye(3), atol=0.01, rtol=0)
                output[:] = 0
                weights[0][:] = 0
                for source, original in zip(images, originals):
                    np.testing.assert_array_equal(source, original)

    def test_homography_parameter_validation(self):
        """新增参数越界及 NaN/无穷大在核心入口被拒绝，即使当前未开启配准。"""
        image = np.zeros((20, 30), np.uint8)
        for field, values in (
                ("alignment_max_features", (63, 100001)),
                ("alignment_match_ratio", (0.0, 1.0, -0.1, np.nan, np.inf)),
                ("alignment_ransac_threshold", (0.0, -1.0, np.nan, np.inf)),
                ("alignment_min_inlier_ratio", (0.0, -0.1, 1.1, np.nan, np.inf))):
            for value in values:
                with self.subTest(field=field, value=value):
                    options = mif.FusionOptions()
                    setattr(options, field, value)
                    with self.assertRaises(ValueError):
                        mif.fuse([image, image], options)

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
        options.focus_window = 2
        with self.assertRaises(ValueError):
            mif.fuse([good, good], options)


if __name__ == "__main__":
    unittest.main()
