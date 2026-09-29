#pragma once

namespace mif {

/// 传统融合方法；两种方法均使用清晰度决策图和引导滤波后的权重。
enum class FusionMethod {
    GuidedFilter,     ///< 分解为基础层与细节层，分别加权后重建。
    LaplacianPyramid  ///< 对拉普拉斯金字塔的各尺度分量加权，再逐层重建。
};

/// 灰度图上的清晰度指标；局部响应越大，通常表示该位置越清晰。
enum class FocusMeasure {
    ModifiedLaplacian, ///< 水平、垂直二阶差分绝对值之和，再做局部平均。
    Tenengrad         ///< 水平、垂直 Sobel 梯度平方和，再做局部平均。
};

/// 融合参数。所有数值参数均在 fuse() 入口校验，包括当前模式未使用的参数。
/// 滤波作用于归一化到 [0, 1] 的浮点图像，像素半径以传入图像的尺寸为准。
struct FusionOptions {
    /// 融合方法，默认使用基础层与细节层分开加权的引导滤波方案。
    FusionMethod method = FusionMethod::GuidedFilter;
    /// 用于比较各输入图像清晰程度的指标。
    FocusMeasure focus_measure = FocusMeasure::ModifiedLaplacian;

    /// 清晰度响应的局部平均窗口边长，单位为像素，必须为 [1, 255] 内的奇数。
    int focus_window = 9;
    /// 基础层均值滤波和基础权重引导滤波的半径，[1, 255]；仅引导滤波融合使用。
    int base_radius = 15;
    /// 细节权重引导滤波半径，[1, 255]；实际窗口边长为 2 * radius + 1。
    int detail_radius = 3;
    /// 基础权重引导滤波的正则项，必须为有限正数；仅引导滤波融合使用。
    /// 该值与 [0, 1] 灰度图的局部方差相加，越大通常越倾向于平滑权重。
    double base_epsilon = 0.01;
    /// 细节权重引导滤波的正则项，必须为有限正数，含义同 base_epsilon。
    double detail_epsilon = 0.0001;
    /// 金字塔层数，[1, 16]，包含最粗层；实际层数还受图像尺寸限制。
    /// 仅拉普拉斯金字塔融合使用；设为 1 时直接进行单尺度加权。
    int pyramid_levels = 5;

    /// 是否在结果中保留每张输入的细节权重图；关闭可减少返回结果占用的内存。
    bool keep_weight_maps = false;

};

} // 命名空间 mif

