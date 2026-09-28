#include "registration/registration.hpp"
#include "common/grayscale.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace mif::detail::registration {
namespace {

/// 在 CV_8UC1 有效区域掩码中寻找完全由非零像素构成的最大轴对齐矩形。
/// 逐行将问题转换为直方图最大矩形，使用单调栈将复杂度控制在 O(宽×高)。
cv::Rect largestRectangle(const cv::Mat& mask) {
    // 最后一列是始终为 0 的哨兵，用于在每一行末尾弹出尚未结算的柱子。
    std::vector<int> heights(mask.cols + 1, 0);
    cv::Rect best;
    long long best_area = 0;
    for (int y = 0; y < mask.rows; ++y) {
        const auto* row = mask.ptr<unsigned char>(y);
        // heights[x] 记录当前像素向上连续有效的高度。
        for (int x = 0; x < mask.cols; ++x) heights[x] = row[x] ? heights[x] + 1 : 0;
        std::vector<int> stack;
        for (int x = 0; x <= mask.cols; ++x) {
            while (!stack.empty() && heights[stack.back()] > heights[x]) {
                // 遇到较矮柱子时，已确定弹出柱子的最宽跨度：[left, x)。
                const int h = heights[stack.back()];
                stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back() + 1;
                const long long area = static_cast<long long>(x - left) * h;
                if (area > best_area) {
                    best_area = area;
                    best = {left, y - h + 1, x - left, h};
                }
            }
            stack.push_back(x);
        }
    }
    return best;
}
/// 工作图采用 resize 的像素中心约定：x_small = sx * (x + 0.5) - 0.5。
/// 分别使用实际横纵比例，兼容奇数尺寸取整；平移以外的模型还需考虑像素中心偏移。
cv::Mat resizeTransform(const cv::Size& original, const cv::Size& working) {
    const double sx = static_cast<double>(working.width) / original.width;
    const double sy = static_cast<double>(working.height) / original.height;
    return (cv::Mat_<double>(3, 3) << sx, 0, (sx - 1) * 0.5,
                                     0, sy, (sy - 1) * 0.5,
                                     0, 0, 1);
}

/// 统一检查几何有效性；有限数值本身不能保证可逆，也不能排除投影穿过无穷远。
/// 这些检查用于拒绝退化结果，并不代表实际对齐误差已经满足某个精度指标。
cv::Mat checkedTransform(const cv::Mat& transform, const cv::Size& size) {
    if (transform.size() != cv::Size(3, 3) || transform.type() != CV_64FC1 ||
        !cv::checkRange(transform) || std::abs(transform.at<double>(2, 2)) < 1e-12)
        throw std::runtime_error("Registration produced an invalid homogeneous transform");
    cv::Mat normalized = transform / transform.at<double>(2, 2);
    if (!cv::checkRange(normalized) || cv::determinant(normalized) <= 1e-10)
        throw std::runtime_error("Registration transform is singular or reverses image orientation");
    // 齐次分母在矩形上是线性函数，四角均为正可排除区域内的零点和符号翻转。
    for (const auto& point : {cv::Point2d(0, 0), cv::Point2d(size.width - 1, 0),
                              cv::Point2d(0, size.height - 1), cv::Point2d(size.width - 1, size.height - 1)}) {
        const double denominator = normalized.at<double>(2, 0) * point.x +
                                   normalized.at<double>(2, 1) * point.y + 1;
        if (!std::isfinite(denominator) || denominator <= 1e-6)
            throw std::runtime_error("Registration transform crosses a projective horizon");
    }
    return normalized;
}

/// 新方法只需提供估计器工厂并在这里增加分派；公共重采样和裁剪无需重复实现。
std::unique_ptr<Estimator> makeEstimator(const cv::Mat& reference, const FusionOptions& options) {
    switch (options.alignment) {
    case Alignment::FeatureHomography:
        return makeHomographyEstimator(reference, options);
    case Alignment::Translation:
    case Alignment::Affine:
    case Alignment::EccHomography:
        return makeEccEstimator(reference, options);
    default:
        throw std::invalid_argument("Unknown registration estimator");
    }
}
} // 匿名命名空间

void alignImages(std::vector<cv::Mat>& images, const FusionOptions& options,
                 FusionResult& result, const ProgressCallback& progress) {
    const cv::Size size = images.front().size();
    result.crop = {0, 0, size.width, size.height};
    const bool perspective = options.alignment == Alignment::FeatureHomography ||
                             options.alignment == Alignment::EccHomography;
    // 原有模式保留 2×3 输出约定；新增单应性模式使用 3×3，参考图始终为单位变换。
    for (size_t i = 0; i < images.size(); ++i)
        result.transforms.push_back(cv::Mat::eye(perspective ? 3 : 2, 3, CV_32F));
    if (options.alignment == Alignment::None) return;

    // 缩小图像仅用于估计变换，随后将矩阵换回原始坐标，在原分辨率上重采样。
    const double scale = std::min(1.0, static_cast<double>(options.alignment_max_size) /
                                         std::max(size.width, size.height));
    const cv::Size small(std::max(1, cvRound(size.width * scale)),
                         std::max(1, cvRound(size.height * scale)));
    if (std::min(small.width, small.height) < 16)
        throw std::invalid_argument("Registration requires working images at least 16 x 16");
    const cv::Mat to_small = resizeTransform(size, small);
    const cv::Mat to_original = to_small.inv();
    auto prepare = [small](const cv::Mat& image) {
        cv::Mat gray;
        cv::resize(grayscale(image), gray, small, 0, 0, cv::INTER_AREA);
        return gray;
    };
    report(progress, 10, "align");
    std::unique_ptr<Estimator> estimator;
    try {
        // 方法自身负责特有预处理与缓存；公共层只统一灰度、尺寸和坐标系。
        estimator = makeEstimator(prepare(images.front()), options);
    } catch (const std::exception& error) {
        throw std::runtime_error("Registration failed for reference image 1: " + std::string(error.what()));
    }
    cv::Mat common(size, CV_8U, cv::Scalar(255));
    const cv::Mat valid(size, CV_32F, cv::Scalar(1));
    for (size_t i = 1; i < images.size(); ++i) {
        report(progress, 10 + static_cast<int>(20 * i / images.size()), "align");
        try {
            // 统一方向为参考 -> 源；矩阵使用双精度完成坐标换算，避免缩放放大舍入误差。
            const cv::Mat working_transform = estimator->estimate(prepare(images[i]));
            const cv::Mat transform = checkedTransform(to_original * working_transform * to_small, size);
            cv::Mat warp;
            if (perspective) transform.convertTo(warp, CV_32F);
            else transform.rowRange(0, 2).convertTo(warp, CV_32F);
            if (!cv::checkRange(warp)) throw std::runtime_error("Registration transform exceeds float range");
            result.transforms[i] = warp;
            cv::Mat aligned, coverage;
            // 只重采样一次原始分辨率图像；相同反向映射同时用于检查像素覆盖范围。
            if (perspective) {
                cv::warpPerspective(images[i], aligned, warp, size,
                                    cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
                cv::warpPerspective(valid, coverage, warp, size,
                                    cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
            } else {
                cv::warpAffine(images[i], aligned, warp, size,
                               cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
                cv::warpAffine(valid, coverage, warp, size,
                               cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
            }
            // 对全 1 掩码进行相同插值，剔除混入边界补零的像素，防止融合后出现黑边。
            common &= coverage >= 0.9999f;
            images[i] = aligned;
        } catch (const std::exception& error) {
            throw std::runtime_error("Registration failed for image " + std::to_string(i + 1) +
                                     ": " + error.what());
        }
    }
    // 直接取掩码外接矩形可能包含无效角落；最大内接矩形保证输出每个像素都有效。
    result.crop = largestRectangle(common);
    if (std::min(result.crop.width, result.crop.height) < 8)
        throw std::runtime_error("Insufficient common image area after alignment");
    // clone 让裁剪图持有独立、连续的数据，也释放对整幅配准缓冲区的引用。
    for (auto& image : images) image = image(result.crop).clone();
}

} // 命名空间 mif::detail::registration

