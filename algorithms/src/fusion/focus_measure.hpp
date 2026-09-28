#pragma once

#include <mif/fusion.hpp>
#include <vector>

namespace mif::detail::fusion {

/// 两种融合方法共用的灰度引导图和初始清晰度决策图，顺序与输入图像一致。
/// 灰度引导图为 CV_32FC1；灰度输入时可能共享输入缓冲区，只能只读使用。
/// 决策图为同尺寸 CV_32FC1，各输入在同一像素处的权重和约为 1。
struct FocusMaps {
    std::vector<cv::Mat> guides;    ///< 灰度引导图，用于生成保持边缘的融合权重。
    std::vector<cv::Mat> decisions; ///< 按清晰度择优的初始权重，并列时共同分配。
};

/// 计算灰度图的非负清晰度响应，输出同尺寸 CV_32FC1。
/// gray 应为归一化浮点图，method 应为合法枚举，window 应为已校验的正奇数。
/// 二阶差分或梯度能量经 window×window 局部平均，以降低单个像素噪声的影响。
cv::Mat focusMeasure(const cv::Mat& gray, FocusMeasure method, int window);

/// 生成灰度图、清晰度响应和初始决策权重；决策生成后立即释放清晰度响应。
/// images 至少含两张同尺寸 CV_32F 灰度或 BGR 图像，值域为 [0, 1]，options 已校验。
/// 进度在调用线程同步报告，不修改输入；取消及算法异常直接向上传播。
FocusMaps prepareFocusMaps(const std::vector<cv::Mat>& images,
                           const FusionOptions& options, const ProgressCallback& progress);

} // 命名空间 mif::detail::fusion

