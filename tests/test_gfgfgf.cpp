#include "fixtures.hpp"
#include "fusion/gfg_fgf/focus_information.hpp"
#include "fusion/common/guided_filter.hpp"
#include <mif/fusion.hpp>
#include <opencv2/ximgproc/edge_filter.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

using mif::detail::fusion::fastGuidedFilter;
using mif::detail::fusion::FastGuidedFilter;
using mif::detail::fusion::gfgPriorityDecisionWeights;
using mif::detail::fusion::gfgFocusInformation;

// 保留优化前的整图实现作为独立数值参考。这里故意不调用生产决策或归一化函数，
// 用原有 OpenCV 运算顺序检查主评分容差、同路 Sobel、ROI 边界和最终均分是否变化。
std::vector<cv::Mat> referencePriorityDecisionWeights(const std::vector<cv::Mat>& gradients,
                                                      const std::vector<cv::Mat>& residuals,
                                                      const std::vector<cv::Mat>& candidates) {
    const auto select = [](const cv::Mat& gradient, const cv::Mat& residual, const cv::Mat& has_gradient) {
        cv::Mat selected = residual.clone();
        gradient.copyTo(selected, has_gradient);
        return selected;
    };
    const auto sobel = [](const cv::Mat& response) -> cv::Mat {
        cv::Mat gx, gy;
        cv::Sobel(response, gx, CV_32F, 1, 0, 3, 1, 0, cv::BORDER_REFLECT_101);
        cv::Sobel(response, gy, CV_32F, 0, 1, 3, 1, 0, cv::BORDER_REFLECT_101);
        return cv::abs(gx) + cv::abs(gy);
    };
    const auto size = gradients.front().size();
    cv::Mat has_gradient = cv::Mat::zeros(size, CV_8U);
    for (const auto& candidate : candidates) cv::bitwise_or(has_gradient, candidate, has_gradient);
    const cv::Mat all_residual = ~has_gradient;
    constexpr float tolerance = 1e-8f;
    constexpr float excluded = std::numeric_limits<float>::lowest();
    cv::Mat maximum(size, CV_32F, cv::Scalar(excluded));
    for (size_t i = 0; i < gradients.size(); ++i) {
        cv::Mat response = select(gradients[i], residuals[i], has_gradient);
        const cv::Mat eligible = all_residual | candidates[i];
        response.setTo(excluded, ~eligible);
        cv::max(maximum, response, maximum);
    }
    cv::Mat maximum_gradient(size, CV_32F, cv::Scalar(excluded));
    std::vector<cv::Mat> decisions;
    for (size_t i = 0; i < gradients.size(); ++i) {
        const cv::Mat response = select(gradients[i], residuals[i], has_gradient);
        const cv::Mat eligible = all_residual | candidates[i];
        cv::Mat candidate;
        cv::compare(response, maximum - tolerance, candidate, cv::CMP_GE);
        cv::bitwise_and(candidate, eligible, candidate);
        cv::Mat gradient = select(sobel(gradients[i]), sobel(residuals[i]), has_gradient);
        gradient.setTo(excluded, ~candidate);
        cv::max(maximum_gradient, gradient, maximum_gradient);
        decisions.push_back(std::move(gradient));
    }
    for (auto& decision : decisions) {
        cv::Mat mask;
        cv::compare(decision, maximum_gradient - tolerance, mask, cv::CMP_GE);
        mask.convertTo(decision, CV_32F, 1.0 / 255.0);
    }
    cv::Mat total = cv::Mat::zeros(size, CV_32F);
    for (auto& decision : decisions) {
        cv::max(decision, 0, decision);
        total += decision;
    }
    const cv::Mat empty = total <= 1e-12f;
    total.setTo(1, empty);
    for (auto& decision : decisions) {
        cv::divide(decision, total, decision);
        decision.setTo(1.0 / static_cast<double>(decisions.size()), empty);
    }
    return decisions;
}

void checkDecisionReference() {
    cv::RNG random(20261009);
    for (const cv::Size size : {cv::Size(3, 2), cv::Size(7, 9), cv::Size(37, 25)}) {
        for (int count : {2, 5}) {
            for (bool use_roi : {false, true}) {
                for (int mode = 0; mode < 8; ++mode) {
                    std::vector<cv::Mat> gradients, residuals, candidates;
                    for (int i = 0; i < count; ++i) {
                        const cv::Size storage_size = use_roi ? cv::Size(size.width + 4, size.height + 4) : size;
                        cv::Mat gradient(storage_size, CV_32F), residual(storage_size, CV_32F), mask(storage_size, CV_8U);
                        random.fill(gradient, cv::RNG::UNIFORM, -2.0, 2.0);
                        random.fill(residual, cv::RNG::UNIFORM, -2.0, 2.0);
                        random.fill(mask, cv::RNG::UNIFORM, 0, 2);
                        cv::compare(mask, 0, mask, cv::CMP_GT);
                        const cv::Rect region = use_roi ? cv::Rect(1, 2, size.width, size.height)
                                                       : cv::Rect(0, 0, size.width, size.height);
                        gradients.push_back(gradient(region));
                        residuals.push_back(residual(region));
                        candidates.push_back(mask(region));
                    }
                    if (mode == 1 || mode == 7) {
                        for (auto& mask : candidates) mask.setTo(0);
                    } else if (mode >= 2 && mode != 3) {
                        for (auto& mask : candidates) mask.setTo(255);
                    }
                    if (mode == 3) {
                        // 唯一 G 候选的负分数仍胜出，不应为任何帧计算 Sobel 消歧。
                        for (int i = 0; i < count; ++i) {
                            candidates[i].setTo(i == count / 2 ? 255 : 0);
                            gradients[i].setTo(i == count / 2 ? -0.25 : 10);
                        }
                    } else if (mode == 4) {
                        // 整幅主分数并列；ROI 外保留各自随机值，额外覆盖真实父图边界行为。
                        for (int i = 1; i < count; ++i) {
                            gradients[0].copyTo(gradients[i]);
                            residuals[0].copyTo(residuals[i]);
                        }
                    } else if (mode == 5) {
                        for (int i = 0; i < count; ++i) gradients[i].setTo(i == 0 ? 0.5 : -1);
                        // 平局仅出现在四角与中心；检查按需导数仍覆盖图像边界。
                        for (const auto point : {cv::Point(0, 0), cv::Point(size.width - 1, 0),
                                                 cv::Point(0, size.height - 1),
                                                 cv::Point(size.width - 1, size.height - 1),
                                                 cv::Point(size.width / 2, size.height / 2)})
                            gradients[1].at<float>(point) = 0.5f;
                    } else if (mode >= 6) {
                        auto& branch = mode == 6 ? gradients : residuals;
                        const float centers[]{-1e-6f, -0.0625f, 0.0f, 0.0625f, 0.5f};
                        for (int y = 0; y < size.height; ++y) {
                            for (int x = 0; x < size.width; ++x) {
                                const float center = centers[(x + y) % 5];
                                const float cutoff = center - 1e-8f;
                                // 恰好在容差边界及相邻 float，能检出比较改用 double 的偏差。
                                const float values[]{center, cutoff,
                                    std::nextafter(cutoff, -std::numeric_limits<float>::infinity()),
                                    std::nextafter(cutoff, std::numeric_limits<float>::infinity()), center};
                                for (int i = 0; i < count; ++i) branch[i].at<float>(y, x) = values[i];
                            }
                        }
                    }
                    std::vector<cv::Mat> gradients_before, residuals_before, candidates_before;
                    for (int i = 0; i < count; ++i) {
                        gradients_before.push_back(gradients[i].clone());
                        residuals_before.push_back(residuals[i].clone());
                        candidates_before.push_back(candidates[i].clone());
                    }
                    const auto expected = referencePriorityDecisionWeights(gradients, residuals, candidates);
                    const auto actual = gfgPriorityDecisionWeights(gradients, residuals, candidates);
                    const auto label = "GFG decision reference mode " + std::to_string(mode);
                    for (int i = 0; i < count; ++i) {
                        require(actual[i].type() == CV_32F && actual[i].size() == size &&
                                cv::norm(actual[i], expected[i], cv::NORM_INF) == 0,
                                label + " changed a decision or equal-share weight");
                        require(cv::norm(gradients[i], gradients_before[i], cv::NORM_INF) == 0 &&
                                cv::norm(residuals[i], residuals_before[i], cv::NORM_INF) == 0 &&
                                cv::norm(candidates[i], candidates_before[i], cv::NORM_INF) == 0,
                                label + " modified a response or eligibility mask");
                    }
                    std::reverse(gradients.begin(), gradients.end());
                    std::reverse(residuals.begin(), residuals.end());
                    std::reverse(candidates.begin(), candidates.end());
                    const auto reversed = gfgPriorityDecisionWeights(gradients, residuals, candidates);
                    for (int i = 0; i < count; ++i)
                        require(cv::norm(reversed[i], actual[count - i - 1], cv::NORM_INF) == 0,
                                label + " depends on input ordering");
                }
            }
        }
    }
}

void checkFocusInformation() {
    // 中心的 3×3 邻域为 [0,0,0; 0,1/2,3/4; 0,1/4,1]。
    // 类高斯参照为 5/16；横向差值和为 3/4，纵向为 3/8。
    // GFG = (3/4)^2 + (3/8)^2 = 45/64；均值残差 = 1/2 - (5/2)/9 = 2/9。
    cv::Mat patch = cv::Mat::zeros(7, 7, CV_32F);
    patch.at<float>(3, 3) = 0.5f;
    patch.at<float>(3, 4) = 0.75f;
    patch.at<float>(4, 3) = 0.25f;
    patch.at<float>(4, 4) = 1.0f;
    const cv::Mat original = patch.clone();
    const float gradient = 45.0f / 64.0f;
    const float residual = 2.0f / 9.0f;
    const auto enhanced = gfgFocusInformation(patch, 3, 0.7);
    const auto boundary = gfgFocusInformation(patch, 3, gradient);
    const auto fallback = gfgFocusInformation(patch, 3, 0.8);
    for (const auto& response : {enhanced.gradient, enhanced.residual})
        require(response.type() == CV_32F && response.size() == patch.size() && cv::checkRange(response),
                "GFG focus information changed dimensions or returned nonfinite values");
    require(enhanced.gradient_candidates.type() == CV_8U && enhanced.gradient_candidates.size() == patch.size(),
            "GFG candidate mask has the wrong dimensions or type");
    require(std::abs(enhanced.gradient.at<float>(3, 3) - gradient) < 1e-7f &&
            std::abs(enhanced.residual.at<float>(3, 3) - residual) < 1e-7f,
            "GFG does not match the hand-calculated Gaussian four-neighbor response");
    require(enhanced.gradient_candidates.at<unsigned char>(3, 3) == 255 &&
            boundary.gradient_candidates.at<unsigned char>(3, 3) == 255 &&
            fallback.gradient_candidates.at<unsigned char>(3, 3) == 0,
            "GFG candidate threshold must include equality and exclude weaker gradients");
    require(cv::norm(enhanced.gradient, fallback.gradient, cv::NORM_INF) == 0 &&
            cv::norm(enhanced.residual, fallback.residual, cv::NORM_INF) == 0,
            "Thresholding changed or zeroed a response before independent filtering");
    require(cv::norm(patch, original, cv::NORM_INF) == 0,
            "GFG focus information modified its source image");

    // 中心亮点的 GFG 只有 1/32，但均值残差为 2/9。
    // 候选资格必须由原始 G 判断，不能误用数值更大的 R。
    cv::Mat point(7, 7, CV_32F, cv::Scalar(0.25));
    point.at<float>(3, 3) = 0.5f;
    const auto point_information = gfgFocusInformation(point, 3, 0.1);
    require(std::abs(point_information.gradient.at<float>(3, 3) - 1.0f / 32.0f) < 1e-7f &&
            std::abs(point_information.residual.at<float>(3, 3) - residual) < 1e-7f &&
            point_information.gradient_candidates.at<unsigned char>(3, 3) == 0,
            "GFG candidate selection used the residual instead of the original gradient");
    // REFLECT_101 在角点不重复中心，因此角点亮点也应得到相同的手算响应。
    cv::Mat corner(3, 3, CV_32F, cv::Scalar(0.25));
    corner.at<float>(0, 0) = 0.5f;
    const auto corner_information = gfgFocusInformation(corner, 3, 0.1);
    require(std::abs(corner_information.gradient.at<float>(0, 0) - 1.0f / 32.0f) < 1e-7f &&
            std::abs(corner_information.residual.at<float>(0, 0) - residual) < 1e-7f,
            "GFG focus information did not apply the documented reflected boundary convention");

    const cv::Mat flat(5, 9, CV_32F, cv::Scalar(0.5));
    const auto flat_information = gfgFocusInformation(flat, 7, 0.005);
    require(cv::norm(flat_information.gradient, cv::NORM_INF) < 1e-7 &&
            cv::norm(flat_information.residual, cv::NORM_INF) < 1e-7 &&
            cv::countNonZero(flat_information.gradient_candidates) == 0,
            "Constant images acquired spurious focus information or gradient candidates");
    require(cv::countNonZero(gfgFocusInformation(flat, 7, 0).gradient_candidates) == flat.total(),
            "Zero threshold did not admit zero gradients at equality");
}

void requireWinner(const std::vector<cv::Mat>& decisions, size_t winner, const std::string& message) {
    for (size_t i = 0; i < decisions.size(); ++i)
        require(cv::norm(decisions[i], cv::Mat(decisions[i].size(), CV_32F, cv::Scalar(i == winner ? 1 : 0)),
                         cv::NORM_INF) < 1e-7, message);
}

void checkPriorityDecisions() {
    const cv::Size size(9, 7);
    const cv::Mat yes(size, CV_8U, cv::Scalar(255)), no(size, CV_8U, cv::Scalar(0));
    const std::vector<cv::Mat> gradients{
        cv::Mat(size, CV_32F, cv::Scalar(-0.2)), cv::Mat(size, CV_32F, cv::Scalar(-0.1)),
        cv::Mat(size, CV_32F, cv::Scalar(10))};
    const std::vector<cv::Mat> residuals{
        cv::Mat(size, CV_32F, cv::Scalar(0.1)), cv::Mat(size, CV_32F, cv::Scalar(100)),
        cv::Mat(size, CV_32F, cv::Scalar(1))};
    requireWinner(gfgPriorityDecisionWeights(gradients, residuals, {yes, no, no}), 0,
                  "A sole G candidate lost to a larger R or an ineligible G response");
    requireWinner(gfgPriorityDecisionWeights(gradients, residuals, {yes, yes, no}), 1,
                  "G candidates were clipped or compared against an ineligible gradient response");
    requireWinner(gfgPriorityDecisionWeights(gradients, residuals, {no, no, no}), 1,
                  "Pixels without G candidates did not compare the complete R responses");

    // 候选资格逐像素决定；唯一 G 位于中心，其余位置仍走 R。
    cv::Mat center = no.clone();
    center.at<unsigned char>(3, 4) = 255;
    const auto local = gfgPriorityDecisionWeights(gradients, residuals, {center, no, no});
    require(local[0].at<float>(3, 4) == 1 && cv::countNonZero(local[0]) == 1 &&
            local[1].at<float>(3, 4) == 0 && cv::countNonZero(local[1]) == size.area() - 1,
            "A local G candidate changed eligibility at unrelated pixels");
}

void checkBranchSobelAndTies() {
    cv::Mat first = cv::Mat::zeros(7, 7, CV_32F);
    cv::Mat second = first.clone(), lower = first.clone();
    first.at<float>(3, 3) = second.at<float>(3, 3) = 0.5f;
    second.at<float>(3, 4) = 0.25f;
    lower.at<float>(3, 3) = 0.49f;
    lower.at<float>(3, 4) = 1.0f;
    // 并列中心：第一张局部对称，Sobel 为 0；第二张右邻居贡献水平响应 1/2。
    // 第三张有更强梯度但主分数较低，不得进入第二轮比较。
    cv::Mat ineligible = second.clone();
    ineligible.at<float>(3, 4) = 2.0f;
    cv::Mat other_branch = first.clone();
    other_branch.at<float>(3, 4) = 100.0f;
    const cv::Mat no(first.size(), CV_8U, cv::Scalar(0));
    cv::Mat center = no.clone();
    center.at<unsigned char>(3, 3) = 255;
    const std::vector<cv::Mat> gradients{first, second, lower, ineligible};
    const std::vector<cv::Mat> residuals{other_branch, first, first, first};
    const std::vector<cv::Mat> candidates{center, center, center, no};
    // 邻居均走 R；若拼接 G/R 后再算 Sobel，第一帧会被另一分支的 100 错误抬高。
    const auto decisions = gfgPriorityDecisionWeights(gradients, residuals, candidates);
    require(decisions.size() == 4 && decisions[0].at<float>(3, 3) == 0 &&
            decisions[1].at<float>(3, 3) == 1 && decisions[2].at<float>(3, 3) == 0 &&
            decisions[3].at<float>(3, 3) == 0,
            "G tie-breaking did not use the complete G branch and only eligible maximum-score candidates");

    // 反过来，中心全 R、邻居有 G 候选时，也必须用完整 R 图计算 Sobel。
    const cv::Mat outside = ~center;
    const auto residual_decisions = gfgPriorityDecisionWeights(
        {other_branch, first, first}, {first, second, lower}, {outside, outside, outside});
    require(residual_decisions[0].at<float>(3, 3) == 0 && residual_decisions[1].at<float>(3, 3) == 1 &&
            residual_decisions[2].at<float>(3, 3) == 0,
            "R tie-breaking used G neighbors or allowed a lower primary response to win");

    const std::vector<int> order{3, 1, 0, 2};
    std::vector<cv::Mat> permuted_gradients, permuted_residuals, permuted_candidates;
    for (int i : order) {
        permuted_gradients.push_back(gradients[i]);
        permuted_residuals.push_back(residuals[i]);
        permuted_candidates.push_back(candidates[i]);
    }
    const auto permuted = gfgPriorityDecisionWeights(permuted_gradients, permuted_residuals, permuted_candidates);
    for (size_t i = 0; i < order.size(); ++i)
        require(cv::norm(permuted[i], decisions[order[i]], cv::NORM_INF) < 1e-7,
                "G-priority decisions changed after reordering the input frames");

    for (const auto& score : {cv::Mat(7, 9, CV_32F, cv::Scalar(0.5)), first}) {
        for (int mask_value : {0, 255}) {
            const cv::Mat mask(score.size(), CV_8U, cv::Scalar(mask_value));
            const auto symmetric = gfgPriorityDecisionWeights({score, score, score}, {score, score, score},
                                                              {mask, mask, mask});
            for (const auto& weight : symmetric)
                require(cv::norm(weight, cv::Mat(score.size(), CV_32F, cv::Scalar(1.0 / 3)), cv::NORM_INF) < 1e-7,
                        "Equal focus scores and equal Sobel responses did not share decisions symmetrically");
        }
    }
}

void checkAllPlanesByDefault() {
    mif::GfgFgfFusionOptions options;
    options.include_weight_maps = true;
    require(options.selection_ratio == 0,
            "GFG-FGF must compare all focal planes by default");
    require(options.guided_subsample_factor == 4,
            "GFG-FGF did not enable fast guided filtering by default");
    cv::Mat strong(65, 97, CV_32F, cv::Scalar(0.5));
    cv::Mat local = strong.clone();
    for (int y = 0; y < strong.rows; ++y) {
        for (int x = 0; x < 24; ++x)
            strong.at<float>(y, x) = ((x / 4) % 2 == 0) ? 0.1f : 0.9f;
        for (int x = 48; x < local.cols; ++x)
            local.at<float>(y, x) = ((x / 2 + y / 2) % 2 == 0) ? 0.49f : 0.51f;
    }
    // 第二张的全局梯度很弱，但独自提供右侧纹理。默认路径必须让它参与该区域。
    const auto all_planes = mif::fuse({strong, local}, options);
    require(all_planes.weight_maps[1].at<float>(32, 80) > 0.9f,
            "Default GFG-FGF discarded a weak frame containing uniquely focused local detail");
    options.selection_ratio = 0.15;
    const auto selected = mif::fuse({strong, local}, options);
    require(cv::norm(selected.weight_maps[1], cv::NORM_INF) == 0,
            "Optional global gradient preselection did not filter the deliberately weak frame");

    // 被排除帧分布在选中帧前后，检查稀疏位置回填零图时不改变原始输入索引。
    const cv::Mat flat(strong.size(), CV_32F, cv::Scalar(0.5));
    const auto sparse = mif::fuse({flat, strong, local, flat}, options);
    require(sparse.weight_maps.size() == 4 && cv::norm(sparse.image, strong, cv::NORM_INF) == 0 &&
            cv::countNonZero(sparse.source_index_map != 1) == 0,
            "Sparse GFG weights changed selected-frame pixels or original source indices");
    for (size_t i = 0; i < sparse.weight_maps.size(); ++i) {
        require(sparse.weight_maps[i].type() == CV_32F && sparse.weight_maps[i].size() == strong.size() &&
                cv::norm(sparse.weight_maps[i], cv::Mat(strong.size(), CV_32F, cv::Scalar(i == 1 ? 1 : 0)),
                         cv::NORM_INF) == 0,
                "GFG returned an empty or misplaced weight for a retained or excluded frame");
    }
    options.include_weight_maps = false;
    const auto without_weights = mif::fuse({flat, strong, local, flat}, options);
    require(without_weights.weight_maps.empty() &&
            cv::norm(without_weights.image, sparse.image, cv::NORM_INF) == 0 &&
            cv::norm(without_weights.source_index_map, sparse.source_index_map, cv::NORM_INF) == 0,
            "Omitting GFG weight diagnostics changed the image or original source indices");
}

void checkStripeFocusReversal() {
    cv::Mat sharp(64, 192, CV_32F);
    for (int y = 0; y < sharp.rows; ++y)
        for (int x = 0; x < sharp.cols; ++x)
            sharp.at<float>(y, x) = static_cast<float>(0.5 + 0.08 * std::cos(2 * CV_PI * x / 8));
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, {9, 9}, 1, 1, cv::BORDER_REFLECT_101);
    const cv::Rect interior(32, 16, 128, 32);
    mif::GfgFgfFusionOptions options;
    options.include_weight_maps = true;
    // 旧 G/R 分段评分在该低对比度纹理上反选模糊帧；验证两轮滤波后的实际输出。
    for (int subsample : {1, 4}) {
        options.guided_subsample_factor = subsample;
        const auto result = mif::fuse({sharp, blurred}, options);
        require(cv::norm(result.weight_maps[0](interior), cv::Mat(interior.size(), CV_32F, cv::Scalar(1)),
                         cv::NORM_INF) < 2e-6,
                "G-priority fusion selected the blurred low-contrast stripe frame after guided filtering");
        require(cv::norm(result.image(interior), sharp(interior), cv::NORM_INF) < 2e-6,
                "G-priority fusion lost focused low-contrast stripe detail");
    }
}

void checkReusableFastGuidedFilter() {
    // 非连续 ROI 检查步长处理；奇数尺寸和短边为 2 的图像覆盖下采样尺寸及有效倍数。
    cv::RNG random(20261008);
    for (const cv::Size size : {cv::Size(37, 25), cv::Size(7, 6), cv::Size(3, 2), cv::Size(2, 3)}) {
        const cv::Size storage_size(size.width + 3, size.height + 2);
        cv::Mat guide_storage(storage_size, CV_32F), first_storage(storage_size, CV_32F),
                second_storage(storage_size, CV_32F);
        random.fill(guide_storage, cv::RNG::UNIFORM, 0.05, 0.95);
        random.fill(first_storage, cv::RNG::UNIFORM, -0.4, 0.8);
        random.fill(second_storage, cv::RNG::UNIFORM, -0.9, 0.1);
        const cv::Rect roi(1, 1, size.width, size.height);
        const cv::Mat guide = guide_storage(roi), first = first_storage(roi), second = second_storage(roi);
        const cv::Mat guide_before = guide_storage.clone(), first_before = first_storage.clone(),
                      second_before = second_storage.clone();
        for (int subsample : {1, 4, 16}) {
            for (double epsilon : {1e-6, 0.3}) {
                const FastGuidedFilter reusable(guide, 5, epsilon, subsample);
                const auto first_result = reusable.filter(first);
                const cv::Mat first_result_before = first_result.clone();
                const auto second_result = reusable.filter(second);
                const cv::Mat negative(size, CV_32F, cv::Scalar(-0.125));
                const auto negative_result = reusable.filter(negative);
                require(first_result.type() == CV_32F && first_result.size() == size &&
                        cv::checkRange(first_result) && cv::checkRange(second_result) &&
                        cv::norm(first_result, fastGuidedFilter(guide, first, 5, epsilon, subsample), cv::NORM_INF) == 0 &&
                        cv::norm(second_result, fastGuidedFilter(guide, second, 5, epsilon, subsample), cv::NORM_INF) == 0,
                        "Reusing guide statistics changed filtering for an independent input or ROI");
                require(cv::norm(negative_result, negative, cv::NORM_INF) < 2e-6,
                        "Reusable fast guided filtering clipped a negative constant response");
                // 多路滤波不能覆盖先前输出，亦不能把上一张输入的系数错误留在缓存中。
                require(cv::norm(first_result, first_result_before, cv::NORM_INF) == 0 &&
                        cv::norm(reusable.filter(first), first_result_before, cv::NORM_INF) == 0,
                        "Reusable fast guided filtering retained input state or overwrote an earlier output");
                bool rejected_size = false;
                try {
                    reusable.filter(cv::Mat(size.height + 1, size.width, CV_32F, cv::Scalar(0)));
                } catch (const cv::Exception&) {
                    rejected_size = true;
                }
                require(rejected_size && cv::norm(reusable.filter(first), first_result_before, cv::NORM_INF) == 0,
                        "A mismatched input size was accepted or damaged reusable guide statistics");
            }
        }
        require(cv::norm(guide_storage, guide_before, cv::NORM_INF) == 0 &&
                cv::norm(first_storage, first_before, cv::NORM_INF) == 0 &&
                cv::norm(second_storage, second_before, cv::NORM_INF) == 0,
                "Reusable fast guided filtering changed an input or its surrounding ROI storage");
    }
}

} // 匿名命名空间

void testGfgfgfPriority() {
    checkDecisionReference();
    checkFocusInformation();
    checkPriorityDecisions();
    checkBranchSobelAndTies();
    checkAllPlanesByDefault();
    checkStripeFocusReversal();
}

void testFastGuidedFilter() {
    checkReusableFastGuidedFilter();
    cv::Mat guide;
    texture(25, 37).convertTo(guide, CV_32F, 1.0 / 255);
    cv::Mat input(guide.size(), CV_32F);
    for (int y = 0; y < input.rows; ++y)
        for (int x = 0; x < input.cols; ++x)
            input.at<float>(y, x) = static_cast<float>(0.5 + 0.2 * std::sin(0.31 * x) + 0.15 * std::cos(0.23 * y));
    const cv::Mat original_guide = guide.clone(), original_input = input.clone();
    for (int radius : {1, 3, 7, 31}) {
        for (double epsilon : {1e-3, 0.3}) {
            cv::Mat oracle;
            cv::ximgproc::guidedFilter(guide, input, oracle, radius, epsilon, CV_32F);
            const auto actual = fastGuidedFilter(guide, input, radius, epsilon, 1);
            // 本项目用双精度统计，OpenCV 用单精度；非退化正则项下允许 5e-5 的绝对误差。
            require(cv::norm(actual, oracle, cv::NORM_INF) < 5e-5,
                    "Full-resolution fast guided filter disagrees with the independent OpenCV oracle");
        }
    }
    require(cv::norm(guide, original_guide, cv::NORM_INF) == 0 &&
            cv::norm(input, original_input, cv::NORM_INF) == 0,
            "Fast guided filtering modified its inputs");

    // 40→10 的双线性采样坐标是 1.5、5.5、…，不读取 (20,20)。
    // 改变这个原图像素不会影响低分辨率拟合，但输出仍应通过完整引导图响应它。
    cv::Mat ramp(40, 40, CV_32F);
    for (int y = 0; y < ramp.rows; ++y)
        for (int x = 0; x < ramp.cols; ++x)
            ramp.at<float>(y, x) = static_cast<float>(0.1 + 0.8 * x / 39);
    const auto before = fastGuidedFilter(ramp, ramp, 4, 1e-3, 4);
    cv::Mat changed_guide = ramp.clone();
    changed_guide.at<float>(20, 20) += 0.1f;
    const auto after = fastGuidedFilter(changed_guide, ramp, 4, 1e-3, 4);
    cv::Mat change = after - before;
    require(change.at<float>(20, 20) > 0.05f && change.at<float>(20, 20) < 0.10001f,
            "Fast guided filter lost original-resolution guide detail when restoring the output");
    change.at<float>(20, 20) = 0;
    require(cv::norm(change, cv::NORM_INF) < 1e-6,
            "A guide pixel outside the downsampling support changed low-resolution coefficients");

    for (const cv::Size size : {cv::Size(37, 25), cv::Size(3, 2), cv::Size(2, 3)}) {
        cv::Mat local_guide;
        texture(size.height, size.width).convertTo(local_guide, CV_32F, 1.0 / 255);
        cv::Mat local_input(size, CV_32F);
        cv::RNG random(20260930);
        random.fill(local_input, cv::RNG::UNIFORM, 0.1, 0.9);
        for (int subsample = 1; subsample <= 16; ++subsample) {
            const auto result = fastGuidedFilter(local_guide, local_input, 5, 1e-6, subsample);
            require(result.type() == CV_32F && result.size() == size && cv::checkRange(result),
                    "Fast guided filter failed on odd/tiny dimensions or a legal subsample factor");
            const auto constant = fastGuidedFilter(local_guide, cv::Mat(size, CV_32F, cv::Scalar(0.375)),
                                                  255, 1e-6, subsample);
            require(cv::norm(constant, cv::Mat(size, CV_32F, cv::Scalar(0.375)), cv::NORM_INF) < 2e-6,
                    "Fast guided filter failed to preserve a constant response");
            const auto signed_response = fastGuidedFilter(local_guide, cv::Mat(size, CV_32F, cv::Scalar(-0.125)),
                                                         5, 0.3, subsample);
            require(cv::norm(signed_response, cv::Mat(size, CV_32F, cv::Scalar(-0.125)), cv::NORM_INF) < 2e-6,
                    "Fast guided filter clipped the signed response before focus selection");
        }
    }
}
