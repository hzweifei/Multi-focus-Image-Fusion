#include "fixtures.hpp"
#include "main_window.hpp"
#include "image_io.hpp"
#include "workers/fusion_worker.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

namespace {
// 等待一个窗口完成任务；定时器绑定局部事件循环，返回时自动释放并断开信号。
void runFusion(mif::desktop::MainWindow& window) {
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
    loop.exec();
    require(done, "Desktop fusion failed or timed out: " + failure.toStdString());
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
        // 可选传入样式表与截图路径，便于人工检查界面；不参与算法质量判定。
        if (argc > 1) {
            QFile style(QString::fromLocal8Bit(argv[1]));
            if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
        }
        app.processEvents();
        if (argc > 2) require(window.grab().save(QString::fromLocal8Bit(argv[2])), "Screenshot save failed");
        std::cout << "PASS desktop import / preview / worker / export / precision / homography selection\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

