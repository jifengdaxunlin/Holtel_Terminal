#ifndef WIFI_SETTINGS_WIDGET_H
#define WIFI_SETTINGS_WIDGET_H

#include <QFrame>
#include <QList>
#include "wifi/wifiinfo.h"

class QLabel;
class QPushButton;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTimer;
class QThread;
class WifiManager;
class SoftKeyboard;

/*
 * WiFi settings card (settings page).
 *
 * - Holds the current connection status (SSID / IP).
 * - Scans nearby networks; user picks one, enters the password
 *   via SoftKeyboard, presses "连接选中" to connect.
 * - Disconnect / refresh buttons.
 * - WifiManager runs on a worker QThread so UI stays responsive.
 *
 * Built entirely from code so it does not touch mainwindow.ui.
 */
class WifiSettingsWidget : public QFrame
{
    Q_OBJECT
public:
    explicit WifiSettingsWidget(QWidget *parent = 0);
    ~WifiSettingsWidget();

    bool isAvailable() const { return m_available; }

    // 供 MainWindow 在页面切换时调用，收起软键盘
    void dismissKeyboard();

    // 由 MainWindow 在主题切换时调用，让卡片跟随当前主题
    void applyTheme(bool dark, const QString &accent, const QString &accent2,
                    const QString &card0, const QString &card1, const QString &border,
                    const QString &textMain, const QString &textSub);

signals:
    void logMessage(const QString &text);
    /** 网络连通状态变化（连接成功/断开/刷新发现状态）—— NTP 校时等模块挂钩 */
    void connectivityChanged(bool connected);

private slots:
    void onScanClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onRefreshClicked();
    void onItemSelectionChanged();

    void onScanStarted();
    void onScanFinished(const QList<WifiInfo> &list);
    void onConnectFinished(bool ok, const QString &ssid, const QString &ip, const QString &message);
    void onStateRefreshed(bool connected, const QString &ssid, const QString &ip);
    void onBusyChanged(bool busy);
    void onOperationFailed(const QString &reason);
    void onLog(const QString &text);

private:
    void buildUi();
    bool eventFilter(QObject *watched, QEvent *event) override;
    void applyStyle();
    void setMsg(const QString &text, bool error = false);
    void updateCurrentStatus(bool connected, const QString &ssid, const QString &ip);
    void showKeyboardForPassword();
    void hideKeyboard();
    WifiInfo selectedInfo() const;

    // ---- UI ----
    QLabel        *m_cardTitle;
    QLabel        *m_cardSub;
    QLabel        *m_statusText;     // "未连接" / "XiaoMi_5G   192.168.1.42"
    QLabel        *m_msgLabel;       // 错误 / 进度提示
    QPushButton   *m_btnRefresh;
    QPushButton   *m_btnScan;
    QPushButton   *m_btnDisconnect;
    QPushButton   *m_btnConnect;
    QListWidget   *m_list;
    QLineEdit     *m_pwdEdit;
    SoftKeyboard  *m_keyboard;

    // ---- Worker ----
    WifiManager   *m_wifi;
    QThread       *m_worker;
    bool           m_busy;
    bool           m_opRunning;
    bool           m_available;
    QList<WifiInfo> m_networks;

    // 当前选中条目的密码缓存（界面层记录；非安全敏感，本地内存）
    QString m_curSsid;
    QString m_pendingPwd;
    bool    m_connecting = false;

    // ---- 主题色（默认深海蓝；applyTheme 覆盖）----
    bool    m_dark     = true;
    QString m_accent   = QStringLiteral("#00d4ff");
    QString m_accent2  = QStringLiteral("#0077be");
    QString m_card0    = QStringLiteral("#0f2a52");
    QString m_card1    = QStringLiteral("#081a33");
    QString m_border   = QStringLiteral("#1a3a6b");
    QString m_textMain = QStringLiteral("#ffffff");
    QString m_textSub  = QStringLiteral("#8aa4c8");
};

#endif // WIFI_SETTINGS_WIDGET_H