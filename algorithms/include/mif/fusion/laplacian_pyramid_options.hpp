#pragma once

#include <mif/fusion/focus_options.hpp>

namespace mif {

/// 拉普拉斯金字塔融合的独立参数；本方法从全分辨率细节权重构建各尺度权重。
struct LaplacianPyramidOptions {
    /// 本方法自己的清晰度评分配置，不会在金字塔各层重新计算评分。
    FocusOptions focus;
    /// 细节权重引导滤波半径，[1, 255]，单位为输入图像像素。
    int detail_radius = 3;
    /// 细节权重正则项，范围 [1e-6, FLT_MAX]，与 [0, 1] 灰度图的局部方差相加。
    /// 下限避免官方 float32 滤波在平坦区域数值退化。
    double detail_epsilon = 0.0001;
    /// 金字塔层数上限，[1, 16]，包含最粗层；实际层数还受图像尺寸限制。
    /// 设为 1 时直接使用全分辨率权重进行单尺度融合。
    int levels = 5;
};

} // 命名空间 mif
