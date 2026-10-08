#include "fixtures.hpp"
#include "fusion/gfg_fgf/focus_information.hpp"
#include "fusion/common/guided_filter.hpp"
#include <mif/fusion.hpp>
#include <opencv2/ximgproc/edge_filter.hpp>
#include <algorithm>
#include <cmath>

namespace {

using mif::detail::fusion::fastGuidedFilter;
using mif::detail::fusion::paperDecisionWeights;
using mif::detail::fusion::paperFocusInformation;

void checkPaperFocusInformation() {
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
    const auto enhanced = paperFocusInformation(patch, 3, 0.7);
    const auto boundary = paperFocusInformation(patch, 3, gradient);
    const auto fallback = paperFocusInformation(patch, 3, 0.8);
    require(enhanced.type() == CV_32F && enhanced.size() == patch.size() && cv::checkRange(enhanced),
            "Paper focus information changed dimensions or returned nonfinite values");
    require(std::abs(enhanced.at<float>(3, 3) - gradient) < 1e-7f,
            "GFG does not match the hand-calculated Gaussian four-neighbor response");
    require(std::abs(boundary.at<float>(3, 3) - gradient) < 1e-7f,
            "GFG threshold equality did not retain the gradient response");
    require(std::abs(fallback.at<float>(3, 3) - residual) < 1e-7f,
            "Weak GFG response must fall back to the mean residual");
    require(cv::norm(patch, original, cv::NORM_INF) == 0,
            "Paper focus information modified its source image");

    // 中心亮点的 GFG 只有 1/32，但均值残差为 2/9。
    // 阈值判断的对象应为 GFG，且高阈值必须保留均值残差。
    cv::Mat point(7, 7, CV_32F, cv::Scalar(0.25));
    point.at<float>(3, 3) = 0.5f;
    const auto point_gradient = paperFocusInformation(point, 3, 0.01);
    const auto point_residual = paperFocusInformation(point, 3, 0.1);
    require(std::abs(point_gradient.at<float>(3, 3) - 1.0f / 32.0f) < 1e-7f &&
            std::abs(point_residual.at<float>(3, 3) - residual) < 1e-7f,
            "Focus information thresholds the residual instead of selecting between GFG and residual");
    // REFLECT_101 在角点不重复中心，因此角点亮点也应得到相同的手算响应。
    cv::Mat corner(3, 3, CV_32F, cv::Scalar(0.25));
    corner.at<float>(0, 0) = 0.5f;
    require(std::abs(paperFocusInformation(corner, 3, 0.01).at<float>(0, 0) - 1.0f / 32.0f) < 1e-7f &&
            std::abs(paperFocusInformation(corner, 3, 0.1).at<float>(0, 0) - residual) < 1e-7f,
            "Paper focus information did not apply the documented reflected boundary convention");

    const cv::Mat flat(5, 9, CV_32F, cv::Scalar(0.5));
    require(cv::norm(paperFocusInformation(flat, 7, 0.005), cv::NORM_INF) < 1e-7,
            "Constant images acquired spurious paper focus information");
}

void checkPaperDecisions() {
    cv::Mat first = cv::Mat::zeros(7, 7, CV_32F);
    cv::Mat second = first.clone(), lower = first.clone();
    first.at<float>(3, 3) = second.at<float>(3, 3) = 0.5f;
    second.at<float>(3, 4) = 0.25f;
    lower.at<float>(3, 3) = 0.49f;
    lower.at<float>(3, 4) = 1.0f;
    // 并列中心：第一张局部对称，Sobel 为 0；第二张右邻居贡献水平响应 1/2。
    // 第三张有更强梯度但主分数较低，不得进入第二轮比较。
    const auto decisions = paperDecisionWeights({first, second, lower});
    require(decisions.size() == 3 && decisions[0].at<float>(3, 3) == 0 &&
            decisions[1].at<float>(3, 3) == 1 && decisions[2].at<float>(3, 3) == 0,
            "Sobel did not resolve tied focus scores using only the maximum-score candidates");

    for (const auto& score : {cv::Mat(7, 9, CV_32F, cv::Scalar(0.5)), first}) {
        const auto symmetric = paperDecisionWeights({score, score, score});
        for (const auto& weight : symmetric)
            require(cv::norm(weight, cv::Mat(score.size(), CV_32F, cv::Scalar(1.0 / 3)), cv::NORM_INF) < 1e-7,
                    "Equal focus scores and equal Sobel responses did not share decisions symmetrically");
    }
}

void checkAllPlanesByDefault() {
    mif::GfgFgfFusionOptions options;
    options.include_weight_maps = true;
    require(options.selection_ratio == 0,
            "The paper path must compare all focal planes by default");
    require(options.guided_subsample_factor == 4,
            "The paper path did not enable fast guided filtering by default");
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
}

} // 匿名命名空间

void testGfgfgfPaper() {
    checkPaperFocusInformation();
    checkPaperDecisions();
    checkAllPlanesByDefault();
}

void testFastGuidedFilter() {
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
