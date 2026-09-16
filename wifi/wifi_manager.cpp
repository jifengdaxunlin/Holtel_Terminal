#include "wifi_manager.h"

#include <QProcess>
#include <QTimer>
#include <QRegularExpression>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <QDebug>

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
namespace {

// 把 dBm 换算成 0~100
int dbmToPercent(int dbm)
{
    if (dbm >= -50) return 100;
    if (dbm <= -100) return 0;
    return (dbm + 100) * 100 / 50;
}

// 从 "ip addr" 输出解析第一个非回环 IPv4
QString inetFromIpOut(const QString &out)
{
    static const QRegularExpression re(QStringLiteral("inet (\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QRegularExpressionMatch m = re.match(l);
        if (m.hasMatch()) {
            const QString ip = m.captured(1);
            if (!ip.startsWith(QLatin1String("127.")))
                return ip;
        }
    }
    return QString();
}

// 从 "ifconfig" 输出解析 IPv4
QString inetFromIfconfigOut(const QString &out)
{
    static const QRegularExpression re(QStringLiteral("inet addr:(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QRegularExpressionMatch m = re.match(l);
        if (m.hasMatch()) {
            const QString ip = m.captured(1);
            if (!ip.startsWith(QLatin1String("127.")))
                return ip;
        }
    }
    return QString();
}

// 从 "ip route" 解析默认网关
QString gatewayFromRoute(const QString &out)
{
    static const QRegularExpression re(QStringLiteral("via (\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        if (!l.trimmed().startsWith(QLatin1String("default")))
            continue;
        const QRegularExpressionMatch m = re.match(l);
        if (m.hasMatch())
            return m.captured(1);
    }
    return QString();
}

// 解析 "wpa_cli status" 中的 key=value
QString statusValue(const QString &out, const QString &key)
{
    const QStringList lines = out.split(QLatin1Char('\n'));
    const QString prefix = key + QLatin1Char('=');
    for (const QString &l : lines) {
        if (l.trimmed().startsWith(prefix))
            return l.trimmed().mid(prefix.size()).trimmed();
    }
    return QString();
}

// wpa_cli 会把非 ASCII 字节转成 \xHH 形式（中文 SSID 会显示成 \xe6\x9d...），
// 这里还原为原始字节再按 UTF-8 解码
QString decodeWpaEscapes(const QString &in)
{
    if (!in.contains(QLatin1String("\\x")))
        return in;
    static const QRegularExpression re(QStringLiteral("\\\\x([0-9A-Fa-f]{2})"));
    QByteArray raw;
    int pos = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        for (int i = pos; i < m.capturedStart(); ++i)
            raw.append(in.at(i).toLatin1());
        raw.append(static_cast<char>(m.captured(1).toInt(0, 16)));
        pos = m.capturedEnd();
    }
    for (int i = pos; i < in.size(); ++i)
        raw.append(in.at(i).toLatin1());
    return QString::fromUtf8(raw);
}

// 简单 XML 转义（Windows netsh 配置 profile 用）
QString xmlEscape(const QString &s)
{
    QString r = s;
    r.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    r.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    r.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    r.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return r;
}

// netsh 解析：当前是否已连接、SSID
bool winConnectedFromInterfaces(const QString &out, QString *ssidOut = 0)
{
    static const QRegularExpression reSsid(QStringLiteral("SSID\\s*:\\s*(.*)"));
    static const QRegularExpression reState(QStringLiteral("(?:状态|State)\\s*:\\s*(.*)"));

    bool conn = false;
    QString ssid;
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QRegularExpressionMatch mState = reState.match(l);
        if (mState.hasMatch()) {
            const QString v = mState.captured(1);
            if (v.contains(QStringLiteral("已连接")) || v.contains(QStringLiteral("connected"), Qt::CaseInsensitive))
                conn = true;
            else if (v.contains(QStringLiteral("已断开")) || v.contains(QStringLiteral("disconnected"), Qt::CaseInsensitive))
                conn = false;
        }
        const QRegularExpressionMatch mSsid = reSsid.match(l);
        if (mSsid.hasMatch()) {
            ssid = mSsid.captured(1).trimmed();
        }
    }
    if (ssidOut)
        *ssidOut = ssid;
    return conn;
}

} // namespace

// ---------------------------------------------------------------------------
// WifiManager
// ---------------------------------------------------------------------------
WifiManager::WifiManager(const QString &iface, QObject *parent)
    : QObject(parent)
    , m_iface(iface)
{
}

// ============================ 命令队列 ============================
void WifiManager::exec(const QString &prog, const QStringList &args,
                       std::function<void(const QString &, const QString &)> done,
                       int postDelayMs,
                       int timeoutMs)
{
    Cmd c;
    c.prog = prog;
    c.args = args;
    c.done = done;
    c.postDelayMs = postDelayMs;
    if (timeoutMs > 0)
        c.timeoutMs = timeoutMs;
    m_cmds.enqueue(c);
    pump();
}

void WifiManager::pump()
{
    if (m_procBusy || m_cmds.isEmpty())
        return;
    m_procBusy = true;
    emit busyChanged(true);

    const Cmd c = m_cmds.dequeue();
    QProcess *p = new QProcess(this);

    // 看门狗：单条命令超过 timeoutMs 强杀。kill 后 finished 照常触发，
    // 队列继续走，界面绝不会因某条命令挂起而永久卡死。
    QTimer *wd = new QTimer(p);
    wd->setSingleShot(true);
    connect(wd, &QTimer::timeout, p, &QProcess::kill);
    wd->start(c.timeoutMs);

    auto onFinish = [this, p, c](int, QProcess::ExitStatus) {
        const QString out = QString::fromLocal8Bit(p->readAllStandardOutput());
        const QString err = QString::fromLocal8Bit(p->readAllStandardError());
        p->deleteLater();
        m_procBusy = false;
        emit busyChanged(false);

        if (c.done)
            c.done(out, err);

        if (c.postDelayMs > 0)
            QTimer::singleShot(c.postDelayMs, this, [this]() { pump(); });
        else
            pump();
    };

    connect(p, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            p, onFinish);

    p->start(c.prog, c.args);
    if (!p->waitForStarted(600)) {
        const QString err = p->errorString();
        p->deleteLater();
        m_procBusy = false;
        emit busyChanged(false);
        if (c.done)
            c.done(QString(), err);
        pump(); // 继续后续命令（多数为收尾，无副作用）
    }
}

// ============================ 对外接口 ============================
void WifiManager::scan()
{
    if (m_connecting) {
        emit operationFailed(QStringLiteral("正在连接中，请稍后再扫描"));
        return;
    }
    m_networks.clear();
    emit scanStarted();
#ifdef Q_OS_WIN
    winScan();
#else
    linuxScan();
#endif
}

void WifiManager::connectTo(const WifiInfo &info, const QString &password)
{
    if (!info.isValid())
        return;
    if (m_connecting) {
        emit operationFailed(QStringLiteral("正在处理其他连接操作，请稍候"));
        return;
    }
    if (m_dhcpBusy) {
        emit operationFailed(QStringLiteral("正在获取 IP 地址，请稍候再试"));
        return;
    }
    if (info.secured && password.isEmpty()) {
        emit operationFailed(QStringLiteral("该网络需要密码，请输入 WiFi 密码"));
        return;
    }
    m_connecting = true;
    m_targetSsid = info.ssid;
    m_connectedIp.clear();
    m_connectedSsid.clear();
    m_polls = 0;
    m_ipAttempts = 0;
    m_triedDhclient = false;

    emit connectStarted(info.ssid);
#ifdef Q_OS_WIN
    winConnect(info, password);
#else
    linuxConnect(info, password);
#endif
}

void WifiManager::disconnectWifi()
{
    if (m_connecting) {
        m_connecting = false; // 取消正在进行的连接尝试
    }
    m_connectedSsid.clear();
    m_connectedIp.clear();
#ifdef Q_OS_WIN
    exec(QStringLiteral("netsh"), QStringList() << QStringLiteral("wlan") << QStringLiteral("disconnect"),
         [this](const QString &, const QString &) {
             emit stateRefreshed(false, QString(), QString());
         });
#else
    // 绕过命令队列直接执行：避免被队列中的长命令（如 udhcpc）堵住导致断开迟迟不生效。
    // disconnect 后 wpa_supplicant 会因已保存网络仍为 enabled 而自动重连，
    // 所以再执行 disable_network all 保持断开状态。
    // 每条命令都挂 10 秒看门狗，wpa_cli 万一挂起也能保证 stateRefreshed 最终发出。
    QProcess *p = new QProcess(this);
    QTimer *wd1 = new QTimer(p);
    wd1->setSingleShot(true);
    connect(wd1, &QTimer::timeout, p, &QProcess::kill);
    wd1->start(10000);
    connect(p, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this, p](int, QProcess::ExitStatus) {
                p->deleteLater();
                QProcess *p2 = new QProcess(this);
                QTimer *wd2 = new QTimer(p2);
                wd2->setSingleShot(true);
                connect(wd2, &QTimer::timeout, p2, &QProcess::kill);
                wd2->start(10000);
                connect(p2, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                        this, [this, p2](int, QProcess::ExitStatus) {
                            p2->deleteLater();
                            emit stateRefreshed(false, QString(), QString());
                        });
                connect(p2, static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
                        this, [this, p2](QProcess::ProcessError) {
                            p2->deleteLater();
                            emit stateRefreshed(false, QString(), QString());
                        });
                p2->start(QStringLiteral("wpa_cli"),
                          QStringList() << QStringLiteral("-i") << m_iface
                                        << QStringLiteral("disable_network") << QStringLiteral("all"));
            });
    connect(p, static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
            this, [this, p](QProcess::ProcessError) {
                p->deleteLater();
                emit stateRefreshed(false, QString(), QString());
            });
    p->start(QStringLiteral("wpa_cli"),
             QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("disconnect"));
#endif
}

void WifiManager::refreshState()
{
    if (m_connecting)
        return; // 连接流程自己会汇报状态
#ifdef Q_OS_WIN
    winReadState();
#else
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("status"),
         [this](const QString &out, const QString &err) {
             if (out.trimmed().isEmpty() && !err.isEmpty()) {
                 m_connectedSsid.clear();
                 m_connectedIp.clear();
                 emit stateRefreshed(false, QString(), QString());
                 emit logMessage(QStringLiteral("wpa_cli 不可用: %1（请确认已运行 wpa_supplicant）").arg(err));
                 return;
             }
             if (statusValue(out, QStringLiteral("wpa_state")) != QStringLiteral("COMPLETED")) {
                 if (!m_connectedSsid.isEmpty() || !m_connectedIp.isEmpty()) {
                     m_connectedSsid.clear();
                     m_connectedIp.clear();
                     emit stateRefreshed(false, QString(), QString());
                 }
                 return;
             }
             m_connectedSsid = decodeWpaEscapes(statusValue(out, QStringLiteral("ssid")));
             linuxReadNetInfo(); // 成功后读取 IP/网关并 emit
         });
#endif
}

void WifiManager::finishConnect(bool ok, const QString &ssid, const QString &ip, const QString &message)
{
    m_connecting = false;
    m_targetSsid.clear();
    if (ok) {
        m_connectedSsid = ssid;
        m_connectedIp = ip;
    } else {
        m_connectedSsid.clear();
        m_connectedIp.clear();
    }
    emit connectFinished(ok, ssid, ip, message);
}

// ============================ Linux 后端 ============================
void WifiManager::linuxScan()
{
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("scan"),
         [this](const QString &out, const QString &err) {
             if ((!out.isEmpty() && out.contains(QStringLiteral("FAIL"))) ||
                 (!err.isEmpty() && err.contains(QStringLiteral("connect")))) {
                 emit operationFailed(QStringLiteral("wpa_cli 扫描失败（请确认 wpa_supplicant 已启动）: ") +
                                      (err.isEmpty() ? out : err));
             }
         },
         1500); // 等 1.5s 让内核完成一轮扫描

    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("scan_results"),
         [this](const QString &out, const QString &) { linuxParseScan(out); });
}

void WifiManager::linuxParseScan(const QString &out)
{
    QList<WifiInfo> list;
    QMap<QString, WifiInfo> best; // ssid -> 信息(同 ssid 取信号最强)

    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.isEmpty())
            continue;
        const QStringList f = l.split(QRegularExpression(QStringLiteral("\\s+")));
        if (f.size() < 4)
            continue;
        if (f.first().count(QLatin1Char(':')) != 5)
            continue; // 第一列必须是 BSSID

        WifiInfo info;
        info.bssid = f.at(0);
        bool ok = false;
        int freq = f.at(1).toInt(&ok);
        Q_UNUSED(freq);
        int dbm = f.at(2).toInt(&ok);
        info.level = dbm;
        info.signal = dbmToPercent(dbm);
        info.security = f.at(3);
        // wpa_cli 的 flags 形如：[WPA2-PSK-CCMP][ESS]（加密）或 [ESS]（开放）
        // 只有出现 WPA/WEP/PSK/SAE/EAP/802.1X 才算需要密码；[ESS]/[IBSS]/[WPS] 不是加密标志
        {
            const QString flags = info.security.toUpper();
            info.secured = flags.contains(QStringLiteral("WPA"))
                        || flags.contains(QStringLiteral("WEP"))
                        || flags.contains(QStringLiteral("PSK"))
                        || flags.contains(QStringLiteral("SAE"))
                        || flags.contains(QStringLiteral("EAP"))
                        || flags.contains(QStringLiteral("802.1X"));
        }

        // SSID 可能在后面的列（含空格时被拆散，重新拼回）
        QStringList ssidParts = f.mid(4);
        info.ssid = decodeWpaEscapes(ssidParts.join(QLatin1Char(' ')).trimmed());
        if (info.ssid.isEmpty() || info.ssid == QLatin1String("[hidden]"))
            continue;

        if (info.security.isEmpty())
            info.security = info.secured ? QStringLiteral("WPA") : QStringLiteral("开放");
        else {
            // 去掉 [ESS]/[IBSS] 等非加密标志和方括号外壳： [WPA2-PSK-CCMP][ESS] -> WPA2-PSK-CCMP
            QString s = info.security;
            s.remove(QStringLiteral("[ESS]"), Qt::CaseInsensitive);
            s.remove(QStringLiteral("[IBSS]"), Qt::CaseInsensitive);
            s.remove(QLatin1Char('[')).remove(QLatin1Char(']'));
            info.security = s.trimmed();
            if (info.security.isEmpty())
                info.security = info.secured ? QStringLiteral("加密") : QStringLiteral("开放");
        }

        QMap<QString, WifiInfo>::iterator it = best.find(info.ssid);
        if (it == best.end() || info.signal > it->signal)
            best.insert(info.ssid, info);
    }

    for (const WifiInfo &v : best)
        list.append(v);

    std::sort(list.begin(), list.end(),
              [](const WifiInfo &a, const WifiInfo &b) { return a.signal > b.signal; });

    m_networks = list;
    emit scanFinished(list);
}

void WifiManager::linuxConnect(const WifiInfo &info, const QString &password)
{
    // 分步状态放到成员里。之前用"递归 lambda 按引用捕获局部 std::function"的写法，
    // 回调在异步命令完成后才执行，局部变量早已销毁，属于悬空引用（未定义行为），
    // 是"断开后连接新网络界面卡死"的根源。
    m_connInfo = info;
    m_connPwd = password;

    // 1) 先断开当前连接，再列出所有已保存网络准备清理
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("disconnect"),
         [this](const QString &, const QString &) { linuxListForCleanup(); });
}

void WifiManager::linuxListForCleanup()
{
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("list_networks"),
         [this](const QString &out, const QString &) {
             m_delIds.clear();
             const QStringList lines = out.split(QLatin1Char('\n'));
             for (const QString &l : lines) {
                 const QString t = l.trimmed();
                 if (t.isEmpty() || t.startsWith(QStringLiteral("network id")))
                     continue;
                 bool ok = false;
                 const int id = t.split(QRegularExpression(QStringLiteral("\\s+"))).first().toInt(&ok);
                 if (ok)
                     m_delIds.append(id);
             }
             linuxDelNext();
         });
}

void WifiManager::linuxDelNext()
{
    if (m_delIds.isEmpty()) {
        linuxAddNetwork();
        return;
    }
    const int id = m_delIds.takeFirst();
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface
                       << QStringLiteral("remove_network") << QString::number(id),
         [this](const QString &, const QString &) { linuxDelNext(); });
}

void WifiManager::linuxAddNetwork()
{
    if (!m_connecting)
        return; // 中途被断开/取消，终止连接流程
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("add_network"),
         [this](const QString &out2, const QString &) {
             bool ok = false;
             const QString firstTok =
                 out2.trimmed().split(QRegularExpression(QStringLiteral("\\s+"))).first();
             const int id = firstTok.toInt(&ok);
             if (!ok) {
                 finishConnect(false, m_connInfo.ssid, QString(),
                               QStringLiteral("wpa_cli add_network 失败: %1").arg(out2.trimmed()));
                 return;
             }
             m_netId = id;

             m_pendingSets.clear();
             m_setIdx = 0;
             m_pendingSets << (QStringList() << QStringLiteral("-i") << m_iface
                                             << QStringLiteral("set_network") << QString::number(id)
                                             << QStringLiteral("ssid")
                                             << QString("\"%1\"").arg(m_connInfo.ssid));
             if (m_connInfo.secured) {
                 m_pendingSets << (QStringList() << QStringLiteral("-i") << m_iface
                                                 << QStringLiteral("set_network") << QString::number(id)
                                                 << QStringLiteral("psk")
                                                 << QString("\"%1\"").arg(m_connPwd));
                 m_pendingSets << (QStringList() << QStringLiteral("-i") << m_iface
                                                 << QStringLiteral("set_network") << QString::number(id)
                                                 << QStringLiteral("key_mgmt") << QStringLiteral("WPA-PSK"));
             } else {
                 m_pendingSets << (QStringList() << QStringLiteral("-i") << m_iface
                                                 << QStringLiteral("set_network") << QString::number(id)
                                                 << QStringLiteral("key_mgmt") << QStringLiteral("NONE"));
             }
             linuxSetNext();
         });
}

void WifiManager::linuxSetNext()
{
    if (!m_connecting)
        return; // 中途被取消
    if (m_setIdx >= m_pendingSets.size()) {
        linuxActivateNetwork();
        return;
    }
    exec(QStringLiteral("wpa_cli"), m_pendingSets.at(m_setIdx),
         [this](const QString &, const QString &) {
             ++m_setIdx;
             linuxSetNext();
         });
}

void WifiManager::linuxActivateNetwork()
{
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface
                       << QStringLiteral("select_network") << QString::number(m_netId),
         [this](const QString &, const QString &) {
             exec(QStringLiteral("wpa_cli"),
                  QStringList() << QStringLiteral("-i") << m_iface
                                << QStringLiteral("enable_network") << QString::number(m_netId),
                  [this](const QString &, const QString &) {
                      exec(QStringLiteral("wpa_cli"),
                           QStringList() << QStringLiteral("-i") << m_iface
                                         << QStringLiteral("reconnect"),
                           [this](const QString &, const QString &) {
                               // 3) 轮询关联结果
                               m_polls = 0;
                               linuxPollConnected();
                           });
                  });
         });
}

void WifiManager::linuxPollConnected()
{
    if (!m_connecting)
        return;
    if (++m_polls > 40) { // 约 28s
        finishConnect(false, m_targetSsid, QString(), QStringLiteral("连接超时：未能关联热点"));
        return;
    }
    exec(QStringLiteral("wpa_cli"),
         QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("status"),
         [this](const QString &out, const QString &err) {
             if (!m_connecting)
                 return;
             if (statusValue(out, QStringLiteral("wpa_state")) == QStringLiteral("COMPLETED")) {
                 emit logMessage(QStringLiteral("已关联热点，正在获取 IP…"));
                 // 若 wpa_supplicant.conf 开启了 update_config=1，则把本次连接持久化(失败忽略)
                 exec(QStringLiteral("wpa_cli"),
                      QStringList() << QStringLiteral("-i") << m_iface << QStringLiteral("save_config"),
                      std::function<void(const QString &, const QString &)>());
                 m_ipAttempts = 0;
                 m_triedDhclient = false;
                 linuxObtainIp(m_targetSsid);
                 return;
             }
             if (out.trimmed().isEmpty() && !err.isEmpty()) {
                 finishConnect(false, m_targetSsid, QString(), QStringLiteral("wpa_cli 不可用: %1").arg(err));
                 return;
             }
             QTimer::singleShot(700, this, [this]() { linuxPollConnected(); });
         });
}

void WifiManager::linuxObtainIp(const QString &ssid)
{
    // -n 拿不到租约就退出  -q 拿到租约后退出  -t 4 -T 2 最多重试 4 次×2 秒
    // 确保 udhcpc 一定结束，不会堵死命令队列
    exec(QStringLiteral("udhcpc"),
         QStringList() << QStringLiteral("-i") << m_iface
                       << QStringLiteral("-n") << QStringLiteral("-q")
                       << QStringLiteral("-t") << QStringLiteral("4")
                       << QStringLiteral("-T") << QStringLiteral("2"),
         [this, ssid](const QString &out, const QString &err) {
             if (!m_connecting)
                 return;
             const bool noUdhcpc = out.isEmpty() && (err.contains(QStringLiteral("not found")) ||
                                                     err.contains(QStringLiteral("No such file")));
             if (noUdhcpc) {
                 m_triedDhclient = true;
                 exec(QStringLiteral("dhclient"), QStringList() << m_iface,
                      [this, ssid](const QString &, const QString &) { linuxWaitIp(ssid); },
                      0, 30000);
             } else {
                 linuxWaitIp(ssid);
             }
         },
         0, 30000);
}

void WifiManager::linuxWaitIp(const QString &ssid)
{
    Q_UNUSED(ssid);
    if (!m_connecting)
        return;
    m_ipAttempts = 0; // 每次读取前清零，由 linuxEmitState 里的等待逻辑计数
    linuxReadNetInfo(); // 读取后若处于连接流程，linuxEmitState 会决定成功/重试/失败
}

void WifiManager::linuxReadNetInfo()
{
    m_connectedIp.clear();
    exec(QStringLiteral("ip"),
         QStringList() << QStringLiteral("addr") << QStringLiteral("show") << QStringLiteral("dev") << m_iface,
         [this](const QString &out, const QString &err) {
             QString ip = inetFromIpOut(out);
             if (ip.isEmpty() && !err.isEmpty()) {
                 // ip 命令不可用，退回 ifconfig
                 exec(QStringLiteral("ifconfig"), QStringList() << m_iface,
                      [this](const QString &out2, const QString &) {
                          m_connectedIp = inetFromIfconfigOut(out2);
                          linuxReadGateway();
                      });
                 return;
             }
             m_connectedIp = ip;
             linuxReadGateway();
         });
}

void WifiManager::linuxReadGateway()
{
    exec(QStringLiteral("ip"), QStringList() << QStringLiteral("route"),
         [this](const QString &out, const QString &) {
             const QString gw = gatewayFromRoute(out);
             if (!gw.isEmpty() && gw != m_gateway) {
                 m_gateway = gw;
                 emit gatewayChanged(gw);
             }
             linuxEmitState();
         });
}

void WifiManager::linuxEmitState()
{
    if (!m_connecting) {
        const bool connected = !m_connectedSsid.isEmpty();

        // 已关联但没有 IP：自动补跑一次 udhcpc（30 秒节流），实现"关联即上网"
        if (connected && m_connectedIp.isEmpty() && !m_dhcpBusy) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_lastDhcpMs == 0 || now - m_lastDhcpMs > 30000) {
                m_lastDhcpMs = now;
                m_dhcpBusy = true;
                emit logMessage(QStringLiteral("检测到已关联但无 IP，自动执行 udhcpc 获取地址…"));
                exec(QStringLiteral("udhcpc"),
                     QStringList() << QStringLiteral("-i") << m_iface
                                   << QStringLiteral("-n") << QStringLiteral("-q")
                                   << QStringLiteral("-t") << QStringLiteral("4")
                                   << QStringLiteral("-T") << QStringLiteral("2"),
                     [this](const QString &, const QString &) {
                         m_dhcpBusy = false;
                         linuxReadNetInfo(); // 读取后由 linuxEmitState 上报最终状态
                     },
                     0, 30000);
                return;
            }
        }

        emit stateRefreshed(connected, m_connectedSsid, m_connectedIp);
        return;
    }

    // 处于连接流程：根据 IP 是否拿到决定下一步
    const QString ssid = m_targetSsid;
    if (!m_connectedIp.isEmpty()) {
        finishConnect(true, ssid, m_connectedIp, QStringLiteral("连接成功"));
        return;
    }
    if (++m_ipAttempts > 12) { // 约 12 * 0.8s
        if (!m_triedDhclient) {
            m_triedDhclient = true;
            m_ipAttempts = 0;
            exec(QStringLiteral("dhclient"), QStringList() << m_iface,
                 [this, ssid](const QString &, const QString &) { linuxWaitIp(ssid); },
                 0, 30000);
            return;
        }
        finishConnect(false, ssid, QString(), QStringLiteral("已关联热点，但获取 IP 失败（请检查 DHCP 服务）"));
        return;
    }
    QTimer::singleShot(800, this, [this, ssid]() { linuxWaitIp(ssid); });
}

// ============================ Windows 后端 ============================
void WifiManager::winScan()
{
    exec(QStringLiteral("netsh"),
         QStringList() << QStringLiteral("wlan") << QStringLiteral("show") << QStringLiteral("networks")
                       << QStringLiteral("mode=bssid"),
         [this](const QString &out, const QString &err) {
             if (out.trimmed().isEmpty()) {
                 emit operationFailed(QStringLiteral("netsh 扫描失败: ") + err);
                 return;
             }
             winParseScan(out);
         });
}

void WifiManager::winParseScan(const QString &out)
{
    static const QRegularExpression reSsid(QStringLiteral("^\\s*SSID\\s+\\d+\\s*:\\s*(.*)$"));
    static const QRegularExpression reAuth(QStringLiteral("(?:身份验证|Authentication)\\s*:\\s*(.*)$"));
    static const QRegularExpression reSignal(QStringLiteral("(?:信号|Signal)\\s*:\\s*(\\d+)%"));

    QList<WifiInfo> list;
    WifiInfo cur;
    bool haveCur = false;

    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QRegularExpressionMatch mSsid = reSsid.match(l);
        if (mSsid.hasMatch()) {
            if (haveCur && cur.isValid())
                list.append(cur);
            cur = WifiInfo();
            cur.ssid = mSsid.captured(1).trimmed();
            haveCur = true;
            continue;
        }
        if (!haveCur)
            continue;
        const QRegularExpressionMatch mAuth = reAuth.match(l);
        if (mAuth.hasMatch()) {
            cur.security = mAuth.captured(1).trimmed();
            const QString a = cur.security;
            cur.secured = !(a.contains(QStringLiteral("开放")) || a.contains(QStringLiteral("Open"), Qt::CaseInsensitive));
            continue;
        }
        const QRegularExpressionMatch mSig = reSignal.match(l);
        if (mSig.hasMatch()) {
            const int pct = mSig.captured(1).toInt();
            if (pct > cur.signal)
                cur.signal = pct;
        }
    }
    if (haveCur && cur.isValid())
        list.append(cur);

    // 去重（按 SSID 保信号最强）
    QMap<QString, WifiInfo> best;
    for (const WifiInfo &w : list) {
        QMap<QString, WifiInfo>::iterator it = best.find(w.ssid);
        if (it == best.end() || w.signal > it->signal)
            best.insert(w.ssid, w);
    }
    list.clear();
    for (const WifiInfo &w : best)
        list.append(w);
    std::sort(list.begin(), list.end(),
              [](const WifiInfo &a, const WifiInfo &b) { return a.signal > b.signal; });

    m_networks = list;
    emit scanFinished(list);
}

void WifiManager::winConnect(const WifiInfo &info, const QString &password)
{
    if (info.security.contains(QStringLiteral("WEP")) || info.security.contains(QStringLiteral("WPA3"))) {
        finishConnect(false, info.ssid, QString(),
                      QStringLiteral("当前测试工具暂不支持 %1，请用系统自带连接。").arg(info.security));
        return;
    }
    if (info.secured && password.size() < 8) {
        finishConnect(false, info.ssid, QString(), QStringLiteral("WPA 密码至少 8 位"));
        return;
    }

    // 生成 netsh profile XML
    const QString fileName = QDir::temp().filePath(QStringLiteral("wifitest_profile.xml"));
    QString xml = QStringLiteral("<?xml version=\"1.0\"?>\n"
                                 "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
                                 "  <name>%1</name>\n"
                                 "  <SSIDConfig>\n"
                                 "    <SSID><name>%1</name></SSID>\n"
                                 "    <nonBroadcast>false</nonBroadcast>\n"
                                 "  </SSIDConfig>\n"
                                 "  <connectionType>ESS</connectionType>\n"
                                 "  <connectionMode>manual</connectionMode>\n"
                                 "  <MSM>\n"
                                 "    <security>\n");
    if (info.secured) {
        xml += QStringLiteral("      <authEncryption>\n"
                              "        <authentication>WPA2PSK</authentication>\n"
                              "        <encryption>AES</encryption>\n"
                              "        <useOneX>false</useOneX>\n"
                              "      </authEncryption>\n"
                              "      <sharedKey>\n"
                              "        <keyType>passPhrase</keyType>\n"
                              "        <protected>false</protected>\n"
                              "        <keyMaterial>%2</keyMaterial>\n"
                              "      </sharedKey>\n");
    } else {
        xml += QStringLiteral("      <authEncryption>\n"
                              "        <authentication>open</authentication>\n"
                              "        <encryption>none</encryption>\n"
                              "        <useOneX>false</useOneX>\n"
                              "      </authEncryption>\n");
    }
    xml += QStringLiteral("    </security>\n"
                          "  </MSM>\n"
                          "</WLANProfile>\n");
    xml = xml.arg(xmlEscape(info.ssid)).arg(xmlEscape(password));

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishConnect(false, info.ssid, QString(), QStringLiteral("无法写入临时配置文件"));
        return;
    }
    f.write(xml.toUtf8());
    f.close();

    // 添加 profile 并连接
    exec(QStringLiteral("netsh"),
         QStringList() << QStringLiteral("wlan") << QStringLiteral("add") << QStringLiteral("profile")
                       << (QStringLiteral("filename=\"") + fileName + QLatin1Char('"'))
                       << QStringLiteral("user=current"),
         [this, info, fileName](const QString &out, const QString &err) {
             if (out.contains(QStringLiteral("already exists"), Qt::CaseInsensitive) ||
                 err.contains(QStringLiteral("already exists"), Qt::CaseInsensitive)) {
                 // profile 已存在也可以继续
             } else if (!out.contains(QStringLiteral("added"), Qt::CaseInsensitive) &&
                        out.contains(QStringLiteral("错误"), Qt::CaseInsensitive)) {
                 finishConnect(false, info.ssid, QString(), QStringLiteral("netsh 添加配置失败: %1").arg(err));
                 return;
             }
             exec(QStringLiteral("netsh"),
                  QStringList() << QStringLiteral("wlan") << QStringLiteral("connect")
                                << (QStringLiteral("name=\"") + info.ssid + QLatin1Char('"')),
                  [this, info](const QString &, const QString &) {
                      m_polls = 0;
                      winPollConnected(info.ssid);
                  });
         });
}

void WifiManager::winPollConnected(const QString &ssid)
{
    if (!m_connecting)
        return;
    if (++m_polls > 40) {
        finishConnect(false, ssid, QString(), QStringLiteral("连接超时（请检查密码是否正确）"));
        return;
    }
    exec(QStringLiteral("netsh"),
         QStringList() << QStringLiteral("wlan") << QStringLiteral("show") << QStringLiteral("interfaces"),
         [this, ssid](const QString &out, const QString &) {
             if (!m_connecting)
                 return;
             QString curSsid;
             const bool conn = winConnectedFromInterfaces(out, &curSsid);
             if (conn && curSsid.compare(ssid, Qt::CaseInsensitive) == 0) {
                 m_connectedSsid = ssid;
                 winReadIpDone(ssid); // 成功后读 IP 并结束
                 return;
             }
             QTimer::singleShot(1000, this, [this, ssid]() { winPollConnected(ssid); });
         });
}

void WifiManager::winReadIpDone(const QString &ssid)
{
    exec(QStringLiteral("netsh"),
         QStringList() << QStringLiteral("interface") << QStringLiteral("ipv4")
                       << QStringLiteral("show") << QStringLiteral("addresses")
                       << (QStringLiteral("name=\"") + m_winIface + QLatin1Char('"')),
         [this, ssid](const QString &out, const QString &) {
             static const QRegularExpression reIp(QStringLiteral("(?:IP 地址|IP Address)\\s*:\\s*(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})"));
             static const QRegularExpression reGw(QStringLiteral("(?:默认网关|Default Gateway)\\s*:\\s*(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})"));
             QString ip, gw;
             const QStringList lines = out.split(QLatin1Char('\n'));
             for (const QString &l : lines) {
                 const QRegularExpressionMatch m1 = reIp.match(l);
                 if (m1.hasMatch() && ip.isEmpty())
                     ip = m1.captured(1);
                 const QRegularExpressionMatch m2 = reGw.match(l);
                 if (m2.hasMatch() && gw.isEmpty())
                     gw = m2.captured(1);
             }
             if (!gw.isEmpty())
                 m_gateway = gw;
             m_connectedIp = ip;
             if (m_connecting)
                 finishConnect(true, ssid, ip, QStringLiteral("连接成功"));
             else
                 emit stateRefreshed(true, ssid, ip);
         });
}

void WifiManager::winReadState()
{
    exec(QStringLiteral("netsh"),
         QStringList() << QStringLiteral("wlan") << QStringLiteral("show") << QStringLiteral("interfaces"),
         [this](const QString &out, const QString &err) {
             if (out.trimmed().isEmpty() && !err.isEmpty()) {
                 m_connectedSsid.clear();
                 m_connectedIp.clear();
                 emit stateRefreshed(false, QString(), QString());
                 emit logMessage(QStringLiteral("netsh 不可用: %1").arg(err));
                 return;
             }
             QString curSsid;
             const bool conn = winConnectedFromInterfaces(out, &curSsid);
             if (!conn) {
                 if (!m_connectedSsid.isEmpty() || !m_connectedIp.isEmpty()) {
                     m_connectedSsid.clear();
                     m_connectedIp.clear();
                     emit stateRefreshed(false, QString(), QString());
                 }
                 return;
             }
             m_connectedSsid = curSsid;
             m_winIface.clear();

             // 顺带解析无线网卡名称（读取 IP 用）
             static const QRegularExpression reName(QStringLiteral("(?:名称|Name)\\s*:\\s*(.+)"));
             static const QRegularExpression reState(QStringLiteral("(?:状态|State)\\s*:\\s*(.+)"));
             const QStringList lines = out.split(QLatin1Char('\n'));
             QString lastName;
             for (const QString &l : lines) {
                 const QRegularExpressionMatch mName = reName.match(l);
                 if (mName.hasMatch())
                     lastName = mName.captured(1).trimmed();
                 const QRegularExpressionMatch mState = reState.match(l);
                 if (mState.hasMatch() &&
                     (mState.captured(1).contains(QStringLiteral("已连接")) ||
                      mState.captured(1).contains(QStringLiteral("connected"), Qt::CaseInsensitive)))
                     m_winIface = lastName;
             }
             winReadIpDone(curSsid);
         });
}
