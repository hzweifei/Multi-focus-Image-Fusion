#pragma once

#include <opencv2/core.hpp>
#include <array>
#include <functional>
#include <vector>

namespace mif::detail::fusion::dtcwt {

/// 一层六个方向的复系数；每张 CV_64FC2 图按实部、虚部存放。
/// 顺序对应约 +15、+45、+75、-75、-45、-15 度的方向选择滤波器。
using DirectionBands = std::array<cv::Mat, 6>;

/// 变换的独立存储；尺寸元数据用于逆变换时精确撤销边界延拓。
struct Pyramid {
    cv::Mat lowpass;                       ///< 最粗层的 CV_64FC1 实低频，四棵实树交错存放。
    std::vector<DirectionBands> highpass; ///< 从细到粗排列的六方向复高频。
    std::vector<cv::Size> input_sizes;    ///< 每层分析前、补边前的低频尺寸。
    cv::Size original_size;              ///< 输入的原始尺寸，包含可能的奇数行、列。
};

/// 层级边界的取消检查；由上层映射到整体进度，不在滤波器内部处理异常。
using Checkpoint = std::function<void()>;

/// 计算实际层数：首层允许 1×1 输入，其余层要求两维低频均大于 2。
int effectiveLevels(cv::Size size, int requested_levels);

/// 单通道双树复小波正变换；接受非空 CV_32FC1 或 CV_64FC1，内部使用双精度。
/// 输入不会被修改；首层奇数维在末端重复一个采样，后续按四的倍数对称补边。
Pyramid forward(const cv::Mat& image, int requested_levels, const Checkpoint& checkpoint = {});

/// 逆变换返回 original_size 大小的 CV_64FC1 图；不会修改传入系数。
/// 调用方必须提供由 forward 生成且系数尺寸未改变的金字塔。
cv::Mat inverse(const Pyramid& pyramid, const Checkpoint& checkpoint = {});

} // 命名空间 mif::detail::fusion::dtcwt
