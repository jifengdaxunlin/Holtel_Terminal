#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/icons/app-icon.ico"));

    MainWindow w;
    // Run full-screen on the target panel so the 800x480 UI fills the display.
    // Pass --windowed on the command line for desktop debugging.
    if (QApplication::arguments().contains("--windowed"))
        w.show();
    else
        w.showFullScreen();
    return a.exec();
}
