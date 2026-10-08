#pragma once

#include <QThread>
#include <QStringList>
#include <QMetaType>
#include <mif/fusion/options_base.hpp>
#include <mif/registration/options_base.hpp>
#include <memory>
#include <opencv2/core.hpp>

// 允许 Qt 排队连接复制 cv::Mat 的引用计数对象，跨线程传递融合结果。
Q_DECLARE_METATYPE(cv::Mat)

namespace mif::desktop {
/// 一次桌面处理任务的后台线程：读取输入、执行可选配准与融合、通过信号汇报状态。
/// run 在后台执行，线程对象本身由界面线程管理；该类不直接访问任何界面控件。
class FusionWorker : public QThread {
    Q_OBJECT
public:
    /// 保存输入路径及两份独立参数快照；父对象通常是主窗口。
    FusionWorker(QStringList paths, const RegistrationOptionsBase& registration_options,
                 const FusionOptionsBase& fusion_options, QObject* parent = nullptr);
signals:
    /// 总进度 [0, 100] 和可直接显示的中文阶段名称。
    void progress(int value, const QString& stage);
    /// 成功结果保持核心算法的位深；信号发送后工作线程不再修改图像内存。
    void completed(const cv::Mat& image, const QString& summary);
    /// 读取或算法异常转换为可读消息，由界面线程决定如何提示用户。
    void failed(const QString& message);
    /// 已响应中断请求并退出计算，与失败分别显示。
    void cancelled();
protected:
    /// 读取占总进度的前 15%，配准与融合管线占后 85%；在读取间隙和阶段回调检查取消。
    void run() override;
private:
    // 顺序在任务启动时固定，第一张图片作为可选配准流程的参考。
    QStringList paths_;
    // 在构造时按真实类型克隆；配置及其所有权随后保持只读，不依赖调用方的生命周期。
    const std::unique_ptr<const RegistrationOptionsBase> registration_options_;
    const std::unique_ptr<const FusionOptionsBase> fusion_options_;
};
} // namespace mif::desktop

