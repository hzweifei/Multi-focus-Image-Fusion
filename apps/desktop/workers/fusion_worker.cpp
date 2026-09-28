#include "fusion_worker.hpp"
#include "image_io.hpp"
#include <QElapsedTimer>
#include <map>

namespace mif::desktop {
FusionWorker::FusionWorker(QStringList paths, FusionOptions options, QObject* parent)
    : QThread(parent), paths_(std::move(paths)), options_(options) {
    qRegisterMetaType<cv::Mat>("cv::Mat");
}
void FusionWorker::run() {
    try {
        QElapsedTimer timer;
        timer.start();
        std::vector<cv::Mat> images;
        for (int i = 0; i < paths_.size(); ++i) {
            if (isInterruptionRequested()) throw Cancelled();
            emit progress(15 * i / paths_.size(), QStringLiteral("读取图片 %1 / %2").arg(i + 1).arg(paths_.size()));
            images.push_back(readImage(paths_[i]));
        }
        const auto result = fuse(images, options_, [this](int percent, const std::string& stage) {
            static const std::map<std::string, QString> names{
                {"prepare", QStringLiteral("准备图像")}, {"align", QStringLiteral("对齐图像")},
                {"focus", QStringLiteral("计算清晰度")}, {"weights", QStringLiteral("优化融合权重")},
                {"blend", QStringLiteral("融合图像")}, {"pyramid", QStringLiteral("重建金字塔")},
                {"finish", QStringLiteral("生成结果")}, {"done", QStringLiteral("处理完成")}};
            const auto it = names.find(stage);
            emit progress(15 + percent * 85 / 100, it == names.end() ? QString::fromStdString(stage) : it->second);
            return !isInterruptionRequested();
        });
        if (isInterruptionRequested()) throw Cancelled();
        emit completed(result.image, QStringLiteral("%1 × %2 · %3 位 · %4 张图片 · %5 秒")
            .arg(result.image.cols).arg(result.image.rows).arg(result.image.elemSize1() * 8)
            .arg(paths_.size()).arg(timer.elapsed() / 1000.0, 0, 'f', 2));
    } catch (const Cancelled&) {
        emit cancelled();
    } catch (const std::exception& error) {
        emit failed(QString::fromUtf8(error.what()));
    }
}
}

