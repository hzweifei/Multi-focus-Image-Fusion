#pragma once

#include <mif/options.hpp>
#include <opencv2/core.hpp>

namespace mif::detail {

/// 将归一化的 CV_32F 灰度或 BGR 图像转换为 CV_32FC1。
/// 灰度输入直接返回共享数据的浅拷贝，调用方应只读使用，避免修改原图。
cv::Mat grayscale(const cv::Mat& image);

/// 计算灰度图的非负清晰度响应，输出同尺寸 CV_32FC1。
/// gray 应为归一化浮点图，method 应为合法枚举，window 应为已校验的正奇数。
/// 二阶差分或梯度能量经 window×window 局部平均，以降低单个像素噪声的影响。
cv::Mat focusMeasure(const cv::Mat& gray, FocusMeasure method, int window);

} // 命名空间 mif::detail

