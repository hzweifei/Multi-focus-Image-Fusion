// G、R 算子源自付宏语等《多聚焦显微图像融合算法》(2024), DOI:10.3788/LOP232015。
// 本项目改为两路独立滤波、跨焦面 G 候选优先；不再采用论文的分段合成评分。
// 融合阶段参考 OpenFocus fusion_methods/gfg_fgf.py，提交 bf3a3a15c1c508fbba117f6e98a64e49a087434e。
// 参考项目为 MIT 许可，Copyright (c) 2025 OpenFocus Contributors；完整声明见 THIRD_PARTY_NOTICES.md。
// 全部焦面参与逐像素决策；数值容差、剩余平局等权、彩色灰度引导与权重截断为工程补充。
#include "fusion/registry.hpp"
#include <mif/fusion/gfg_fgf_fusion_options.hpp>
#include "fusion/gfg_fgf/focus_information.hpp"
#include "fusion/common/guided_filter.hpp"
#include "fusion/common/weight_map.hpp"
#include "fusion/common/parallel_frames.hpp"
#include "common/grayscale.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mif::detail::fusion {
namespace {

/// 校验局部差异阈值、窗口、引导半径与正则项。
void validateGfgFgfOptions(const GfgFgfFusionOptions& options) {
    if (options.local_mean_window_size < 1 || options.local_mean_window_size > 255 || options.local_mean_window_size % 2 == 0 ||
        !std::isfinite(options.gfg_threshold) || options.gfg_threshold < 0 || options.gfg_threshold > 1 ||
        options.guided_radius < 1 || options.guided_radius > 255 ||
        options.guided_subsample_factor < 1 || options.guided_subsample_factor > 16)
        throw std::invalid_argument("Invalid GFG-FGF options; check windows, thresholds and guided epsilon");
    validateGuidedEpsilon(options.guided_epsilon);
}

/// 归一化 CV_32F 灰度/BGR 栈的两阶段引导滤波融合，全部输入参与逐像素决策。
/// 权重与诊断索引始终使用原始输入顺序。
/// 保留输入尺寸与通道，配置应已校验；进度及取消异常原样传播。
MethodResult gfgFgfFusion(const std::vector<cv::Mat>& images, const GfgFgfFusionOptions& options,
                          const ProgressCallback& progress) {
    const auto size = images.front().size();
    std::vector<cv::Mat> guides(images.size());
    parallelFrames(images.size(), size, [&](size_t i) {
        report(progress, 30 + static_cast<int>(15 * i / images.size()), "focus");
    }, [&](size_t i) {
        guides[i] = grayscale(images[i]);
    });
    std::vector<cv::Mat> gradient_responses(images.size()), residual_responses(images.size()),
                         gradient_candidates(images.size());
    parallelFrames(images.size(), size, [&](size_t j) {
        report(progress, 45 + static_cast<int>(15 * j / images.size()), "focus");
    }, [&](size_t j) {
        auto information = gfgFocusInformation(guides[j], options.local_mean_window_size, options.gfg_threshold);
        // 第一轮分别处理完整 G、R，保留有符号输出；原始 G 的资格掩码不参与滤波。
        // 两路共用当前帧的引导统计；循环结束即释放缓存，不为整批图像累积统计量。
        const FastGuidedFilter filter(guides[j], options.guided_radius,
                                      options.guided_epsilon, options.guided_subsample_factor);
        gradient_responses[j] = filter.filter(information.gradient);
        residual_responses[j] = filter.filter(information.residual);
        gradient_candidates[j] = std::move(information.gradient_candidates);
    });
    report(progress, 60, "weights");
    auto decisions = gfgPriorityDecisionWeights(gradient_responses, residual_responses, gradient_candidates);
    gradient_responses.clear();
    residual_responses.clear();
    gradient_candidates.clear();
    std::vector<cv::Mat> weights(images.size());
    parallelFrames(images.size(), size, [&](size_t j) {
        report(progress, 60 + static_cast<int>(15 * j / images.size()), "weights");
    }, [&](size_t j) {
        // 第二遍处理决策；系数上采样后仍由原分辨率图像引导，权重裁剪后归一化。
        cv::Mat weight = fastGuidedFilter(guides[j], decisions[j], options.guided_radius,
                                          options.guided_epsilon, options.guided_subsample_factor);
        decisions[j].release();
        guides[j].release();
        cv::max(weight, 0, weight);
        cv::min(weight, 1, weight);
        weights[j] = std::move(weight);
    });
    decisions.clear();
    guides.clear();
    normalizeWeights(weights);
    MethodResult result;
    result.image = cv::Mat::zeros(size, images.front().type());
    result.weight_maps = std::move(weights);
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 75 + static_cast<int>(20 * i / images.size()), "blend");
        result.image += images[i].mul(expandWeight(result.weight_maps[i], images.front().channels()));
    }
    result.source_index_map = dominantIndices(result.weight_maps);
    return result;
}

} // 匿名命名空间

/// 在本文件绑定参数校验和执行；注册表只需显式引用这个函数。
void registerGfgFgfFusionMethod() {
    registerFusionMethod(validateGfgFgfOptions, gfgFgfFusion);
}

} // 命名空间 mif::detail::fusion
