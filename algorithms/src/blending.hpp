#pragma once
#include <mif/fusion.hpp>
namespace mif::detail {
cv::Mat blendGuided(const std::vector<cv::Mat>& images,
                    const std::vector<cv::Mat>& base_weights,
                    const std::vector<cv::Mat>& detail_weights,
                    int radius, const ProgressCallback& progress);
cv::Mat blendPyramid(const std::vector<cv::Mat>& images,
                     const std::vector<cv::Mat>& weights,
                     int levels, const ProgressCallback& progress);
void report(const ProgressCallback& callback, int percent, const std::string& stage);
}

