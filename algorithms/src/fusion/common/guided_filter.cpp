#include "fusion/common/guided_filter.hpp"
#include <opencv2/ximgproc/edge_filter.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mif::detail::fusion {

void validateGuidedEpsilon(double epsilon) {
    if (!std::isfinite(epsilon) || epsilon < 1e-6 || epsilon > std::numeric_limits<float>::max())
        throw std::invalid_argument("Guided-filter epsilon must be in [1e-6, FLT_MAX] for float32 stability");
}

cv::Mat guidedFilter(const cv::Mat& guide, const cv::Mat& input, int radius, double epsilon) {
    // 使用 opencv_contrib 的正式实现。引导图与输入均为 [0, 1] 的 CV_32FC1，
    // epsilon 直接使用归一化强度的平方单位，radius 对应 2r+1 的完整窗口。
    // 官方默认按完整分辨率计算，未启用近似降采样；其 BORDER_REFLECT 边界
    // 与旧实现的 BORDER_REFLECT_101 不同，因此图像边缘允许出现数值差异。
    cv::Mat result;
    cv::ximgproc::guidedFilter(guide, input, result, radius, epsilon, CV_32F);
    // 局部线性模型可能产生轻微过冲；权重限制在 [0, 1] 后再由调用方跨图归一化。
    cv::max(result, 0, result);
    cv::min(result, 1, result);
    return result;
}

} // 命名空间 mif::detail::fusion
