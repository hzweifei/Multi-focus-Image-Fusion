#pragma once
#include <mif/fusion.hpp>
namespace mif::detail {
void alignImages(std::vector<cv::Mat>& images, const FusionOptions& options,
                 FusionResult& result, const ProgressCallback& progress);
}

