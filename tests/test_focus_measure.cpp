#include "fixtures.hpp"
#include "fusion/focus_measure.hpp"

// 相同纹理模糊后，两种清晰度指标的平均响应都应显著下降。
// 在归一化灰度图上直接检查内部指标，以便将评分问题与后续融合问题区分。
void testFocusMeasure() {
    auto sharp = texture();
    cv::Mat input, blurred;
    sharp.convertTo(input, CV_32F, 1.0 / 255.0);
    cv::GaussianBlur(input, blurred, {0, 0}, 3);
    for (auto method : {mif::FocusMeasure::ModifiedLaplacian, mif::FocusMeasure::Tenengrad}) {
        const auto a = mif::detail::fusion::focusMeasure(input, method, 9);
        const auto b = mif::detail::fusion::focusMeasure(blurred, method, 9);
        require(cv::mean(a)[0] > 3 * cv::mean(b)[0], "Focus metric does not distinguish sharp detail");
    }
}

