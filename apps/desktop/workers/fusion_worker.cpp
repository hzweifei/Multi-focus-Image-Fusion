#include "fusion_worker.hpp"
#include "image_io.hpp"
#include <QElapsedTimer>
#include <map>

namespace mif::desktop {
FusionWorker::FusionWorker(QStringList paths, FusionOptions options, QObject* parent)
    : QThread(parent), paths_(std::move(paths)), options_(options) {
    // 排队信号复制的是 Mat 头和共享内存的引用计数，像素内存会保持到接收方释放。
    qRegisterMetaType<cv::Mat>("cv::Mat");
}

void FusionWorker::run() {
    try {
        // 总耗时包含磁盘读取、配准、清晰度分析和融合。
        QElapsedTimer timer;
        timer.start();
        std::vector<cv::Mat> images;
        for (int i = 0; i < paths_.size(); ++i) {
            // 取消是协作式的；正在进行的单次图片解码结束后才会进入下一检查点。
            if (isInterruptionRequested()) throw Cancelled();
            emit progress(15 * i / paths_.size(), QStringLiteral("读取图片 %1 / %2").arg(i + 1).arg(paths_.size()));
            images.push_back(readImage(paths_[i]));
        }
        // 回调仍在工作线程执行，只发信号并返回是否继续，不操作 GUI 对象。
        const auto result = fuse(images, options_, [this](int percent, const std::string& stage) {
            // 核心库用稳定的阶段标识，中文显示文案只放在界面层。
            static const std::map<std::string, QString> names{
                {"prepare", QStringLiteral("准备图像")}, {"align", QStringLiteral("对齐图像")},
                {"focus", QStringLiteral("计算清晰度")}, {"weights", QStringLiteral("优化融合权重")},
                {"blend", QStringLiteral("融合图像")}, {"pyramid", QStringLiteral("重建金字塔")},
                {"finish", QStringLiteral("生成结果")}, {"done", QStringLiteral("处理完成")}};
            const auto it = names.find(stage);
            emit progress(15 + percent * 85 / 100, it == names.end() ? QString::fromStdString(stage) : it->second);
            return !isInterruptionRequested();
        });
        // 即使取消恰好发生在最后一个算法回调之后，也避免发布已取消的任务结果。
        if (isInterruptionRequested()) throw Cancelled();
        emit completed(result.image, QStringLiteral("%1 × %2 · %3 位 · %4 张图片 · %5 秒")
            .arg(result.image.cols).arg(result.image.rows).arg(result.image.elemSize1() * 8)
            .arg(paths_.size()).arg(timer.elapsed() / 1000.0, 0, 'f', 2));
    } catch (const Cancelled&) {
        // 分开处理取消与错误，保证用户取消时不会看到失败提示。
        emit cancelled();
    } catch (const std::exception& error) {
        // 异常不能逸出线程入口，统一转换为界面可接收的失败信号。
        emit failed(QString::fromUtf8(error.what()));
    }
}
} // namespace mif::desktop

