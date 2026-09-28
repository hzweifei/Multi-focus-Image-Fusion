#pragma once

#include <mif/options.hpp>
#include <mif/export.hpp>
#include <opencv2/core.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mif {

/// 进度回调返回 false 时抛出，调用者可将用户取消与处理失败分开处理。
class Cancelled : public std::runtime_error {
public:
    /// 构造统一的取消异常；异常消息保留英文，便于跨语言调用时保持一致。
    Cancelled() : std::runtime_error("Fusion cancelled") {}
};

/// 在调用 fuse() 的线程内同步执行的进度回调，不会自动切换到界面线程。
/// percent 为 [0, 100] 的阶段进度；stage 为 prepare、align、focus、weights、
/// blend、pyramid、finish 或 done 等阶段标识，调用方可自行映射为显示文字。
/// 返回 false 会在本次回调后抛出 Cancelled；取消检查只发生在进度报告点，
/// 不会中断正在执行的单次 OpenCV 操作。回调抛出的异常会继续向调用方传播。
using ProgressCallback = std::function<bool(int percent, const std::string& stage)>;

/// 融合结果。图像数据不引用输入缓冲区，由 cv::Mat 引用计数管理生命周期。
/// 复制此结构时 cv::Mat 默认共享结果数据；需要独立可写副本时请调用 clone()。
struct FusionResult {
    /// 融合图像，深度和通道数与输入一致；尺寸等于 crop.size()。
    cv::Mat image;
    /// CV_32SC1 索引图，与 image 同尺寸，记录各像素细节权重最大的输入序号。
    /// 序号从 0 开始；权重并列时取较小序号。该图用于观察来源，不代表置信度。
    cv::Mat focus_indices;
    /// 可选的 CV_32FC1 细节权重图，顺序与输入相同，与 image 同尺寸。
    /// 每个像素处各图权重非负且总和约为 1；keep_weight_maps 为 false 时为空。
    std::vector<cv::Mat> weights;
    /// 输出区域在第一张原始输入图像中的坐标；未配准时为整幅图像。
    cv::Rect crop;
    /// 每张输入对应一个 CV_32F 矩阵，将参考图坐标映射到该输入坐标。
    /// None/Translation/Affine 保留 2×3 格式；FeatureHomography/EccHomography 为 3×3。
    /// 矩阵使用裁剪前的原始像素坐标；第一张图及未配准的图像均为单位变换。
    /// 将输出像素映射回输入时，应先加上 crop 的左上角偏移，再应用此矩阵。
    /// 对 3×3 矩阵，应将 [x, y, 1] 相乘后用前两项除以第三项，得到源图像素坐标。
    std::vector<cv::Mat> transforms;
};

/**
 * @brief 对两张及以上不同焦点的图像进行融合，不使用 AI 模型。
 * @param images 尺寸和类型完全相同的二维图像，各边至少 2 像素，数量须能用 int 表示。
 *               支持 CV_8U、CV_16U、CV_32F，以及单通道灰度或三通道 BGR。
 *               浮点输入必须全部为 [0, 1] 内的有限值；允许非连续内存的 ROI。
 * @param options 算法、滤波和配准参数，范围见 FusionOptions。
 * @param progress 可选的同步进度回调；空回调表示不报告进度、不请求取消。
 * @return 融合图、来源索引及配准信息；配准后仅保留所有输入均有效的最大轴对齐矩形。
 * @throws std::invalid_argument 输入或参数无效，或配准缩小后的图像不足 16×16。
 * @throws std::runtime_error 配准无法收敛、纹理/匹配不足、变换退化或公共区域不足 8×8。
 * @throws cv::Exception OpenCV 处理失败；配准求解异常会附加图像序号后转为 runtime_error。
 * @throws Cancelled 进度回调请求取消。
 *
 * 本函数同步执行，不修改输入。调用期间应保持输入缓冲区有效，且不要从其他线程修改。
 * 各次调用不共享可变算法状态，可在不同线程独立使用；回调及结果的跨线程访问由调用者管理。
 */
MIF_EXPORT FusionResult fuse(const std::vector<cv::Mat>& images,
                  const FusionOptions& options = {},
                  const ProgressCallback& progress = {});

} // 命名空间 mif

