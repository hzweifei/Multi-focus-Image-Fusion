#pragma once

#include <opencv2/imgproc.hpp>

namespace mif::detail {

/// 将归一化的 CV_32F 灰度或 BGR 图像转换为 CV_32FC1，供融合和配准共同使用。
/// 灰度输入直接返回共享数据的浅拷贝，调用方应只读使用；BGR 输入产生新缓冲区。
inline cv::Mat grayscale(const cv::Mat& image) {
    if (image.channels() == 1) return image;
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

} // 命名空间 mif::detail
