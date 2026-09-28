#include "fixtures.hpp"
#include "main_window.hpp"
#include "image_io.hpp"
#include "workers/fusion_worker.hpp"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

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
        QEventLoop loop;
        bool done = false; QString failure;
        // 信号退出局部事件循环；额外设置超时，避免线程或信号异常使测试无限挂起。
        QObject::connect(&window, &mif::desktop::MainWindow::fusionCompleted, &loop, [&] { done = true; loop.quit(); });
        QObject::connect(&window, &mif::desktop::MainWindow::fusionFailed, &loop, [&](const QString& message) { failure = message; loop.quit(); });
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        window.startFusion(); loop.exec();
        require(done, "Desktop fusion failed: " + failure.toStdString());
        require(mae(window.resultImage(), sharp) < mae(stack[0], sharp) * 0.65, "Desktop result quality failed");
        const auto saved = temporary.filePath(QStringLiteral("融合结果.png"));
        mif::desktop::writeImage(saved, window.resultImage());
        require(cv::norm(window.resultImage(), mif::desktop::readImage(saved), cv::NORM_INF) == 0, "Result export differs");
        // 可选传入样式表与截图路径，便于人工检查界面；不参与算法质量判定。
        if (argc > 1) {
            QFile style(QString::fromLocal8Bit(argv[1]));
            if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
        }
        app.processEvents();
        if (argc > 2) require(window.grab().save(QString::fromLocal8Bit(argv[2])), "Screenshot save failed");
        std::cout << "PASS desktop import / preview / worker / export / precision\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

