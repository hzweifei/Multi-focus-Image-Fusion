#include "fixtures.hpp"
#include "main_window.hpp"
#include "image_io.hpp"
#include "widgets/image_view.hpp"
#include "widgets/registration_settings.hpp"
#include "workers/fusion_worker.hpp"
#include <mif/fusion_options.hpp>
#include <mif/pipeline.hpp>
#include <mif/registration_options.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <cmath>
#include <functional>
#include <iostream>

namespace {
// 派发布局请求，确保切换参数页及模式后的可见性、滚动范围已更新。
void flushLayout() {
    for (int i = 0; i < 3; ++i) {
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::processEvents();
    }
}

// 用普通 Qt 基类查找无 Q_OBJECT 的参数组件，再通过 C++ RTTI 检查真实类型。
// 控件值和最终算法输出分别验证，避免只检查界面显示而遗漏任务参数快照。
struct RegistrationControls {
    mif::desktop::RegistrationSettings* settings;
    QComboBox* method;
    QComboBox* motion_model;
    QSpinBox* max_size;
    QSpinBox* iterations;
    QDoubleSpinBox* epsilon;
    QSpinBox* max_features;
    QDoubleSpinBox* match_ratio;
    QDoubleSpinBox* ransac_threshold;
    QDoubleSpinBox* min_inlier_ratio;
    QPushButton* reset;

    explicit RegistrationControls(mif::desktop::MainWindow& window)
        : settings(dynamic_cast<mif::desktop::RegistrationSettings*>(window.findChild<QGroupBox*>("registrationSettings"))),
          method(window.findChild<QComboBox*>("registrationMethod")),
          motion_model(window.findChild<QComboBox*>("registrationMotionModel")),
          max_size(window.findChild<QSpinBox*>("registrationMaxSize")),
          iterations(window.findChild<QSpinBox*>("registrationIterations")),
          epsilon(window.findChild<QDoubleSpinBox*>("registrationEpsilon")),
          max_features(window.findChild<QSpinBox*>("registrationMaxFeatures")),
          match_ratio(window.findChild<QDoubleSpinBox*>("registrationMatchRatio")),
          ransac_threshold(window.findChild<QDoubleSpinBox*>("registrationRansacThreshold")),
          min_inlier_ratio(window.findChild<QDoubleSpinBox*>("registrationMinInlierRatio")),
          reset(window.findChild<QPushButton*>("resetRegistrationOptions")) {
        require(settings && method && motion_model && max_size && iterations && epsilon && max_features &&
                match_ratio && ransac_threshold && min_inlier_ratio && reset,
                "Registration parameter controls are missing");
    }

    void selectMethod(mif::RegistrationMethod value) const {
        const int index = method->findData(static_cast<int>(value));
        require(index >= 0, "Registration method item data is missing");
        method->setCurrentIndex(index);
        flushLayout();
    }

    void selectMotionModel(mif::MotionModel value) const {
        const int index = motion_model->findData(static_cast<int>(value));
        require(index >= 0, "Motion model item data is missing");
        motion_model->setCurrentIndex(index);
        flushLayout();
    }

    void setValues(const mif::RegistrationOptions& options) const {
        selectMotionModel(options.motion_model);
        max_size->setValue(options.max_size);
        iterations->setValue(options.iterations);
        epsilon->setValue(options.epsilon);
        max_features->setValue(options.max_features);
        match_ratio->setValue(options.match_ratio);
        ransac_threshold->setValue(options.ransac_threshold);
        min_inlier_ratio->setValue(options.min_inlier_ratio);
    }

    void requireValues(const mif::RegistrationOptions& expected) const {
        const auto actual = settings->options();
        auto close = [](double a, double b) { return std::abs(a - b) < 1e-12; };
        require(actual.method == expected.method && actual.motion_model == expected.motion_model &&
                actual.max_size == expected.max_size &&
                actual.iterations == expected.iterations && close(actual.epsilon, expected.epsilon) &&
                actual.max_features == expected.max_features && close(actual.match_ratio, expected.match_ratio) &&
                close(actual.ransac_threshold, expected.ransac_threshold) &&
                close(actual.min_inlier_ratio, expected.min_inlier_ratio),
                "Registration options do not match the edited controls");
        require(method->currentData().toInt() == static_cast<int>(expected.method) &&
                motion_model->currentData().toInt() == static_cast<int>(expected.motion_model) &&
                max_size->value() == expected.max_size && iterations->value() == expected.iterations &&
                close(epsilon->value(), expected.epsilon) && max_features->value() == expected.max_features &&
                close(match_ratio->value(), expected.match_ratio) &&
                close(ransac_threshold->value(), expected.ransac_threshold) &&
                close(min_inlier_ratio->value(), expected.min_inlier_ratio),
                "Registration controls lost their selected values");
    }

    void requireVisibility(mif::RegistrationMethod value) const {
        const bool sift = value == mif::RegistrationMethod::Sift;
        const bool active = value != mif::RegistrationMethod::None;
        const bool ecc = value == mif::RegistrationMethod::Ecc;
        require(motion_model->isVisibleTo(settings) == ecc && max_size->isVisibleTo(settings) == active &&
                iterations->isVisibleTo(settings) == ecc &&
                epsilon->isVisibleTo(settings) == ecc && max_features->isVisibleTo(settings) == sift &&
                match_ratio->isVisibleTo(settings) == sift && ransac_threshold->isVisibleTo(settings) == sift &&
                min_inlier_ratio->isVisibleTo(settings) == sift,
                "Registration mode shows unrelated parameters or hides required parameters");
    }
};

// 所有字段都偏离默认值，最长边还会触发缩小，真实计算可发现主窗口漏传参数。
mif::RegistrationOptions customRegistrationOptions(mif::RegistrationMethod method, mif::MotionModel model) {
    mif::RegistrationOptions options;
    options.method = method;
    options.motion_model = model;
    options.max_size = 256;
    options.iterations = 90;
    options.epsilon = 0.00002;
    options.max_features = 1200;
    options.match_ratio = 0.82;
    options.ransac_threshold = 2.25;
    options.min_inlier_ratio = 0.35;
    return options;
}

// 等待一个窗口完成任务；定时器绑定局部事件循环，返回时自动释放并断开信号。
void runFusion(mif::desktop::MainWindow& window, const std::function<void()>& while_running = {}) {
    QEventLoop loop;
    bool done = false;
    QString failure;
    QObject::connect(&window, &mif::desktop::MainWindow::fusionCompleted, &loop, [&] { done = true; loop.quit(); });
    QObject::connect(&window, &mif::desktop::MainWindow::fusionFailed, &loop, [&](const QString& message) { failure = message; loop.quit(); });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(30000);
    window.startFusion();
    if (while_running) while_running();
    loop.exec();
    require(done, "Desktop fusion failed or timed out: " + failure.toStdString());
    // completed 比 finished 早到达；下一次导入必须等线程退出并处理界面解锁通知。
    // 等待真实线程状态，不用固定休眠，避免快速机器和慢速机器的时序差异。
    for (auto* worker : window.findChildren<mif::desktop::FusionWorker*>())
        require(worker->wait(5000), "Fusion worker did not finish after delivering its result");
    QApplication::processEvents();
    auto* save = window.findChild<QPushButton*>("saveButton");
    require(save && save->isEnabled(), "Desktop did not unlock export after fusion finished");
}

// 读取预览真正显示的像素，避免只检查列表文字而遗漏旧图片仍留在右侧的回归。
QImage previewPixels(mif::desktop::ImageView* view) {
    require(view && view->scene(), "Preview widget or scene is missing");
    for (auto* item : view->scene()->items())
        if (auto* pixmap = qgraphicsitem_cast<QGraphicsPixmapItem*>(item))
            return pixmap->pixmap().toImage().convertToFormat(QImage::Format_ARGB32);
    return {};
}

// 在独立窗口验证批次切换，避免清空供测试末尾截图使用的主窗口结果。
void verifyFolderBatchImport() {
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create folder import test directory");
    const QDir root(temporary.path());
    const QString first_folder = root.filePath("batch_1");
    const QString second_folder = root.filePath("batch_2");
    const QString empty_folder = root.filePath("empty");
    const QString unsupported_folder = root.filePath("unsupported");
    require(root.mkpath("batch_1/nested") && root.mkpath("batch_2") && root.mkpath("empty") &&
            root.mkpath("unsupported/nested"), "Cannot create batch fixture folders");

    const auto first_stack = focusStack(texture(96, 144));
    const cv::Mat second_image(71, 113, CV_8UC3, cv::Scalar(30, 170, 220));
    const QString first_2 = QDir(first_folder).filePath("focus_2.png");
    const QString first_10 = QDir(first_folder).filePath("focus_10.png");
    const QString second_1 = QDir(second_folder).filePath("focus_1.png");
    const QString second_3 = QDir(second_folder).filePath("focus_3.png");
    const QString extra = root.filePath("extra.png");
    const QString obsolete = root.filePath("obsolete.png");
    // 写入顺序故意与自然排序相反，两个批次使用不同尺寸和内容，便于发现预览残留。
    mif::desktop::writeImage(first_10, first_stack[1]);
    mif::desktop::writeImage(first_2, first_stack[0]);
    mif::desktop::writeImage(second_3, second_image);
    mif::desktop::writeImage(second_1, second_image);
    mif::desktop::writeImage(extra, second_image);
    mif::desktop::writeImage(obsolete, second_image);
    mif::desktop::writeImage(QDir(first_folder).filePath("nested/hidden.png"), second_image);
    mif::desktop::writeImage(QDir(unsupported_folder).filePath("nested/hidden.png"), second_image);
    for (const auto& folder : {first_folder, unsupported_folder}) {
        QFile note(QDir(folder).filePath("notes.txt"));
        require(note.open(QIODevice::WriteOnly), "Cannot write unsupported-file fixture");
        note.write("not an image");
    }

    mif::desktop::MainWindow window;
    window.show();
    QApplication::processEvents();
    auto* files = window.findChild<QListWidget*>("imageList");
    auto* save = window.findChild<QPushButton*>("saveButton");
    auto* source = window.findChild<mif::desktop::ImageView*>("sourcePreview");
    auto* result = window.findChild<mif::desktop::ImageView*>("resultPreview");
    require(files && save && source && result, "Batch import controls are missing");
    auto listed_paths = [files] {
        QStringList paths;
        for (int i = 0; i < files->count(); ++i)
            paths.push_back(files->item(i)->data(Qt::UserRole).toString());
        return paths;
    };
    auto require_empty_batch = [&] {
        require(files->count() == 0 && !files->currentItem(), "Empty folder retained old input items");
        require(window.resultImage().empty() && !save->isEnabled(), "Empty folder retained an exportable result");
        require(previewPixels(source).isNull() && previewPixels(result).isNull(), "Empty folder retained preview pixels");
        bool explained = false;
        for (auto* label : window.findChildren<QLabel*>())
            if (label->text().contains(QStringLiteral("没有")) && label->text().contains(QStringLiteral("图片")))
                explained = true;
        require(explained, "Empty folder did not show an explanatory status message");
    };

    window.addPaths({first_folder});
    const QStringList first_paths{first_2, first_10};
    require(listed_paths() == first_paths, "Folder import must naturally sort, filter and avoid recursion");
    // 检查主窗口调用层保留导航状态；缩放和平移自身的鼠标交互由控件测试覆盖。
    const mif::desktop::ViewState inspected{2.5, QPointF(0.4, 0.6), false};
    auto same_view = [](const mif::desktop::ViewState& actual, const mif::desktop::ViewState& expected) {
        return actual.fit == expected.fit && std::abs(actual.zoom - expected.zoom) < 1e-10 &&
               std::abs(actual.center.x() - expected.center.x()) < 1e-10 &&
               std::abs(actual.center.y() - expected.center.y()) < 1e-10;
    };
    source->applyViewState(inspected);
    files->setCurrentRow(1);
    require(same_view(source->viewState(), inspected), "Switching source images reset the inspection view");
    runFusion(window, [&] {
        // startFusion 返回时线程仍归窗口持有；即使计算很快，也尚未处理其排队通知。
        window.addPaths({second_folder});
        require(listed_paths() == first_paths, "Import changed the batch while fusion was running");
    });
    require(same_view(result->viewState(), source->viewState()), "New fusion result did not inherit the source view");
    const cv::Mat original_result = window.resultImage().clone();
    const QImage original_preview = previewPixels(source);

    // 取消选择或无效路径不是新批次，必须完整保留旧列表、预览和可导出的结果。
    window.addPaths({});
    window.addPaths({root.filePath("missing-folder"), root.filePath("missing.png")});
    require(listed_paths() == first_paths && save->isEnabled(), "Cancelled or invalid import changed the old batch");
    require(cv::norm(window.resultImage(), original_result, cv::NORM_INF) == 0 &&
            previewPixels(source) == original_preview, "Cancelled or invalid import cleared the old result or preview");
    require(same_view(source->viewState(), inspected), "Cancelled or invalid import reset the inspection view");

    // 文件夹与其中的显式文件、重复文件夹共同构成一次新批次，重复路径只保留一次。
    window.addPaths({second_folder, second_1, second_folder});
    const QStringList second_paths{second_1, second_3};
    require(listed_paths() == second_paths, "New folder did not replace the old batch or remove duplicates");
    require(window.resultImage().empty() && !save->isEnabled() && previewPixels(result).isNull(),
            "New folder retained the old fused result or enabled export");
    require(files->currentItem() && files->currentItem()->data(Qt::UserRole).toString() == second_1,
            "New folder did not select its first naturally sorted image");
    require(previewPixels(source) == mif::desktop::previewImage(second_image).convertToFormat(QImage::Format_ARGB32),
            "Source preview does not show the new batch");
    require(source->viewState().fit && result->viewState().fit, "New folder retained the previous batch's inspection view");

    // 仅文件导入仍追加；已在列表中的文件和本次重复文件不会重复插入。
    window.addPaths({first_10, second_3, first_10});
    require(listed_paths() == QStringList({second_1, second_3, first_10}), "File-only import stopped appending or duplicated files");

    window.addPaths({empty_folder});
    require_empty_batch();
    window.addPaths({second_folder});
    window.addPaths({unsupported_folder});
    require_empty_batch();

    // 多个文件夹只在整批开始时清空一次，同时保留本次显式文件；子目录始终不递归。
    window.addPaths({obsolete});
    window.addPaths({second_folder, first_10, first_folder, second_1, second_folder, extra});
    require(listed_paths() == QStringList({first_2, first_10, second_1, second_3, extra}),
            "Mixed multi-folder import cleared part of the new batch, retained old files or recursed");

    // 实际发送拖入和放下事件，保证文件夹拖放也经过相同的替换批次入口。
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(second_folder)});
    QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    require(enter.isAccepted(), "Window did not accept a folder drag");
    QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    require(drop.isAccepted() && listed_paths() == second_paths, "Folder drop did not replace the previous batch");
    // 通过主窗口工具栏触发适应，验证两侧连接；空结果面板也应重置缓存视野。
    auto* fit = window.findChild<QPushButton*>("fitPreviews");
    require(fit != nullptr, "Fit previews button is missing");
    source->applyViewState(inspected);
    result->applyViewState(inspected);
    fit->click();
    require(source->viewState().fit && result->viewState().fit, "Fit button did not reset both preview views");
}

// 独立窗口检查参数编辑与布局，不改变供最终截图使用的主窗口及其融合结果。
void verifyRegistrationSettings() {
    mif::desktop::MainWindow window;
    window.show();
    RegistrationControls controls(window);
    auto* tabs = window.findChild<QTabWidget*>("parameterTabs");
    auto* fusion = window.findChild<QGroupBox*>("fusionSettings");
    auto* run = window.findChild<QPushButton*>("primaryButton");
    require(tabs && tabs->count() == 2 && fusion && run, "Parameter tabs or fixed run button are missing");
    require(tabs->widget(0)->isAncestorOf(controls.settings) && tabs->widget(1)->isAncestorOf(fusion),
            "Registration and fusion must have separate parameter pages in the expected order");
    auto scrollAncestor = [](QWidget* widget) {
        for (auto* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
            if (auto* area = qobject_cast<QScrollArea*>(parent)) return area;
        return static_cast<QScrollArea*>(nullptr);
    };
    auto* registration_scroll = scrollAncestor(controls.settings);
    auto* fusion_scroll = scrollAncestor(fusion);
    require(registration_scroll && fusion_scroll && registration_scroll != fusion_scroll &&
            !registration_scroll->isAncestorOf(run) && !fusion_scroll->isAncestorOf(run),
            "Each parameter page needs a scroll area, with the run button outside both");
    tabs->setCurrentIndex(0);
    flushLayout();
    controls.requireValues(mif::RegistrationOptions{});
    require(controls.method->count() == 3 && controls.motion_model->count() == 3,
            "Registration methods and motion models need separate three-item selectors");
    for (const auto method : {mif::RegistrationMethod::None, mif::RegistrationMethod::Ecc,
                              mif::RegistrationMethod::Sift})
        require(controls.method->findData(static_cast<int>(method)) >= 0,
                "Registration method item data is missing");
    for (const auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine,
                             mif::MotionModel::Homography})
        require(controls.motion_model->findData(static_cast<int>(model)) >= 0,
                "Motion model item data is missing");
    require(controls.max_size->minimum() == 16 && controls.max_size->maximum() == 8192 &&
            controls.iterations->minimum() == 1 && controls.iterations->maximum() == 10000 &&
            controls.max_features->minimum() == 64 && controls.max_features->maximum() == 100000,
            "Integer registration controls have incorrect supported ranges");
    auto requireDoubleRange = [](QDoubleSpinBox* box, double minimum, double maximum, int decimals) {
        require(std::abs(box->minimum() - minimum) < 1e-12 &&
                std::abs(box->maximum() - maximum) < 1e-12 && box->decimals() == decimals,
                "Floating-point registration control has an incorrect range or display precision");
    };
    requireDoubleRange(controls.epsilon, 1e-8, 1.0, 8);
    requireDoubleRange(controls.match_ratio, 0.01, 0.99, 2);
    requireDoubleRange(controls.ransac_threshold, 0.01, 1000.0, 2);
    requireDoubleRange(controls.min_inlier_ratio, 0.01, 1.0, 2);
    require(std::abs(controls.epsilon->singleStep() - 1e-5) < 1e-12,
            "ECC epsilon control has an incorrect step");

    auto edited = customRegistrationOptions(mif::RegistrationMethod::None, mif::MotionModel::Translation);
    controls.setValues(edited);
    // 三种模型分别经历 ECC → SIFT → 关闭 → ECC，隐藏期间仍需保留所选模型与数值。
    for (const auto model : {mif::MotionModel::Translation, mif::MotionModel::Affine,
                             mif::MotionModel::Homography}) {
        controls.selectMethod(mif::RegistrationMethod::Ecc);
        controls.selectMotionModel(model);
        edited.motion_model = model;
        for (const auto method : {mif::RegistrationMethod::Ecc, mif::RegistrationMethod::Sift,
                                  mif::RegistrationMethod::None, mif::RegistrationMethod::Ecc}) {
            controls.selectMethod(method);
            edited.method = method;
            controls.requireVisibility(method);
            controls.requireValues(edited);
            // 将滚轮发送给有键盘焦点的真实控件，算法、模型和数值均不能误改。
            for (QWidget* widget : std::vector<QWidget*>{controls.method, controls.motion_model,
                    controls.max_size, controls.iterations, controls.epsilon, controls.max_features,
                    controls.match_ratio, controls.ransac_threshold, controls.min_inlier_ratio}) {
                if (!widget->isVisibleTo(controls.settings)) continue;
                widget->setFocus();
                const QPoint point = widget->rect().center();
                QWheelEvent event(QPointF(point), QPointF(widget->mapToGlobal(point)), QPoint(), QPoint(0, 120),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(widget, &event);
                controls.requireValues(edited);
            }
            // 标签页切换不能重建参数控件或丢弃隐藏算法的配置。
            tabs->setCurrentIndex(1);
            flushLayout();
            require(fusion->isVisibleTo(&window), "Fusion page did not become visible");
            tabs->setCurrentIndex(0);
            flushLayout();
            controls.requireValues(edited);
        }
    }

    // 所有算法下恢复默认都覆盖隐藏数值，但保留算法及模型；切回 ECC 时模型仍可见。
    for (const auto method : {mif::RegistrationMethod::Ecc, mif::RegistrationMethod::Sift,
                              mif::RegistrationMethod::None}) {
        controls.selectMethod(method);
        edited = customRegistrationOptions(method, mif::MotionModel::Homography);
        controls.setValues(edited);
        controls.reset->click();
        mif::RegistrationOptions defaults;
        defaults.method = method;
        defaults.motion_model = mif::MotionModel::Homography;
        controls.requireValues(defaults);
        controls.requireVisibility(method);
        controls.selectMethod(mif::RegistrationMethod::Ecc);
        defaults.method = mif::RegistrationMethod::Ecc;
        controls.requireValues(defaults);
        controls.requireVisibility(defaults.method);
    }

    // 压缩可用高度产生真实滚动范围；不依赖字体和平台对应的精确像素尺寸。
    controls.selectMethod(mif::RegistrationMethod::Sift);
    window.resize(window.minimumSize());
    tabs->setMaximumHeight(160);
    flushLayout();
    auto* vertical = registration_scroll->verticalScrollBar();
    require(vertical->maximum() > vertical->minimum(), "Small parameter page cannot scroll to its lower fields");
    registration_scroll->ensureWidgetVisible(controls.reset);
    flushLayout();
    require(registration_scroll->viewport()->rect().contains(
                controls.reset->mapTo(registration_scroll->viewport(), controls.reset->rect().center())),
            "Reset button cannot be reached by scrolling the registration page");
    require(run->isVisibleTo(&window) && window.rect().contains(QRect(run->mapTo(&window, QPoint()), run->size())),
            "Run button is clipped or hidden in the small window");
}

// 将算法和模型都挪到与枚举值不同的位置后真实执行，发现误用 currentIndex 的回归。
void verifyRegistrationSelection(const QStringList& paths, const std::vector<cv::Mat>& images,
                                 mif::RegistrationMethod method, mif::MotionModel model) {
    mif::desktop::MainWindow window;
    window.addPaths(paths);
    // 主窗口会自然排序文件；核心对照必须使用同一顺序，第一张始终是配准参考。
    auto* files = window.findChild<QListWidget*>("imageList");
    require(files && files->count() == paths.size() && images.size() == static_cast<std::size_t>(paths.size()),
            "Registration fixture paths and core images must have matching counts");
    for (int i = 0; i < paths.size(); ++i)
        require(files->item(i)->data(Qt::UserRole).toString() == QFileInfo(paths[i]).absoluteFilePath(),
                "Registration fixture order differs from desktop import order: expected " + paths[i].toStdString() +
                ", imported " + files->item(i)->data(Qt::UserRole).toString().toStdString());
    RegistrationControls controls(window);
    auto* registration_settings = window.findChild<QGroupBox*>("registrationSettings");
    auto* fusion_settings = window.findChild<QGroupBox*>("fusionSettings");
    require(registration_settings && fusion_settings && registration_settings != fusion_settings,
            "Registration and fusion settings must have separate groups");
    for (auto* selector : {controls.method, controls.motion_model})
        require(registration_settings->isAncestorOf(selector) && !fusion_settings->isAncestorOf(selector),
                "Registration selectors must belong to the registration settings");
    auto moveToFirst = [](QComboBox* selector, int data) {
        const int index = selector->findData(data);
        require(index > 0, "Registration fixture must move the selected item away from its enum position");
        const auto label = selector->itemText(index);
        const auto value = selector->itemData(index);
        selector->removeItem(index);
        selector->insertItem(0, label, value);
        selector->setCurrentIndex(0);
        require(selector->currentData().toInt() == data, "Reordered registration selection failed");
    };
    moveToFirst(controls.method, static_cast<int>(method));
    moveToFirst(controls.motion_model, static_cast<int>(model));

    // 非默认参数与缩小工作图同时生效；ECC 仿射/单应性和 SIFT 分别与核心入口对照。
    // SIFT 接收并保留合法模型字段，但算法自身固定单应性，不使用这个 ECC 专属选择。
    const auto registration_options = customRegistrationOptions(method, model);
    controls.setValues(registration_options);
    controls.requireValues(registration_options);
    const mif::FusionOptions fusion_options;
    const auto expected = mif::registerAndFuse(images, registration_options, fusion_options);
    require(expected.fusion.image.size() != images.front().size(), "Registration fixture must crop transformed borders");
    runFusion(window, [&] {
        require(!registration_settings->isEnabled() && !fusion_settings->isEnabled(),
                "Both parameter groups must be locked while the pipeline runs");
        require(!controls.method->isEnabled() && !controls.motion_model->isEnabled() &&
                !controls.reset->isEnabled() && !controls.max_size->isEnabled() && !controls.iterations->isEnabled() &&
                !controls.epsilon->isEnabled() && !controls.max_features->isEnabled() &&
                !controls.match_ratio->isEnabled() && !controls.ransac_threshold->isEnabled() &&
                !controls.min_inlier_ratio->isEnabled(),
                "Registration algorithm, model or parameters remained editable during processing");
    });
    require(registration_settings->isEnabled() && fusion_settings->isEnabled(),
            "Both parameter groups must be unlocked when the pipeline finishes");
    require(controls.method->isEnabled() && controls.motion_model->isEnabled() && controls.reset->isEnabled() &&
            controls.max_size->isEnabled() && controls.iterations->isEnabled() && controls.epsilon->isEnabled() &&
            controls.max_features->isEnabled() && controls.match_ratio->isEnabled() &&
            controls.ransac_threshold->isEnabled() && controls.min_inlier_ratio->isEnabled(),
            "Registration fields did not unlock after processing");
    controls.requireValues(registration_options);
    require(window.resultImage().size() == expected.fusion.image.size(),
            "Desktop passed the wrong registration algorithm or motion model");
    require(mae(window.resultImage(), expected.fusion.image) < 0.1,
            "Desktop registration differs from the selected algorithm and motion model");
}
} // 匿名命名空间

// 在真实 Qt 事件循环中串联导入、预览、后台融合与导出，验证界面到核心的连接。
// 使用临时目录和合成图片，测试结束后不在用户数据目录留下结果。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setOrganizationName("MultiFocusTests"); app.setApplicationName("MultiFocusTests");
    try {
        // 可选样式在任何窗口创建前生效，使交互检查和截图使用相同的真实布局。
        if (argc > 1) {
            QFile style(QString::fromLocal8Bit(argv[1]));
            require(style.open(QIODevice::ReadOnly), "Cannot open the requested desktop stylesheet");
            app.setStyleSheet(QString::fromUtf8(style.readAll()));
        }
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create test directory");
        auto sharp = texture(192, 320);
        cv::cvtColor(sharp, sharp, cv::COLOR_GRAY2BGR);
        const auto stack = focusStack(sharp);
        // 中文路径检查 Windows 文件读写适配，16 位 PNG 和浮点 TIFF 检查精度保留。
        const auto first = temporary.filePath(QStringLiteral("焦点_01.png"));
        const auto second = temporary.filePath(QStringLiteral("焦点_02.png"));
        mif::desktop::writeImage(first, stack[0]); mif::desktop::writeImage(second, stack[1]);
        cv::Mat high;
        sharp.convertTo(high, CV_16U, 257);
        const auto precision = temporary.filePath(QStringLiteral("精度.png"));
        mif::desktop::writeImage(precision, high);
        require(cv::norm(high, mif::desktop::readImage(precision), cv::NORM_INF) == 0, "Unicode 16-bit round trip failed");
        cv::Mat floating;
        sharp.convertTo(floating, CV_32F, 1.0 / 255);
        const auto floatPath = temporary.filePath(QStringLiteral("浮点.tif"));
        mif::desktop::writeImage(floatPath, floating);
        require(cv::norm(floating, mif::desktop::readImage(floatPath), cv::NORM_INF) < 1e-7, "Float TIFF round trip was lossy");
        // 重复加入同一路径应被去重，后台任务使用的输入列表也应保持一致。
        mif::desktop::MainWindow window;
        window.show(); window.addPaths({first, second, first});
        require(window.findChild<QListWidget*>("imageList")->count() == 2, "Duplicate imports were not removed");
        runFusion(window);
        require(mae(window.resultImage(), sharp) < mae(stack[0], sharp) * 0.65, "Desktop result quality failed");
        const auto saved = temporary.filePath(QStringLiteral("融合结果.png"));
        mif::desktop::writeImage(saved, window.resultImage());
        require(cv::norm(window.resultImage(), mif::desktop::readImage(saved), cv::NORM_INF) == 0, "Result export differs");
        // 固定随机纹理与图形提供稳定特征；同一轻微透视变形分别验证 SIFT 和 ECC 单应性。
        auto reference = texture(240, 320);
        cv::circle(reference, {60, 60}, 18, cv::Scalar(245), 3);
        cv::rectangle(reference, {205, 140, 65, 40}, cv::Scalar(25), 3);
        const cv::Mat homography = (cv::Mat_<float>(3, 3) <<
            1.0f, 0.001f, 3.0f, 0.001f, 1.0f, 2.0f, 0.00002f, -0.00002f, 1.0f);
        cv::Mat shifted;
        cv::warpPerspective(reference, shifted, homography, reference.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
        const auto alignmentFirst = temporary.filePath(QStringLiteral("配准_01.png"));
        const auto alignmentSecond = temporary.filePath(QStringLiteral("配准_02.png"));
        mif::desktop::writeImage(alignmentFirst, reference);
        mif::desktop::writeImage(alignmentSecond, shifted);
        verifyRegistrationSelection({alignmentFirst, alignmentSecond}, {reference, shifted},
                                    mif::RegistrationMethod::Sift, mif::MotionModel::Affine);
        verifyRegistrationSelection({alignmentFirst, alignmentSecond}, {reference, shifted},
                                    mif::RegistrationMethod::Ecc, mif::MotionModel::Homography);
        // 独立仿射样本包含小幅旋转和缩放，确保遗漏 motion_model 时默认平移无法冒充。
        cv::Mat affine = cv::getRotationMatrix2D(cv::Point2f(160.f, 120.f), 1.2, 1.015);
        affine.at<double>(0, 2) += 3.0;
        affine.at<double>(1, 2) += 2.0;
        cv::Mat affine_shifted;
        cv::warpAffine(reference, affine_shifted, affine, reference.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
        // 使用同一前缀保证自然排序后仍以 reference 为首张，与核心对照的输入顺序一致。
        const auto affineFirst = temporary.filePath(QStringLiteral("仿射_01.png"));
        const auto affineSecond = temporary.filePath(QStringLiteral("仿射_02.png"));
        mif::desktop::writeImage(affineFirst, reference);
        mif::desktop::writeImage(affineSecond, affine_shifted);
        verifyRegistrationSelection({affineFirst, affineSecond}, {reference, affine_shifted},
                                    mif::RegistrationMethod::Ecc, mif::MotionModel::Affine);
        verifyRegistrationSettings();
        verifyFolderBatchImport();
        // 可选截图使用同一个已有结果的主窗口，依次检查默认、ECC、SIFT、融合和小窗口。
        // 不传截图路径时跳过此分支，不增加普通 CTest 的操作或输出文件。
        if (argc > 2) {
            RegistrationControls controls(window);
            auto* tabs = window.findChild<QTabWidget*>("parameterTabs");
            require(tabs != nullptr, "Parameter tabs are missing from the screenshot window");
            const QFileInfo target(QString::fromLocal8Bit(argv[2]));
            auto capture = [&](const QString& suffix) {
                flushLayout();
                for (auto* area : tabs->findChildren<QScrollArea*>()) {
                    area->verticalScrollBar()->setValue(area->verticalScrollBar()->minimum());
                    area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->minimum());
                }
                flushLayout();
                const auto path = suffix.isEmpty() ? target.filePath()
                    : target.dir().filePath(target.completeBaseName() + suffix + ".png");
                require(window.grab().save(path), "Screenshot save failed: " + path.toStdString());
            };
            tabs->setCurrentIndex(0);
            controls.selectMethod(mif::RegistrationMethod::None);
            capture({});
            controls.selectMethod(mif::RegistrationMethod::Ecc);
            controls.selectMotionModel(mif::MotionModel::Affine);
            capture("_ecc");
            controls.selectMethod(mif::RegistrationMethod::Sift);
            capture("_sift");
            tabs->setCurrentIndex(1);
            capture("_fusion");
            tabs->setCurrentIndex(0);
            window.resize(940, 670);
            capture("_small");
        }
        std::cout << "PASS desktop import / preview / worker / export / precision / registration settings / folder batches\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

