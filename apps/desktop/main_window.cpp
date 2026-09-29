#include "main_window.hpp"
#include "ui_main_window.h"
#include "image_io.hpp"
#include "widgets/image_view.hpp"
#include "widgets/registration_settings.hpp"
#include "workers/fusion_worker.hpp"
#include <mif/fusion_options.hpp>
#include <mif/registration_options.hpp>
#include <QCloseEvent>
#include <QCollator>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace mif::desktop {
namespace {
// 文件选择器和拖放导入使用相同的格式范围；真正的解码检查在 readImage 中进行。
const QString imageFilter = QStringLiteral("图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");

// 先按扩展名过滤列表，避免导入目录时立即解码所有图片。
bool supported(const QString& path) {
    static const QStringList extensions{"png", "jpg", "jpeg", "bmp", "tif", "tiff"};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
// 使用 Qt 的用户级设置记录最近访问目录，首次打开时从用户主目录开始。
QString lastFolder() { return QSettings().value("lastFolder", QDir::homePath()).toString(); }
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui_(std::make_unique<Ui::MainWindow>()) {
    ui_->setupUi(this);
    setAcceptDrops(true);
    setMinimumSize(940, 670);
    createSidebar();
    createPreviewArea();
    connectActions();

    const auto geometry = QSettings().value("windowGeometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    updateControls();
}

void MainWindow::createSidebar() {
    // 左侧第一组：导入、浏览和移除图像。完整路径保存在条目的 Qt::UserRole 中。
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("sidebar");
    sidebar->setMinimumWidth(320);
    sidebar->setMaximumWidth(360);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(12, 12, 12, 12);
    side->setSpacing(10);
    count_ = new QLabel(QStringLiteral("01  /  输入图像"));
    count_->setObjectName("sectionTitle");
    side->addWidget(count_);
    auto* imports = new QHBoxLayout;
    add_ = new QPushButton(QStringLiteral("添加图片"));
    folder_ = new QPushButton(QStringLiteral("导入文件夹"));
    folder_->setToolTip(QStringLiteral("将所选文件夹作为新对焦批次，替换当前图片列表和融合结果。"));
    imports->addWidget(add_); imports->addWidget(folder_);
    side->addLayout(imports);
    files_ = new QListWidget;
    files_->setObjectName("imageList");
    files_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files_->setMinimumHeight(86);
    files_->setMaximumHeight(150);
    files_->setToolTip(QStringLiteral("第一张图片作为配准参考；单独添加文件继续追加，导入文件夹会替换当前批次。"));
    side->addWidget(files_, 1);
    auto* edits = new QHBoxLayout;
    remove_ = new QPushButton(QStringLiteral("移除选中"));
    clear_ = new QPushButton(QStringLiteral("清空"));
    edits->addWidget(remove_); edits->addWidget(clear_);
    side->addLayout(edits);

    // 参数独立分页，长表单在页内滚动；输入列表与运行按钮不会被参数挤出窗口。
    auto* settings_title = new QLabel(QStringLiteral("02  /  处理设置"));
    settings_title->setObjectName("sectionTitle");
    side->addWidget(settings_title);
    auto* tabs = new QTabWidget;
    tabs->setObjectName("parameterTabs");
    tabs->setMinimumHeight(230);
    tabs->setDocumentMode(true);
    tabs->tabBar()->setExpanding(true);
    tabs->tabBar()->setDrawBase(false);
    auto addPage = [tabs](QWidget* content, const QString& title) {
        auto* scroll = new QScrollArea;
        scroll->setObjectName("parameterScroll");
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(content);
        tabs->addTab(scroll, title);
    };
    registration_parameters_ = new RegistrationSettings;
    addPage(registration_parameters_, QStringLiteral("配准"));

    // 融合设置独立于配准选项；两组配置分别保存，只由一键处理流程串联执行。
    fusion_parameters_ = new QGroupBox;
    fusion_parameters_->setObjectName("fusionSettings");
    auto* form = new QFormLayout(fusion_parameters_);
    form->setContentsMargins(12, 14, 12, 14);
    form->setVerticalSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setAlignment(Qt::AlignTop);
    auto* fusion_note = new QLabel(QStringLiteral("合成各张图像中的清晰区域。已对齐的图片可以直接融合。"));
    fusion_note->setObjectName("parameterHint");
    fusion_note->setWordWrap(true);
    form->addRow(fusion_note);
    method_ = new QComboBox;
    method_->setObjectName("fusionMethod");
    method_->addItem(QStringLiteral("引导滤波"), static_cast<int>(FusionMethod::GuidedFilter));
    method_->addItem(QStringLiteral("拉普拉斯金字塔"), static_cast<int>(FusionMethod::LaplacianPyramid));
    focus_ = new QComboBox;
    focus_->addItem(QStringLiteral("改进拉普拉斯"), static_cast<int>(FocusMeasure::ModifiedLaplacian));
    focus_->addItem(QStringLiteral("Tenengrad 梯度"), static_cast<int>(FocusMeasure::Tenengrad));
    window_ = new QSpinBox; window_->setRange(1, 99); window_->setSingleStep(2); window_->setValue(9);
    window_->setToolTip(QStringLiteral("清晰度统计窗口，必须为奇数。增大可抑制噪声，但可能损失细小结构。"));
    radius_ = new QSpinBox; radius_->setRange(1, 64); radius_->setValue(3);
    levels_ = new QSpinBox; levels_->setRange(1, 10); levels_->setValue(5);
    QWidget* fusion_controls[] = {method_, focus_, window_, radius_, levels_};
    for (auto* control : fusion_controls)
        control->installEventFilter(this);
    form->addRow(QStringLiteral("融合方法"), method_);
    form->addRow(QStringLiteral("清晰度"), focus_);
    form->addRow(QStringLiteral("统计窗口"), window_);
    form->addRow(QStringLiteral("细节半径"), radius_);
    form->addRow(QStringLiteral("金字塔层数"), levels_);
    levels_->setToolTip(QStringLiteral("仅拉普拉斯金字塔融合使用；实际层数还受图像尺寸限制。"));
    auto* reset_fusion = new QPushButton(QStringLiteral("恢复融合默认值"));
    reset_fusion->setObjectName("resetFusionOptions");
    form->addRow(reset_fusion);
    connect(reset_fusion, &QPushButton::clicked, this, [this] {
        const FusionOptions defaults;
        method_->setCurrentIndex(method_->findData(static_cast<int>(defaults.method)));
        focus_->setCurrentIndex(focus_->findData(static_cast<int>(defaults.focus_measure)));
        window_->setValue(defaults.focus_window);
        radius_->setValue(defaults.detail_radius);
        levels_->setValue(defaults.pyramid_levels);
    });
    addPage(fusion_parameters_, QStringLiteral("融合"));
    side->addWidget(tabs, 4);
    run_ = new QPushButton(QStringLiteral("开始融合"));
    run_->setObjectName("primaryButton"); run_->setMinimumHeight(44);
    side->addWidget(run_);
    ui_->workspaceLayout->addWidget(sidebar);
}

void MainWindow::createPreviewArea() {
    // 右侧工具栏和并排预览区。两个预览都只显示 8 位副本，导出仍使用原始结果。
    auto* workspace = new QVBoxLayout;
    auto* toolbar = new QHBoxLayout;
    auto* heading = new QLabel(QStringLiteral("03  /  图像对比"));
    heading->setObjectName("sectionTitle");
    toolbar->addWidget(heading);
    auto* linked = new QLabel(QStringLiteral("视野联动"));
    linked->setObjectName("linkedBadge");
    linked->setToolTip(QStringLiteral("两个预览同步缩放和平移，方便对比相同位置的细节。"));
    toolbar->addWidget(linked);
    toolbar->addStretch();
    auto* fit = new QPushButton(QStringLiteral("适应窗口"));
    fit->setObjectName("fitPreviews");
    save_ = new QPushButton(QStringLiteral("导出结果"));
    save_->setObjectName("saveButton");
    toolbar->addWidget(fit); toolbar->addWidget(save_);
    workspace->addLayout(toolbar);
    auto* previews = new QSplitter(Qt::Horizontal);
    // 两个面板结构相同，通过一个局部构造函数保持布局一致。
    auto addPreview = [previews](const QString& title, ImageView*& view, QLabel*& info) {
        auto* panel = new QWidget;
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(title); label->setObjectName("previewTitle");
        layout->addWidget(label);
        view = new ImageView; layout->addWidget(view, 1);
        info = new QLabel(QStringLiteral("等待图片")); info->setObjectName("muted"); info->setWordWrap(true);
        // 两侧都预留名称与属性两行，避免说明行数不同把并排预览上下错开。
        info->setMinimumHeight(info->fontMetrics().lineSpacing() * 2);
        layout->addWidget(info);
        previews->addWidget(panel);
    };
    addPreview(QStringLiteral("输入图像"), source_view_, source_info_);
    addPreview(QStringLiteral("全聚焦结果"), result_view_, result_info_);
    source_view_->setObjectName("sourcePreview");
    result_view_->setObjectName("resultPreview");
    previews->setChildrenCollapsible(false);
    workspace->addWidget(previews, 1);
    auto* tip = new QLabel(QStringLiteral("滚轮缩放  ·  拖动平移  ·  双击适应窗口"));
    tip->setObjectName("muted"); workspace->addWidget(tip);
    status_ = new QLabel(QStringLiteral("添加至少两张不同焦点的图片，即可开始。"));
    status_->setWordWrap(true); workspace->addWidget(status_);
    progress_ = new QProgressBar; progress_->setRange(0, 100); progress_->setValue(0);
    progress_->setFixedHeight(16); workspace->addWidget(progress_);
    ui_->workspaceLayout->addLayout(workspace, 1);

    // 适应窗口按钮只与本区域有关，无需保存为窗口成员。
    connect(fit, &QPushButton::clicked, source_view_, &ImageView::fitImage);
}

void MainWindow::connectActions() {
    // 传递相对倍率和图像中的相对中心，兼容面板大小不同和配准后结果裁剪。
    // 接收端只应用状态、不再发送导航信号，因此双向连接不会互相回调。
    connect(source_view_, &ImageView::viewChanged, result_view_, &ImageView::applyViewState);
    connect(result_view_, &ImageView::viewChanged, source_view_, &ImageView::applyViewState);
    // 所有导入方式复用 addPaths，统一完成过滤、排序、去重和结果失效处理。
    connect(add_, &QPushButton::clicked, this, [this] {
        addPaths(QFileDialog::getOpenFileNames(this, QStringLiteral("选择不同焦点的图片"), lastFolder(), imageFilter));
    });
    connect(folder_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("导入图像文件夹"), lastFolder());
        if (!path.isEmpty()) addPaths({path});
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        qDeleteAll(files_->selectedItems()); clearResult(); updateControls();
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        files_->clear(); source_view_->setImage({}); source_info_->setText(QStringLiteral("等待图片"));
        clearResult(); updateControls();
    });
    connect(files_, &QListWidget::currentRowChanged, this, [this] { previewSelected(); });
    connect(files_, &QListWidget::itemSelectionChanged, this, &MainWindow::updateControls);
    connect(method_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateControls);
    connect(run_, &QPushButton::clicked, this, &MainWindow::startFusion);
    connect(save_, &QPushButton::clicked, this, &MainWindow::exportResult);
}

MainWindow::~MainWindow() {
    // 常规关闭由 closeEvent 异步等待；这里保证程序直接析构窗口时也能安全退出。
    if (worker_) { worker_->requestInterruption(); worker_->wait(); }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        // 保持 ignored，使外层滚动区继续接收滚轮；数值仍可键入或通过箭头调整。
        event->ignore();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::addPaths(const QStringList& paths) {
    if (worker_ || paths.isEmpty()) return;
    // 文件夹仅导入当前层可读图片，避免隐式递归收集其他场景。
    QStringList expanded;
    QString batch_folder;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            // 同次拖入多个目录或混合文件时，整体作为一个新批次，只清空一次。
            if (batch_folder.isEmpty()) batch_folder = info.absoluteFilePath();
            for (const auto& file : QDir(path).entryInfoList(QDir::Files | QDir::Readable))
                if (supported(file.filePath())) expanded.push_back(file.absoluteFilePath());
        } else if (info.isFile() && supported(path)) expanded.push_back(info.absoluteFilePath());
    }
    // 自然排序使 focus_2 排在 focus_10 前；第一张图片将作为配准参考。
    QCollator collator(QLocale::English); collator.setNumericMode(true);
    std::sort(expanded.begin(), expanded.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(a, b) < 0;
    });
    const bool replace_batch = !batch_folder.isEmpty();
    const bool refresh_preview = replace_batch || !files_->currentItem();
    int added = 0;
    {
        // 批量更新时不预览中间条目；列表、结果和选中图片在导入结束后一起更新。
        const QSignalBlocker blocker(files_);
        if (replace_batch) files_->clear();
        // 单独添加文件时保留原列表顺序；绝对路径用于识别新旧条目中的重复文件。
        for (const auto& path : expanded) {
            bool exists = false;
            for (int i = 0; i < files_->count(); ++i)
                if (files_->item(i)->data(Qt::UserRole).toString() == path) { exists = true; break; }
            if (exists) continue;
            auto* item = new QListWidgetItem(QFileInfo(path).fileName(), files_);
            item->setData(Qt::UserRole, path); item->setToolTip(path); ++added;
        }
        if (refresh_preview && files_->count() > 0) files_->setCurrentRow(0);
    }
    if (added || replace_batch) {
        clearResult();
        if (refresh_preview) previewSelected();
        if (replace_batch) {
            // 新批次同时恢复两侧整图视野，避免沿用上一批次的局部放大位置。
            source_view_->fitImage();
            QSettings().setValue("lastFolder", batch_folder);
            status_->setText(added > 0
                ? QStringLiteral("已切换到新批次，共 %1 张图片。第一张将作为配准参考。未读取的文件将在融合时检查。").arg(added)
                : QStringLiteral("所选文件夹中没有受支持的图片，已清空上一批次。"));
        } else {
            QSettings().setValue("lastFolder", QFileInfo(expanded.front()).absolutePath());
            status_->setText(QStringLiteral("已添加 %1 张图片。第一张将作为配准参考。未读取的文件将在融合时检查。").arg(added));
        }
    } else status_->setText(QStringLiteral("没有新的受支持图片可导入。"));
    updateControls();
}

void MainWindow::previewSelected() {
    const auto* item = files_->currentItem();
    if (!item) { source_view_->setImage({}); source_info_->setText(QStringLiteral("等待图片")); return; }
    try {
        // 显示副本拥有独立像素内存，因此离开此作用域后仍可安全显示。
        const auto image = readImage(item->data(Qt::UserRole).toString());
        // 切换同批次的输入图片时保留对比位置，便于逐张观察同一局部的清晰度。
        source_view_->setImage(previewImage(image), true);
        source_info_->setText(QStringLiteral("%1\n%2 × %3 · %4 位 · %5 通道")
            .arg(item->text()).arg(image.cols).arg(image.rows).arg(image.elemSize1() * 8).arg(image.channels()));
    } catch (const std::exception& error) {
        // 单张图片读取失败时保留当前对比视野，下一张可读图片仍回到相同局部。
        source_view_->setImage({}, true); source_info_->setText(QString::fromUtf8(error.what()));
    }
}

void MainWindow::clearResult() {
    // 输入发生改变时不能继续导出旧结果，以免误认其来自当前图像栈。
    result_.release(); result_view_->setImage({});
    result_info_->setText(QStringLiteral("完成融合后在此显示")); progress_->setValue(0);
}

void MainWindow::updateControls() {
    // 以工作线程是否仍存在作为唯一的忙闲依据，任务取消期间仍保持输入锁定。
    const bool busy = worker_ != nullptr;
    add_->setEnabled(!busy); folder_->setEnabled(!busy);
    remove_->setEnabled(!busy && !files_->selectedItems().isEmpty());
    clear_->setEnabled(!busy && files_->count() > 0);
    registration_parameters_->setEnabled(!busy);
    fusion_parameters_->setEnabled(!busy);
    levels_->setEnabled(method_->currentData().toInt() == static_cast<int>(FusionMethod::LaplacianPyramid));
    run_->setEnabled(busy || files_->count() >= 2);
    run_->setText(busy ? QStringLiteral("取消处理") : QStringLiteral("开始融合"));
    save_->setEnabled(!busy && !result_.empty());
    count_->setText(QStringLiteral("01  /  输入图像 · %1 张").arg(files_->count()));
}

void MainWindow::startFusion() {
    if (worker_) {
        // Qt 中断是请求标记；核心算法在阶段回调中检查，不强行终止 OpenCV 调用。
        worker_->requestInterruption(); run_->setEnabled(false);
        status_->setText(QStringLiteral("正在取消，将在当前处理步骤结束后停止…")); return;
    }
    if (files_->count() < 2) return;
    if (window_->value() % 2 == 0) {
        status_->setText(QStringLiteral("统计窗口必须为奇数，例如 7、9 或 11。")); return;
    }
    // 配准和融合分别建立参数快照，后台线程只接收值，不读取界面控件。
    const RegistrationOptions registration_options = registration_parameters_->options();
    FusionOptions fusion_options;
    // 从条目数据读取枚举，不将下拉框位置当作模式值；新增或重排选项不会改变含义。
    fusion_options.method = static_cast<FusionMethod>(method_->currentData().toInt());
    fusion_options.focus_measure = static_cast<FocusMeasure>(focus_->currentData().toInt());
    fusion_options.focus_window = window_->value(); fusion_options.detail_radius = radius_->value();
    fusion_options.pyramid_levels = levels_->value();
    QStringList paths;
    for (int i = 0; i < files_->count(); ++i) paths.push_back(files_->item(i)->data(Qt::UserRole).toString());
    clearResult();
    worker_ = new FusionWorker(paths, registration_options, fusion_options, this);
    // 信号从工作线程发出，Qt 将下面的接收回调排入界面线程执行。
    connect(worker_, &FusionWorker::progress, this, [this](int value, const QString& stage) {
        progress_->setValue(value);
        if (!worker_->isInterruptionRequested()) status_->setText(stage);
    });
    connect(worker_, &FusionWorker::completed, this, [this](const cv::Mat& image, const QString& summary) {
        // cv::Mat 的引用计数保留结果像素；工作线程此后不再修改该缓冲区。
        result_ = image;
        result_view_->setImage(previewImage(result_), true);
        // 计算期间用户仍可浏览源图；新结果沿用此刻的对比视野。
        result_view_->applyViewState(source_view_->viewState());
        result_info_->setText(QStringLiteral("融合结果\n%1").arg(summary));
        status_->setText(QStringLiteral("融合完成，可导出结果。自动配准开启时，结果已裁剪为共有区域。"));
        emit fusionCompleted();
    });
    connect(worker_, &FusionWorker::failed, this, [this](const QString& message) {
        status_->setText(QStringLiteral("处理失败：") + message); emit fusionFailed(message);
    });
    connect(worker_, &FusionWorker::cancelled, this, [this] { status_->setText(QStringLiteral("已取消处理。")); progress_->setValue(0); });
    connect(worker_, &QThread::finished, this, [this] {
        // 必须等 run 完全退出后再销毁线程对象；普通完成、失败和取消共用此清理路径。
        worker_->deleteLater(); worker_ = nullptr; updateControls();
        if (closing_) close();
    });
    updateControls(); worker_->start();
}

void MainWindow::exportResult() {
    if (result_.empty()) return;
    // 根据位深限制格式：16 位不允许 JPEG，浮点结果使用 TIFF 保留数据精度。
    const bool floating = result_.depth() == CV_32F;
    const auto filter = floating ? QStringLiteral("TIFF (*.tif *.tiff)") :
        result_.depth() == CV_16U ? QStringLiteral("PNG (*.png);;TIFF (*.tif *.tiff)") :
        QStringLiteral("PNG (*.png);;TIFF (*.tif *.tiff);;JPEG (*.jpg *.jpeg)");
    QString selected;
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出融合结果"),
        QDir(lastFolder()).filePath(floating ? "fused.tif" : "fused.png"), filter, &selected);
    if (path.isEmpty()) return;
    // 未输入扩展名时按当前选中的格式补齐，实际编码和检查由 writeImage 完成。
    if (QFileInfo(path).suffix().isEmpty())
        path += selected.startsWith("TIFF") ? ".tif" : selected.startsWith("JPEG") ? ".jpg" : ".png";
    try {
        writeImage(path, result_); status_->setText(QStringLiteral("已保存：") + path);
        QSettings().setValue("lastFolder", QFileInfo(path).absolutePath());
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (worker_) {
        // 暂缓关闭让界面事件循环继续工作；finished 回调会重新触发 close。
        closing_ = true; worker_->requestInterruption(); event->ignore();
        status_->setText(QStringLiteral("正在停止处理，完成后关闭窗口…")); return;
    }
    QSettings().setValue("windowGeometry", saveGeometry()); event->accept();
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!worker_ && event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event) {
    // 忽略网页等非本地 URL，保持导入行为与文件选择器一致。
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) if (url.isLocalFile()) paths.push_back(url.toLocalFile());
    addPaths(paths); event->acceptProposedAction();
}
} // namespace mif::desktop

