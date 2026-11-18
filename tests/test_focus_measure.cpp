#include "fixtures.hpp"
#include "fusion/common/focus_measure.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// 旧枚举值保持不变，避免调用方持久化的选项被解释为另一个指标。
static_assert(static_cast<int>(mif::FocusMeasure::ModifiedLaplacian) == 0);
static_assert(static_cast<int>(mif::FocusMeasure::Tenengrad) == 1);

void checkModifiedTenengradFormula() {
    // 二次灰度曲面的 3×3 Sobel 响应可解析求得：Gx=16*ax*x，Gy=16*ay*y。
    // 直接按解析式逐点求和作为预期，不复用被测函数的 Sobel/boxFilter 实现。
    // 系数取 2 的幂次分数，使输入灰度和 Sobel 响应可由 float32 精确表示。
    // 分母加常数、除法及均值会舍入，因此按预期分数的尺度允许 8 倍 float epsilon。
    constexpr double ax = 1.0 / 512, ay = 1.0 / 256;
    const auto grayAt = [ax, ay](int x, int y) { return 0.0625 + ax * x * x + ay * y * y; };
    const auto energyAt = [ax, ay](int x, int y) {
        const double gx = 16 * ax * x, gy = 16 * ay * y;
        return gx * gx + gy * gy;
    };
    const auto matchesFloat = [](double actual, double expected) {
        return std::abs(actual - expected) <= 8 * std::numeric_limits<float>::epsilon() * std::abs(expected);
    };
    cv::Mat input(11, 13, CV_32F);
    for (int y = 0; y < input.rows; ++y)
        for (int x = 0; x < input.cols; ++x)
            input.at<float>(y, x) = static_cast<float>(grayAt(x, y));
    const cv::Mat before = input.clone();
    double wrong_order_gap = 0;
    for (int window : {1, 3}) {
        const auto weighted = mif::detail::fusion::focusMeasure(input, mif::FocusMeasure::ModifiedTenengrad, window);
        const auto original = mif::detail::fusion::focusMeasure(input, mif::FocusMeasure::Tenengrad, window);
        const auto laplacian = mif::detail::fusion::focusMeasure(input, mif::FocusMeasure::ModifiedLaplacian, window);
        require(weighted.type() == CV_32FC1 && weighted.size() == input.size() && cv::checkRange(weighted),
                "Modified Tenengrad produced invalid score data");
        // 留出两像素，排除 Sobel 及局部平均在图像边缘的反射补齐。
        for (int y = 2; y < input.rows - 2; ++y) {
            for (int x = 2; x < input.cols - 2; ++x) {
                double expected = 0, energy_sum = 0, gray_square_sum = 0;
                for (int dy = -window / 2; dy <= window / 2; ++dy) {
                    for (int dx = -window / 2; dx <= window / 2; ++dx) {
                        const double energy = energyAt(x + dx, y + dy);
                        const double gray = grayAt(x + dx, y + dy);
                        expected += energy / (gray * gray + 0.001);
                        energy_sum += energy;
                        gray_square_sum += gray * gray;
                    }
                }
                const double area = window * window;
                expected /= area;
                require(matchesFloat(weighted.at<float>(y, x), expected),
                        "Modified Tenengrad must divide each pixel's energy before averaging");
                require(std::abs(original.at<float>(y, x) - energy_sum / area) < 1e-6,
                        "Original Tenengrad no longer matches its gradient-energy definition");
                require(std::abs(laplacian.at<float>(y, x) - 2 * (ax + ay)) < 1e-6,
                        "Modified Laplacian no longer matches second differences");
                wrong_order_gap = std::max(wrong_order_gap,
                    std::abs(expected - (energy_sum / area) / (0.001 + gray_square_sum / area)));
            }
        }
    }
    require(wrong_order_gap > 1e-5, "Formula fixture does not distinguish averaging/division order");
    require(cv::norm(input, before, cv::NORM_INF) == 0, "Focus score modified its input");

    // 加常量不改变梯度，但增大本指标的分母；输入仍在 [0,1] 内，没有裁剪影响。
    const cv::Mat brighter = input + 0.125f;
    const cv::Rect interior(2, 2, input.cols - 4, input.rows - 4);
    const auto dark_score = mif::detail::fusion::focusMeasure(input, mif::FocusMeasure::ModifiedTenengrad, 3);
    const auto bright_score = mif::detail::fusion::focusMeasure(brighter, mif::FocusMeasure::ModifiedTenengrad, 3);
    require(cv::countNonZero(bright_score(interior) >= dark_score(interior)) == 0,
            "Brighter pixels with the same gradients should receive lower modified Tenengrad scores");
    const auto original_dark = mif::detail::fusion::focusMeasure(input, mif::FocusMeasure::Tenengrad, 3);
    const auto original_bright = mif::detail::fusion::focusMeasure(brighter, mif::FocusMeasure::Tenengrad, 3);
    require(cv::norm(original_dark(interior), original_bright(interior), cv::NORM_INF) < 1e-6,
            "Adding an intensity constant changed the original gradient energy");

    // 黑色像素的分母为 0.001，不会除零；全黑/全白图均应得到有限的零分数。
    for (float value : {0.0f, 1.0f}) {
        const cv::Mat flat(5, 7, CV_32F, cv::Scalar(value));
        const auto score = mif::detail::fusion::focusMeasure(flat, mif::FocusMeasure::ModifiedTenengrad, 9);
        require(cv::checkRange(score) && cv::norm(score, cv::NORM_INF) == 0,
                "Flat range endpoints must produce finite zero focus scores");
    }

    // 黑色/近黑像素旁的微弱台阶仍有非零梯度。沿阶跃两侧，Gx=4*step，Gy=0。
    // 精确检查 1/(g²+0.001) 的放大倍数，防止只测全黑图而漏掉暗区除法错误。
    constexpr float step = 1.0f / 1024;
    cv::Mat near_black = cv::Mat::zeros(7, 9, CV_32F);
    near_black.colRange(4, near_black.cols).setTo(step);
    const auto near_black_score = mif::detail::fusion::focusMeasure(near_black, mif::FocusMeasure::ModifiedTenengrad, 1);
    const auto near_black_original = mif::detail::fusion::focusMeasure(near_black, mif::FocusMeasure::Tenengrad, 1);
    require(cv::checkRange(near_black_score), "Near-black gradients produced nonfinite focus scores");
    for (int x : {3, 4}) {
        const double gray = near_black.at<float>(3, x);
        const double energy = 16.0 * step * step;
        const double expected = energy / (gray * gray + 0.001);
        require(matchesFloat(near_black_score.at<float>(3, x), expected),
                "Modified Tenengrad did not use the squared near-black intensity plus 0.001");
        require(near_black_score.at<float>(3, x) > 900 * near_black_original.at<float>(3, x),
                "Near-black nonzero gradients did not receive the expected finite amplification");
    }
}

} // 匿名命名空间

// 相同纹理模糊后，三种清晰度指标的平均响应都应显著下降。
// 在归一化灰度图上直接检查内部指标，以便将评分问题与后续融合问题区分。
void testFocusMeasure() {
    auto sharp = texture();
    cv::Mat input, blurred;
    sharp.convertTo(input, CV_32F, 1.0 / 255.0);
    cv::GaussianBlur(input, blurred, {0, 0}, 3);
    for (auto method : {mif::FocusMeasure::ModifiedLaplacian, mif::FocusMeasure::Tenengrad,
                        mif::FocusMeasure::ModifiedTenengrad}) {
        const auto a = mif::detail::fusion::focusMeasure(input, method, 9);
        const auto b = mif::detail::fusion::focusMeasure(blurred, method, 9);
        require(cv::mean(a)[0] > 3 * cv::mean(b)[0], "Focus metric does not distinguish sharp detail");
    }
    checkModifiedTenengradFormula();
}

