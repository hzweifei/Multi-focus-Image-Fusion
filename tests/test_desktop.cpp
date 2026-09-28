#include "fixtures.hpp"
#include "main_window.hpp"
#include "image_io.hpp"
#include "widgets/image_view.hpp"
#include "workers/fusion_worker.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cmath>
#include <functional>
#include <iostream>

namespace {
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

// 将选项挪到与枚举值不同的位置后真实执行，能发现误用 currentIndex 的传参回归。
void verifyHomographySelection(const QStringList& paths, const std::vector<cv::Mat>& images) {
    for (const auto mode : {mif::Alignment::FeatureHomography, mif::Alignment::EccHomography}) {
        mif::desktop::MainWindow window;
        window.addPaths(paths);
        auto* selector = window.findChild<QComboBox*>("alignmentMode");
        require(selector != nullptr && selector->count() == 5, "Alignment modes are missing from the desktop UI");
        for (const auto existing : {mif::Alignment::None, mif::Alignment::Translation, mif::Alignment::Affine,
                mif::Alignment::FeatureHomography, mif::Alignment::EccHomography})
            require(selector->findData(static_cast<int>(existing)) >= 0, "Alignment item data is missing");
        const int index = selector->findData(static_cast<int>(mode));
        const auto label = selector->itemText(index);
        const auto value = selector->itemData(index);
        selector->removeItem(index);
        selector->insertItem(0, label, value);
        selector->setCurrentIndex(0);
        require(selector->currentData().toInt() == static_cast<int>(mode), "Alignment mode selection failed");

        mif::FusionOptions options;
        options.alignment = mode;
        const auto expected = mif::fuse(images, options);
        require(expected.image.size() != images.front().size(), "Alignment fixture must crop its perspective borders");
        runFusion(window);
        // 界面和核心应选择同一模式；有透视边缘的样例还可以区分错误的“关闭配准”。
        require(window.resultImage().size() == expected.image.size(), "Desktop passed the wrong alignment mode");
        require(mae(window.resultImage(), expected.image) < 0.1, "Desktop alignment differs from the selected core mode");
    }
}
} // 匿名命名空间

// 在真实 Qt 事件循环中串联导入、预览、后台融合与导出，验证界面到核心的连接。
// 使用临时目录和合成图片，测试结束后不在用户数据目录留下结果。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("MultiFocusTests"); app.setApplicationName("MultiFocusTests");
    try {
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
        // 固定随机纹理、文字与图形提供稳定的特征；轻微透视变形适合两种单应性方法。
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
        verifyHomographySelection({alignmentFirst, alignmentSecond}, {reference, shifted});
        verifyFolderBatchImport();
        // 可选传入样式表与截图路径，便于人工检查界面；不参与算法质量判定。
        if (argc > 1) {
            QFile style(QString::fromLocal8Bit(argv[1]));
            if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
        }
        app.processEvents();
        if (argc > 2) require(window.grab().save(QString::fromLocal8Bit(argv[2])), "Screenshot save failed");
        std::cout << "PASS desktop import / preview / worker / export / precision / homography selection / folder batches\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

