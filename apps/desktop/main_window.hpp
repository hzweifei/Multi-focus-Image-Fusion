#pragma once
#include <QMainWindow>
#include <QStringList>
#include <opencv2/core.hpp>
#include <memory>
class QListWidget;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QGroupBox;
namespace Ui { class MainWindow; }

namespace mif::desktop {
class ImageView;
class FusionWorker;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void addPaths(const QStringList& paths);
    const cv::Mat& resultImage() const { return result_; }
public slots:
    void startFusion();
signals:
    void fusionCompleted();
    void fusionFailed(const QString& message);
protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
private:
    void previewSelected();
    void updateControls();
    void clearResult();
    void exportResult();
    std::unique_ptr<Ui::MainWindow> ui_;
    QListWidget* files_;
    QComboBox* method_;
    QComboBox* focus_;
    QComboBox* alignment_;
    QSpinBox* window_;
    QSpinBox* levels_;
    QSpinBox* radius_;
    QGroupBox* parameters_;
    QPushButton* add_;
    QPushButton* folder_;
    QPushButton* remove_;
    QPushButton* clear_;
    QPushButton* run_;
    QPushButton* save_;
    QLabel* count_;
    QLabel* source_info_;
    QLabel* result_info_;
    QLabel* status_;
    QProgressBar* progress_;
    ImageView* source_view_;
    ImageView* result_view_;
    FusionWorker* worker_ = nullptr;
    cv::Mat result_;
    bool closing_ = false;
};
}

