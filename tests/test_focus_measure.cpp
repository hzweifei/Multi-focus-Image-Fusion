#include "fixtures.hpp"
#include "focus_measure.hpp"
void testFocusMeasure() {
    auto sharp = texture();
    cv::Mat input, blurred;
    sharp.convertTo(input, CV_32F, 1.0 / 255.0);
    cv::GaussianBlur(input, blurred, {0, 0}, 3);
    for (auto method : {mif::FocusMeasure::ModifiedLaplacian, mif::FocusMeasure::Tenengrad}) {
        const auto a = mif::detail::focusMeasure(input, method, 9);
        const auto b = mif::detail::focusMeasure(blurred, method, 9);
        require(cv::mean(a)[0] > 3 * cv::mean(b)[0], "Focus metric does not distinguish sharp detail");
    }
}

