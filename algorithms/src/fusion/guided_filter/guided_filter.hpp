#pragma once

#include "fusion/method_result.hpp"
#include <mif/fusion/guided_filter_options.hpp>
#include <mif/progress.hpp>

namespace mif::detail::fusion {

/// 校验本方法的清晰度、两组半径及正则项；无效参数抛 invalid_argument。
void validateGuidedFilterOptions(const GuidedFilterOptions& options);

/// 双尺度融合：自行评分、生成两组权重、重建图像并提供来源诊断。
/// images 至少两张同尺寸 CV_32F 灰度或 BGR 图像，值域 [0, 1]；options 已校验。
/// 不修改输入；进度回调同步执行，取消及调用者异常原样传播。
MethodResult guidedFilterFusion(const std::vector<cv::Mat>& images,
                                const GuidedFilterOptions& options, const ProgressCallback& progress);

} // 命名空间 mif::detail::fusion
