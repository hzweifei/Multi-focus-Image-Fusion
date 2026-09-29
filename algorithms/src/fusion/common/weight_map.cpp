#include "fusion/common/weight_map.hpp"
#include <opencv2/imgproc.hpp>

namespace mif::detail::fusion {

std::vector<cv::Mat> decisionWeights(const std::vector<cv::Mat>& scores) {
    cv::Mat maximum = scores.front().clone();
    for (const auto& score : scores) cv::max(maximum, score, maximum);
    std::vector<cv::Mat> weights;
    for (const auto& score : scores) {
        cv::Mat mask, weight;
        // 将近似并列的最大响应共同标记，随后平均分配权重，避免平坦区域偏向首图。
        cv::compare(score, maximum - 1e-8f, mask, cv::CMP_GE);
        mask.convertTo(weight, CV_32F, 1.0 / 255.0);
        weights.push_back(weight);
    }
    normalizeWeights(weights);
    return weights;
}

void normalizeWeights(std::vector<cv::Mat>& weights) {
    cv::Mat total = cv::Mat::zeros(weights.front().size(), CV_32F);
    for (auto& weight : weights) {
        cv::max(weight, 0, weight);
        total += weight;
    }
    const cv::Mat empty = total <= 1e-12f;
    // 对退化像素先替换分母以避免除零，再给每张输入相同权重，保持总和为 1。
    total.setTo(1, empty);
    for (auto& weight : weights) {
        cv::divide(weight, total, weight);
        weight.setTo(1.0 / static_cast<double>(weights.size()), empty);
    }
}

cv::Mat dominantIndices(const std::vector<cv::Mat>& weights) {
    cv::Mat indices = cv::Mat::zeros(weights.front().size(), CV_32S);
    cv::Mat maximum = weights.front().clone();
    for (size_t i = 1; i < weights.size(); ++i) {
        // 仅在严格更大时替换来源，所以并列权重会稳定地保留较小的输入序号。
        const cv::Mat better = weights[i] > maximum;
        indices.setTo(static_cast<int>(i), better);
        cv::max(maximum, weights[i], maximum);
    }
    return indices;
}

cv::Mat expandWeight(const cv::Mat& weight, int channels) {
    // 单通道路径无需额外分配；多通道路径将同一个标量权重用于所有颜色通道。
    if (channels == 1) return weight;
    cv::Mat expanded;
    cv::merge(std::vector<cv::Mat>(channels, weight), expanded);
    return expanded;
}

} // 命名空间 mif::detail::fusion

