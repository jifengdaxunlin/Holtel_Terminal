#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QQueue>
#include <functional>

#include "wifiinfo.h"

// WiFi 底层管理类：
//   - Linux / ARM 板（Qt for Linux）: 通过 wpa_cli + udhcpc/dhclient + ip 命令操作 wpa_supplicant
//   - Windows 开发机（本机联调 UI 用）: 通过 netsh wlan 扫描/连接
// 所有命令通过内部串行队列异步执行，不会阻塞界面线程。
class WifiManager : public QObject
{
    Q_OBJECT
public:
    explicit WifiManager(const QString &iface = QStringLiteral("wlan0"), QObject *parent = 0);

    QString interfaceName() const { return m_iface; }
    bool    isBusy() const        { return m_procBusy; }
    QString gateway() const       { return m_gateway; }
    QString currentSsid() const   { return m_connectedSsid; }
    QString currentIp() const     { return m_connectedIp; }

public slots:
    void scan();                                          // 扫描附近热点
    void connectTo(const WifiInfo &info, const QString &password); // 连接(开放网络传空密码)
    void disconnectWifi();                                // 断开当前连接
    void refreshState();                                  // 主动查询一次当前连接状态

signals:
    void scanStarted();
    void scanFinished(const QList<WifiInfo> &list);       // 扫描结果（已按信号排序）
    void operationFailed(const QString &reason);

    void connectStarted(const QString &ssid);
    void connectFinished(bool ok, const QString &ssid, const QString &ip, const QString &message);

    void stateRefreshed(bool connected, const QString &ssid, const QString &ip);
    void gatewayChanged(const QString &gw);
    void busyChanged(bool busy);
    void logMessage(const QString &text);

private:
    // ---------- 串行命令队列 ----------
    struct Cmd
    {
        QString prog;
        QStringList args;
        std::function<void(const QString &out, const QString &err)> done;
        int postDelayMs = 0;   // 命令结束后延迟 postDelayMs 再执行下一条
        int timeoutMs = 20000; // 看门狗：超时强杀进程，防止队列被挂起的命令永久卡死
    };
    void exec(const QString &prog, const QStringList &args,
              std::function<void(const QString &out, const QString &err)> done = std::function<void(const QString&, const QString&)>(),
              int postDelayMs = 0,
              int timeoutMs = 20000);
    void pump();

    // ---------- Linux / wpa_supplicant 后端 ----------
    void linuxScan();
    void linuxParseScan(const QString &out);
    void linuxConnect(const WifiInfo &info, const QString &password);
    void linuxListForCleanup();                    // 列出已保存网络准备清理
    void linuxDelNext();                           // 逐个删除旧网络
    void linuxAddNetwork();                        // add_network 并准备 set 命令序列
    void linuxSetNext();                           // 依次执行 set_network
    void linuxActivateNetwork();                   // select/enable/reconnect 后进入轮询
    void linuxPollConnected();                     // 轮询 wpa_state 直到 COMPLETED
    void linuxObtainIp(const QString &ssid);       // 先 udhcpc，取不到再 dhclient
    void linuxWaitIp(const QString &ssid);
    void linuxReadNetInfo();                       // 读取当前 IP/网关(ip addr / ifconfig / ip route)
    void linuxReadGateway();
    void linuxEmitState();

    // ---------- Windows / netsh 后端 ----------
    void winScan();
    void winParseScan(const QString &out);
    void winConnect(const WifiInfo &info, const QString &password);
    void winPollConnected(const QString &ssid);
    void winReadState();
    void winReadIpDone(const QString &ssid);

    void finishConnect(bool ok, const QString &ssid, const QString &ip, const QString &message);

private:
    QString  m_iface;            // Linux 网卡名，默认 wlan0
    QQueue<Cmd> m_cmds;
    bool     m_procBusy = false;
    bool     m_connecting = false;

    int      m_netId = -1;       // wpa_cli add_network 返回的网络 id
    int      m_polls = 0;        // 通用轮询计数
    int      m_ipAttempts = 0;
    bool     m_triedDhclient = false;
    QString  m_targetSsid;       // 正在尝试连接的 ssid

    QString  m_gateway;
    QString  m_connectedSsid;
    QString  m_connectedIp;
    QString  m_winIface;         // Windows 无线网卡名称

    QList<WifiInfo> m_networks;  // 最近一次扫描结果

    bool     m_dhcpBusy = false; // 后台补跑 DHCP 中
    qint64   m_lastDhcpMs = 0;   // 上次补跑 DHCP 的时间戳（30s 节流）

    // 连接流程的分步状态（回调跨事件循环执行，禁止用引用捕获局部变量，统一放成员里）
    WifiInfo         m_connInfo;      // 本次连接的目标网络
    QString          m_connPwd;       // 本次连接的密码
    QList<int>       m_delIds;        // 待删除的旧网络 id
    QList<QStringList> m_pendingSets; // 待执行的 set_network 命令
    int              m_setIdx = 0;
};

#endif // WIFI_MANAGER_H
