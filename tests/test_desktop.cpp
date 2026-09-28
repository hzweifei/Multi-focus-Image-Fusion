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

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("MultiFocusTests"); app.setApplicationName("MultiFocusTests");
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create test directory");
        auto sharp = texture(192, 320);
        cv::cvtColor(sharp, sharp, cv::COLOR_GRAY2BGR);
        const auto stack = focusStack(sharp);
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
        mif::desktop::MainWindow window;
        window.show(); window.addPaths({first, second, first});
        require(window.findChild<QListWidget*>("imageList")->count() == 2, "Duplicate imports were not removed");
        QEventLoop loop;
        bool done = false; QString failure;
        QObject::connect(&window, &mif::desktop::MainWindow::fusionCompleted, &loop, [&] { done = true; loop.quit(); });
        QObject::connect(&window, &mif::desktop::MainWindow::fusionFailed, &loop, [&](const QString& message) { failure = message; loop.quit(); });
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        window.startFusion(); loop.exec();
        require(done, "Desktop fusion failed: " + failure.toStdString());
        require(mae(window.resultImage(), sharp) < mae(stack[0], sharp) * 0.65, "Desktop result quality failed");
        const auto saved = temporary.filePath(QStringLiteral("融合结果.png"));
        mif::desktop::writeImage(saved, window.resultImage());
        require(cv::norm(window.resultImage(), mif::desktop::readImage(saved), cv::NORM_INF) == 0, "Result export differs");
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

