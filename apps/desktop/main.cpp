#include "main_window.hpp"
#include <QApplication>
#include <QFile>
#include <QIcon>

// 桌面入口：初始化 Qt 设置、加载内置资源，再将命令行路径交给主窗口导入。
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // 固定组织名和应用名，让 QSettings 在多次运行间读取同一份用户设置。
    QCoreApplication::setOrganizationName("MultiFocus");
    QCoreApplication::setApplicationName("MultiFocus");
    QCoreApplication::setApplicationVersion("0.1.0");
    // 图标和样式编译进 Qt 资源，无需依赖程序启动时所在的工作目录。
    app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/icons/focus.svg"));
    QFile style(":/style.qss");
    if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
    mif::desktop::MainWindow window;
    // 支持从命令行传入图片或文件夹，也方便通过文件关联打开图像。
    if (app.arguments().size() > 1) window.addPaths(app.arguments().mid(1));
    window.show();
    // 界面事件循环负责用户操作、工作线程信号和延迟释放对象。
    return app.exec();
}

