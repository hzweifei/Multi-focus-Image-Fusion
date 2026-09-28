#include "fusion/fusion.hpp"
#include "fusion/focus_measure.hpp"
#include "fusion/weight_map.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <utility>

namespace mif::detail::fusion {
namespace {

/// 均值滤波得到基础层 B，细节层 D = I - B；两组权重分别融合色调与清晰纹理。
/// 输入为归一化浮点图像，两组权重均已按像素跨输入归一化。
cv::Mat blendGuided(const std::vector<cv::Mat>& images,
                    const std::vector<cv::Mat>& base_weights,
                    const std::vector<cv::Mat>& detail_weights,
                    int radius, const ProgressCallback& progress) {
    cv::Mat result = cv::Mat::zeros(images.front().size(), images.front().type());
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 70 + static_cast<int>(25 * i / images.size()), "blend");
        cv::Mat base;
        cv::blur(images[i], base, {2 * radius + 1, 2 * radius + 1});
        // 每张图像先累加加权基础层，再补入加权细节，所有颜色通道共用标量权重。
        result += base.mul(expandWeight(base_weights[i], base.channels()));
        const cv::Mat detail = images[i] - base;
        result += detail.mul(expandWeight(detail_weights[i], base.channels()));
    }
    return result;
}

} // 匿名命名空间

MethodResult guidedFilterFusion(const std::vector<cv::Mat>& images,
                                const FusionOptions& options, const ProgressCallback& progress) {
    auto maps = prepareFocusMaps(images, options, progress);
    std::vector<cv::Mat> base_weights, detail_weights;
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 50 + static_cast<int>(20 * i / images.size()), "weights");
        // 基础层和细节层分别使用自己的半径与正则项，逐张图像生成两组权重。
        base_weights.push_back(guidedFilter(maps.guides[i], maps.decisions[i],
                                           options.base_radius, options.base_epsilon));
        detail_weights.push_back(guidedFilter(maps.guides[i], maps.decisions[i],
                                             options.detail_radius, options.detail_epsilon));
    }
    maps.decisions.clear();
    // 各图权重独立滤波后总和不再严格为 1，需要重新归一化。
    // 两组权重分别归一化，避免不同滤波参数影响重建亮度。
    normalizeWeights(detail_weights);
    normalizeWeights(base_weights);
    MethodResult result;
    result.image = blendGuided(images, base_weights, detail_weights, options.base_radius, progress);
    result.weights = std::move(detail_weights);
    return result;
}

} // 命名空间 mif::detail::fusion
