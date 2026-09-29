// 算法流程参考 OpenFocus 的 fusion_methods/gfg_fgf.py，提交 bf3a3a15c1c508fbba117f6e98a64e49a087434e。
// 参考项目为 MIT 许可，Copyright (c) 2025 OpenFocus Contributors；完整声明见 THIRD_PARTY_NOTICES.md。
// 本文件独立实现梯度筛帧、局部差异和两阶段引导滤波，沿用参考默认值及选通道策略。
// 与上游相比，补齐灰度/高位深/小图支持、无纹理等权处理与筛选后原输入索引映射。
#include "fusion/gfgfgf/gfgfgf.hpp"
#include "fusion/common/guided_filter.hpp"
#include "fusion/common/weight_map.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc/edge_filter.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mif::detail::fusion {

void validateGfgfgfOptions(const GfgfgfOptions& options) {
    if (options.difference_window < 1 || options.difference_window > 255 || options.difference_window % 2 == 0 ||
        !std::isfinite(options.selection_ratio) || options.selection_ratio < 0 || options.selection_ratio > 1 ||
        !std::isfinite(options.difference_threshold) || options.difference_threshold < 0 || options.difference_threshold > 1 ||
        options.guided_radius < 1 || options.guided_radius > 255)
        throw std::invalid_argument("Invalid GFG-FGF options; check windows, thresholds and guided epsilon");
    validateGuidedEpsilon(options.guided_epsilon);
}

MethodResult gfgfgfFusion(const std::vector<cv::Mat>& images, const GfgfgfOptions& options,
                          const ProgressCallback& progress) {
    const auto size = images.front().size();
    int channel = 0;
    if (images.front().channels() == 3) {
        const cv::Scalar totals = cv::sum(images.front());
        for (int c = 1; c < 3; ++c) if (totals[c] > totals[channel]) channel = c;
    }
    std::vector<cv::Mat> guides;
    std::vector<double> scores;
    guides.reserve(images.size());
    scores.reserve(images.size());
    double maximum_score = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 30 + static_cast<int>(15 * i / images.size()), "focus");
        cv::Mat guide;
        if (images[i].channels() == 1) guide = images[i];
        else cv::extractChannel(images[i], guide, channel);
        cv::Mat gx, gy;
        // Scharr 核等同参考实现的 [-3,-10,-3;0,0,0;3,10,3] 及其转置。
        cv::Scharr(guide, gx, CV_32F, 1, 0, 1, 0, cv::BORDER_REFLECT);
        cv::Scharr(guide, gy, CV_32F, 0, 1, 1, 0, cv::BORDER_REFLECT);
        const cv::Mat energy = gx.mul(gx) + gy.mul(gy);
        // 常规尺寸排除一圈边界；2 像素小图改用全图，避免空 ROI 和 NaN 平均值。
        const cv::Rect interior = size.width > 2 && size.height > 2
            ? cv::Rect(1, 1, size.width - 2, size.height - 2) : cv::Rect(0, 0, size.width, size.height);
        const double score = cv::mean(energy(interior))[0];
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
        cv::Mat local_mean, difference, response;
        cv::blur(guides[i], local_mean, {options.difference_window, options.difference_window});
        cv::absdiff(guides[i], local_mean, difference);
        cv::threshold(difference, difference, options.difference_threshold, 0, cv::THRESH_TOZERO);
        // 第一遍处理的是响应，不是概率权重。官方滤波允许有符号输出，不能提前裁到 [0,1]。
        cv::ximgproc::guidedFilter(guides[i], difference, response,
                                  options.guided_radius, options.guided_epsilon, CV_32F);
        responses.push_back(std::move(response));
    }
    report(progress, 60, "weights");
    auto decisions = decisionWeights(responses);
    responses.clear();
    std::vector<cv::Mat> weights;
    weights.reserve(selected.size());
    for (size_t j = 0; j < selected.size(); ++j) {
        report(progress, 60 + static_cast<int>(15 * j / selected.size()), "weights");
        // 第二遍滤波决策权重；公共 wrapper 裁到 [0,1]，再只在保留帧之间归一化。
        weights.push_back(guidedFilter(guides[selected[j]], decisions[j],
                                      options.guided_radius, options.guided_epsilon));
    }
    normalizeWeights(weights);
    MethodResult result;
    result.image = cv::Mat::zeros(size, images.front().type());
    result.weights.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i)
        result.weights.push_back(cv::Mat::zeros(size, CV_32F));
    for (size_t j = 0; j < selected.size(); ++j) {
        report(progress, 75 + static_cast<int>(20 * j / selected.size()), "blend");
        const auto i = selected[j];
        result.weights[i] = std::move(weights[j]);
        result.image += images[i].mul(expandWeight(result.weights[i], images.front().channels()));
    }
    result.focus_indices = dominantIndices(result.weights);
    return result;
}

} // 命名空间 mif::detail::fusion
