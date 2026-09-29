#pragma once

#include <mif/fusion.hpp>
#include <mif/registration.hpp>

namespace mif {

/// 顺序执行配准和融合的结果，不长期保留中间的整批配准图像。
struct PipelineResult {
    /// 融合阶段的输出。
    FusionResult fusion;
    /// 配准阶段的公共裁剪区域，坐标相对第一张原始输入。
    cv::Rect crop;
    /// 配准阶段的变换矩阵，格式和坐标约定见 RegistrationResult。
    std::vector<cv::Mat> transforms;
};

/// 便捷流程：registerImages(images, registration) 后，将其 images 交给 fuse()。
/// 与显式调用两步的结果一致；整数图像在配准后先恢复原位深，再交给融合。
/// 总进度中配准占 [0, 40]，融合占 [40, 100]；仅全部完成时报告 done。
/// 各阶段独立校验自己的选项；异常和取消原样传播。
MIF_EXPORT PipelineResult registerAndFuse(const std::vector<cv::Mat>& images,
                                          const RegistrationOptions& registration = {},
                                          const FusionOptions& fusion = {},
                                          const ProgressCallback& progress = {});

} // 命名空间 mif
