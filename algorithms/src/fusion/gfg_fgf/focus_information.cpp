#include "fusion/gfg_fgf/focus_information.hpp"
#include "fusion/common/weight_map.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace mif::detail::fusion {
namespace {

cv::Mat sobelResponse(const cv::Mat& response) {
    cv::Mat gx, gy;
    cv::Sobel(response, gx, CV_32F, 1, 0, 3, 1, 0, cv::BORDER_REFLECT_101);
    cv::Sobel(response, gy, CV_32F, 0, 1, 3, 1, 0, cv::BORDER_REFLECT_101);
    return cv::abs(gx) + cv::abs(gy);
}

} // 匿名命名空间

GfgFocusInformation gfgFocusInformation(const cv::Mat& guide, int window, double threshold) {
    CV_Assert(guide.type() == CV_32FC1 && window >= 1 && window % 2 == 1);
    cv::Mat mean, residual, smooth, padded;
    cv::blur(guide, mean, {window, window}, {-1, -1}, cv::BORDER_REFLECT_101);
    cv::absdiff(guide, mean, residual);
    const cv::Mat kernel = (cv::Mat_<float>(3, 3) << 1, 2, 1, 2, 4, 2, 1, 2, 1) / 16.0f;
    cv::filter2D(guide, smooth, CV_32F, kernel, {-1, -1}, 0, cv::BORDER_REFLECT_101);
    cv::copyMakeBorder(guide, padded, 1, 1, 1, 1, cv::BORDER_REFLECT_101);
    cv::Mat gradient(guide.size(), CV_32F), candidates(guide.size(), CV_8U);
    for (int y = 0; y < guide.rows; ++y) {
        const float* top = padded.ptr<float>(y);
        const float* middle = padded.ptr<float>(y + 1);
        const float* bottom = padded.ptr<float>(y + 2);
        const float* local = smooth.ptr<float>(y);
        float* output = gradient.ptr<float>(y);
        auto* candidate = candidates.ptr<unsigned char>(y);
        for (int x = 0; x < guide.cols; ++x) {
            const float horizontal = std::abs(middle[x + 2] - local[x]) + std::abs(middle[x] - local[x]);
            const float vertical = std::abs(top[x + 1] - local[x]) + std::abs(bottom[x + 1] - local[x]);
            output[x] = horizontal * horizontal + vertical * vertical;
            // 阈值只确定候选资格；两路评分完整保留，分别交给第一轮滤波。
            candidate[x] = output[x] >= threshold ? 255 : 0;
        }
    }
    return {gradient, residual, candidates};
}

std::vector<cv::Mat> gfgPriorityDecisionWeights(const std::vector<cv::Mat>& gradient_responses,
                                               const std::vector<cv::Mat>& residual_responses,
                                               const std::vector<cv::Mat>& gradient_candidates) {
    CV_Assert(!gradient_responses.empty() && gradient_responses.size() == residual_responses.size() &&
              gradient_responses.size() == gradient_candidates.size());
    const auto size = gradient_responses.front().size();
    const size_t count = gradient_responses.size();
    std::vector<cv::Mat> decisions;
    decisions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        CV_Assert(gradient_responses[i].type() == CV_32FC1 && gradient_responses[i].size() == size &&
                  residual_responses[i].type() == CV_32FC1 && residual_responses[i].size() == size &&
                  gradient_candidates[i].type() == CV_8UC1 && gradient_candidates[i].size() == size);
        decisions.emplace_back(size, CV_32F);
    }
    constexpr float tolerance = 1e-8f;
    constexpr float excluded = std::numeric_limits<float>::lowest();
    // 0 表示唯一主候选，1/2 分别表示需要用 R/G 路 Sobel 消歧的位置。
    cv::Mat ties = cv::Mat::zeros(size, CV_8U);
    std::vector<unsigned char> need_gradient(count, 0), need_residual(count, 0);
    std::vector<const float*> gradient_rows(count), residual_rows(count);
    std::vector<const unsigned char*> candidate_rows(count);
    std::vector<float*> decision_rows(count);
    bool has_ties = false;
    for (int y = 0; y < size.height; ++y) {
        for (size_t i = 0; i < count; ++i) {
            gradient_rows[i] = gradient_responses[i].ptr<float>(y);
            residual_rows[i] = residual_responses[i].ptr<float>(y);
            candidate_rows[i] = gradient_candidates[i].ptr<unsigned char>(y);
            decision_rows[i] = decisions[i].ptr<float>(y);
        }
        auto* tie_row = ties.ptr<unsigned char>(y);
        for (int x = 0; x < size.width; ++x) {
            float maximum_gradient = excluded, maximum_residual = excluded;
            bool has_gradient = false;
            // 合并资格与最大值扫描；负响应也参与比较，零值不能用于排除候选。
            for (size_t i = 0; i < count; ++i) {
                maximum_residual = std::max(maximum_residual, residual_rows[i][x]);
                if (candidate_rows[i][x]) {
                    has_gradient = true;
                    maximum_gradient = std::max(maximum_gradient, gradient_rows[i][x]);
                }
            }
            // 显式保存为 float，保持最大值减容差后再比较的单精度舍入规则。
            const float cutoff = (has_gradient ? maximum_gradient : maximum_residual) - tolerance;
            const auto& response_rows = has_gradient ? gradient_rows : residual_rows;
            size_t matches = 0;
            for (size_t i = 0; i < count; ++i) {
                const bool candidate = (!has_gradient || candidate_rows[i][x]) && response_rows[i][x] >= cutoff;
                decision_rows[i][x] = candidate ? 1.0f : 0.0f;
                matches += candidate;
            }
            if (matches > 1) {
                has_ties = true;
                tie_row[x] = has_gradient ? 2 : 1;
                auto& needed = has_gradient ? need_gradient : need_residual;
                for (size_t i = 0; i < count; ++i)
                    if (decision_rows[i][x] != 0) needed[i] = 1;
            }
        }
    }
    // 唯一候选的最终权重必为 1，无需计算任何导数或再次归一化。
    if (!has_ties) return decisions;

    cv::Mat maximum_sobel(size, CV_32F, cv::Scalar(excluded));
    for (size_t i = 0; i < count; ++i) {
        if (!need_gradient[i] && !need_residual[i]) {
            decisions[i].setTo(excluded, ties);
            continue;
        }
        // 只有真正参与平局的帧和分支才求导；仍使用完整同路响应和原 OpenCV 算子，
        // 不拼接 G/R、不裁小邻域，保留图像边界及非连续 ROI 的导数计算约定。
        const cv::Mat gradient = need_gradient[i] ? sobelResponse(gradient_responses[i]) : cv::Mat{};
        const cv::Mat residual = need_residual[i] ? sobelResponse(residual_responses[i]) : cv::Mat{};
        for (int y = 0; y < size.height; ++y) {
            const auto* tie_row = ties.ptr<unsigned char>(y);
            const float* gradient_row = gradient.empty() ? nullptr : gradient.ptr<float>(y);
            const float* residual_row = residual.empty() ? nullptr : residual.ptr<float>(y);
            float* decision = decisions[i].ptr<float>(y);
            float* maximum = maximum_sobel.ptr<float>(y);
            for (int x = 0; x < size.width; ++x) {
                if (!tie_row[x]) continue;
                if (decision[x] == 0) {
                    decision[x] = excluded;
                    continue;
                }
                decision[x] = tie_row[x] == 2 ? gradient_row[x] : residual_row[x];
                maximum[x] = std::max(maximum[x], decision[x]);
            }
        }
    }
    for (int y = 0; y < size.height; ++y) {
        const auto* tie_row = ties.ptr<unsigned char>(y);
        const float* maximum = maximum_sobel.ptr<float>(y);
        for (size_t i = 0; i < count; ++i) decision_rows[i] = decisions[i].ptr<float>(y);
        for (int x = 0; x < size.width; ++x) {
            if (!tie_row[x]) continue;
            const float cutoff = maximum[x] - tolerance;
            for (size_t i = 0; i < count; ++i)
                decision_rows[i][x] = decision_rows[i][x] >= cutoff ? 1.0f : 0.0f;
        }
    }
    // 沿用逐帧累加和除法，让仍然并列的候选按相同数值路径均分权重。
    normalizeWeights(decisions);
    return decisions;
}

} // 命名空间 mif::detail::fusion
