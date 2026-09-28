#pragma once
#include <mif/options.hpp>
#include <opencv2/core.hpp>
namespace mif::detail {
cv::Mat grayscale(const cv::Mat& image);
cv::Mat focusMeasure(const cv::Mat& gray, FocusMeasure method, int window);
}

