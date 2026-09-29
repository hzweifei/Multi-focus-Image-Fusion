#pragma once

#include <mif/progress.hpp>
#include <opencv2/core.hpp>
#include <vector>

namespace mif::detail {

/// 配准与融合共用的输入约束，不检查任何阶段专属参数。
void validateImages(const std::vector<cv::Mat>& inputs);

/// 将已校验的输入复制为 [0, 1] 的 CV_32F 工作图像；报告 0～10 的 prepare 进度。
std::vector<cv::Mat> normalizeImages(const std::vector<cv::Mat>& inputs,
                                     const ProgressCallback& progress);

/// 已校验位深的强度范围，用于归一化和恢复原位深。
double imageRange(int depth);

} // 命名空间 mif::detail
