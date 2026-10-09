#include "fusion/registry.hpp"
#include <mif/fusion/guided_filter_fusion_options.hpp>
#include "fusion/common/focus_measure.hpp"
#include "fusion/common/guided_filter.hpp"
#include "fusion/common/weight_map.hpp"
#include "fusion/common/parallel_frames.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <stdexcept>
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

/// 校验本方法的清晰度、两组半径及正则项；无效参数抛 invalid_argument。
void validateGuidedFilterOptions(const GuidedFilterFusionOptions& options) {
    validateFocusOptions(options.focus);
    if (options.base_radius < 1 || options.base_radius > 255 ||
        options.detail_radius < 1 || options.detail_radius > 255)
        throw std::invalid_argument("Invalid guided-filter options; check radii");
    validateGuidedEpsilon(options.base_epsilon);
    validateGuidedEpsilon(options.detail_epsilon);
}

/// 双尺度融合：自行评分、生成两组权重、重建图像并提供来源诊断。
/// images 至少两张同尺寸 CV_32F 灰度或 BGR 图像，值域 [0, 1]；options 已校验。
/// 不修改输入；进度回调同步执行，取消及调用者异常原样传播。
MethodResult guidedFilterFusion(const std::vector<cv::Mat>& images,
                                const GuidedFilterFusionOptions& options, const ProgressCallback& progress) {
    auto maps = prepareFocusMaps(images, options.focus, progress);
    std::vector<cv::Mat> base_weights(images.size()), detail_weights(images.size());
    parallelFrames(images.size(), images.front().size(), [&](size_t i) {
        report(progress, 50 + static_cast<int>(20 * i / images.size()), "weights");
    }, [&](size_t i) {
        // 基础层和细节层分别使用自己的半径与正则项，各帧的滤波相互独立。
        base_weights[i] = guidedFilter(maps.guides[i], maps.decisions[i],
                                       options.base_radius, options.base_epsilon);
        detail_weights[i] = guidedFilter(maps.guides[i], maps.decisions[i],
                                         options.detail_radius, options.detail_epsilon);
        // 本帧两组权重已完成，后续重建只使用原图和权重，及时释放评分阶段的缓冲。
        maps.decisions[i].release();
        maps.guides[i].release();
    });
    maps.decisions.clear();
    maps.guides.clear();
    // 各图权重独立滤波后总和不再严格为 1，需要重新归一化。
    // 两组权重分别归一化，避免不同滤波参数影响重建亮度。
    normalizeWeights(detail_weights);
    normalizeWeights(base_weights);
    MethodResult result;
    result.image = blendGuided(images, base_weights, detail_weights, options.base_radius, progress);
    // 来源图描述细节权重的主导输入，不把基础层和细节层简化成一张实际像素贡献图。
    result.source_index_map = dominantIndices(detail_weights);
    result.weight_maps = std::move(detail_weights);
    return result;
}

} // 匿名命名空间

/// 在本文件绑定参数校验和执行；注册表只需显式引用这个函数。
void registerGuidedFilterFusionMethod() {
    registerFusionMethod(validateGuidedFilterOptions, guidedFilterFusion);
}

} // 命名空间 mif::detail::fusion
