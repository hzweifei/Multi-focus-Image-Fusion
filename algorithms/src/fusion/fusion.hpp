#pragma once

#include <mif/fusion.hpp>
#include <vector>

namespace mif::detail::fusion {

/// 方法内部的浮点结果，由公共融合入口恢复输入位深并生成来源索引。
struct MethodResult {
    /// 与输入尺寸、通道数相同的 CV_32F 图像，分层重建可能产生少量越界值。
    /// 此处不裁剪到 [0, 1]，由公共融合入口统一处理。
    cv::Mat image;
    /// 每张输入对应一张归一化的 CV_32FC1 细节权重图，供公共入口计算来源索引。
    /// 即使 keep_weight_maps 为 false 也必须返回，由公共入口决定是否对外保留。
    std::vector<cv::Mat> weights;
};

/// 根据 options.method 调用对应融合方法；仅分派，不重复生成中间图像。
/// images 至少含两张同尺寸 CV_32F 灰度或 BGR 图像，值域为 [0, 1]，options 已校验。
/// 进度在调用线程同步报告，不修改输入；取消及算法异常直接向上传播。
/// 未知方法抛出 std::invalid_argument。
MethodResult run(const std::vector<cv::Mat>& images,
                 const FusionOptions& options, const ProgressCallback& progress);

/// 引导滤波融合：独立生成基础层和细节层权重，再分别加权重建。
/// 输入约定同 run，返回未恢复位深的图像及归一化细节权重。
MethodResult guidedFilterFusion(const std::vector<cv::Mat>& images,
                                const FusionOptions& options, const ProgressCallback& progress);

/// 拉普拉斯金字塔融合：生成细节权重后，对图像与权重逐尺度处理并重建。
/// 输入约定同 run；实际金字塔层数还受图像尺寸限制。
MethodResult laplacianPyramidFusion(const std::vector<cv::Mat>& images,
                                    const FusionOptions& options, const ProgressCallback& progress);

} // 命名空间 mif::detail::fusion
