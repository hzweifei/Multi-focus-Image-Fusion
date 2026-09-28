#pragma once

#include <mif/fusion.hpp>

namespace mif::detail {

/// 将归一化的浮点图像配准到第一张图像，并更新结果中的 crop 和 transforms。
/// images 至少含两张同尺寸、同类型的图像，options 已校验，result 应是新建结果。
/// 开启配准时会替换 images 中的图像并裁剪出共同有效区域；不会修改外部原始输入。
/// 未开启配准时保留图像、记录完整区域和单位变换。失败或取消时抛出异常。
void alignImages(std::vector<cv::Mat>& images, const FusionOptions& options,
                 FusionResult& result, const ProgressCallback& progress);

} // 命名空间 mif::detail

