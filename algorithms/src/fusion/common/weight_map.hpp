#pragma once

#include <opencv2/core.hpp>
#include <vector>

namespace mif::detail::fusion {

/// 将一组同尺寸 CV_32FC1 清晰度图转换为归一化决策权重，输入列表不能为空。
/// 与最大响应相差不超过 1e-8 的输入共同分配权重，避免平坦区域总偏向第一张图。
std::vector<cv::Mat> decisionWeights(const std::vector<cv::Mat>& scores);

/// 原地将非空的一组同尺寸、有限 CV_32FC1 权重归一化，使每个像素的权重和约为 1。
/// 负值先截为 0；总和小于等于 1e-12 时改为均匀权重，防止除零和黑洞。
/// 会修改底层数据；调用方不应将这些权重的共享视图作为不可变数据继续使用。
void normalizeWeights(std::vector<cv::Mat>& weights);

/// 返回 CV_32SC1 最大权重索引图；输入为非空、同尺寸 CV_32FC1 权重列表。
/// 输入序号从 0 开始，完全并列时保留较小序号。
cv::Mat dominantIndices(const std::vector<cv::Mat>& weights);

/// 将 CV_32FC1 权重复制到指定数量的通道，以便与多通道图像逐元素相乘。
/// 内部调用仅使用 1 或 3 通道；channels 为 1 时返回共享数据的只读浅拷贝。
cv::Mat expandWeight(const cv::Mat& weight, int channels);

} // 命名空间 mif::detail::fusion

