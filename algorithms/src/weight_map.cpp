#include "weight_map.hpp"
#include <opencv2/imgproc.hpp>

namespace mif::detail {
cv::Mat guidedFilter(const cv::Mat& guide, const cv::Mat& input, int radius, double epsilon) {
    const cv::Size size(2 * radius + 1, 2 * radius + 1);
    auto mean = [size](const cv::Mat& image) {
        cv::Mat result;
        cv::boxFilter(image, result, CV_32F, size);
        return result;
    };
    const cv::Mat mean_i = mean(guide), mean_p = mean(input);
    cv::Mat variance = mean(guide.mul(guide)) - mean_i.mul(mean_i);
    cv::max(variance, 0, variance);
    cv::Mat a;
    cv::divide(mean(guide.mul(input)) - mean_i.mul(mean_p), variance + epsilon, a);
    const cv::Mat b = mean_p - a.mul(mean_i);
    cv::Mat result = mean(a).mul(guide) + mean(b);
    cv::max(result, 0, result);
    cv::min(result, 1, result);
    return result;
}

std::vector<cv::Mat> decisionWeights(const std::vector<cv::Mat>& scores) {
    cv::Mat maximum = scores.front().clone();
    for (const auto& score : scores) cv::max(maximum, score, maximum);
    std::vector<cv::Mat> weights;
    for (const auto& score : scores) {
        cv::Mat mask, weight;
        // Share ties instead of arbitrarily preferring the first image in flat areas.
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
        const cv::Mat better = weights[i] > maximum;
        indices.setTo(static_cast<int>(i), better);
        cv::max(maximum, weights[i], maximum);
    }
    return indices;
}

cv::Mat expandWeight(const cv::Mat& weight, int channels) {
    if (channels == 1) return weight;
    cv::Mat expanded;
    cv::merge(std::vector<cv::Mat>(channels, weight), expanded);
    return expanded;
}
}

