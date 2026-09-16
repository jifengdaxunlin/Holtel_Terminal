#ifndef NTPSYNC_H
#define NTPSYNC_H

#include <QObject>
#include <QStringList>
#include <QUdpSocket>

class QTimer;
class QHostInfo;

/*
 * NTP 网络校时：联网后自动校准系统时间
 *
 * 流程：QHostInfo 异步解析服务器 -> QUdpSocket 发 48 字节 NTP 请求（UDP 123）
 *      -> 解析响应的 Transmit Timestamp（字节 40..47，1900 纪元）
 *      -> Linux 下 date -s 应用到系统时钟（需 root）；Windows 开发机只校验不修改
 *
 * 特性：
 *   - 服务器链依次回退：ntp.aliyun.com / ntp1.aliyun.com / cn.pool.ntp.org / time.windows.com
 *   - 偏差小于 1 秒不写系统时钟（避免无谓跳变），仍算作同步成功
 *   - synced(info, adjusted) 只有 adjusted=true 才需要提示用户
 */
class NtpSync : public QObject
{
    Q_OBJECT
public:
    explicit NtpSync(QObject *parent = 0);

    bool isSynced() const { return m_synced; }

public slots:
    void syncNow();

signals:
    void synced(const QString &info, bool adjusted);
    void failed(const QString &reason);
    void logMessage(const QString &text);

private slots:
    void onLookup(const QHostInfo &host);
    void onReadyRead();
    void onTimeout();

private:
    void tryNextServer();
    void sendRequest(const QHostAddress &addr);
    void applyTime(quint32 ntpSecs);

    QUdpSocket *m_sock;
    QTimer     *m_timer;       // 单次请求超时
    QStringList m_servers;
    int         m_serverIndex;
    bool        m_synced;      // 本次开机已成功校准过
    bool        m_inFlight;
};

#endif // NTPSYNC_H
