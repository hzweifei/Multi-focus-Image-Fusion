#pragma once
#include <QThread>
#include <QStringList>
#include <mif/fusion.hpp>
Q_DECLARE_METATYPE(cv::Mat)

namespace mif::desktop {
class FusionWorker : public QThread {
    Q_OBJECT
public:
    FusionWorker(QStringList paths, FusionOptions options, QObject* parent = nullptr);
signals:
    void progress(int value, const QString& stage);
    void completed(const cv::Mat& image, const QString& summary);
    void failed(const QString& message);
    void cancelled();
protected:
    void run() override;
private:
    QStringList paths_;
    FusionOptions options_;
};
}

