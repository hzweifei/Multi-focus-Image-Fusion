#include "fusion/fusion.hpp"
#include "fusion/weight_map.hpp"
#include "common/image_stack.hpp"
#include "common/progress.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mif {
namespace {

/// 这里只校验融合配置；输入图像的共同约束由 common/image_stack 统一维护。
void validateOptions(const FusionOptions& o) {
    if (o.method != FusionMethod::GuidedFilter && o.method != FusionMethod::LaplacianPyramid)
        throw std::invalid_argument("Unknown fusion method");
    if (o.focus_measure != FocusMeasure::ModifiedLaplacian && o.focus_measure != FocusMeasure::Tenengrad)
        throw std::invalid_argument("Unknown focus measure");
    if (o.focus_window < 1 || o.focus_window > 255 || o.focus_window % 2 == 0 ||
        o.base_radius < 1 || o.base_radius > 255 || o.detail_radius < 1 || o.detail_radius > 255 ||
        !std::isfinite(o.base_epsilon) || o.base_epsilon <= 0 ||
        !std::isfinite(o.detail_epsilon) || o.detail_epsilon <= 0 ||
        o.pyramid_levels < 1 || o.pyramid_levels > 16)
        throw std::invalid_argument("Invalid fusion options; check window, radius, levels and epsilon");
}

} // 匿名命名空间

FusionResult fuse(const std::vector<cv::Mat>& inputs, const FusionOptions& options,
                  const ProgressCallback& progress) {
    detail::validateImages(inputs);
    validateOptions(options);
    const auto images = detail::normalizeImages(inputs, progress);
    // 融合方法只接收调用者提供的图像，不依赖配准模块，也不改变输入的空间范围。
    auto fused = detail::fusion::run(images, options, progress);
    detail::report(progress, 97, "finish");
    // 分层重建可能在强边缘附近产生越界值；先裁到有效区间，再恢复原始位深。
    cv::max(fused.image, 0, fused.image);
    cv::min(fused.image, 1, fused.image);
    FusionResult result;
    fused.image.convertTo(result.image, inputs.front().type(), detail::imageRange(inputs.front().depth()));
    result.focus_indices = detail::fusion::dominantIndices(fused.weights);
    if (options.keep_weight_maps) result.weights = std::move(fused.weights);
    detail::report(progress, 100, "done");
    return result;
}

} // 命名空间 mif

namespace mif::detail::fusion {

MethodResult run(const std::vector<cv::Mat>& images,
                 const FusionOptions& options, const ProgressCallback& progress) {
    // 方法只在这里集中分派，各自文件负责权重生成和重建，便于独立阅读与维护。
    switch (options.method) {
    case FusionMethod::GuidedFilter:
        return guidedFilterFusion(images, options, progress);
    case FusionMethod::LaplacianPyramid:
        return laplacianPyramidFusion(images, options, progress);
    default:
        throw std::invalid_argument("Unknown fusion method");
    }
}

} // 命名空间 mif::detail::fusion
