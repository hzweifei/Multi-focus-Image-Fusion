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

/// 以第一张图像为参考的配准模式；现有枚举数值保持不变。
/// ECC 通过灰度相关性求解；FeatureHomography 通过 SIFT 匹配和 RANSAC 求解。
enum class Alignment {
    None = 0,               ///< 不配准；调用者需确保输入已经对齐。
    Translation = 1,        ///< ECC 平移，仅估计水平和垂直位移。
    Affine = 2,             ///< ECC 六参数仿射，可表示平移、旋转、缩放和剪切。
    FeatureHomography = 3,  ///< SIFT 特征匹配 + RANSAC，估计八自由度单应性变换。
    EccHomography = 4       ///< ECC 单应性，可处理全局透视变化，需要较好的初始对齐。
};

/// 融合参数。所有数值参数均在 fuse() 入口校验，包括当前模式未使用的参数。
/// 滤波作用于归一化到 [0, 1] 的浮点图像，像素半径以配准裁剪后的图像为准。
struct FusionOptions {
    /// 融合方法，默认使用基础层与细节层分开加权的引导滤波方案。
    FusionMethod method = FusionMethod::GuidedFilter;
    /// 用于比较各输入图像清晰程度的指标。
    FocusMeasure focus_measure = FocusMeasure::ModifiedLaplacian;
    /// 配准模式；默认跳过配准，开启后会裁掉无法被所有图像覆盖的边缘。
    Alignment alignment = Alignment::None;

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

    /// 每张图像 ECC 配准的最大迭代次数，[1, 10000]。
    int alignment_iterations = 150;
    /// ECC 相关系数的收敛阈值，必须为有限正数；与迭代上限共同决定停止条件。
    double alignment_epsilon = 1e-5;
    /// 配准估计时的最长边上限，[16, 8192]，不会放大小图；适用于所有配准方法。
    /// 缩小后的短边仍须至少 16 像素；最终变换在原始分辨率上执行。
    int alignment_max_size = 1200;

    /// 是否在结果中保留每张输入的细节权重图；关闭可减少返回结果占用的内存。
    bool keep_weight_maps = false;

    // 新字段追加到旧字段之后，保持原有按位置聚合初始化的含义。
    /// SIFT 最多保留的特征数，[64, 100000]；仅 FeatureHomography 使用。
    int alignment_max_features = 4000;
    /// 最近邻/次近邻描述子距离比值阈值，有限且位于 (0, 1)；越小筛选越严格。
    double alignment_match_ratio = 0.75;
    /// RANSAC 重投影误差阈值，工作分辨率下的像素数，必须为有限正数。
    double alignment_ransac_threshold = 3.0;
    /// RANSAC 内点占有效匹配的最低比例，有限且位于 (0, 1]；还须至少 6 个内点。
    double alignment_min_inlier_ratio = 0.25;

};

} // 命名空间 mif

