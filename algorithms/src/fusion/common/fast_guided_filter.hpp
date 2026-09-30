#pragma once

#include <opencv2/core.hpp>

namespace mif::detail::fusion {

/// 快速引导滤波：低分辨率拟合并平均线性系数，上采样系数后结合完整引导图。
/// 输入为同尺寸 CV_32FC1；使用双精度统计，输出 CV_32F，保留有符号响应。
/// 半径、正则项、下采样倍数由方法入口校验；此算子不裁剪或归一化权重。
cv::Mat fastGuidedFilter(const cv::Mat& guide, const cv::Mat& input,
                         int radius, double epsilon, int subsample);

} // 命名空间 mif::detail::fusion
