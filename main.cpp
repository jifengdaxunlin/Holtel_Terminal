#include "mainwindow.h"
#include "wifi/wifiinfo.h"

#include <QApplication>
#include <QTextCodec>
#include <time.h>   // tzset

int main(int argc, char *argv[])
{
    // 强制北京时间（UTC+8）：X6818 嵌入式系统默认时区为 UTC，会导致界面时钟、
    // 日期、MQTT ClientID 时间戳等全部慢 8 小时。用 POSIX 格式 CST-8，
    // 不依赖 /usr/share/zoneinfo 时区数据文件（busybox 系统通常没有）。
    // 必须在 QApplication 构造之前设置，否则 Qt 会缓存旧时区。
    qputenv("TZ", "CST-8");
    tzset();

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/icons/app-icon.ico"));

    // 强制 UTF-8 文件系统编码：X6818 嵌入式系统 locale 通常为 C/POSIX，
    // 中文文件名（U 盘 FAT32/NTFS 拷入）会被按 Latin-1 解析导致乱码。
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    if (utf8) QTextCodec::setCodecForLocale(utf8);

    // WifiManager runs on a worker thread; queued signal/slot delivers custom types.
    // Register them here so signals connect even when the GUI is running but the
    // application has not been constructed yet.
    qRegisterMetaType<WifiInfo>("WifiInfo");
    qRegisterMetaType<QList<WifiInfo>>("QList<WifiInfo>");

    MainWindow w;
    // Run full-screen on the target panel so the 800x480 UI fills the display.
    // Pass --windowed on the command line for desktop debugging.
    if (QApplication::arguments().contains("--windowed"))
        w.show();
    else
        w.showFullScreen();
    return a.exec();
}
