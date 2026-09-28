#include "registration.hpp"
#include "focus_measure.hpp"
#include "blending.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <algorithm>

namespace mif::detail {
namespace {
cv::Rect largestRectangle(const cv::Mat& mask) {
    std::vector<int> heights(mask.cols + 1, 0);
    cv::Rect best;
    long long best_area = 0;
    for (int y = 0; y < mask.rows; ++y) {
        const auto* row = mask.ptr<unsigned char>(y);
        for (int x = 0; x < mask.cols; ++x) heights[x] = row[x] ? heights[x] + 1 : 0;
        std::vector<int> stack;
        for (int x = 0; x <= mask.cols; ++x) {
            while (!stack.empty() && heights[stack.back()] > heights[x]) {
                const int h = heights[stack.back()];
                stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back() + 1;
                const long long area = static_cast<long long>(x - left) * h;
                if (area > best_area) {
                    best_area = area;
                    best = {left, y - h + 1, x - left, h};
                }
            }
            stack.push_back(x);
        }
    }
    return best;
}
}

void alignImages(std::vector<cv::Mat>& images, const FusionOptions& options,
                 FusionResult& result, const ProgressCallback& progress) {
    const cv::Size size = images.front().size();
    result.crop = {0, 0, size.width, size.height};
    for (size_t i = 0; i < images.size(); ++i)
        result.transforms.push_back(cv::Mat::eye(2, 3, CV_32F));
    if (options.alignment == Alignment::None) return;

    const double scale = std::min(1.0, static_cast<double>(options.alignment_max_size) /
                                         std::max(size.width, size.height));
    const cv::Size small(std::max(1, cvRound(size.width * scale)),
                         std::max(1, cvRound(size.height * scale)));
    if (std::min(small.width, small.height) < 16)
        throw std::invalid_argument("ECC alignment requires images at least 16 x 16");
    const double sx = static_cast<double>(small.width) / size.width;
    const double sy = static_cast<double>(small.height) / size.height;
    auto prepare = [small](const cv::Mat& image) {
        cv::Mat gray;
        cv::resize(grayscale(image), gray, small, 0, 0, cv::INTER_AREA);
        cv::GaussianBlur(gray, gray, {5, 5}, 1.2);
        return gray;
    };
    const cv::Mat reference = prepare(images.front());
    cv::Scalar mean, deviation;
    cv::meanStdDev(reference, mean, deviation);
    if (deviation[0] < 1e-6) throw std::runtime_error("ECC cannot align a textureless reference image");
    cv::Mat common(size, CV_8U, cv::Scalar(255));
    const cv::Mat valid(size, CV_32F, cv::Scalar(1));
    for (size_t i = 1; i < images.size(); ++i) {
        report(progress, 10 + static_cast<int>(20 * i / images.size()), "align");
        cv::Mat warp = cv::Mat::eye(2, 3, CV_32F);
        try {
            cv::findTransformECC(reference, prepare(images[i]), warp,
                options.alignment == Alignment::Translation ? cv::MOTION_TRANSLATION : cv::MOTION_AFFINE,
                {cv::TermCriteria::COUNT | cv::TermCriteria::EPS,
                 options.alignment_iterations, options.alignment_epsilon});
        } catch (const cv::Exception& error) {
            throw std::runtime_error("ECC alignment failed for image " + std::to_string(i + 1) +
                                     ": " + error.what());
        }
        // Convert S^-1 * W * S back to original coordinates, including rounded scales.
        warp.at<float>(0, 1) *= static_cast<float>(sy / sx);
        warp.at<float>(1, 0) *= static_cast<float>(sx / sy);
        warp.at<float>(0, 2) /= static_cast<float>(sx);
        warp.at<float>(1, 2) /= static_cast<float>(sy);
        if (!cv::checkRange(warp)) throw std::runtime_error("ECC produced an invalid transform");
        result.transforms[i] = warp;
        cv::Mat aligned, coverage;
        cv::warpAffine(images[i], aligned, warp, size,
                       cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
        cv::warpAffine(valid, coverage, warp, size,
                       cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
        common &= coverage >= 0.9999f;
        images[i] = aligned;
    }
    result.crop = largestRectangle(common);
    if (std::min(result.crop.width, result.crop.height) < 8)
        throw std::runtime_error("Insufficient common image area after alignment");
    for (auto& image : images) image = image(result.crop).clone();
}
}

