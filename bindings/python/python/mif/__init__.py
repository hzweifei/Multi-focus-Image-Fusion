"""独立图像配准与传统多聚焦融合；彩色数组使用 OpenCV 的 BGR 通道顺序。

fuse/fuse_detailed 只执行融合，register_images 只执行配准；需要连续处理时
调用 register_and_fuse，或把 register_images 返回的 images 显式传给 fuse。
两个阶段分别使用 RegistrationOptions 和 FusionOptions，均不使用 AI 模型。

迁移：原 Alignment 已移除。RegistrationOptions.method 使用 RegistrationMethod，
ECC 的平移/仿射/单应性改由 motion_model 选择 MotionModel；SIFT 固定求解单应性。
原 FusionOptions.alignment_* 数值字段去掉此前缀后移到 RegistrationOptions。
旧的配准加融合调用改用 register_and_fuse；fuse_detailed 不再返回 crop 和 transforms。
"""
import os
from pathlib import Path

# 保存目录句柄，让包内 Windows DLL 在模块整个生命周期内保持可搜索。
_dll_directory = os.add_dll_directory(str(Path(__file__).resolve().parent)) if os.name == "nt" else None

import numpy as np
from . import _mif
from ._mif import FocusMeasure, FusionMethod, FusionOptions, MotionModel, RegistrationMethod, RegistrationOptions

__all__ = ["FocusMeasure", "FusionMethod", "FusionOptions", "MotionModel", "RegistrationMethod", "RegistrationOptions",
           "fuse", "fuse_detailed", "register_images", "register_and_fuse"]
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
    """仅融合至少两张已对齐的图像，返回独立存储的 NumPy 数组。

    images: H×W 灰度或 H×W×3 BGR 数组序列，精度为 uint8、uint16 或
        float32；float32 必须全部为 [0, 1] 范围内的有限数值。
    options: FusionOptions 参数对象；传入 None 时为本次调用创建默认参数。

    接受非连续切片和只读数组。C++ 处理前复制输入并释放 GIL，处理结束后
    恢复 GIL。输出保留输入尺寸、通道和精度，输入数组不会被修改。
    本入口只校验融合参数，不执行配准或裁剪；需要配准时先调用 register_images。

    输入类型错误抛出 TypeError，非法图像或参数抛出 ValueError，运行失败抛出
    RuntimeError。配准参数不能传给本函数。
    """
    return _mif.fuse(_prepare(images), options if options is not None else FusionOptions())


def fuse_detailed(images, options=None):
    """使用与 fuse 相同的输入约定，仅返回融合结果与诊断信息。

    image: 融合图像，精度和通道数与输入一致。
    focus_indices: int32 数组，每个位置记录从 0 开始的源图像索引。
    weights: 设置 options.keep_weight_maps = True 时返回归一化细节权重
        数组列表；默认返回空列表。

    字典仅含 image、focus_indices、weights 三个键；本阶段没有配准元数据。
    所有返回数组在调用结束后仍持有有效存储，异常约定与 fuse 相同。
    """
    return _mif.fuse_detailed(_prepare(images), options if options is not None else FusionOptions())


def register_images(images, options=None):
    """独立配准图像，返回 images、crop、transforms 字典，不执行融合。

    输入图像格式、精度、值域及只读切片约定与 fuse 相同；options 使用独立的
    RegistrationOptions，None 表示创建默认配置，默认 method=RegistrationMethod.NONE。
    NONE 仍返回独立图像副本，各输出互不共享可写存储，便于继续修改或重复融合。

    method: RegistrationMethod.NONE 跳过配准；ECC 根据灰度相关性估计变换；
        SIFT 使用特征匹配与 RANSAC，固定求解单应性。
    motion_model: 仅 ECC 使用，MotionModel.TRANSLATION/AFFINE/HOMOGRAPHY
        分别表示平移/仿射/单应性，默认 TRANSLATION；ECC 单应性适合较小的初始偏差。
        SIFT 和 NONE 忽略此字段的合法值，但所有方法都会拒绝非法枚举值。
    iterations、epsilon: ECC 迭代上限和收敛阈值，默认 150 和 1e-5。
    max_size: 估计变换的最长边上限，默认 1200；最终在原分辨率重采样。
    max_features: SIFT 特征上限，[64, 100000]，默认 4000。
    match_ratio: 最近邻/次近邻描述子距离比阈值，范围 (0, 1)，默认 0.75。
    ransac_threshold: 工作分辨率像素中的正数重投影阈值，默认 3.0。
    min_inlier_ratio: 最小内点比例，范围 (0, 1]，默认 0.25。
    配准入口检查这些配准参数，不检查任何融合参数。

    images: 与输入顺序一致的配准图像列表，保留原始精度和通道，裁剪为共同区域。
    crop: 第一张原始输入坐标系中的 (x, y, width, height)。
    transforms: 每张输入对应一个 float32 矩阵，方向为参考图坐标到该源图坐标。
        NONE 为 2×3 单位变换；ECC 平移/仿射为 2×3，ECC 单应性与 SIFT 为 3×3。
        第一张始终对应所用矩阵尺寸的单位变换。
        输出像素 (u, v) 先构造 p=[u+crop[0], v+crop[1], 1]。2×3 直接计算
        q=M@p；3×3 计算 q=H@p 后除以第三项，源坐标为 (q[0]/q[2], q[1]/q[2])。
        矩阵不包含裁剪偏移，使用齐次除法前应检查 q[2] 非零。

    输入和配置在释放 GIL 前复制。返回数组在字典或 C++ 结果销毁后仍持有存储。
    非法图像或参数抛出 ValueError，类型不匹配抛出 TypeError；纹理不足、无法收敛
    或变换退化等配准失败抛出 RuntimeError。
    """
    return _mif.register_images(_prepare(images), options if options is not None else RegistrationOptions())


def register_and_fuse(images, registration_options=None, fusion_options=None):
    """顺序执行独立配准与融合阶段，返回平坦的详细结果字典。

    registration_options 使用 RegistrationOptions；fusion_options 使用
    FusionOptions。各自为 None 时创建自己的默认配置，两个阶段分别校验参数。
    图像和参数的格式、异常约定分别见 register_images 与 fuse。

    等价于先调用 register_images，再对返回的 images 调用 fuse_detailed；
    配准阶段保留原始 dtype，因此整数重采样与显式两步采用同样的舍入过程。
    字典包含 image、focus_indices、weights，以及配准阶段的 crop、transforms。
    所有返回数组独立于输入，耗时计算期间释放 GIL。
    """
    return _mif.register_and_fuse(
        _prepare(images),
        registration_options if registration_options is not None else RegistrationOptions(),
        fusion_options if fusion_options is not None else FusionOptions(),
    )

