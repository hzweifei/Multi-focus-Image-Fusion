// 依据付宏语等《多聚焦显微图像融合算法》(2024), DOI:10.3788/LOP232015 的公式 (1)-(15)。
// 可选 Scharr 筛帧沿用 OpenFocus fusion_methods/gfg_fgf.py，提交 bf3a3a15c1c508fbba117f6e98a64e49a087434e。
// 参考项目为 MIT 许可，Copyright (c) 2025 OpenFocus Contributors；完整声明见 THIRD_PARTY_NOTICES.md。
// 默认保留全部焦面；数值容差、剩余平局等权、彩色灰度引导与权重截断为工程补充。
#include "fusion/registry.hpp"
#include <mif/fusion/gfg_fgf_fusion_options.hpp>
#include "fusion/gfg_fgf/focus_information.hpp"
#include "fusion/common/guided_filter.hpp"
#include "fusion/common/weight_map.hpp"
#include "common/grayscale.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mif::detail::fusion {
namespace {

/// 校验筛选比例、局部差异阈值、窗口、引导半径与正则项。
void validateGfgFgfOptions(const GfgFgfFusionOptions& options) {
    if (options.local_mean_window_size < 1 || options.local_mean_window_size > 255 || options.local_mean_window_size % 2 == 0 ||
        !std::isfinite(options.selection_ratio) || options.selection_ratio < 0 || options.selection_ratio > 1 ||
        !std::isfinite(options.gfg_threshold) || options.gfg_threshold < 0 || options.gfg_threshold > 1 ||
        options.guided_radius < 1 || options.guided_radius > 255 ||
        options.guided_subsample_factor < 1 || options.guided_subsample_factor > 16)
        throw std::invalid_argument("Invalid GFG-FGF options; check windows, thresholds and guided epsilon");
    validateGuidedEpsilon(options.guided_epsilon);
}

/// 归一化 CV_32F 灰度/BGR 栈的梯度筛选与两阶段引导滤波融合。
/// 被筛除的原始输入返回零权重；诊断索引始终使用原始输入顺序。
/// 保留输入尺寸与通道，配置应已校验；进度及取消异常原样传播。
MethodResult gfgFgfFusion(const std::vector<cv::Mat>& images, const GfgFgfFusionOptions& options,
                          const ProgressCallback& progress) {
    const auto size = images.front().size();
    std::vector<cv::Mat> guides;
    std::vector<double> scores;
    guides.reserve(images.size());
    scores.reserve(images.size());
    double maximum_score = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 30 + static_cast<int>(15 * i / images.size()), "focus");
        cv::Mat guide = grayscale(images[i]);
        double score = 0;
        if (options.selection_ratio > 0) {
            cv::Mat gx, gy;
            // 保留旧筛帧扩展，但默认不执行，避免丢弃只含少量清晰结构的焦面。
            cv::Scharr(guide, gx, CV_32F, 1, 0, 1, 0, cv::BORDER_REFLECT);
            cv::Scharr(guide, gy, CV_32F, 0, 1, 1, 0, cv::BORDER_REFLECT);
            const cv::Mat energy = gx.mul(gx) + gy.mul(gy);
            const cv::Rect interior = size.width > 2 && size.height > 2
                ? cv::Rect(1, 1, size.width - 2, size.height - 2) : cv::Rect(0, 0, size.width, size.height);
            score = cv::mean(energy(interior))[0];
        }
        scores.push_back(score);
        maximum_score = std::max(maximum_score, score);
        guides.push_back(std::move(guide));
    }

    // 只给保留帧建立响应集合，排除帧的零占位不能压过保留帧的负引导响应。
    // 保留原索引，后面把权重放回完整输入列表；筛选不会重编号诊断数据。
    std::vector<size_t> selected;
    for (size_t i = 0; i < images.size(); ++i)
        if (scores[i] >= options.selection_ratio * maximum_score) selected.push_back(i);
    std::vector<cv::Mat> responses;
    responses.reserve(selected.size());
    for (size_t j = 0; j < selected.size(); ++j) {
        report(progress, 45 + static_cast<int>(15 * j / selected.size()), "focus");
        const auto i = selected[j];
        const cv::Mat information = paperFocusInformation(guides[i], options.local_mean_window_size,
                                                          options.gfg_threshold);
        // 第一遍处理聚焦信息，保留有符号输出；它不是概率或融合权重。
        cv::Mat response = fastGuidedFilter(guides[i], information, options.guided_radius,
                                            options.guided_epsilon, options.guided_subsample_factor);
        responses.push_back(std::move(response));
    }
    report(progress, 60, "weights");
    auto decisions = paperDecisionWeights(responses);
    responses.clear();
    std::vector<cv::Mat> weights;
    weights.reserve(selected.size());
    for (size_t j = 0; j < selected.size(); ++j) {
        report(progress, 60 + static_cast<int>(15 * j / selected.size()), "weights");
        // 第二遍处理决策；系数上采样后仍由原分辨率图像引导，权重裁剪后归一化。
        cv::Mat weight = fastGuidedFilter(guides[selected[j]], decisions[j], options.guided_radius,
                                          options.guided_epsilon, options.guided_subsample_factor);
        decisions[j].release();
        guides[selected[j]].release();
        cv::max(weight, 0, weight);
        cv::min(weight, 1, weight);
        weights.push_back(std::move(weight));
    }
    decisions.clear();
    guides.clear();
    normalizeWeights(weights);
    MethodResult result;
    result.image = cv::Mat::zeros(size, images.front().type());
    result.weight_maps.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i)
        result.weight_maps.push_back(cv::Mat::zeros(size, CV_32F));
    for (size_t j = 0; j < selected.size(); ++j) {
        report(progress, 75 + static_cast<int>(20 * j / selected.size()), "blend");
        const auto i = selected[j];
        result.weight_maps[i] = std::move(weights[j]);
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
