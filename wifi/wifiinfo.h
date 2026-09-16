#ifndef WIFIINFO_H
#define WIFIINFO_H

#include <QString>
#include <QMetaType>

// 一个 WiFi 热点的基本信息
struct WifiInfo
{
    QString ssid;            // 网络名称（SSID）
    QString bssid;           // 热点 MAC
    QString security;        // 加密方式文字描述，如 "WPA2-PSK" / "开放"
    int     signal = 0;      // 信号强度 0~100(%)
    int     level = 0;       // 原始信号电平 dBm（Linux wpa_cli 才有）
    bool    secured = false; // 是否需要密码

    bool isValid() const { return !ssid.trimmed().isEmpty(); }
};

Q_DECLARE_METATYPE(WifiInfo)

#endif // WIFIINFO_H
