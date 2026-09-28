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
        // 基础层 B 是局部均值，细节层 D = I - B；两组权重分别控制色调和清晰纹理。
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
    // pyrDown 的奇数尺寸向上取整。显式记录各层尺寸，重建时才能准确恢复原尺寸。
    // 短边不大于 2 时停止下采样，避免生成没有实际意义的更小尺度。
    while (static_cast<int>(sizes.size()) < levels &&
           std::min(sizes.back().width, sizes.back().height) > 2) {
        sizes.emplace_back((sizes.back().width + 1) / 2, (sizes.back().height + 1) / 2);
    }
    std::vector<cv::Mat> sums, totals;
    // 每个尺度分别累计带权图像与权重和；彩色图使用同一标量权重作用于各通道。
    for (const auto& size : sizes) {
        sums.push_back(cv::Mat::zeros(size, images.front().type()));
        totals.push_back(cv::Mat::zeros(size, CV_32F));
    }
    // 逐张输入、逐层累加，只保留当前图像尺度和全局累加器，减少金字塔的内存占用。
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 70 + static_cast<int>(25 * i / images.size()), "pyramid");
        cv::Mat current = images[i], weight = weights[i];
        for (size_t level = 0; level < sizes.size(); ++level) {
            cv::Mat band, down;
            if (level + 1 < sizes.size()) {
                // 拉普拉斯频带 L_k = G_k - up(G_{k+1})，保存该尺度丢失的细节。
                cv::pyrDown(current, down, sizes[level + 1]);
                cv::Mat up;
                cv::pyrUp(down, up, sizes[level]);
                band = current - up;
            } else {
                // 最粗层直接保存低频图像，保证之后能够完整重建。
                band = current;
            }
            sums[level] += band.mul(expandWeight(weight, band.channels()));
            totals[level] += weight;
            if (level + 1 < sizes.size()) {
                current = down;
                cv::Mat next_weight;
                // 对权重作高斯下采样，使每层权重与该层图像处于相同分辨率。
                cv::pyrDown(weight, next_weight, sizes[level + 1]);
                weight = next_weight;
            }
        }
    }
    for (size_t level = 0; level < sizes.size(); ++level) {
        // 每个尺度都按实际权重和归一化，并给分母设下限以防止数值退化。
        cv::max(totals[level], 1e-12f, totals[level]);
        cv::divide(sums[level], expandWeight(totals[level], sums[level].channels()), sums[level]);
    }
    cv::Mat result = sums.back();
    // 从最粗层开始上采样，并补回各层拉普拉斯频带。
    for (int level = static_cast<int>(sizes.size()) - 2; level >= 0; --level) {
        cv::Mat up;
        cv::pyrUp(result, up, sizes[level]);
        result = up + sums[level];
    }
    return result;
}

} // 命名空间 mif::detail

