#include "registration.hpp"
#include "focus_measure.hpp"
#include "blending.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <algorithm>

namespace mif::detail {
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
} // 匿名命名空间

void alignImages(std::vector<cv::Mat>& images, const FusionOptions& options,
                 FusionResult& result, const ProgressCallback& progress) {
    const cv::Size size = images.front().size();
    result.crop = {0, 0, size.width, size.height};
    // 未配准时仍提供完整元数据；参考图始终对应单位变换。
    for (size_t i = 0; i < images.size(); ++i)
        result.transforms.push_back(cv::Mat::eye(2, 3, CV_32F));
    if (options.alignment == Alignment::None) return;

    // 缩小图像仅用于估计变换，随后将矩阵换回原始坐标，在原分辨率上重采样。
    const double scale = std::min(1.0, static_cast<double>(options.alignment_max_size) /
                                         std::max(size.width, size.height));
    const cv::Size small(std::max(1, cvRound(size.width * scale)),
                         std::max(1, cvRound(size.height * scale)));
    if (std::min(small.width, small.height) < 16)
        throw std::invalid_argument("ECC alignment requires images at least 16 x 16");
    // 尺寸取整后水平与垂直缩放比例可能略有差别，必须分别保留。
    const double sx = static_cast<double>(small.width) / size.width;
    const double sy = static_cast<double>(small.height) / size.height;
    auto prepare = [small](const cv::Mat& image) {
        cv::Mat gray;
        cv::resize(grayscale(image), gray, small, 0, 0, cv::INTER_AREA);
        // 轻度平滑降低不同焦点导致的高频差异，使 ECC 更容易匹配共同结构。
        cv::GaussianBlur(gray, gray, {5, 5}, 1.2);
        return gray;
    };
    const cv::Mat reference = prepare(images.front());
    cv::Scalar mean, deviation;
    cv::meanStdDev(reference, mean, deviation);
    // 近乎常量的参考图没有足够纹理约束运动，提前返回清晰的错误原因。
    if (deviation[0] < 1e-6) throw std::runtime_error("ECC cannot align a textureless reference image");
    cv::Mat common(size, CV_8U, cv::Scalar(255));
    const cv::Mat valid(size, CV_32F, cv::Scalar(1));
    for (size_t i = 1; i < images.size(); ++i) {
        report(progress, 10 + static_cast<int>(20 * i / images.size()), "align");
        // ECC 从单位变换开始寻找参考图到源图的映射；大位移或强外观差异可能不收敛。
        cv::Mat warp = cv::Mat::eye(2, 3, CV_32F);
        try {
            cv::findTransformECC(reference, prepare(images[i]), warp,
                options.alignment == Alignment::Translation ? cv::MOTION_TRANSLATION : cv::MOTION_AFFINE,
                {cv::TermCriteria::COUNT | cv::TermCriteria::EPS,
                 options.alignment_iterations, options.alignment_epsilon});
        } catch (const cv::Exception& error) {
            throw std::runtime_error("ECC alignment failed for image " + std::to_string(i + 1) +
                                     ": " + error.what());
        }
        // 通过 S^-1 * W * S 将缩小图上的变换还原到原始坐标；
        // 非对角线元素需补偿两个方向不同的取整缩放，平移量分别除以对应缩放。
        warp.at<float>(0, 1) *= static_cast<float>(sy / sx);
        warp.at<float>(1, 0) *= static_cast<float>(sx / sy);
        warp.at<float>(0, 2) /= static_cast<float>(sx);
        warp.at<float>(1, 2) /= static_cast<float>(sy);
        if (!cv::checkRange(warp)) throw std::runtime_error("ECC produced an invalid transform");
        result.transforms[i] = warp;
        cv::Mat aligned, coverage;
        // WARP_INVERSE_MAP 直接使用“参考坐标 -> 源图坐标”映射执行反向采样。
        cv::warpAffine(images[i], aligned, warp, size,
                       cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
        cv::warpAffine(valid, coverage, warp, size,
                       cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT);
        // 对全 1 掩码采用相同的插值过程，剔除混入边界补零的像素，防止融合后出现黑边。
        // 阈值接近 1，并留少量浮点容差；common 为所有源图完整覆盖区域的交集。
        common &= coverage >= 0.9999f;
        images[i] = aligned;
    }
    // 直接取掩码外接矩形可能包含无效角落；最大内接矩形保证输出每个像素都有效。
    result.crop = largestRectangle(common);
    if (std::min(result.crop.width, result.crop.height) < 8)
        throw std::runtime_error("Insufficient common image area after alignment");
    // clone 让裁剪图持有独立、连续的数据，也释放对整幅配准缓冲区的引用。
    for (auto& image : images) image = image(result.crop).clone();
}

} // 命名空间 mif::detail

