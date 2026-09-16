#include "time/ntpsync.h"

#include <QHostInfo>
#include <QTimer>
#include <QDateTime>
#include <QDebug>

#ifndef Q_OS_WIN
#include <QProcess>
#endif

// 1900-01-01 到 1970-01-01 的秒数（NTP 纪元 -> Unix 纪元）
static const qint64 kNtpEpochDelta = 2208988800LL;

NtpSync::NtpSync(QObject *parent)
    : QObject(parent)
    , m_sock(0)
    , m_timer(0)
    , m_serverIndex(0)
    , m_synced(false)
    , m_inFlight(false)
{
    m_servers << QStringLiteral("ntp.aliyun.com")
              << QStringLiteral("ntp1.aliyun.com")
              << QStringLiteral("cn.pool.ntp.org")
              << QStringLiteral("time.windows.com");

    m_sock = new QUdpSocket(this);
    connect(m_sock, &QUdpSocket::readyRead, this, &NtpSync::onReadyRead);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &NtpSync::onTimeout);
}

void NtpSync::syncNow()
{
    if (m_inFlight) return;
    m_serverIndex = 0;
    tryNextServer();
}

void NtpSync::tryNextServer()
{
    if (m_serverIndex >= m_servers.size()) {
        m_inFlight = false;
        emit failed(QStringLiteral("NTP 服务器均无响应（请检查网络与 UDP 123 出站）"));
        return;
    }
    const QString server = m_servers.at(m_serverIndex);
    m_inFlight = true;
    emit logMessage(QStringLiteral("[ntp] 解析 %1 ...").arg(server));
    // 异步 DNS；结果回来后 sendRequest
    QHostInfo::lookupHost(server, this, SLOT(onLookup(QHostInfo)));
}

void NtpSync::onLookup(const QHostInfo &host)
{
    if (!m_inFlight) return;
    if (host.error() != QHostInfo::NoError || host.addresses().isEmpty()) {
        emit logMessage(QStringLiteral("[ntp] DNS 解析失败，切换下一个服务器"));
        ++m_serverIndex;
        tryNextServer();
        return;
    }
    sendRequest(host.addresses().first());
}

void NtpSync::sendRequest(const QHostAddress &addr)
{
    // LI=0(无告警) VN=4 Mode=3(客户端)，其余字节为 0
    char packet[48];
    for (int i = 0; i < 48; ++i) packet[i] = 0;
    packet[0] = char(0x1B);

    const qint64 written = m_sock->writeDatagram(packet, 48, addr, 123);
    if (written < 0) {
        emit logMessage(QStringLiteral("[ntp] 发送失败(%1)，切换下一个服务器").arg(m_sock->errorString()));
        ++m_serverIndex;
        tryNextServer();
        return;
    }
    m_timer->start(4000);   // 单服务器 4s 超时
}

void NtpSync::onReadyRead()
{
    while (m_sock->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_sock->pendingDatagramSize()));
        m_sock->readDatagram(datagram.data(), datagram.size());
        if (datagram.size() < 48) continue;

        const unsigned char li  = (unsigned char)(datagram.at(0) >> 6);
        const unsigned char mode = (unsigned char)(datagram.at(0) & 0x07);
        if (mode != 4 && mode != 5) continue;      // 只要服务端/广播响应
        if (li == 3) {                              // 告警闰秒，时钟不可信
            emit logMessage(QStringLiteral("[ntp] 服务器报告时钟告警，忽略"));
            continue;
        }

        // Transmit Timestamp：字节 40..43 秒（大端）+ 44..47 小数（忽略）
        const quint32 secs = (quint32)((unsigned char)datagram.at(40) << 24)
                           | ((quint32)((unsigned char)datagram.at(41) & 0xff) << 16)
                           | ((quint32)((unsigned char)datagram.at(42) & 0xff) << 8)
                           |  (quint32)((unsigned char)datagram.at(43) & 0xff);
        if (secs == 0) continue;

        m_timer->stop();
        m_inFlight = false;
        m_serverIndex = 0;
        m_synced = true;
        applyTime(secs);
        return;
    }
}

void NtpSync::onTimeout()
{
    if (!m_inFlight) return;
    emit logMessage(QStringLiteral("[ntp] 响应超时，切换下一个服务器"));
    ++m_serverIndex;
    tryNextServer();
}

void NtpSync::applyTime(quint32 ntpSecs)
{
    const qint64 utcSecs = qint64(ntpSecs) - kNtpEpochDelta;
    if (utcSecs <= 100000000) {   // 明显非法（< 1973 年）
        emit failed(QStringLiteral("NTP 返回时间非法"));
        return;
    }

    const qint64 localSecs = QDateTime::currentMSecsSinceEpoch() / 1000;
    const qint64 drift = utcSecs - localSecs;
    const QString serverTime = QDateTime::fromMSecsSinceEpoch(utcSecs * 1000)
                                   .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));

    if (drift > -1 && drift < 1) {
        emit synced(tr("时间准确（%1）").arg(serverTime), false);
        return;
    }

#ifdef Q_OS_WIN
    // 开发机不写系统时钟，只报告
    emit synced(tr("网络时间 %1（本机偏差 %2 秒，开发机不修改系统时间）")
                .arg(serverTime).arg(drift), false);
#else
    // X6818 终端以 root 运行：busybox/coreutils 的 date 都支持 -s @epoch
    QProcess date;
    date.start(QStringLiteral("date"), QStringList()
               << QStringLiteral("-s") << QStringLiteral("@%1").arg(utcSecs));
    date.waitForFinished(2000);
    if (date.exitStatus() != QProcess::NormalExit || date.exitCode() != 0) {
        // 回退：标准格式字符串
        const QString stamp = QDateTime::fromMSecsSinceEpoch(utcSecs * 1000)
                                  .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        QProcess date2;
        date2.start(QStringLiteral("date"), QStringList()
                    << QStringLiteral("-s") << stamp);
        date2.waitForFinished(2000);
        if (date2.exitStatus() != QProcess::NormalExit || date2.exitCode() != 0) {
            emit failed(tr("校时失败：date -s 需 root 权限（网络时间 %1）").arg(serverTime));
            return;
        }
    }
    emit synced(tr("系统时间已校准为 %1").arg(serverTime), true);
#endif
}
