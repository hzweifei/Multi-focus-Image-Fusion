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
    /// CV_32SC1 索引图，与 image 同尺寸，记录细节权重最大的输入序号。
    /// 序号从 0 开始；权重并列时取较小序号。用于观察来源，不代表置信度。
    cv::Mat focus_indices;
    /// 可选的 CV_32FC1 细节权重图，顺序与输入相同，与 image 同尺寸。
    /// 各图权重非负且逐像素总和约为 1；keep_weight_maps 为 false 时为空。
    std::vector<cv::Mat> weights;
};

/**
 * @brief 直接融合传入的图像，不执行配准，不使用 AI 模型。
 * @param images 至少两张已对齐的同尺寸、同类型二维图像，各边至少 2 像素。
 *               支持 CV_8U/CV_16U/CV_32F、灰度或 BGR；浮点值须有限且在 [0, 1]。
 *               允许非连续 ROI；图像数量须能用 int 表示。
 * @param options 融合方法、清晰度和滤波参数，范围见 FusionOptions。
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
