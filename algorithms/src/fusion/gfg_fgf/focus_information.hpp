#pragma once

#include <opencv2/core.hpp>
#include <vector>

namespace mif::detail::fusion {

/// 论文公式 (1)-(9)：均值残差和类高斯四邻域梯度按阈值分段选择。
cv::Mat paperFocusInformation(const cv::Mat& guide, int window, double threshold);

/// 跨焦面最大响应；近似平局比较聚焦图的 3x3 Sobel 响应。
/// 两种响应均无法区分时等权处理，保留平坦区域和相同输入的对称性。
std::vector<cv::Mat> paperDecisionWeights(const std::vector<cv::Mat>& responses);

} // 命名空间 mif::detail::fusion
