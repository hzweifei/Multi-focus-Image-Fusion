#include "fusion/gfg_fgf/focus_information.hpp"
#include "fusion/common/weight_map.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <limits>

namespace mif::detail::fusion {

cv::Mat paperFocusInformation(const cv::Mat& guide, int window, double threshold) {
    CV_Assert(guide.type() == CV_32FC1 && window >= 1 && window % 2 == 1);
    cv::Mat mean, residual, smooth, padded;
    cv::blur(guide, mean, {window, window}, {-1, -1}, cv::BORDER_REFLECT_101);
    cv::absdiff(guide, mean, residual);
    const cv::Mat kernel = (cv::Mat_<float>(3, 3) << 1, 2, 1, 2, 4, 2, 1, 2, 1) / 16.0f;
    cv::filter2D(guide, smooth, CV_32F, kernel, {-1, -1}, 0, cv::BORDER_REFLECT_101);
    cv::copyMakeBorder(guide, padded, 1, 1, 1, 1, cv::BORDER_REFLECT_101);
    cv::Mat information(guide.size(), CV_32F);
    for (int y = 0; y < guide.rows; ++y) {
        const float* top = padded.ptr<float>(y);
        const float* middle = padded.ptr<float>(y + 1);
        const float* bottom = padded.ptr<float>(y + 2);
        const float* local = smooth.ptr<float>(y);
        const float* rough = residual.ptr<float>(y);
        float* output = information.ptr<float>(y);
        for (int x = 0; x < guide.cols; ++x) {
            const float horizontal = std::abs(middle[x + 2] - local[x]) + std::abs(middle[x] - local[x]);
            const float vertical = std::abs(top[x + 1] - local[x]) + std::abs(bottom[x + 1] - local[x]);
            const float gradient = horizontal * horizontal + vertical * vertical;
            // 公式 (8)-(9) 合并；弱梯度使用残差，而不是把该位置清零。
            output[x] = gradient >= threshold ? gradient : rough[x];
        }
    }
    return information;
}

std::vector<cv::Mat> paperDecisionWeights(const std::vector<cv::Mat>& responses) {
    CV_Assert(!responses.empty());
    constexpr float tolerance = 1e-8f;
    cv::Mat maximum = responses.front().clone();
    for (const auto& response : responses) cv::max(maximum, response, maximum);
    cv::Mat maximum_gradient(maximum.size(), CV_32F, cv::Scalar(std::numeric_limits<float>::lowest()));
    std::vector<cv::Mat> decisions;
    decisions.reserve(responses.size());
    for (const auto& response : responses) {
        cv::Mat gx, gy, gradient, candidate;
        cv::Sobel(response, gx, CV_32F, 1, 0, 3, 1, 0, cv::BORDER_REFLECT_101);
        cv::Sobel(response, gy, CV_32F, 0, 1, 3, 1, 0, cv::BORDER_REFLECT_101);
        gradient = cv::abs(gx) + cv::abs(gy);
        cv::compare(response, maximum - tolerance, candidate, cv::CMP_GE);
        // 只允许主分数并列最大的焦面参与 Sobel 比较。
        gradient.setTo(std::numeric_limits<float>::lowest(), ~candidate);
        cv::max(maximum_gradient, gradient, maximum_gradient);
        decisions.push_back(std::move(gradient));
    }
    for (auto& decision : decisions) {
        cv::Mat mask;
        cv::compare(decision, maximum_gradient - tolerance, mask, cv::CMP_GE);
        mask.convertTo(decision, CV_32F, 1.0 / 255.0);
    }
    normalizeWeights(decisions);
    return decisions;
}

} // 命名空间 mif::detail::fusion
