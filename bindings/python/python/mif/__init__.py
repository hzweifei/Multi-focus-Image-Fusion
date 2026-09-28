"""传统多聚焦图像融合接口；彩色数组使用 OpenCV 的 BGR 通道顺序。"""
import os
from pathlib import Path

# 保存目录句柄，让包内 Windows DLL 在模块整个生命周期内保持可搜索。
_dll_directory = os.add_dll_directory(str(Path(__file__).resolve().parent)) if os.name == "nt" else None

import numpy as np
from . import _mif
from ._mif import Alignment, FocusMeasure, FusionMethod, FusionOptions

__all__ = ["Alignment", "FocusMeasure", "FusionMethod", "FusionOptions", "fuse", "fuse_detailed"]
__version__ = "0.1.0"


def _prepare(images):
    """检查数组类型与布局，并把非连续切片整理为底层绑定需要的 C 连续数组。

    此处不强制复制连续数组；C++ 绑定会在释放 GIL 前统一复制输入。
    图像数量、尺寸一致性以及浮点值域由核心校验，避免在多层重复维护规则。
    """
    prepared = []
    for image in images:
        if not isinstance(image, np.ndarray):
            raise TypeError("Each image must be a NumPy array")
        if image.dtype not in (np.dtype("uint8"), np.dtype("uint16"), np.dtype("float32")):
            raise ValueError("Images must have native uint8, uint16 or float32 dtype")
        if image.ndim != 2 and not (image.ndim == 3 and image.shape[2] == 3):
            raise ValueError("Expected H x W grayscale or H x W x 3 BGR arrays")
        prepared.append(np.ascontiguousarray(image))
    return prepared


def fuse(images, options=None):
    """融合至少两张尺寸和精度一致的图像，返回独立存储的 NumPy 数组。

    images: H×W 灰度或 H×W×3 BGR 数组序列，精度为 uint8、uint16 或
        float32；float32 必须全部为 [0, 1] 范围内的有限数值。
    options: FusionOptions 参数对象；传入 None 时为本次调用创建默认参数。

    接受非连续切片和只读数组。C++ 处理前复制输入并释放 GIL，处理结束后
    恢复 GIL。输出保留输入精度，尤其保留 uint16 的 16 位信息；启用配准时
    裁剪到所有图像的共同有效矩形区域。输入数组不会被修改。

    输入类型错误抛出 TypeError，非法图像或参数抛出 ValueError；配准等
    运行过程失败会抛出 RuntimeError。
    """
    return _mif.fuse(_prepare(images), options if options is not None else FusionOptions())


def fuse_detailed(images, options=None):
    """使用与 fuse 相同的输入约定，返回包含图像及诊断信息的字典。

    image: 融合图像，精度和通道数与输入一致。
    focus_indices: int32 数组，每个位置记录从 0 开始的源图像索引。
    crop: 第一张输入图像坐标系中的 (x, y, width, height)。
    transforms: 每张输入对应的 2×3 float32 矩阵，将参考坐标映射到源图坐标。
        若从输出像素定位源图像素，应先加上 crop 的左上角偏移，再应用矩阵。
    weights: 设置 options.keep_weight_maps = True 时返回归一化细节权重
        数组列表；默认返回空列表。

    所有返回数组在调用结束后仍持有有效存储，异常约定与 fuse 相同。
    """
    return _mif.fuse_detailed(_prepare(images), options if options is not None else FusionOptions())

