#include "fusion/common/guided_filter.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace mif::detail::fusion {
namespace {

cv::Mat localMean(const cv::Mat& input, int radius) {
    cv::Mat mean;
    cv::boxFilter(input, mean, CV_64F, {2 * radius + 1, 2 * radius + 1},
                  {-1, -1}, true, cv::BORDER_REFLECT);
    return mean;
}

} // 匿名命名空间

FastGuidedFilter::FastGuidedFilter(const cv::Mat& guide, int radius, double epsilon, int subsample)
    : guide_(guide) {
    CV_Assert(!guide.empty() && guide.type() == CV_32FC1);
    CV_Assert(radius >= 1 && epsilon > 0 && subsample >= 1);
    // 极小/窄图避免把短边压成单像素，使输入支持与其他融合方法一致。
    factor_ = std::min(subsample, std::max(1, std::min(guide.cols, guide.rows) / 2));
    cv::Mat small_guide;
    if (factor_ == 1) {
        small_guide = guide;
    } else {
        const cv::Size size(1 + (guide.cols - 1) / factor_, 1 + (guide.rows - 1) / factor_);
        cv::resize(guide, small_guide, size, 0, 0, cv::INTER_LINEAR);
    }
    small_guide.convertTo(small_guide_, CV_64F);
    small_radius_ = std::max(1, static_cast<int>(std::lround(static_cast<double>(radius) / factor_)));
    mean_guide_ = localMean(small_guide_, small_radius_);
    cv::Mat variance = localMean(small_guide_.mul(small_guide_), small_radius_) - mean_guide_.mul(mean_guide_);
    cv::max(variance, 0, variance);
    denominator_ = variance + epsilon;
}

cv::Mat FastGuidedFilter::filter(const cv::Mat& input) const {
    CV_Assert(input.type() == CV_32FC1 && guide_.size() == input.size());
    cv::Mat small_input;
    if (factor_ == 1) small_input = input;
    else cv::resize(input, small_input, small_guide_.size(), 0, 0, cv::INTER_LINEAR);
    cv::Mat p;
    small_input.convertTo(p, CV_64F);
    const cv::Mat mean_p = localMean(p, small_radius_);
    const cv::Mat covariance = localMean(small_guide_.mul(p), small_radius_) - mean_guide_.mul(mean_p);
    cv::Mat a;
    cv::divide(covariance, denominator_, a);
    const cv::Mat b = mean_p - a.mul(mean_guide_);
    cv::Mat mean_a = localMean(a, small_radius_);
    cv::Mat mean_b = localMean(b, small_radius_);
    if (factor_ != 1) {
        cv::resize(mean_a, mean_a, guide_.size(), 0, 0, cv::INTER_LINEAR);
        cv::resize(mean_b, mean_b, guide_.size(), 0, 0, cv::INTER_LINEAR);
    }
    // 最终恢复输出使用原图，不能把低分辨率输出直接放大替代。
    // 逐行恢复，避免再分配完整分辨率的双精度引导图和双精度输出图。
    cv::Mat result(guide_.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, guide_.rows), [&](const cv::Range& rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const double* a_row = mean_a.ptr<double>(y);
            const double* b_row = mean_b.ptr<double>(y);
            const float* g_row = guide_.ptr<float>(y);
            float* output = result.ptr<float>(y);
            for (int x = 0; x < guide_.cols; ++x)
                output[x] = static_cast<float>(a_row[x] * g_row[x] + b_row[x]);
        }
    });
    return result;
}

cv::Mat fastGuidedFilter(const cv::Mat& guide, const cv::Mat& input,
                         int radius, double epsilon, int subsample) {
    return FastGuidedFilter(guide, radius, epsilon, subsample).filter(input);
}

} // 命名空间 mif::detail::fusion
