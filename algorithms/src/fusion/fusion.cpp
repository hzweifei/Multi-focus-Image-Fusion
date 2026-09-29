#include <mif/fusion.hpp>
#include "fusion/guided_filter/guided_filter.hpp"
#include "fusion/laplacian_pyramid/laplacian_pyramid.hpp"
#include "fusion/dct/dct.hpp"
#include "fusion/dtcwt/dtcwt.hpp"
#include "fusion/gfgfgf/gfgfgf.hpp"
#include "common/image_stack.hpp"
#include "common/progress.hpp"
#include <stdexcept>
#include <utility>

namespace mif {
namespace {

/// 这里只选择校验器，数值约束由各方法维护；未选中的配置不参与校验。
void validateOptions(const FusionOptions& o) {
    switch (o.method) {
    case FusionMethod::GuidedFilter:
        return detail::fusion::validateGuidedFilterOptions(o.guided_filter);
    case FusionMethod::LaplacianPyramid:
        return detail::fusion::validateLaplacianPyramidOptions(o.laplacian_pyramid);
    case FusionMethod::Dct:
        return detail::fusion::validateDctOptions(o.dct);
    case FusionMethod::Dtcwt:
        return detail::fusion::validateDtcwtOptions(o.dtcwt);
    case FusionMethod::Gfgfgf:
        return detail::fusion::validateGfgfgfOptions(o.gfgfgf);
    default:
        throw std::invalid_argument("Unknown fusion method");
    }
}

/// 各方法只接收自己的配置，独立决定评分、权重、重建及诊断流程。
detail::fusion::MethodResult run(const std::vector<cv::Mat>& images, const FusionOptions& options,
                               const ProgressCallback& progress) {
    switch (options.method) {
    case FusionMethod::GuidedFilter:
        return detail::fusion::guidedFilterFusion(images, options.guided_filter, progress);
    case FusionMethod::LaplacianPyramid:
        return detail::fusion::laplacianPyramidFusion(images, options.laplacian_pyramid, progress);
    case FusionMethod::Dct:
        return detail::fusion::dctFusion(images, options.dct, progress);
    case FusionMethod::Dtcwt:
        return detail::fusion::dtcwtFusion(images, options.dtcwt, progress);
    case FusionMethod::Gfgfgf:
        return detail::fusion::gfgfgfFusion(images, options.gfgfgf, progress);
    default:
        throw std::invalid_argument("Unknown fusion method");
    }
}

} // 匿名命名空间

FusionResult fuse(const std::vector<cv::Mat>& inputs, const FusionOptions& options,
                  const ProgressCallback& progress) {
    detail::validateImages(inputs);
    validateOptions(options);
    const auto images = detail::normalizeImages(inputs, progress);
    // 融合方法只接收调用者提供的图像，不依赖配准模块，也不改变输入的空间范围。
    auto fused = run(images, options, progress);
    detail::report(progress, 97, "finish");
    // 分层重建可能在强边缘附近产生越界值；先裁到有效区间，再恢复原始位深。
    cv::max(fused.image, 0, fused.image);
    cv::min(fused.image, 1, fused.image);
    FusionResult result;
    fused.image.convertTo(result.image, inputs.front().type(), detail::imageRange(inputs.front().depth()));
    result.focus_indices = std::move(fused.focus_indices);
    if (options.keep_weight_maps) result.weights = std::move(fused.weights);
    detail::report(progress, 100, "done");
    return result;
}

} // 命名空间 mif
