#include "main_window.hpp"
#include <QApplication>
#include <QFile>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("MultiFocus");
    QCoreApplication::setApplicationName("MultiFocus");
    QCoreApplication::setApplicationVersion("0.1.0");
    app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/icons/focus.svg"));
    QFile style(":/style.qss");
    if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
    mif::desktop::MainWindow window;
    if (app.arguments().size() > 1) window.addPaths(app.arguments().mid(1));
    window.show();
    return app.exec();
}

