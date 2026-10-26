#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("korg-kontrol-linux"));
    QApplication::setApplicationName(QStringLiteral("nts-nts-kontrol"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setDesktopFileName(QStringLiteral("nts-nts-kontrol"));

    korg::MainWindow window;
    window.show();
    return app.exec();
}
