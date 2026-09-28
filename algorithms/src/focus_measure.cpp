#include "focus_measure.hpp"
#include <opencv2/imgproc.hpp>

namespace mif::detail {
cv::Mat grayscale(const cv::Mat& image) {
    if (image.channels() == 1) return image;
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

cv::Mat focusMeasure(const cv::Mat& gray, FocusMeasure method, int window) {
    cv::Mat x, y, energy;
    if (method == FocusMeasure::Tenengrad) {
        cv::Sobel(gray, x, CV_32F, 1, 0, 3);
        cv::Sobel(gray, y, CV_32F, 0, 1, 3);
        energy = x.mul(x) + y.mul(y);
    } else {
        const cv::Mat kernel = (cv::Mat_<float>(1, 3) << -1, 2, -1);
        cv::filter2D(gray, x, CV_32F, kernel);
        cv::filter2D(gray, y, CV_32F, kernel.t());
        energy = cv::abs(x) + cv::abs(y);
    }
    cv::boxFilter(energy, energy, CV_32F, {window, window});
    return energy;
}
}

