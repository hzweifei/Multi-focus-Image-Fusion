#pragma once

#include "fusion/method_result.hpp"
#include <mif/fusion/dct_options.hpp>
#include <mif/progress.hpp>

namespace mif::detail::fusion {

/// 校验块边长和块级一致性窗口；无效配置抛 std::invalid_argument。
void validateDctOptions(const DctOptions& options);

/// 归一化 CV_32F 灰度/BGR 栈的块方差选帧融合；不修改输入，配置应已校验。
/// 返回与原图同尺寸的图像、CV_32S 原始输入索引与逐图归一化权重。
/// 块方差完全并列时均分权重；进度及取消异常原样传播。
MethodResult dctFusion(const std::vector<cv::Mat>& images, const DctOptions& options,
                       const ProgressCallback& progress);

} // 命名空间 mif::detail::fusion
