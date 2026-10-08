#include "registration/registry.hpp"
#include <mif/registration.hpp>
#include "common/grayscale.hpp"
#include "common/image_stack.hpp"
#include "common/progress.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace mif::detail::registration {
namespace {

/// 公共入口只校验所有方法共有的工作尺寸；方法专属校验器由注册表提供。
void validateCommonOptions(const RegistrationOptionsBase& options) {
    if (options.max_working_dimension < 16 || options.max_working_dimension > 8192)
        throw std::invalid_argument("Registration working dimension must be within [16, 8192]");
}

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

/// 对独立的 [0, 1] 浮点工作图进行配准与共同区域裁剪，元数据写入配准结果。
/// 此处不恢复位深；输入及实际参数类型均已由公开入口和方法校验器检查。
void alignNormalized(std::vector<cv::Mat>& images, const RegistrationOptionsBase& options,
                     const RegistrationMethodEntry& method,
                     RegistrationResult& result, const ProgressCallback& progress) {
    const cv::Size size = images.front().size();
    result.crop_region = {0, 0, size.width, size.height};
    // 缩小图像仅用于估计变换，随后将矩阵换回原始坐标，在原分辨率上重采样。
    const double scale = std::min(1.0, static_cast<double>(options.max_working_dimension) /
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
        estimator = method.create(prepare(images.front()), options);
        if (!estimator) throw std::runtime_error("Registration factory returned no estimator");
    } catch (const std::exception& error) {
        throw std::runtime_error("Registration failed for reference image 1: " + std::string(error.what()));
    }
    // 具体估计器报告本次任务的模型；公共流程不判断方法名称或参数派生类型。
    const bool perspective = estimator->isProjective();
    for (size_t i = 0; i < images.size(); ++i)
        result.transforms.push_back(cv::Mat::eye(perspective ? 3 : 2, 3, CV_32F));
    cv::Mat common(size, CV_8U, cv::Scalar(255));
    const cv::Mat valid(size, CV_32F, cv::Scalar(1));
    for (size_t i = 1; i < images.size(); ++i) {
        report(progress, 10 + static_cast<int>(80 * i / images.size()), "align");
        try {
            // 统一方向为参考 -> 源；矩阵使用双精度完成坐标换算，避免缩放放大舍入误差。
            const cv::Mat working_transform = estimator->estimate(prepare(images[i]));
            const cv::Mat transform = checkedTransform(to_original * working_transform * to_small, size);
            // 仿射采样会丢掉最后一行，不能把扩展方法误报的透视项静默截断。
            if (!perspective && (std::abs(transform.at<double>(2, 0)) > 1e-12 ||
                                 std::abs(transform.at<double>(2, 1)) > 1e-12))
                throw std::runtime_error("Affine registration estimator returned a projective transform");
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
            // 对全 1 掩码进行相同插值，剔除混入边界补零的像素，防止结果出现黑边。
            common &= coverage >= 0.9999f;
            images[i] = aligned;
        } catch (const std::exception& error) {
            throw std::runtime_error("Registration failed for image " + std::to_string(i + 1) +
                                     ": " + error.what());
        }
    }
    // 进度回调位于求解异常包装之外，确保 Cancelled 或调用者异常保持原类型。
    report(progress, 90, "align");
    // 直接取掩码外接矩形可能包含无效角落；最大内接矩形保证输出每个像素都有效。
    result.crop_region = largestRectangle(common);
    if (std::min(result.crop_region.width, result.crop_region.height) < 8)
        throw std::runtime_error("Insufficient common image area after alignment");
    // clone 让裁剪图持有独立、连续的数据，也释放对整幅配准缓冲区的引用。
    for (auto& image : images) image = image(result.crop_region).clone();
}

} // 匿名命名空间
} // 命名空间 mif::detail::registration

namespace mif {

RegistrationResult registerImages(const std::vector<cv::Mat>& inputs, const RegistrationOptionsBase& options,
                                  const ProgressCallback& progress) {
    detail::validateImages(inputs);
    detail::registration::validateCommonOptions(options);
    const auto method = detail::registration::findRegistrationMethod(options);
    method.validate(options);
    RegistrationResult result;
    if (method.copy_only) {
        // 跳过估计与重采样，但仍返回独立数据，调用者可以安全修改结果而不影响输入。
        result.crop_region = {0, 0, inputs.front().cols, inputs.front().rows};
        for (size_t i = 0; i < inputs.size(); ++i) {
            detail::report(progress, static_cast<int>(10 * i / inputs.size()), "prepare");
            result.images.push_back(inputs[i].clone());
            result.transforms.push_back(cv::Mat::eye(2, 3, CV_32F));
        }
    } else {
        // 工作图采用统一浮点范围，ECC 与特征提取的参数不随输入位深变化。
        // 公共归一化函数会分配独立缓冲区，并报告 prepare 阶段的进度。
        auto images = detail::normalizeImages(inputs, progress);
        detail::registration::alignNormalized(images, options, method, result, progress);
        detail::report(progress, 95, "finish");
        const double range = detail::imageRange(inputs.front().depth());
        for (const auto& image : images) {
            cv::Mat restored;
            // 返回值保留输入位深；整数图像在此发生一次量化，后续调用者拿到的即是这些像素。
            image.convertTo(restored, inputs.front().type(), range);
            result.images.push_back(std::move(restored));
        }
    }
    if (method.copy_only) detail::report(progress, 95, "finish");
    detail::report(progress, 100, "done");
    return result;
}

} // 命名空间 mif

