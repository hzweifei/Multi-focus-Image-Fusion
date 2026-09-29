#pragma once

#include "fusion/method_result.hpp"
#include <mif/fusion/dtcwt_options.hpp>
#include <mif/progress.hpp>

namespace mif::detail::fusion {

/// 校验本方法层数及活动度窗口；无效值抛出 std::invalid_argument。
void validateDtcwtOptions(const DtcwtOptions& options);

/// 六方向双树复小波融合：低频均值，高频按活动度和邻域多数一致性选择复系数。
/// 输入为至少两张同尺寸 CV_32F 灰度或 BGR 图，值域 [0, 1]；options 已校验。
/// 各通道独立处理，多帧按输入顺序合并；不修改输入，回调异常原样传播。
/// 系数选择发生在多个尺度和方向，故不返回空间域来源索引或逐图权重。
MethodResult dtcwtFusion(const std::vector<cv::Mat>& images, const DtcwtOptions& options,
                        const ProgressCallback& progress);

} // 命名空间 mif::detail::fusion
