#include "blending.hpp"
#include "weight_map.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace mif::detail {
void report(const ProgressCallback& callback, int percent, const std::string& stage) {
    if (callback && !callback(percent, stage)) throw Cancelled();
}

cv::Mat blendGuided(const std::vector<cv::Mat>& images,
                    const std::vector<cv::Mat>& base_weights,
                    const std::vector<cv::Mat>& detail_weights,
                    int radius, const ProgressCallback& progress) {
    cv::Mat result = cv::Mat::zeros(images.front().size(), images.front().type());
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 70 + static_cast<int>(25 * i / images.size()), "blend");
        cv::Mat base;
        cv::blur(images[i], base, {2 * radius + 1, 2 * radius + 1});
        result += base.mul(expandWeight(base_weights[i], base.channels()));
        const cv::Mat detail = images[i] - base;
        result += detail.mul(expandWeight(detail_weights[i], base.channels()));
    }
    return result;
}

cv::Mat blendPyramid(const std::vector<cv::Mat>& images,
                     const std::vector<cv::Mat>& weights,
                     int levels, const ProgressCallback& progress) {
    std::vector<cv::Size> sizes{images.front().size()};
    while (static_cast<int>(sizes.size()) < levels &&
           std::min(sizes.back().width, sizes.back().height) > 2) {
        sizes.emplace_back((sizes.back().width + 1) / 2, (sizes.back().height + 1) / 2);
    }
    std::vector<cv::Mat> sums, totals;
    for (const auto& size : sizes) {
        sums.push_back(cv::Mat::zeros(size, images.front().type()));
        totals.push_back(cv::Mat::zeros(size, CV_32F));
    }
    // Stream one source pyramid at a time to avoid storing every full pyramid.
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 70 + static_cast<int>(25 * i / images.size()), "pyramid");
        cv::Mat current = images[i], weight = weights[i];
        for (size_t level = 0; level < sizes.size(); ++level) {
            cv::Mat band, down;
            if (level + 1 < sizes.size()) {
                cv::pyrDown(current, down, sizes[level + 1]);
                cv::Mat up;
                cv::pyrUp(down, up, sizes[level]);
                band = current - up;
            } else {
                band = current;
            }
            sums[level] += band.mul(expandWeight(weight, band.channels()));
            totals[level] += weight;
            if (level + 1 < sizes.size()) {
                current = down;
                cv::Mat next_weight;
                cv::pyrDown(weight, next_weight, sizes[level + 1]);
                weight = next_weight;
            }
        }
    }
    for (size_t level = 0; level < sizes.size(); ++level) {
        cv::max(totals[level], 1e-12f, totals[level]);
        cv::divide(sums[level], expandWeight(totals[level], sums[level].channels()), sums[level]);
    }
    cv::Mat result = sums.back();
    for (int level = static_cast<int>(sizes.size()) - 2; level >= 0; --level) {
        cv::Mat up;
        cv::pyrUp(result, up, sizes[level]);
        result = up + sums[level];
    }
    return result;
}
}

