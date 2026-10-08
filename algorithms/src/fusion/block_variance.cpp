// 算法流程参考 OpenFocus 的 fusion_methods/dct.py，提交 bf3a3a15c1c508fbba117f6e98a64e49a087434e。
// 参考项目为 MIT 许可，Copyright (c) 2025 OpenFocus Contributors；完整声明见 THIRD_PARTY_NOTICES.md。
// 本文件为独立 C++ 实现：保留块方差与两次中值一致性步骤，同时支持原位深、完整边缘、
// int32 多帧索引，以及平坦/并列块的等权融合；不复刻上游裁边和 uint8 索引限制。
#include "fusion/registry.hpp"
#include <mif/fusion/block_variance_fusion_options.hpp>
#include "fusion/common/weight_map.hpp"
#include "common/grayscale.hpp"
#include "common/progress.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace mif::detail::fusion {
namespace {

/// CV_32S 中值保持全部原始输入序号；OpenCV medianBlur 的大窗口不支持此类型。
/// 边界复制与上游 medianBlur 一致。中值处理的是按输入次序编号的块来源，而非像素值。
cv::Mat medianIndices(const cv::Mat& input, int window, int pass, const ProgressCallback& progress) {
    cv::Mat output(input.size(), CV_32S);
    const int radius = window / 2;
    std::vector<int> neighborhood(static_cast<size_t>(window) * window);
    for (int y = 0; y < input.rows; ++y) {
        report(progress, 50 + static_cast<int>(20.0 * (pass * input.rows + y) / (2.0 * input.rows)), "weights");
        int* destination = output.ptr<int>(y);
        for (int x = 0; x < input.cols; ++x) {
            size_t count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int* row = input.ptr<int>(std::clamp(y + dy, 0, input.rows - 1));
                for (int dx = -radius; dx <= radius; ++dx)
                    neighborhood[count++] = row[std::clamp(x + dx, 0, input.cols - 1)];
            }
            auto middle = neighborhood.begin() + neighborhood.size() / 2;
            std::nth_element(neighborhood.begin(), middle, neighborhood.end());
            destination[x] = *middle;
        }
    }
    return output;
}

/// 按真实块边界展开权重，避免把不整除的宽高整体 resize 后移动所有块边界。
cv::Mat expandBlocks(const cv::Mat& blocks, const cv::Size& size, int block_size) {
    cv::Mat expanded(size, CV_32F);
    for (int y = 0; y < size.height; ++y) {
        const float* source = blocks.ptr<float>(y / block_size);
        float* destination = expanded.ptr<float>(y);
        for (int bx = 0; bx < blocks.cols; ++bx) {
            const int start = bx * block_size;
            const int length = std::min(block_size, size.width - start);
            std::fill(destination + start, destination + start + length, source[bx]);
        }
    }
    return expanded;
}

/// 校验块边长和块级一致性窗口；无效配置抛 std::invalid_argument。
void validateBlockVarianceOptions(const BlockVarianceFusionOptions& options) {
    if (options.block_size < 2 || options.block_size > 128 || options.consistency_window_size < 1 ||
        options.consistency_window_size > 31 || options.consistency_window_size % 2 == 0)
        throw std::invalid_argument("Invalid DCT block-variance options; check block size and consistency window");
}

/// 归一化 CV_32F 灰度/BGR 栈的块方差选帧融合；不修改输入，配置应已校验。
/// 返回与原图同尺寸的图像、CV_32S 原始输入索引与逐图归一化权重。
/// 块方差完全并列时均分权重；进度及取消异常原样传播。
MethodResult blockVarianceFusion(const std::vector<cv::Mat>& images, const BlockVarianceFusionOptions& options,
                       const ProgressCallback& progress) {
    const auto size = images.front().size();
    const cv::Size grid((size.width - 1) / options.block_size + 1,
                        (size.height - 1) / options.block_size + 1);
    std::vector<cv::Mat> scores;
    scores.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 30 + static_cast<int>(20 * i / images.size()), "focus");
        const cv::Mat gray = grayscale(images[i]);
        cv::Mat variance(grid, CV_32F);
        for (int by = 0; by < grid.height; ++by) {
            float* row = variance.ptr<float>(by);
            for (int bx = 0; bx < grid.width; ++bx) {
                const int x = bx * options.block_size, y = by * options.block_size;
                const cv::Rect block(x, y, std::min(options.block_size, size.width - x),
                                          std::min(options.block_size, size.height - y));
                cv::Scalar mean, deviation;
                // 总体方差等于正交 DCT 的交流系数平方和除以块像素数。
                // meanStdDev 使用更高精度统计，避免直接在 float 中相减引起平坦块负方差。
                cv::meanStdDev(gray(block), mean, deviation);
                row[bx] = static_cast<float>(deviation[0] * deviation[0]);
            }
        }
        scores.push_back(std::move(variance));
    }

    report(progress, 50, "weights");
    auto block_weights = decisionWeights(scores);
    scores.clear();
    cv::Mat indices = dominantIndices(block_weights);
    indices = medianIndices(indices, options.consistency_window_size, 0, progress);
    indices = medianIndices(indices, options.consistency_window_size, 1, progress);
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            size_t tied = 0;
            for (const auto& weight : block_weights) tied += weight.at<float>(y, x) > 0;
            // 完全无纹理或并列的块没有可靠首选，保留平均，防止输入顺序决定平坦区亮度。
            // 有唯一首选的块才采用中值一致性修正后的来源。
            if (tied == 1) {
                const int selected = indices.at<int>(y, x);
                for (size_t i = 0; i < block_weights.size(); ++i)
                    block_weights[i].at<float>(y, x) = static_cast<int>(i) == selected ? 1.0f : 0.0f;
            }
        }
    }
    report(progress, 70, "weights");
    MethodResult result;
    result.image = cv::Mat::zeros(size, images.front().type());
    result.weight_maps.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        report(progress, 75 + static_cast<int>(20 * i / images.size()), "blend");
        result.weight_maps.push_back(expandBlocks(block_weights[i], size, options.block_size));
        result.image += images[i].mul(expandWeight(result.weight_maps.back(), images.front().channels()));
    }
    result.source_index_map = dominantIndices(result.weight_maps);
    return result;
}

} // 匿名命名空间

/// 在本文件绑定参数校验和执行；注册表只需显式引用这个函数。
void registerBlockVarianceFusionMethod() {
    registerFusionMethod(validateBlockVarianceOptions, blockVarianceFusion);
}

} // 命名空间 mif::detail::fusion
