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

cv::Mat fastGuidedFilter(const cv::Mat& guide, const cv::Mat& input,
                         int radius, double epsilon, int subsample) {
    CV_Assert(guide.type() == CV_32FC1 && input.type() == CV_32FC1 && guide.size() == input.size());
    CV_Assert(radius >= 1 && epsilon > 0 && subsample >= 1);
    // 极小/窄图避免把短边压成单像素，使输入支持与其他融合方法一致。
    const int factor = std::min(subsample, std::max(1, std::min(guide.cols, guide.rows) / 2));
    cv::Mat small_guide, small_input;
    if (factor == 1) {
        small_guide = guide;
        small_input = input;
    } else {
        const cv::Size size(1 + (guide.cols - 1) / factor, 1 + (guide.rows - 1) / factor);
        cv::resize(guide, small_guide, size, 0, 0, cv::INTER_LINEAR);
        cv::resize(input, small_input, size, 0, 0, cv::INTER_LINEAR);
    }
    cv::Mat g, p;
    small_guide.convertTo(g, CV_64F);
    small_input.convertTo(p, CV_64F);
    const int small_radius = std::max(1, static_cast<int>(std::lround(static_cast<double>(radius) / factor)));
    const cv::Mat mean_g = localMean(g, small_radius);
    const cv::Mat mean_p = localMean(p, small_radius);
    cv::Mat variance = localMean(g.mul(g), small_radius) - mean_g.mul(mean_g);
    cv::max(variance, 0, variance);
    const cv::Mat covariance = localMean(g.mul(p), small_radius) - mean_g.mul(mean_p);
    cv::Mat a;
    cv::divide(covariance, variance + epsilon, a);
    const cv::Mat b = mean_p - a.mul(mean_g);
    cv::Mat mean_a = localMean(a, small_radius);
    cv::Mat mean_b = localMean(b, small_radius);
    if (factor != 1) {
        cv::resize(mean_a, mean_a, guide.size(), 0, 0, cv::INTER_LINEAR);
        cv::resize(mean_b, mean_b, guide.size(), 0, 0, cv::INTER_LINEAR);
    }
    // 最终恢复输出使用原图，不能把低分辨率输出直接放大替代。
    // 逐行恢复，避免再分配完整分辨率的双精度引导图和双精度输出图。
    cv::Mat result(guide.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, guide.rows), [&](const cv::Range& rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const double* a_row = mean_a.ptr<double>(y);
            const double* b_row = mean_b.ptr<double>(y);
            const float* g_row = guide.ptr<float>(y);
            float* output = result.ptr<float>(y);
            for (int x = 0; x < guide.cols; ++x)
                output[x] = static_cast<float>(a_row[x] * g_row[x] + b_row[x]);
        }
    });
    return result;
}

} // 命名空间 mif::detail::fusion
