#include <mif/fusion.hpp>
#include "common/progress.hpp"
#include "fusion/fusion.hpp"
#include "fusion/weight_map.hpp"
#include "registration/registration.hpp"
#include <cmath>
#include <limits>
#include <utility>

// 公开 fuse() 的完整处理流程：校验 → 归一化 → 配准 → 融合 → 整理输出。
// 具体方法及其工具集中在对应子目录，阅读算法库时可从本文件进入。

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
    if (o.alignment != Alignment::None && o.alignment != Alignment::Translation &&
        o.alignment != Alignment::Affine && o.alignment != Alignment::FeatureHomography &&
        o.alignment != Alignment::EccHomography)
        throw std::invalid_argument("Unknown alignment method");
    // 限定窗口、层数和迭代次数，避免异常配置造成过量计算；正则项必须有限且大于零。
    if (o.focus_window < 1 || o.focus_window > 255 || o.focus_window % 2 == 0 ||
        o.base_radius < 1 || o.base_radius > 255 || o.detail_radius < 1 || o.detail_radius > 255 ||
        !std::isfinite(o.base_epsilon) || o.base_epsilon <= 0 ||
        !std::isfinite(o.detail_epsilon) || o.detail_epsilon <= 0 ||
        o.pyramid_levels < 1 || o.pyramid_levels > 16 ||
        o.alignment_iterations < 1 || o.alignment_iterations > 10000 ||
        !std::isfinite(o.alignment_epsilon) || o.alignment_epsilon <= 0 ||
        o.alignment_max_size < 16 || o.alignment_max_size > 8192 ||
        o.alignment_max_features < 64 || o.alignment_max_features > 100000 ||
        !std::isfinite(o.alignment_match_ratio) || o.alignment_match_ratio <= 0 || o.alignment_match_ratio >= 1 ||
        !std::isfinite(o.alignment_ransac_threshold) || o.alignment_ransac_threshold <= 0 ||
        !std::isfinite(o.alignment_min_inlier_ratio) || o.alignment_min_inlier_ratio <= 0 || o.alignment_min_inlier_ratio > 1)
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
    detail::registration::alignImages(images, options, result, progress);

    // 各融合方法独立完成权重生成和图像重建；公共入口只整理最终图像与诊断信息。
    // 方法返回已归一化的细节权重，保证来源索引与 keep_weight_maps 使用同一组数据。
    auto fused = detail::fusion::run(images, options, progress);
    detail::report(progress, 97, "finish");
    // 分层重建可能在强边缘附近产生越界值；先裁到有效区间，再恢复输入的位深。
    cv::max(fused.image, 0, fused.image);
    cv::min(fused.image, 1, fused.image);
    fused.image.convertTo(result.image, inputs.front().type(), range);
    // 来源索引以最终细节权重为准；只有显式请求时才让结果持有整组权重图。
    result.focus_indices = detail::fusion::dominantIndices(fused.weights);
    if (options.keep_weight_maps) result.weights = std::move(fused.weights);
    detail::report(progress, 100, "done");
    return result;
}
} // 命名空间 mif
