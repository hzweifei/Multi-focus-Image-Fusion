#pragma once

namespace mif {

/// GFG-FGF：按全图梯度筛选帧，再用局部差异和两阶段引导滤波生成融合权重。
/// 彩色输入沿用参考实现：选择第一张图像总亮度最大的 B/G/R 通道用于评分与引导。
struct GfgfgfOptions {
    /// 局部均值窗口边长，[1, 255] 内的奇数；灰度减去局部均值后取绝对值。
    int difference_window = 7;
    /// 保留全图梯度分数不低于最高分数此比例的输入，有限且位于 [0, 1]。
    /// 0 保留全部输入；所有帧均无纹理时也保留全部，避免空候选集合。
    double selection_ratio = 0.15;
    /// 把不大于该值的局部绝对差异置零，有限且位于 [0, 1]，作用于归一化图像。
    double difference_threshold = 0.005;
    /// 两阶段引导滤波共用的窗口半径，[1, 255]，单位为输入像素。
    int guided_radius = 5;
    /// 两阶段引导滤波共用的正则项，范围 [1e-6, FLT_MAX]，作用于 [0, 1] 图像。
    /// 下限避免官方 float32 滤波在平坦区域数值退化。
    double guided_epsilon = 0.3;
};

} // 命名空间 mif
