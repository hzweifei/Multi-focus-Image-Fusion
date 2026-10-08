#pragma once

namespace mif {

/// 灰度图上的清晰度指标；局部响应越大，通常表示该位置越清晰。
enum class FocusMeasure {
    ModifiedLaplacian, ///< 水平、垂直二阶差分绝对值之和，再做局部平均。
    Tenengrad         ///< 水平、垂直 Sobel 梯度平方和，再做局部平均。
};

/// 局部清晰度评分参数；各融合方法分别持有自己的实例，互不共享可变配置。
struct FocusMeasureOptions {
    /// 用于比较各输入图像清晰程度的指标。
    FocusMeasure measure = FocusMeasure::ModifiedLaplacian;
    /// 清晰度响应的平均窗口边长，单位为像素，必须为 [1, 255] 内的奇数。
    int window_size = 9;
};

} // 命名空间 mif
