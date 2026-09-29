#include "fusion/laplacian_pyramid/laplacian_pyramid.hpp"
#include "fusion/common/focus_measure.hpp"
#include "fusion/common/guided_filter.hpp"
#include "fusion/common/weight_map.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace mif::detail::fusion {
namespace {

/// 用权重的高斯金字塔融合图像的拉普拉斯金字塔，返回同尺寸浮点图像。
/// 逐张输入、逐层累加，避免同时持有每张图像的完整金字塔。
cv::Mat blendPyramid(const std::vector<cv::Mat>& images,
                     const std::vector<cv::Mat>& weights,
                     int levels, const ProgressCallback& progress) {
    std::vector<cv::Size> sizes{images.front().size()};
    // pyrDown 的奇数尺寸向上取整，显式记录各层尺寸以便重建原始大小。
    // 短边不大于 2 时停止下采样，实际层数可能少于请求值。
    while (static_cast<int>(sizes.size()) < levels &&
           std::min(sizes.back().width, sizes.back().height) > 2) {
        sizes.emplace_back((sizes.back().width + 1) / 2, (sizes.back().height + 1) / 2);
    }
    std::vector<cv::Mat> sums, totals;
    // 各尺度分别累计带权图像与权重和；彩色图的所有通道共用同一标量权重。
    for (const auto& size : sizes) {
        sums.push_back(cv::Mat::zeros(size, images.front().type()));
        totals.push_back(cv::Mat::zeros(size, CV_32F));
    }
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 70 + static_cast<int>(25 * i / images.size()), "pyramid");
        cv::Mat current = images[i], weight = weights[i];
        for (size_t level = 0; level < sizes.size(); ++level) {
            cv::Mat band, down;
            if (level + 1 < sizes.size()) {
                // L_k = G_k - up(G_{k+1})，保存该尺度下采样时丢失的细节。
                cv::pyrDown(current, down, sizes[level + 1]);
                cv::Mat up;
                cv::pyrUp(down, up, sizes[level]);
                band = current - up;
            } else {
                // 最粗层直接保留低频图像，作为重建的起点。
                band = current;
            }
            sums[level] += band.mul(expandWeight(weight, band.channels()));
            totals[level] += weight;
            if (level + 1 < sizes.size()) {
                current = down;
                cv::Mat next_weight;
                // 对权重作高斯下采样，使它与下一层图像使用相同的分辨率。
                cv::pyrDown(weight, next_weight, sizes[level + 1]);
                weight = next_weight;
            }
        }
    }
    for (size_t level = 0; level < sizes.size(); ++level) {
        // 按各尺度实际权重和归一化，并给分母设下限，避免数值退化时除零。
        cv::max(totals[level], 1e-12f, totals[level]);
        cv::divide(sums[level], expandWeight(totals[level], sums[level].channels()), sums[level]);
    }
    cv::Mat result = sums.back();
    // 从最粗层逐级上采样并补回细节，直到恢复原始图像尺寸。
    for (int level = static_cast<int>(sizes.size()) - 2; level >= 0; --level) {
        cv::Mat up;
        cv::pyrUp(result, up, sizes[level]);
        result = up + sums[level];
    }
    return result;
}

} // 匿名命名空间

void validateLaplacianPyramidOptions(const LaplacianPyramidOptions& options) {
    validateFocusOptions(options.focus);
    if (options.detail_radius < 1 || options.detail_radius > 255 ||
        options.levels < 1 || options.levels > 16)
        throw std::invalid_argument("Invalid Laplacian-pyramid options; check radius and levels");
    validateGuidedEpsilon(options.detail_epsilon);
}

MethodResult laplacianPyramidFusion(const std::vector<cv::Mat>& images,
                                    const LaplacianPyramidOptions& options, const ProgressCallback& progress) {
    auto maps = prepareFocusMaps(images, options.focus, progress);
    std::vector<cv::Mat> detail_weights;
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 50 + static_cast<int>(20 * i / images.size()), "weights");
        // 金字塔直接从细节权重构建各尺度权重，不生成基础层权重。
        detail_weights.push_back(guidedFilter(maps.guides[i], maps.decisions[i],
                                             options.detail_radius, options.detail_epsilon));
    }
    maps.decisions.clear();
    normalizeWeights(detail_weights);
    MethodResult result;
    result.image = blendPyramid(images, detail_weights, options.levels, progress);
    // 诊断使用全分辨率细节权重；它不包含重建时各尺度平滑后的全部贡献。
    result.focus_indices = dominantIndices(detail_weights);
    result.weights = std::move(detail_weights);
    return result;
}

} // 命名空间 mif::detail::fusion
