#pragma once

#include <QString>
#include <QImage>
#include <opencv2/core.hpp>

namespace mif::desktop {
/// 从支持中文等 Unicode 字符的路径读取图像，保持原始位深和 OpenCV 的 BGR 顺序。
/// 支持 1/3 通道、8/16 位无符号整数或 32 位浮点；读取和格式错误抛出异常。
/// 浮点输入的有限性及 [0, 1] 范围由核心算法在融合前验证。
cv::Mat readImage(const QString& path);

/// 按扩展名编码并原子保存图像，失败时抛出异常，避免留下半写入的目标文件。
/// 16 位只允许 PNG/TIFF，浮点只允许 TIFF；8 位可额外使用有损的 JPEG。
void writeImage(const QString& path, const cv::Mat& image);

/// 将内部图像转换成持有独立内存的 8 位 QImage，仅供显示，不修改原始图像。
/// 16 位按完整范围缩放，浮点按 [0, 1] 映射；3 通道从 BGR 转为 Qt 的 RGB 顺序。
QImage previewImage(const cv::Mat& image);
} // namespace mif::desktop

