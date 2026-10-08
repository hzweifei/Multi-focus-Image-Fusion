#pragma once

#include <opencv2/core.hpp>

namespace mif::detail::fusion {

/// 官方内部采用 float32 统计；过小正则项会在平坦区域被舍去，造成除零。
/// 统一要求 epsilon 位于 [1e-6, FLT_MAX]，同时避免转成 float 时溢出。
/// 各使用该算子的方法在处理前调用。
void validateGuidedEpsilon(double epsilon);

/// 官方 ximgproc 灰度引导滤波的权重适配：用 guide 约束 input，输出裁到 [0, 1]。
/// 两个输入应为同尺寸 CV_32FC1，值域 [0, 1]；半径和正则项由调用方法校验。
/// 不修改输入；本算子只负责权重滤波，不执行图像分解或完整融合。
cv::Mat guidedFilter(const cv::Mat& guide, const cv::Mat& input, int radius, double epsilon);

/// 快速引导滤波：低分辨率拟合并平均线性系数，上采样系数后结合完整引导图。
/// 输入为同尺寸 CV_32FC1；使用双精度统计，输出 CV_32F，保留有符号响应。
/// 半径、正则项、下采样倍数由方法入口校验；此算子不裁剪或归一化权重。
cv::Mat fastGuidedFilter(const cv::Mat& guide, const cv::Mat& input,
                         int radius, double epsilon, int subsample);

} // 命名空间 mif::detail::fusion
