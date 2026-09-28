#pragma once

#include <mif/fusion.hpp>

namespace mif::detail {

/// 将每张浮点图像分成均值基础层和残差细节层，分别按对应权重融合。
/// images 应非空、尺寸类型一致且已归一化；两组权重均为数量匹配、同尺寸的
/// CV_32FC1，每个像素跨图像归一化。radius 为基础层均值滤波半径。
/// 返回同类型、同尺寸的新图像，可能有少量越界值，由融合入口统一裁剪。
cv::Mat blendGuided(const std::vector<cv::Mat>& images,
                    const std::vector<cv::Mat>& base_weights,
                    const std::vector<cv::Mat>& detail_weights,
                    int radius, const ProgressCallback& progress);

/// 用权重的高斯金字塔融合图像的拉普拉斯金字塔，返回重建的浮点图像。
/// 输入和权重约束同 blendGuided；levels 包含最粗层，实际层数受短边尺寸限制。
/// 每次只保存一张输入的当前尺度，避免同时存放所有输入的完整金字塔。
cv::Mat blendPyramid(const std::vector<cv::Mat>& images,
                     const std::vector<cv::Mat>& weights,
                     int levels, const ProgressCallback& progress);

/// 同步报告阶段进度；空回调直接跳过，回调返回 false 时抛出 Cancelled。
/// 回调自身抛出的异常不在此处捕获，直接由上层调用者处理。
void report(const ProgressCallback& callback, int percent, const std::string& stage);

} // 命名空间 mif::detail

