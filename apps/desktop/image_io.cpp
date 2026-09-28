#include "image_io.hpp"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace mif::desktop {
cv::Mat readImage(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error((QStringLiteral("无法读取：") + path).toStdString());
    const auto bytes = file.readAll();
    if (bytes.isEmpty()) throw std::runtime_error("图片文件为空");
    const std::vector<uchar> buffer(bytes.begin(), bytes.end());
    cv::Mat image = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
    if (image.empty()) throw std::runtime_error((QStringLiteral("无法解码图片：") + path).toStdString());
    if (image.channels() != 1 && image.channels() != 3)
        throw std::runtime_error("请选择灰度或三通道图片；暂不支持含透明通道的图片");
    if (image.depth() != CV_8U && image.depth() != CV_16U && image.depth() != CV_32F)
        throw std::runtime_error("图片必须为 8 位、16 位无符号整数或 32 位浮点格式");
    return image;
}

void writeImage(const QString& path, const cv::Mat& image) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "png" && suffix != "tif" && suffix != "tiff" && suffix != "jpg" && suffix != "jpeg")
        throw std::runtime_error("支持导出 PNG、TIFF 或 JPEG");
    if (image.depth() == CV_16U && suffix != "png" && suffix != "tif" && suffix != "tiff")
        throw std::runtime_error("16 位结果请使用 PNG 或 TIFF 保存，以保留精度");
    if (image.depth() == CV_32F && suffix != "tif" && suffix != "tiff")
        throw std::runtime_error("浮点结果请使用 TIFF 保存，以保留精度");
    std::vector<uchar> bytes;
    // Disable TIFF's default float-color LogLuv encoding, which is lossy.
    const std::vector<int> params = (suffix == "tif" || suffix == "tiff")
        ? std::vector<int>{cv::IMWRITE_TIFF_COMPRESSION, 1} : std::vector<int>{};
    if (!cv::imencode("." + suffix.toStdString(), image, bytes, params))
        throw std::runtime_error("图片编码失败");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) ||
        !file.commit())
        throw std::runtime_error((QStringLiteral("无法保存：") + path).toStdString());
}

QImage previewImage(const cv::Mat& image) {
    if (image.empty()) return {};
    cv::Mat display;
    const double scale = image.depth() == CV_16U ? 1.0 / 257.0 : image.depth() == CV_32F ? 255.0 : 1.0;
    image.convertTo(display, CV_8U, scale);
    if (display.channels() == 1)
        return QImage(display.data, display.cols, display.rows, static_cast<int>(display.step), QImage::Format_Grayscale8).copy();
    cv::cvtColor(display, display, cv::COLOR_BGR2RGB);
    return QImage(display.data, display.cols, display.rows, static_cast<int>(display.step), QImage::Format_RGB888).copy();
}
}

