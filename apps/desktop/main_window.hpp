#pragma once

#include <QMainWindow>
#include <QStringList>
#include <opencv2/core.hpp>
#include <memory>

// 前置声明可以减少公开此窗口类时需要包含的 Qt 头文件。
class QListWidget;
class QPushButton;
class QLabel;
class QProgressBar;
namespace Ui { class MainWindow; }

namespace mif::desktop {
class ImageView;
class FusionWorker;
class RegistrationSettings;
class FusionSettings;

/// 桌面程序主窗口：管理输入、参数和预览，耗时的读取与融合交给后台线程。
/// 所有控件和 result_ 只由界面线程访问，窗口拥有 Qt 子控件的生命周期。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /// 创建界面并恢复上次保存的窗口位置与大小。
    explicit MainWindow(QWidget* parent = nullptr);
    /// 销毁前确保后台线程已退出，避免线程继续访问已释放的窗口数据。
    ~MainWindow() override;
    /// 文件追加到当前列表；包含文件夹时，以本次导入整体替换旧批次。
    /// 文件夹仅展开一层，统一自然排序并去重；空目录也替换，取消选择不改变当前批次。
    /// 运行中不接受新输入。
    void addPaths(const QStringList& paths);
    /// 返回保留原始位深的融合结果；引用仅在窗口存活且结果未清空时有效。
    const cv::Mat& resultImage() const { return result_; }

public slots:
    /// 空闲时启动融合；已有任务时请求协作取消，在下一个检查点退出。
    void startFusion();

signals:
    /// 融合结果已在界面线程接收；后台线程对象可能尚未触发 finished。
    void fusionCompleted();
    /// 后台读取或融合失败，message 可直接用于界面提示。
    void fusionFailed(const QString& message);

protected:
    /// 正在处理时暂缓关闭，等工作线程结束后再关闭并保存窗口布局。
    void closeEvent(QCloseEvent* event) override;
    /// 空闲时允许拖入包含文件路径的内容。
    void dragEnterEvent(QDragEnterEvent* event) override;
    /// 只提取本地文件和文件夹路径，交给统一的导入入口处理。
    void dropEvent(QDropEvent* event) override;

private:
    /// 创建左侧的图像列表、独立配准/融合设置和统一运行按钮。
    void createSidebar();
    /// 创建右侧的输入/结果预览、导出按钮和进度显示。
    void createPreviewArea();
    /// 连接输入、参数、运行与保存操作，集中描述窗口交互。
    void connectActions();
    /// 按需读取当前条目并生成显示副本；读取失败只影响此条目的预览。
    void previewSelected();
    /// 根据运行状态、选中条目和结果是否存在统一更新控件可用性。
    void updateControls();
    /// 输入列表变化或开始新任务时释放旧结果，并重置结果区和进度。
    void clearResult();
    /// 按结果位深选择导出格式，并保存原始结果数据。
    void exportResult();

    // Designer 文件只提供窗口外壳；其余控件由两个创建函数填入 workspaceLayout。
    std::unique_ptr<Ui::MainWindow> ui_;

    // 输入列表与两组独立配置。具体字段和方法切换由各自的参数控件管理。
    QListWidget* files_;
    // 两组参数独立组织，运行期间一起锁定，防止界面显示的配置与任务快照不一致。
    RegistrationSettings* registration_parameters_;
    FusionSettings* fusion_parameters_;

    // 输入管理及任务操作按钮，由 Qt 父子对象关系统一释放。
    QPushButton* add_;
    QPushButton* folder_;
    QPushButton* remove_;
    QPushButton* clear_;
    QPushButton* run_;
    QPushButton* save_;

    // 文本状态、总进度和两个可缩放的图像预览。
    QLabel* count_;
    QLabel* source_info_;
    QLabel* result_info_;
    QLabel* status_;
    QProgressBar* progress_;
    ImageView* source_view_;
    ImageView* result_view_;

    // 非空表示任务尚未完全结束；finished 后 deleteLater 并置空。
    FusionWorker* worker_ = nullptr;
    // 持有融合结果的 cv::Mat 引用计数，预览降位深不会修改这份原始数据。
    cv::Mat result_;
    // 用户在运行中关闭窗口时置位，线程结束后再次触发关闭。
    bool closing_ = false;
};
} // namespace mif::desktop

