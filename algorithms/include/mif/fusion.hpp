#pragma once

#include <mif/options.hpp>
#include <opencv2/core.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mif {

class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("Fusion cancelled") {}
};

// Called synchronously on the caller's thread. Return false to cancel.
using ProgressCallback = std::function<bool(int percent, const std::string& stage)>;

struct FusionResult {
    cv::Mat image;                   // Same depth/channels as input.
    cv::Mat focus_indices;           // CV_32SC1, zero-based dominant detail weight.
    std::vector<cv::Mat> weights;    // Optional normalized CV_32FC1 detail weights.
    cv::Rect crop;                   // Output ROI in the first input's coordinates.
    std::vector<cv::Mat> transforms; // 2x3 CV_32F, reference -> source, before crop.
};

// At least two equally sized 2-D images, all with the same type:
// CV_8U / CV_16U / CV_32F, one gray or three BGR channels.
// Float inputs must be finite and in [0, 1]. Inputs are never modified.
// Alignment crops to a rectangle valid in every warped source image.
// Throws invalid_argument, runtime_error, cv::Exception or Cancelled.
FusionResult fuse(const std::vector<cv::Mat>& images,
                  const FusionOptions& options = {},
                  const ProgressCallback& progress = {});

} // namespace mif

