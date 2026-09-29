#pragma once

#include "fusion/method_result.hpp"
#include <mif/fusion/gfgfgf_options.hpp>
#include <mif/progress.hpp>

namespace mif::detail::fusion {

/// 校验筛选比例、局部差异阈值、窗口、引导半径与正则项。
void validateGfgfgfOptions(const GfgfgfOptions& options);

/// 归一化 CV_32F 灰度/BGR 栈的梯度筛选与两阶段引导滤波融合。
/// 被筛除的原始输入返回零权重；诊断索引始终使用原始输入顺序。
/// 保留输入尺寸与通道，配置应已校验；进度及取消异常原样传播。
MethodResult gfgfgfFusion(const std::vector<cv::Mat>& images, const GfgfgfOptions& options,
                          const ProgressCallback& progress);

} // 命名空间 mif::detail::fusion
