"""Usage: python fuse_images.py output.png focus_01.png focus_02.png"""
import sys
import cv2
import mif

if len(sys.argv) < 4:
    raise SystemExit(__doc__)
images = [cv2.imread(path, cv2.IMREAD_UNCHANGED) for path in sys.argv[2:]]
if any(image is None for image in images):
    raise SystemExit("One or more images could not be read")
options = mif.FusionOptions()
options.method = mif.FusionMethod.GUIDED_FILTER
result = mif.fuse(images, options)
suffix = sys.argv[1].lower().rsplit(".", 1)[-1]
if result.dtype.name == "uint16" and suffix not in ("png", "tif", "tiff"):
    raise SystemExit("Use PNG or TIFF to preserve 16-bit depth")
if result.dtype.name == "float32" and suffix not in ("tif", "tiff"):
    raise SystemExit("Use TIFF to preserve float depth")
if not cv2.imwrite(sys.argv[1], result, [cv2.IMWRITE_TIFF_COMPRESSION, 1]):
    raise SystemExit("Could not save result")
