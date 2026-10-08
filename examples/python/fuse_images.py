"""纯融合调用示例：python fuse_images.py output.png focus_01.png focus_02.png

运行前安装 mif、NumPy 和 OpenCV 的 Python 包。至少传入两张图像，可在末尾
追加更多输入，输入须已对齐。16 位结果使用 PNG 或 TIFF 保存，浮点结果使用 TIFF。
需要先对齐时，可运行同目录 register_images.py，或使用 mif.register_and_fuse。
"""
import sys
import cv2
import mif

if len(sys.argv) < 4:
    raise SystemExit(__doc__)
# 保留位深与通道；cv2 返回的彩色数组已经是 mif 接口要求的 BGR 顺序。
images = [cv2.imread(path, cv2.IMREAD_UNCHANGED) for path in sys.argv[2:]]
if any(image is None for image in images):
    raise SystemExit("One or more images could not be read")
# 参数类型决定方法，调用入口统一为 mif.fuse。
options = mif.GuidedFilterFusionOptions()
options.focus.measure = mif.FocusMeasure.MODIFIED_LAPLACIAN
options.focus.window_size = 9
options.detail_radius = 3
# 可替换为 LaplacianPyramidFusionOptions、BlockVarianceFusionOptions、
# DtcwtFusionOptions 或 GfgFgfFusionOptions，并直接设置该方法的字段。
result = mif.fuse(images, options)
suffix = sys.argv[1].lower().rsplit(".", 1)[-1]
# 根据输出精度限制文件格式，避免编码器自动降精度而丢失数据。
if result.dtype.name == "uint16" and suffix not in ("png", "tif", "tiff"):
    raise SystemExit("Use PNG or TIFF to preserve 16-bit depth")
if result.dtype.name == "float32" and suffix not in ("tif", "tiff"):
    raise SystemExit("Use TIFF to preserve float depth")
# 对 TIFF 显式使用无压缩编码，保留彩色浮点图像的像素值。
if not cv2.imwrite(sys.argv[1], result, [cv2.IMWRITE_TIFF_COMPRESSION, 1]):
    raise SystemExit("Could not save result")
