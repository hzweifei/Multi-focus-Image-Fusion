#include <mif/fusion.hpp>
#include "blending.hpp"
#include "focus_measure.hpp"
#include "registration.hpp"
#include "weight_map.hpp"
#include <cmath>
#include <limits>

namespace mif {
namespace {

/// 在分配工作图像之前统一校验输入和配置，使两种算法具有相同的输入约束。
/// 逐一匹配受支持的枚举项，也能拦截跨语言接口传入的非法枚举值。
void validate(const std::vector<cv::Mat>& images, const FusionOptions& o) {
    if (images.size() < 2 || images.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Provide at least two images (count must fit int32)");
    if (o.method != FusionMethod::GuidedFilter && o.method != FusionMethod::LaplacianPyramid)
        throw std::invalid_argument("Unknown fusion method");
    if (o.focus_measure != FocusMeasure::ModifiedLaplacian && o.focus_measure != FocusMeasure::Tenengrad)
        throw std::invalid_argument("Unknown focus measure");
    if (o.alignment != Alignment::None && o.alignment != Alignment::Translation && o.alignment != Alignment::Affine)
        throw std::invalid_argument("Unknown alignment method");
    // 限定窗口、层数和迭代次数，避免异常配置造成过量计算；正则项必须有限且大于零。
    if (o.focus_window < 1 || o.focus_window > 255 || o.focus_window % 2 == 0 ||
        o.base_radius < 1 || o.base_radius > 255 || o.detail_radius < 1 || o.detail_radius > 255 ||
        !std::isfinite(o.base_epsilon) || o.base_epsilon <= 0 ||
        !std::isfinite(o.detail_epsilon) || o.detail_epsilon <= 0 ||
        o.pyramid_levels < 1 || o.pyramid_levels > 16 ||
        o.alignment_iterations < 1 || o.alignment_iterations > 10000 ||
        !std::isfinite(o.alignment_epsilon) || o.alignment_epsilon <= 0 ||
        o.alignment_max_size < 16 || o.alignment_max_size > 8192)
        throw std::invalid_argument("Invalid fusion options; check window, radius, levels and epsilon");
    for (const auto& image : images) {
        if (image.empty() || image.dims != 2 || image.rows < 2 || image.cols < 2)
            throw std::invalid_argument("Images must be nonempty 2-D arrays at least 2 x 2");
        if (image.size() != images.front().size() || image.type() != images.front().type())
            throw std::invalid_argument("All images must have the same dimensions, depth and channels");
        if ((image.channels() != 1 && image.channels() != 3) ||
            (image.depth() != CV_8U && image.depth() != CV_16U && image.depth() != CV_32F))
            throw std::invalid_argument("Supported inputs: uint8, uint16 or float32, grayscale or BGR");
        // checkRange 的上界不包含在内，且会按输入深度取整；使用紧邻 1 的下一个
        // float 值作为上界，既允许合法的 1.0，又拒绝任何大于 1.0 的浮点输入。
        const double upper = static_cast<double>(std::nextafter(1.0f, 2.0f));
        if (image.depth() == CV_32F && !cv::checkRange(image, true, nullptr, 0.0, upper))
            throw std::invalid_argument("Float images must contain finite values in [0, 1]");
    }
}
} // 匿名命名空间

FusionResult fuse(const std::vector<cv::Mat>& inputs, const FusionOptions& options,
                  const ProgressCallback& progress) {
    validate(inputs, options);
    detail::report(progress, 0, "prepare");
    // 统一在 [0, 1] 的 CV_32F 图像上运算，使滤波参数不随输入位深变化。
    // convertTo 创建工作缓冲区，后续配准和裁剪不会写入调用者的输入数据。
    const double range = inputs.front().depth() == CV_8U ? 255.0 :
                         inputs.front().depth() == CV_16U ? 65535.0 : 1.0;
    std::vector<cv::Mat> images;
    for (size_t i = 0; i < inputs.size(); ++i) {
        detail::report(progress, static_cast<int>(10 * i / inputs.size()), "prepare");
        cv::Mat normalized;
        inputs[i].convertTo(normalized, CV_32F, 1.0 / range);
        images.push_back(normalized);
    }
    FusionResult result;
    detail::alignImages(images, options, result, progress);

    // 彩色图仅用灰度计算清晰度和引导权重；融合阶段仍保留原来的全部颜色通道。
    std::vector<cv::Mat> guides, scores;
    for (size_t i = 0; i < images.size(); ++i) {
        detail::report(progress, 30 + static_cast<int>(20 * i / images.size()), "focus");
        guides.push_back(detail::grayscale(images[i]));
        scores.push_back(detail::focusMeasure(guides.back(), options.focus_measure, options.focus_window));
    }
    auto decisions = detail::decisionWeights(scores);
    // 决策图生成后不再需要清晰度响应，及时释放引用以降低多图融合的峰值内存。
    scores.clear();
    std::vector<cv::Mat> base_weights, detail_weights;
    for (size_t i = 0; i < images.size(); ++i) {
        detail::report(progress, 50 + static_cast<int>(20 * i / images.size()), "weights");
        // 基础层使用较平滑的权重，细节层使用更贴近图像边缘的权重。
        // 金字塔方法直接从细节权重构建各尺度权重，因此不计算基础权重。
        if (options.method == FusionMethod::GuidedFilter)
            base_weights.push_back(detail::guidedFilter(guides[i], decisions[i], options.base_radius, options.base_epsilon));
        detail_weights.push_back(detail::guidedFilter(guides[i], decisions[i], options.detail_radius, options.detail_epsilon));
    }
    decisions.clear();
    // 每张图的权重独立滤波后，总和不再严格为 1，必须跨输入重新归一化。
    detail::normalizeWeights(detail_weights);
    cv::Mat output;
    if (options.method == FusionMethod::GuidedFilter) {
        detail::normalizeWeights(base_weights);
        output = detail::blendGuided(images, base_weights, detail_weights, options.base_radius, progress);
    } else {
        output = detail::blendPyramid(images, detail_weights, options.pyramid_levels, progress);
    }
    detail::report(progress, 97, "finish");
    // 分层重建可能在强边缘附近产生越界值；先裁到有效区间，再恢复输入的位深。
    cv::max(output, 0, output);
    cv::min(output, 1, output);
    output.convertTo(result.image, inputs.front().type(), range);
    // 来源索引以最终细节权重为准；只有显式请求时才让结果持有整组权重图。
    result.focus_indices = detail::dominantIndices(detail_weights);
    if (options.keep_weight_maps) result.weights = std::move(detail_weights);
    detail::report(progress, 100, "done");
    return result;
}
} // 命名空间 mif
