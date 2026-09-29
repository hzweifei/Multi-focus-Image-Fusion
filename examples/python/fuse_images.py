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
options = mif.FusionOptions()
# 默认演示引导滤波，也可选择 LAPLACIAN_PYRAMID、DCT、DTCWT、GFGFGF。
options.method = mif.FusionMethod.GUIDED_FILTER
# 每种方法各自保存完整参数，修改一种方法不会覆盖另一种方法的设置。
options.guided_filter.focus.measure = mif.FocusMeasure.MODIFIED_LAPLACIAN
options.guided_filter.focus.window = 9
options.guided_filter.detail_radius = 3
# 改用金字塔时可设置 options.laplacian_pyramid.focus.window 和 .levels。
# 块方差：options.method = mif.FusionMethod.DCT；options.dct.block_size = 8。
# 复小波：options.method = mif.FusionMethod.DTCWT；options.dtcwt.levels = 4。
# GFG-FGF：options.method = mif.FusionMethod.GFGFGF；options.gfgfgf.selection_ratio = 0.15。
# 此入口只执行融合；配准使用独立的 RegistrationOptions 和 register_images。
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
