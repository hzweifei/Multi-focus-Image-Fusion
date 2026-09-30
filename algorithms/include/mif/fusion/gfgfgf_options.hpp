#pragma once

namespace mif {

/// GFG-FGF：类高斯四邻域聚焦度量、Sobel 消歧与两阶段快速引导滤波。
/// 彩色输入转灰度评分和引导，融合权重共同作用于原图的全部颜色通道。
struct GfgfgfOptions {
    /// 局部均值窗口边长，[1, 255] 内的奇数；灰度减去局部均值后取绝对值。
    int difference_window = 7;
    /// 可选工程扩展：保留全图 Scharr 分数不低于最高分数此比例的输入。
    /// 默认 0 保留全部焦面，符合论文；有限且位于 [0, 1]，无纹理时也保留全部。
    double selection_ratio = 0;
    /// 论文 T0：GFG 响应达到阈值时采用 GFG，否则回退到均值残差。
    /// 沿用字段名以兼容调用代码；有限且位于 [0, 1]，作用于归一化图像。
    double difference_threshold = 0.005;
    /// 两阶段引导滤波共用的窗口半径，[1, 255]，单位为输入像素。
    int guided_radius = 5;
    /// 两阶段共用正则项，[1e-6, FLT_MAX]；这是项目默认值，论文未给出该参数。
    double guided_epsilon = 0.3;
    /// 两阶段共用下采样倍数，[1, 16]；1 为全分辨率，4 为项目默认值。
    /// 小图自动降低有效倍数；低分辨率系数上采样后结合原分辨率引导图输出。
    int guided_subsample = 4;
};

} // 命名空间 mif
