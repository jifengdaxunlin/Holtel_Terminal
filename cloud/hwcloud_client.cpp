#include "cloud/hwcloud_client.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSslSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QDebug>

// ---------------------------------------------------------------- helpers

static QString sha256Hex(const QString &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256).toHex());
}

static QByteArray hmacSha256(const QByteArray &key, const QByteArray &msg)
{
    return QMessageAuthenticationCode::hash(msg, key, QCryptographicHash::Sha256);
}

static QString hexLower(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex());
}

/** UTC 时间戳 YYYYMMDDTHHMMSSZ（手动拼串，避开 Qt 5.4 的时区格式符差异） */
static QString utcDateTime()
{
    const QDateTime u = QDateTime::currentDateTimeUtc();
    const QDate d = u.date();
    const QTime t = u.time();
    return QStringLiteral("%1%2%3T%4%5%6Z")
        .arg(d.year(), 4, 10, QLatin1Char('0'))
        .arg(d.month(), 2, 10, QLatin1Char('0'))
        .arg(d.day(), 2, 10, QLatin1Char('0'))
        .arg(t.hour(), 2, 10, QLatin1Char('0'))
        .arg(t.minute(), 2, 10, QLatin1Char('0'))
        .arg(t.second(), 2, 10, QLatin1Char('0'));
}

// ---------------------------------------------------------------- impl

HwCloudClient::HwCloudClient(QObject *parent)
    : QObject(parent)
    , m_nam(0)
    , m_reply(0)
    , m_inFlight(false)
    , m_tlsWarned(false)
{
    m_nam = new QNetworkAccessManager(this);

    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &HwCloudClient::pollNow);

    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_reply) m_reply->abort();   // 触发 finished -> onFinished 报错
    });
}

void HwCloudClient::setConfig(const Config &c)
{
    m_cfg = c;
}

bool HwCloudClient::tlsAvailable() const
{
    return QSslSocket::supportsSsl();
}

void HwCloudClient::start(int intervalMs)
{
    if (intervalMs < 2000) intervalMs = 2000;
    m_pollTimer.start(intervalMs);
    pollNow();
}

void HwCloudClient::stop()
{
    m_pollTimer.stop();
}

void HwCloudClient::pollNow()
{
    if (m_inFlight) return;
    if (!m_cfg.valid()) {
        emit requestFailed(QStringLiteral("云配置不完整（区域/项目ID/设备ID/AK/SK）"));
        return;
    }
    if (!tlsAvailable()) {
        if (!m_tlsWarned) {
            m_tlsWarned = true;
            emit requestFailed(QStringLiteral("Qt 缺少 OpenSSL 支持，无法访问 HTTPS 应用侧接口"));
        }
        return;
    }

    const QString host = m_cfg.instancePrefix.isEmpty()
        ? QStringLiteral("iotda.%1.myhuaweicloud.com").arg(m_cfg.region)
        : QStringLiteral("%1.st1.iotda-app.%2.myhuaweicloud.com")
              .arg(m_cfg.instancePrefix, m_cfg.region);
    const QString path = QStringLiteral("/v5/iot/%1/devices/%2/shadow")
                             .arg(m_cfg.projectId, m_cfg.deviceId);

    request(QStringLiteral("GET"), host, path, QByteArray());
}

/**
 * 写设备影子 desired（应用侧 API，与小程序控制 fan/water/gate 同通道）：
 *   PUT /v5/iot/{projectId}/devices/{deviceId}/shadow
 *   body: {"shadow":[{"service_id":"...","desired":{...}}]}
 * 注意：desired 属性需已在产品模型中定义（如房态 dnd/clean），否则平台报错。
 */
void HwCloudClient::setDesired(const QVariantMap &props)
{
    m_pendingDesired = props;
    flushPendingDesired();
}

void HwCloudClient::flushPendingDesired()
{
    if (m_pendingDesired.isEmpty())
        return;
    if (!m_cfg.valid()) {
        m_pendingDesired.clear();
        emit desiredWritten(false, QStringLiteral("云配置不完整（区域/项目ID/设备ID/AK/SK）"));
        return;
    }
    if (m_inFlight)
        return;   // 有在途请求（2s 轮询）：onFinished 结束后会自动补发

    const QString host = m_cfg.instancePrefix.isEmpty()
        ? QStringLiteral("iotda.%1.myhuaweicloud.com").arg(m_cfg.region)
        : QStringLiteral("%1.st1.iotda-app.%2.myhuaweicloud.com")
              .arg(m_cfg.instancePrefix, m_cfg.region);
    const QString path = QStringLiteral("/v5/iot/%1/devices/%2/shadow")
                             .arg(m_cfg.projectId, m_cfg.deviceId);

    QJsonObject desired = QJsonObject::fromVariantMap(m_pendingDesired);
    QJsonObject svc;
    svc.insert(QStringLiteral("service_id"), m_cfg.serviceId);
    svc.insert(QStringLiteral("desired"), desired);
    QJsonArray arr;
    arr.append(svc);
    QJsonObject top;
    top.insert(QStringLiteral("shadow"), arr);
    const QByteArray body = QJsonDocument(top).toJson(QJsonDocument::Compact);

    m_pendingDesired.clear();
    request(QStringLiteral("PUT"), host, path, body);
}

void HwCloudClient::request(const QString &method, const QString &host,
                            const QString &path, const QByteArray &body)
{
    const QString dateTime = utcDateTime();
    const QString date8 = dateTime.left(8);
    const QString payloadHash = sha256Hex(QString::fromUtf8(body));

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(host);
    url.setPath(path);

    QNetworkRequest req(url);
    req.setRawHeader("Content-Type", "application/json");
    req.setRawHeader("X-Sdk-Date", dateTime.toLatin1());
    req.setRawHeader("X-Project-Id", m_cfg.projectId.toUtf8());
    req.setRawHeader("Authorization",
                     buildAuthorization(method, host, path, dateTime, date8, payloadHash).toLatin1());

    m_inFlight = true;
    if (method == QLatin1String("GET"))
        m_reply = m_nam->get(req);
    else if (method == QLatin1String("PUT"))
        m_reply = m_nam->put(req, body);   // 写设备影子 desired（房态等）
    else
        m_reply = m_nam->post(req, body);
    // 板载系统通常没有 CA 证书库：跳过服务器证书校验（应用层已有 AK/SK 签名），
    // 并把具体错误打进日志，便于区分「证书缺失」与「TLS 协议版本不兼容」
    connect(m_reply, &QNetworkReply::sslErrors, this,
            [this](const QList<QSslError> &errs) {
        for (int i = 0; i < errs.size(); ++i)
            qDebug().noquote() << "[cloud][ssl]" << errs.at(i).errorString();
        if (m_reply)
            m_reply->ignoreSslErrors();
    });
    connect(m_reply, &QNetworkReply::finished, this, &HwCloudClient::onFinished);
    m_timeoutTimer.start(12000);
    emit logMessage(QStringLiteral("[cloud] %1 https://%2%3").arg(method, host, path));
}

/**
 * 华为云签名（对齐小程序 huawei.ts signRequest，逐行等价）：
 *   参与签名的头（字母序）：content-type; host; x-project-id; x-sdk-date
 *   规范 URI 结尾补 "/"
 *   实例专属域名（含 ".st1."）→ V11-HMAC-SHA256 派生签名：
 *       info         = 日期8/区域/iotdm
 *       PRK          = HMAC-SHA256(key=AK, msg=SK)
 *       派生密钥      = Hex(HMAC-SHA256(key=PRK, msg=info+0x01))
 *       StringToSign = "V11-HMAC-SHA256\n时间戳\ninfo\nHex(SHA256(规范请求))"
 *       Signature    = Hex(HMAC-SHA256(key=派生密钥hex串, msg=StringToSign))
 *   标准域名 → SDK-HMAC-SHA256：Signature = Hex(HMAC-SHA256(key=SK, msg=StringToSign))
 */
QString HwCloudClient::buildAuthorization(const QString &method, const QString &host,
                                          const QString &path, const QString &dateTime,
                                          const QString &date8, const QString &payloadHash) const
{
    const QString signedHeaders = QStringLiteral("content-type;host;x-project-id;x-sdk-date");
    const QString canonicalHeaders =
        QStringLiteral("content-type:application/json\n"
                       "host:%1\n"
                       "x-project-id:%2\n"
                       "x-sdk-date:%3\n")
            .arg(host, m_cfg.projectId, dateTime);

    // 本客户端的 path 段（项目ID/设备ID）均为 URL 安全字符，无需再编码
    QString uri = path;
    if (uri != QLatin1String("/") && !uri.endsWith(QLatin1Char('/')))
        uri += QLatin1Char('/');

    const QString canonicalRequest = method + QStringLiteral("\n")
        + uri + QStringLiteral("\n")
        + QString() + QStringLiteral("\n")                 // query 为空
        + canonicalHeaders + QStringLiteral("\n")
        + signedHeaders + QStringLiteral("\n")
        + payloadHash;

    QString stringToSign;
    QString authorization;
    const bool derived = host.contains(QStringLiteral(".st1."));

    if (derived) {
        const QString info = date8 + QStringLiteral("/") + m_cfg.region
                           + QStringLiteral("/iotdm");
        stringToSign = QStringLiteral("V11-HMAC-SHA256\n%1\n%2\n%3")
            .arg(dateTime, info, sha256Hex(canonicalRequest));
        // HKDF expand 第 1 轮
        const QByteArray prk = hmacSha256(m_cfg.accessKey.toUtf8(), m_cfg.secretKey.toUtf8());
        QByteArray infoMsg = info.toUtf8();
        infoMsg.append(char(0x01));
        const QString derivedKeyHex = hexLower(hmacSha256(prk, infoMsg));
        const QString signature = hexLower(hmacSha256(derivedKeyHex.toLatin1(),
                                                      stringToSign.toUtf8()));
        authorization = QStringLiteral("V11-HMAC-SHA256 Credential=%1/%2, "
                                       "SignedHeaders=%3, Signature=%4")
                            .arg(m_cfg.accessKey, info, signedHeaders, signature);
    } else {
        stringToSign = QStringLiteral("SDK-HMAC-SHA256\n%1\n%2")
            .arg(dateTime, sha256Hex(canonicalRequest));
        const QString signature = hexLower(hmacSha256(m_cfg.secretKey.toUtf8(),
                                                      stringToSign.toUtf8()));
        authorization = QStringLiteral("SDK-HMAC-SHA256 Access=%1, "
                                       "SignedHeaders=%2, Signature=%3")
                            .arg(m_cfg.accessKey, signedHeaders, signature);
    }
    return authorization;
}

void HwCloudClient::onFinished()
{
    if (!m_reply) { m_inFlight = false; flushPendingDesired(); return; }
    m_timeoutTimer.stop();
    m_inFlight = false;

    QNetworkReply *reply = m_reply;
    m_reply = 0;
    reply->deleteLater();

    // 此处已无在途请求：把轮询期间挂起的 desired 写入立即补发（单点，覆盖所有 return 路径）
    flushPendingDesired();
    const bool isPut = (reply->operation() == QNetworkAccessManager::PutOperation);
    auto failMsg = [this, isPut](const QString &m) {
        if (isPut) emit desiredWritten(false, m);
        else       emit requestFailed(m);
    };

    const QVariant statusAttr =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusAttr.isValid() ? statusAttr.toInt() : 0;
    const QByteArray body = reply->readAll();
    const QString errText = reply->errorString();

    if (reply->error() != QNetworkReply::NoError) {
        if (status == 200) {
            // abort 超时后偶发已收全数据，继续按成功处理
        } else if (status == 0) {
            failMsg(QStringLiteral("网络错误：%1").arg(errText));
            return;
        } else {
            failMsg(QStringLiteral("HTTP %1：%2").arg(status).arg(errText));
            return;
        }
    }

    if (status != 200) {
        QString msg = QStringLiteral("HTTP %1").arg(status);
        const QJsonObject top = QJsonDocument::fromJson(body).object();
        if (!top.isEmpty())
            msg += QStringLiteral("：") + QString::fromUtf8(body.left(160));
        if (status == 401 || status == 403)
            msg += QStringLiteral("（AK/SK 认证失败或无 IoTDA 权限）");
        else if (status == 404)
            msg += QStringLiteral("（项目ID或设备ID不正确）");
        failMsg(msg);
        return;
    }

    // PUT（写 desired）成功：响应体即更新后的影子，只报结果不重复解析
    if (isPut) {
        emit desiredWritten(true, QStringLiteral("房态已同步到云端影子"));
        return;
    }

    // ---- 解析影子 ----
    const QJsonObject top = QJsonDocument::fromJson(body).object();
    const QJsonArray shadow = top.value(QStringLiteral("shadow")).toArray();
    QJsonObject svc;
    for (int i = 0; i < shadow.size(); ++i) {
        const QJsonObject s = shadow.at(i).toObject();
        if (s.value(QStringLiteral("service_id")).toString() == m_cfg.serviceId) {
            svc = s;
            break;
        }
        if (svc.isEmpty()) svc = s;   // 找不到指定服务时取第一个
    }
    const QJsonObject reported = svc.value(QStringLiteral("reported")).toObject();
    const QJsonObject props = reported.value(QStringLiteral("properties")).toObject();
    if (props.isEmpty()) {
        failMsg(QStringLiteral("影子中暂无上报数据（设备可能离线未上报）"));
        return;
    }

    Snapshot s;
    s.temp  = props.value(QStringLiteral("temp")).toDouble();
    s.humi  = props.value(QStringLiteral("humi")).toDouble();
    s.fire  = props.value(QStringLiteral("fire")).toDouble();
    s.gas   = props.value(QStringLiteral("gas")).toDouble();
    s.fan   = props.value(QStringLiteral("fan")).toInt();
    s.water = props.value(QStringLiteral("water")).toInt();
    s.gate  = props.value(QStringLiteral("gate")).toInt();
    s.led   = props.value(QStringLiteral("led")).toInt();
    s.stranger = props.value(QStringLiteral("stranger")).toInt();
    s.mode  = props.value(QStringLiteral("Auto-Manual")).toInt();
    s.hasData = true;
    s.eventTime = reported.value(QStringLiteral("event_time")).toString();
    const QString raw = s.eventTime;
    // event_time 是 UTC（如 20260913T121010Z）：转本地时区显示，否则东八区差 8 小时
    const QDateTime utc = (raw.size() >= 15)
        ? QDateTime::fromString(raw.left(15), QStringLiteral("yyyyMMdd'T'hhmmss"))
        : QDateTime();
    if (utc.isValid()) {
        // 板端系统时区通常为 UTC，toLocalTime() 后仍显示 UTC；按北京时间 UTC+8 固定换算
        QDateTime bj = utc.addSecs(8 * 3600);
        s.eventTimeText = bj.toString(QStringLiteral("MM-dd hh:mm:ss"));
    } else if (raw.size() >= 15) {
        s.eventTimeText = raw.mid(4, 2) + QStringLiteral("-") + raw.mid(6, 2)
                        + QStringLiteral(" ") + raw.mid(9, 2) + QStringLiteral(":")
                        + raw.mid(11, 2) + QStringLiteral(":") + raw.mid(13, 2);
    } else {
        s.eventTimeText = QStringLiteral("--");
    }

    m_snap = s;
    emit shadowUpdated(m_snap);
}
