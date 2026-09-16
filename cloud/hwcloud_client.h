#ifndef HWCLOUD_CLIENT_H
#define HWCLOUD_CLIENT_H

#include <QObject>
#include <QTimer>
#include <QVariant>

class QNetworkAccessManager;
class QNetworkReply;

/*
 * 华为云 IoTDA 应用侧客户端：AK/SK 请求签名 + 轮询设备影子
 *
 * 数据通道与小程序（miniprogram/utils/huawei.ts）完全一致：
 *   GET https://{实例前缀}.st1.iotda-app.{区域}.myhuaweicloud.com
 *       /v5/iot/{项目ID}/devices/{设备ID}/shadow
 *   实例专属域名 → V11-HMAC-SHA256 派生签名（info = 日期/区域/iotdm，HKDF expand 1 轮）
 *   标准域名     → SDK-HMAC-SHA256 标准签名
 *
 * 解析 shadow[].reported.properties：
 *   temp / humi / fire / gas + fan / gate / water / led / stranger / Auto-Manual
 *   与 STM32 固件 MQTT_PublicTopic 上报字段一一对应。
 *
 * 注意：需要 Qt 带 OpenSSL（QSslSocket::supportsSsl()），否则报"协议未知"。
 */
class HwCloudClient : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QString region;          // 如 cn-north-4
        QString projectId;       // 项目 ID
        QString instancePrefix;  // 标准版实例域名前缀（基础版留空）
        QString deviceId;        // 设备 ID
        QString serviceId;       // 产品模型服务 ID
        QString accessKey;       // IAM 访问密钥 AK
        QString secretKey;       // IAM 安全访问密钥 SK

        bool valid() const {
            return !region.trimmed().isEmpty() && !projectId.trimmed().isEmpty()
                && !deviceId.trimmed().isEmpty() && !accessKey.trimmed().isEmpty()
                && !secretKey.trimmed().isEmpty();
        }
    };

    struct Snapshot {
        double temp = 0;      // 温度 ℃
        double humi = 0;      // 湿度 %RH
        double fire = 0;      // 火焰电阻 Ω
        double gas  = 0;      // 气体浓度 %
        int fan = 0;          // 排风扇 0/1
        int water = 0;        // 水泵 0/1
        int gate = 0;         // 仓库门 0/1
        int led = 0;          // 警示声光 0/1
        int stranger = 0;     // 陌生人 0/1
        int mode = 0;         // Auto-Manual 0=自动 1=手动
        bool hasData = false;
        QString eventTime;      // 平台事件时间（原始 20260908T121010Z）
        QString eventTimeText;  // 格式化 MM-dd hh:mm:ss
    };

    explicit HwCloudClient(QObject *parent = 0);

    void setConfig(const Config &c);
    const Snapshot &snapshot() const { return m_snap; }
    bool tlsAvailable() const;

    /** 向设备影子写 desired（如房态 dnd/clean）；属性需已在产品模型中定义 */
    void setDesired(const QVariantMap &props);

public slots:
    void start(int intervalMs = 6000);   // 启动/重启轮询并立即拉取一次
    void stop();
    void pollNow();

signals:
    void shadowUpdated(const HwCloudClient::Snapshot &snap);
    void requestFailed(const QString &why);
    void desiredWritten(bool ok, const QString &msg);
    void logMessage(const QString &text);

private slots:
    void onFinished();

private:
    void request(const QString &method, const QString &host,
                 const QString &path, const QByteArray &body);
    QString buildAuthorization(const QString &method, const QString &host,
                               const QString &path, const QString &dateTime,
                               const QString &date8, const QString &payloadHash) const;
    void flushPendingDesired();

    QNetworkAccessManager *m_nam;
    QTimer                 m_pollTimer;
    QTimer                 m_timeoutTimer;
    Config                 m_cfg;
    Snapshot               m_snap;
    QNetworkReply         *m_reply;
    bool                   m_inFlight;
    bool                   m_tlsWarned;
    QVariantMap            m_pendingDesired;   // 轮询在途时暂存的 desired 写入
};

#endif // HWCLOUD_CLIENT_H
