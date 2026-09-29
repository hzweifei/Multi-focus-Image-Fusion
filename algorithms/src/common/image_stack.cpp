#include "common/image_stack.hpp"
#include "common/progress.hpp"
#include <cmath>
#include <limits>

namespace mif::detail {

void validateImages(const std::vector<cv::Mat>& images) {
    if (images.size() < 2 || images.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Provide at least two images (count must fit int32)");
    for (const auto& image : images) {
        if (image.empty() || image.dims != 2 || image.rows < 2 || image.cols < 2)
            throw std::invalid_argument("Images must be nonempty 2-D arrays at least 2 x 2");
        if (image.size() != images.front().size() || image.type() != images.front().type())
            throw std::invalid_argument("All images must have the same dimensions, depth and channels");
        if ((image.channels() != 1 && image.channels() != 3) ||
            (image.depth() != CV_8U && image.depth() != CV_16U && image.depth() != CV_32F))
            throw std::invalid_argument("Supported inputs: uint8, uint16 or float32, grayscale or BGR");
        // checkRange 的上界不包含在内；使用紧邻 1 的下一个 float，允许 1.0 并拒绝更大值。
        const double upper = static_cast<double>(std::nextafter(1.0f, 2.0f));
        if (image.depth() == CV_32F && !cv::checkRange(image, true, nullptr, 0.0, upper))
            throw std::invalid_argument("Float images must contain finite values in [0, 1]");
    }
}

double imageRange(int depth) {
    return depth == CV_8U ? 255.0 : depth == CV_16U ? 65535.0 : 1.0;
}

std::vector<cv::Mat> normalizeImages(const std::vector<cv::Mat>& inputs,
                                     const ProgressCallback& progress) {
    report(progress, 0, "prepare");
    const double range = imageRange(inputs.front().depth());
    std::vector<cv::Mat> images;
    images.reserve(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        report(progress, static_cast<int>(10 * i / inputs.size()), "prepare");
        cv::Mat normalized;
        // convertTo 输出新缓冲区，即使输入本身为 CV_32F，也不把输入交给后续原地处理。
        inputs[i].convertTo(normalized, CV_32F, 1.0 / range);
        images.push_back(normalized);
    }
    report(progress, 10, "prepare");
    return images;
}

} // 命名空间 mif::detail
