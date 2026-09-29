#pragma once

#include <mif/fusion_options.hpp>
#include <mif/progress.hpp>
#include <mif/export.hpp>
#include <opencv2/core.hpp>
#include <vector>

namespace mif {

/// 融合结果，不包含配准信息。图像数据不引用输入缓冲区，由 cv::Mat 引用计数管理。
/// 复制结构时默认共享结果数据；需要独立可写副本时请调用 clone()。
struct FusionResult {
    /// 融合图像，尺寸、深度和通道数均与传入的图像一致。
    cv::Mat image;
    /// 可选的 CV_32SC1 来源索引图，与 image 同尺寸；方法未提供来源诊断时为空。
    /// GFF/金字塔记录细节权重最大的零起始输入序号；DCT 记录最终选块来源；
    /// GFG-FGF 记录最终权重最大的原始输入序号，并列时取较小序号。
    /// DTCWT 按尺度、方向选择复系数，无单一像素来源，因此返回空图。
    /// 来源图用于观察贡献，不代表置信度。
    cv::Mat focus_indices;
    /// 可选的 CV_32FC1 权重诊断图，顺序与输入相同，与 image 同尺寸。
    /// GFF/金字塔提供细节权重，DCT 提供选块权重，GFG-FGF 提供最终融合权重。
    /// 这些权重非负、逐像素总和约为 1；不含 GFF 基础权重或金字塔各层权重。
    /// DTCWT 不提供空间权重，始终返回空列表。
    /// keep_weight_maps 为 false 或方法未提供权重诊断时为空。
    std::vector<cv::Mat> weights;
};

/**
 * @brief 直接融合传入的图像，不执行配准，不使用 AI 模型。
 * @param images 至少两张已对齐的同尺寸、同类型二维图像，各边至少 2 像素。
 *               支持 CV_8U/CV_16U/CV_32F、灰度或 BGR；浮点值须有限且在 [0, 1]。
 *               允许非连续 ROI；图像数量须能用 int 表示。
 * @param options 方法选择、各方法独立配置和诊断开关；仅校验所选方法的配置。
 * @param progress 可选同步回调，进度为 [0, 100]，返回 false 请求取消。
 * @throws std::invalid_argument 输入或融合参数无效。
 * @throws cv::Exception OpenCV 处理失败。
 * @throws Cancelled 回调请求取消。
 *
 * 不修改输入。调用期间须保持输入缓冲区有效且不被其他线程修改。
 * 各次调用不共享可变算法状态；回调及结果的跨线程访问由调用者管理。
 */
MIF_EXPORT FusionResult fuse(const std::vector<cv::Mat>& images,
                             const FusionOptions& options = {},
                             const ProgressCallback& progress = {});

} // 命名空间 mif
