"""独立图像配准与传统多聚焦融合；彩色数组使用 OpenCV 的 BGR 通道顺序。

参数类型决定方法，例如 EccRegistrationOptions 对应 ECC 配准，
BlockVarianceFusionOptions 对应块方差融合。各方法直接保存自己的参数。
fuse/fuse_detailed 只融合，register_images 只配准，register_and_fuse 顺序执行两阶段。
"""
import os
from pathlib import Path

# 保存目录句柄，让包内 Windows DLL 在模块整个生命周期内保持可搜索。
_dll_directory = os.add_dll_directory(str(Path(__file__).resolve().parent)) if os.name == "nt" else None

import numpy as np
from . import _mif
from ._mif import (
    FocusMeasure, FocusMeasureOptions, FusionOptionsBase,
    GuidedFilterFusionOptions, LaplacianPyramidFusionOptions,
    BlockVarianceFusionOptions, DtcwtFusionOptions, GfgFgfFusionOptions,
    MotionModel, RegistrationOptionsBase, NoRegistrationOptions,
    EccRegistrationOptions, SiftRegistrationOptions,
)

__all__ = [
    "FocusMeasure", "FocusMeasureOptions", "FusionOptionsBase",
    "GuidedFilterFusionOptions", "LaplacianPyramidFusionOptions",
    "BlockVarianceFusionOptions", "DtcwtFusionOptions", "GfgFgfFusionOptions",
    "MotionModel", "RegistrationOptionsBase", "NoRegistrationOptions",
    "EccRegistrationOptions", "SiftRegistrationOptions",
    "fuse", "fuse_detailed", "register_images", "register_and_fuse",
]
__version__ = "0.2.0rc2"


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
        float32；图像必须同尺寸、同通道、同精度。float32 值域为有限的 [0, 1]。
    options: FusionOptionsBase 的具体派生参数类；None 为本次调用创建
        GuidedFilterFusionOptions。参数类型决定方法，无需再设置 method。
        GuidedFilterFusionOptions: focus、base_radius、detail_radius、
            base_epsilon、detail_epsilon。
        LaplacianPyramidFusionOptions: focus、detail_radius、detail_epsilon、max_levels。
        BlockVarianceFusionOptions: block_size、consistency_window_size。
        DtcwtFusionOptions: max_levels、activity_window_size。
        GfgFgfFusionOptions: local_mean_window_size、selection_ratio、gfg_threshold、
            guided_radius、guided_epsilon、guided_subsample_factor。
        focus 包含 measure 和 window_size；读取得到内部引用，整体赋值复制数值。
        所有方法继承 include_weight_maps，默认为 False。

    所有引导滤波 epsilon 须在 [1e-6, float32 最大有限值]。
    GFG-FGF 的 selection_ratio 默认 0 保留全部帧；G、R 分别引导滤波。
    原始 G 达到 gfg_threshold 的帧优先参与比较，全部未达标时才比较 R 路。
    guided_subsample_factor 范围 [1,16]，默认 4。

    接受非连续切片和只读数组。C++ 在释放 GIL 前复制输入并克隆具体参数，
    计算期间修改 Python 参数不会影响本次调用。输出保留输入尺寸、通道和精度，
    输入数组不会被修改。需要配准时先调用 register_images。

    类型错误抛出 TypeError，非法图像或参数抛出 ValueError，运行失败抛出 RuntimeError。
    """
    return _mif.fuse(_prepare(images), options if options is not None else GuidedFilterFusionOptions())


def fuse_detailed(images, options=None):
    """使用与 fuse 相同的输入约定，返回三个字段的融合结果字典。

    image: 融合图像，精度和通道数与输入一致。
    source_index_map: int32 数组，记录从 0 开始的原始源图索引。
        DTCWT 在多个尺度与方向选择系数，无法对应单一来源图，返回 None。
    weight_maps: include_weight_maps=True 时返回方法提供的归一化权重。
        GFF/金字塔为细节权重，块方差为选块权重，GFG-FGF 为最终融合权重。
        列表按原始输入排序，GFG-FGF 筛选排除的帧权重全零。
        默认以及 DTCWT 方法均返回空列表。

    所有返回数组在调用结束后仍持有有效存储，异常约定与 fuse 相同。
    """
    return _mif.fuse_detailed(_prepare(images), options if options is not None else GuidedFilterFusionOptions())


def register_images(images, options=None):
    """独立配准图像，返回 images、crop_region、transforms 字典。

    输入数组约定与 fuse 相同。options 为 RegistrationOptionsBase 的具体派生类：
        NoRegistrationOptions: 保持坐标并返回独立副本，也是 None 时的默认配置。
        EccRegistrationOptions: 根据灰度相关性估计变换；motion_model 选择
            TRANSLATION/AFFINE/HOMOGRAPHY，默认 TRANSLATION；
            max_iterations=150，convergence_tolerance=1e-5。
        SiftRegistrationOptions: 特征匹配与 RANSAC，固定求解单应性；
            max_features=4000、match_ratio_threshold=0.75、
            ransac_reprojection_threshold=3.0、min_inlier_ratio=0.25。
        共有字段 max_working_dimension=1200 是估计变换时的最长边上限，
        最终在原分辨率重采样。SIFT 的重投影阈值单位为工作分辨率像素。

    images: 按输入顺序返回独立图像，保留原始精度和通道，裁剪为共同区域。
    crop_region: 第一张原始输入坐标系中的 (x, y, width, height)。
    transforms: 每张输入对应一个 float32 矩阵，方向为参考图坐标到该源图坐标。
        跳过配准时为 2×3 单位变换；ECC 平移/仿射为 2×3，ECC 单应性与 SIFT 为 3×3。
        第一张始终对应所用矩阵尺寸的单位变换。
        输出像素 (u, v) 先构造 p=[u+crop_region[0], v+crop_region[1], 1]。
        2×3 直接计算 q=M@p；3×3 计算 q=H@p 后除以第三项，源坐标为
        (q[0]/q[2], q[1]/q[2])。矩阵不包含裁剪偏移，除法前应检查 q[2] 非零。

    输入与具体参数在释放 GIL 前复制。返回数组在字典或 C++ 结果销毁后仍持有存储。
    非法图像或参数抛出 ValueError，类型错误抛出 TypeError；纹理不足、无法收敛
    或变换退化等配准失败抛出 RuntimeError。
    """
    return _mif.register_images(_prepare(images), options if options is not None else NoRegistrationOptions())


def register_and_fuse(images, registration_options=None, fusion_options=None):
    """顺序执行独立配准与融合，返回平坦的详细结果字典。

    registration_options、fusion_options 分别接收具体配准、融合参数对象；
    None 分别创建 NoRegistrationOptions、GuidedFilterFusionOptions。
    图像格式和异常约定分别见 register_images 与 fuse。

    等价于先 register_images，再对返回的 images 调用 fuse_detailed。
    配准保留原始 dtype，因此整数重采样与显式两步采用同样的舍入过程。
    字典包含 image、source_index_map、weight_maps、crop_region、transforms。
    所有返回数组独立于输入，耗时计算期间释放 GIL。
    """
    return _mif.register_and_fuse(
        _prepare(images),
        registration_options if registration_options is not None else NoRegistrationOptions(),
        fusion_options if fusion_options is not None else GuidedFilterFusionOptions(),
    )
