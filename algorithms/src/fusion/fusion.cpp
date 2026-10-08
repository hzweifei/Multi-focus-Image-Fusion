#include <mif/fusion.hpp>
#include "fusion/registry.hpp"
#include "common/image_stack.hpp"
#include "common/progress.hpp"
#include <stdexcept>
#include <utility>

namespace mif {

FusionResult fuse(const std::vector<cv::Mat>& inputs, const FusionOptionsBase& options,
                  const ProgressCallback& progress) {
    detail::validateImages(inputs);
    const auto method = detail::fusion::findFusionMethod(options);
    method.validate(options);
    const auto images = detail::normalizeImages(inputs, progress);
    // 融合方法只接收调用者提供的图像，不依赖配准模块，也不改变输入的空间范围。
    auto fused = method.run(images, options, progress);
    // 注册方法必须遵守公共输出契约，防止错误结果进入重建和类型转换。
    if (fused.image.size() != inputs.front().size() ||
        fused.image.type() != CV_MAKETYPE(CV_32F, inputs.front().channels()) ||
        !cv::checkRange(fused.image))
        throw std::runtime_error("Fusion method returned an invalid image");
    detail::report(progress, 97, "finish");
    // 分层重建可能在强边缘附近产生越界值；先裁到有效区间，再恢复原始位深。
    cv::max(fused.image, 0, fused.image);
    cv::min(fused.image, 1, fused.image);
    FusionResult result;
    fused.image.convertTo(result.image, inputs.front().type(), detail::imageRange(inputs.front().depth()));
    result.source_index_map = std::move(fused.source_index_map);
    if (options.include_weight_maps) result.weight_maps = std::move(fused.weight_maps);
    detail::report(progress, 100, "done");
    return result;
}

} // 命名空间 mif
