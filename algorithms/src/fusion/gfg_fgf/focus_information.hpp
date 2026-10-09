#pragma once

#include <opencv2/core.hpp>
#include <vector>

namespace mif::detail::fusion {

/// 分别保留论文算子的完整 G、R，以及原始 G 达到阈值的位置（CV_8U，0/255）。
struct GfgFocusInformation {
    cv::Mat gradient;
    cv::Mat residual;
    cv::Mat gradient_candidates;
};

GfgFocusInformation gfgFocusInformation(const cv::Mat& guide, int window, double threshold);

/// G、R 分别滤波后决策：有 G 候选时只比较候选的 G 路，否则比较全部 R 路。
/// 近似平局比较同一路完整响应的 3x3 Sobel；仍无法区分时等权。
std::vector<cv::Mat> gfgPriorityDecisionWeights(const std::vector<cv::Mat>& gradient_responses,
                                               const std::vector<cv::Mat>& residual_responses,
                                               const std::vector<cv::Mat>& gradient_candidates);

} // 命名空间 mif::detail::fusion
