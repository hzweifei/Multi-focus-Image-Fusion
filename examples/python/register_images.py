"""单独配准示例：python register_images.py aligned focus_01.png focus_02.png

运行前安装 mif、NumPy 和 OpenCV 的 Python 包。--method 选择 none/ecc/sift，
默认 ecc；--motion-model 选择 ECC 的 translation/affine/homography，默认 translation。
SIFT 固定求解单应性，--motion-model 只在 ECC 下生效。
输出保留位深，整数图写 PNG，浮点图写无压缩 TIFF。
这些输出之后可交给 fuse_images.py，也可在内存中直接交给 mif.fuse。
"""
import argparse
from pathlib import Path

import cv2
import mif


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output_dir", type=Path, help="保存配准图像的目录")
parser.add_argument("inputs", nargs="+", help="至少两张同尺寸、同精度图像")
parser.add_argument("--method", default="ecc", choices=("none", "ecc", "sift"),
                    help="配准算法；SIFT 固定求解单应性（默认：ecc）")
parser.add_argument("--motion-model", default="translation", choices=("translation", "affine", "homography"),
                    help="仅 ECC 使用的运动模型（默认：translation）")
args = parser.parse_args()
if len(args.inputs) < 2:
    parser.error("at least two input images are required")
images = [cv2.imread(path, cv2.IMREAD_UNCHANGED) for path in args.inputs]
if any(image is None for image in images):
    raise SystemExit("One or more images could not be read")

# 这里只创建配准设置，不需要准备任何融合参数。
options = {
    "none": mif.NoRegistrationOptions,
    "ecc": mif.EccRegistrationOptions,
    "sift": mif.SiftRegistrationOptions,
}[args.method]()
if isinstance(options, mif.EccRegistrationOptions):
    options.motion_model = {
        "translation": mif.MotionModel.TRANSLATION,
        "affine": mif.MotionModel.AFFINE,
        "homography": mif.MotionModel.HOMOGRAPHY,
    }[args.motion_model]
registered = mif.register_images(images, options)
args.output_dir.mkdir(parents=True, exist_ok=True)
for index, image in enumerate(registered["images"], start=1):
    suffix = ".tif" if image.dtype.name == "float32" else ".png"
    target = args.output_dir / f"aligned_{index:03d}{suffix}"
    # 输入顺序与结果一一对应，文件名使用连续序号以保持后续导入顺序。
    if not cv2.imwrite(str(target), image, [cv2.IMWRITE_TIFF_COMPRESSION, 1]):
        raise SystemExit(f"Could not save {target}")
print("共同有效区域：", registered["crop_region"])
print("已保存配准图像到：", args.output_dir)

# 如需继续融合，可直接调用：mif.fuse(registered["images"], mif.GuidedFilterFusionOptions())。
# 一次完成两步则使用 mif.register_and_fuse(images, options, mif.GuidedFilterFusionOptions())。
