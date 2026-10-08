#pragma once

#include <mif/registration_options.hpp>
#include <mif/progress.hpp>
#include <mif/export.hpp>
#include <opencv2/core.hpp>
#include <vector>

namespace mif {

/// 配准结果，与融合过程无关；cv::Mat 通过引用计数管理结果的生命周期。
struct RegistrationResult {
    /// 配准并裁剪的图像，顺序、深度和通道数与输入一致，尺寸为 crop_region.size()。
    /// 每张图均有独立缓冲区，不引用输入；NoRegistrationOptions 也返回独立副本。
    std::vector<cv::Mat> images;
    /// 有效区域在第一张原始图像中的坐标；跳过配准时为整幅图像。
    cv::Rect crop_region;
    /// 每张输入对应一个 CV_32F 矩阵，将参考图原始坐标映射到该输入原始坐标。
    /// 跳过配准时为 2×3；ECC 的平移/仿射模型为 2×3，单应性模型为 3×3。
    /// SIFT 固定为 3×3，其参数中不包含运动模型。
    /// 第一张图及跳过配准时均为单位变换。映射输出像素时，先加上 crop_region 的左上角，
    /// 再应用矩阵；3×3 矩阵还需除以齐次坐标的第三项。
    std::vector<cv::Mat> transforms;
};

/**
 * @brief 将同一焦点批次的图像对齐到第一张图像，并裁剪公共有效矩形。
 * @param images 至少两张同尺寸、同类型的二维图像，各边至少 2 像素。
 *               支持 CV_8U/CV_16U/CV_32F、灰度或 BGR；浮点值须有限且在 [0, 1]。
 *               允许非连续 ROI；图像数量须能用 int 表示。
 * @param options 具体配准参数对象，实际类型决定方法；默认跳过配准，不包含融合设置。
 * @param progress 可选同步回调，进度为 [0, 100]，返回 false 请求取消。
 * @throws std::invalid_argument 输入或参数无效、参数类型未注册，或求解分辨率不足 16×16。
 * @throws std::runtime_error 配准失败、变换退化或公共区域不足 8×8；求解失败时附图像序号。
 * @throws cv::Exception 其他 OpenCV 操作失败。
 * @throws Cancelled 回调请求取消。
 *
 * 不修改输入或参数。调用期间须保持它们有效且不被其他线程修改。
 * 各次调用不共享可变算法状态；复制结果结构会共享其 Mat，需要独立副本时用 clone()。
 */
MIF_EXPORT RegistrationResult registerImages(const std::vector<cv::Mat>& images,
                                             const RegistrationOptionsBase& options = NoRegistrationOptions{},
                                             const ProgressCallback& progress = {});

} // 命名空间 mif
