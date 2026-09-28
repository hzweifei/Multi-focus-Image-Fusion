#pragma once
#include <QString>
#include <QImage>
#include <opencv2/core.hpp>
namespace mif::desktop {
cv::Mat readImage(const QString& path);
void writeImage(const QString& path, const cv::Mat& image);
QImage previewImage(const cv::Mat& image);
}

