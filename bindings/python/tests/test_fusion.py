"""Run directly or with unittest; requires a built mif extension and NumPy."""
import gc
import os
from pathlib import Path
import unittest

# Test-only runtime discovery, configured by the person running the build.
_dll_handles = []
if os.name == "nt":
    for folder in os.environ.get("MIF_TEST_DLL_DIRS", "").split(os.pathsep):
        if folder and Path(folder).is_dir():
            _dll_handles.append(os.add_dll_directory(folder))

import numpy as np
import mif


class FusionTests(unittest.TestCase):
    def test_float_endpoints(self):
        for value in (0.0, 1.0):
            image = np.full((5, 7, 3), value, np.float32)
            np.testing.assert_allclose(mif.fuse([image, image]), image, atol=1e-6)

    def test_precision_and_ownership(self):
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
                    gc.collect()
                    self.assertEqual(result.dtype, dtype)
                    np.testing.assert_allclose(result, image, atol=2e-6 if dtype == np.float32 else 1, rtol=0)
                    result[:] = 0
                    np.testing.assert_array_equal(image, original)

    def test_strided_readonly(self):
        image = np.arange(40 * 60, dtype=np.uint16).reshape(40, 60)[:, ::2]
        image.flags.writeable = False
        np.testing.assert_allclose(mif.fuse([image, image]), image, atol=1, rtol=0)

    def test_diagnostics(self):
        options = mif.FusionOptions()
        options.keep_weight_maps = True
        result = mif.fuse_detailed([np.full((21, 33), 10000, np.uint16), np.full((21, 33), 50000, np.uint16)], options)
        gc.collect()
        self.assertEqual(result["crop"], (0, 0, 33, 21))
        self.assertEqual(result["focus_indices"].dtype, np.int32)
        self.assertEqual(len(result["transforms"]), 2)
        np.testing.assert_allclose(sum(result["weights"]), 1, atol=1e-6)
        np.testing.assert_allclose(result["image"], 30000, atol=1)

    def test_validation(self):
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
