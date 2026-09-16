#include "cloud/hwcloud_settings_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>
#include <QClipboard>
#include <QApplication>
#include <QDebug>
#include "wifi/softkeyboard.h"
#include "hwcloud_client.h"
#include <QColor>
#include <cstring>

// ===========================================================================
// 轻量 SHA-256 / HMAC-SHA256（无外部依赖，供生成 MQTT 登录密码与签名测试）
// 与华为云 IoTDA 设备接入规则一致：password = hex(HMAC-SHA256(secret, clientId))
// ===========================================================================
namespace {

const quint32 SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline quint32 rotr(quint32 x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256(const unsigned char *msg, size_t len, unsigned char out[32])
{
    // 1. 填充
    const size_t bitLenHi = (size_t)(len >> 29) & 0xffffffffu;
    const size_t bitLenLo = (size_t)(len << 3) & 0xffffffffu;
    size_t padLen = ((len + 8) / 64 + 1) * 64;
    QByteArray padded((int)padLen, char(0));
    if (!padded.isEmpty())
        std::memcpy(padded.data(), msg, len);
    padded[(int)len] = char(0x80);
    // 末尾 8 字节：消息位长（大端）
    padded[(int)padLen - 8] = char((bitLenHi >> 24) & 0xff);
    padded[(int)padLen - 7] = char((bitLenHi >> 16) & 0xff);
    padded[(int)padLen - 6] = char((bitLenHi >> 8) & 0xff);
    padded[(int)padLen - 5] = char(bitLenHi & 0xff);
    padded[(int)padLen - 4] = char((bitLenLo >> 24) & 0xff);
    padded[(int)padLen - 3] = char((bitLenLo >> 16) & 0xff);
    padded[(int)padLen - 2] = char((bitLenLo >> 8) & 0xff);
    padded[(int)padLen - 1] = char(bitLenLo & 0xff);

    quint32 h[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };

    const unsigned char *p = reinterpret_cast<const unsigned char *>(padded.constData());
    for (size_t off = 0; off < padLen; off += 64) {
        quint32 w[64];
        for (int i = 0; i < 16; ++i) {
            const unsigned char *b = p + off + (size_t)i * 4;
            w[i] = ((quint32)b[0] << 24) | ((quint32)b[1] << 16)
                 | ((quint32)b[2] << 8) | (quint32)b[3];
        }
        for (int i = 16; i < 64; ++i) {
            quint32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            quint32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        quint32 a = h[0], b = h[1], c = h[2], d = h[3];
        quint32 e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            quint32 S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            quint32 ch = (e & f) ^ (~e & g);
            quint32 t1 = hh + S1 + ch + SHA256_K[i] + w[i];
            quint32 S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            quint32 maj = (a & b) ^ (a & c) ^ (b & c);
            quint32 t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = char((h[i] >> 24) & 0xff);
        out[i * 4 + 1] = char((h[i] >> 16) & 0xff);
        out[i * 4 + 2] = char((h[i] >> 8) & 0xff);
        out[i * 4 + 3] = char(h[i] & 0xff);
    }
}

QByteArray hmacSha256(const QByteArray &key, const QByteArray &msg)
{
    QByteArray k = key;
    if (k.size() > 64)
        k = QByteArray(32, char(0));   // 先占位，下面重算
    unsigned char digest[32];
    if (key.size() > 64) {
        sha256(reinterpret_cast<const unsigned char *>(key.constData()),
               (size_t)key.size(), digest);
        k = QByteArray(reinterpret_cast<const char *>(digest), 32);
    }

    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        unsigned char kv = (i < k.size()) ? (unsigned char)k.at(i) : 0;
        ipad[i] = kv ^ 0x36;
        opad[i] = kv ^ 0x5c;
    }

    QByteArray inner(64 + msg.size(), char(0));
    std::memcpy(inner.data(), ipad, 64);
    std::memcpy(inner.data() + 64, msg.constData(), (size_t)msg.size());
    unsigned char ihash[32];
    sha256(reinterpret_cast<const unsigned char *>(inner.constData()),
           (size_t)inner.size(), ihash);

    QByteArray outer(64 + 32, char(0));
    std::memcpy(outer.data(), opad, 64);
    std::memcpy(outer.data() + 64, ihash, 32);
    unsigned char ohash[32];
    sha256(reinterpret_cast<const unsigned char *>(outer.constData()),
           (size_t)outer.size(), ohash);
    return QByteArray(reinterpret_cast<const char *>(ohash), 32);
}

QString toHexLower(const QByteArray &data)
{
    static const char hex[] = "0123456789abcdef";
    QString s;
    s.reserve(data.size() * 2);
    for (int i = 0; i < data.size(); ++i) {
        unsigned char c = (unsigned char)data.at(i);
        s.append(QChar(hex[c >> 4]));
        s.append(QChar(hex[c & 0xf]));
    }
    return s;
}

/** 补零两位 */
QString pad2(int n)
{
    return n < 10 ? QStringLiteral("0") + QString::number(n) : QString::number(n);
}

} // namespace

// ===========================================================================
// MQTT 3.1.1 CONNECT 握手测试器（QTcpSocket 实现，无需外部 MQTT 库）
// ===========================================================================
class MqttTester : public QObject
{
    Q_OBJECT
public:
    explicit MqttTester(QObject *parent = 0)
        : QObject(parent), m_sock(0), m_timer(0) {}

    void start(const QString &host, int port,
               const QString &clientId, const QString &username,
               const QString &password)
    {
        m_host = host;
        m_port = port;
        m_clientId = clientId.toUtf8();
        m_username = username.toUtf8();
        m_password = password.toUtf8();   // 华为云要求密码为 64 位十六进制字符串本身，不得转回原始字节
        m_rx.clear();

        m_sock = new QTcpSocket(this);
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &MqttTester::onTimeout);
        connect(m_sock, &QTcpSocket::connected, this, &MqttTester::onConnected);
        connect(m_sock, &QTcpSocket::readyRead, this, &MqttTester::onReadyRead);
        connect(m_sock,
                static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error),
                this, &MqttTester::onSocketError);
        m_timer->start(8000);
        emit logMessage(QString::fromUtf8("正在连接 %1:%2 ...").arg(host).arg(port));
        m_sock->connectToHost(host, (quint16)port);
    }

signals:
    void finished(bool ok, const QString &message);
    void logMessage(const QString &text);

private slots:
    void onConnected()
    {
        m_timer->start(6000);   // 连接后等 CONNACK 的超时
        const QByteArray packet = buildConnectPacket();
        emit logMessage(QString::fromUtf8("已建立 TCP 连接，发送 MQTT CONNECT（%1 字节）...")
                        .arg(packet.size()));
        m_sock->write(packet);
    }

    void onReadyRead()
    {
        m_rx.append(m_sock->readAll());
        // CONNACK 最小 4 字节：0x20 0x02 0x00 0x00
        if (m_rx.size() >= 4 && (unsigned char)m_rx.at(0) == 0x20) {
            const unsigned char code = (unsigned char)m_rx.at(3);
            m_timer->stop();
            // 发 DISCONNECT 后关闭连接
            m_sock->write(QByteArray::fromHex("e000"));
            m_sock->flush();
            m_sock->close();
            switch (code) {
            case 0:
                emit finished(true, QString::fromUtf8(
                    "MQTT 连接成功：服务端返回 CONNACK 0（用户名/密码正确，设备已通过认证）"));
                break;
            case 4:
                emit finished(false, QString::fromUtf8(
                    "MQTT 认证失败：CONNACK 4（用户名或密码错误——请核对设备ID、设备密钥与生成的密码）"));
                break;
            case 5:
                emit finished(false, QString::fromUtf8(
                    "MQTT 认证失败：CONNACK 5（未授权——设备密钥/HMAC 签名不正确，或设备已在别处登录）"));
                break;
            default:
                emit finished(false, QString::fromUtf8(
                    "MQTT 连接被拒绝：CONNACK %1").arg((int)code));
                break;
            }
        } else if (m_rx.size() > 64) {
            // 收到非预期数据
            m_timer->stop();
            m_sock->close();
            emit finished(false, QString::fromUtf8("收到非 MQTT 响应（服务端可能不是 IoTDA 设备接入端点）"));
        }
    }

    void onSocketError(QAbstractSocket::SocketError)
    {
        if (!m_sock) return;
        m_timer->stop();
        const QString err = m_sock->errorString();
        m_sock->close();
        emit finished(false, QString::fromUtf8("TCP 连接失败：%1").arg(err));
    }

    void onTimeout()
    {
        m_timer->stop();
        if (m_sock) m_sock->close();
        emit finished(false, QString::fromUtf8("连接超时：请检查地址、端口与网络（1883 端口需放行）"));
    }

private:
    QByteArray buildConnectPacket() const
    {
        QByteArray vh;
        vh.append(char(0x00)).append(char(0x04));   // 协议名长度
        vh.append("MQTT");                           // 协议名
        vh.append(char(0x04));                       // 协议级别 3.1.1
        vh.append(char(0xC2));                       // 标志：CleanSession+用户名+密码
        vh.append(char(0x00)).append(char(0x3C));    // KeepAlive 60s

        QByteArray payload;
        payload.append(utf8Field(m_clientId));
        payload.append(utf8Field(m_username));
        // 密码是 64 位十六进制字符串（HMAC 结果的明文 hex），直接按长度前缀发送
        payload.append(char((m_password.size() >> 8) & 0xff));
        payload.append(char(m_password.size() & 0xff));
        payload.append(m_password);

        QByteArray pkt;
        pkt.append(char(0x10));   // CONNECT
        pkt.append(encodeRemainingLength(vh.size() + payload.size()));
        pkt.append(vh);
        pkt.append(payload);
        return pkt;
    }

    static QByteArray utf8Field(const QByteArray &utf8)
    {
        QByteArray f;
        f.append(char((utf8.size() >> 8) & 0xff));
        f.append(char(utf8.size() & 0xff));
        f.append(utf8);
        return f;
    }

    static QByteArray encodeRemainingLength(int len)
    {
        QByteArray out;
        do {
            unsigned char d = (unsigned char)(len % 128);
            len /= 128;
            if (len > 0) d |= 0x80;
            out.append(char(d));
        } while (len > 0);
        return out;
    }

    QTcpSocket *m_sock;
    QTimer     *m_timer;
    QString     m_host;
    int         m_port;
    QByteArray  m_clientId;
    QByteArray  m_username;
    QByteArray  m_password;
    QByteArray  m_rx;
};


// ===========================================================================
// HwCloudSettingsWidget
// ===========================================================================

HwCloudSettingsWidget::HwCloudSettingsWidget(QWidget *parent)
    : QFrame(parent)
    , m_cardTitle(0), m_cardSub(0), m_statusText(0), m_msgLabel(0)
    , m_region(0), m_projectId(0), m_prefix(0), m_deviceId(0), m_secret(0)
    , m_clientId(0), m_serviceId(0), m_port(0), m_accessKey(0), m_secretKey(0)
    , m_password(0)
    , m_btnGenClientId(0), m_btnGenPwd(0), m_btnCopyPwd(0)
    , m_btnSave(0), m_btnTest(0), m_btnReset(0)
    , m_tester(0), m_probe(0), m_keyboard(0), m_busy(false)
    , m_mqttDone(false), m_mqttOk(false), m_probeDone(false), m_probeOk(false)
{
    // 默认深海蓝主题；MainWindow::applyTheme() 在 setupIntegrationWidgets 后统一覆盖
    m_dark     = true;
    m_accent   = QStringLiteral("#00d4ff");
    m_accent2  = QStringLiteral("#0077be");
    m_card0    = QStringLiteral("#0f2a52");
    m_card1    = QStringLiteral("#081a33");
    m_border   = QStringLiteral("#1a3a6b");
    m_textMain = QStringLiteral("#ffffff");
    m_textSub  = QStringLiteral("#8aa4c8");

    buildUi();
    applyStyle();
    loadConfig();

    m_tester = new MqttTester(this);
    connect(m_tester, &MqttTester::finished, this, &HwCloudSettingsWidget::onTestFinished);
    connect(m_tester, &MqttTester::logMessage, this, &HwCloudSettingsWidget::logMessage);

    // 应用侧影子 API 探针（一键测试第二路：验证 AK/SK 签名 + HTTPS 数据通道）
    m_probe = new HwCloudClient(this);
    connect(m_probe, &HwCloudClient::shadowUpdated,
            this, &HwCloudSettingsWidget::onProbeShadowUpdated);
    connect(m_probe, &HwCloudClient::requestFailed,
            this, &HwCloudSettingsWidget::onProbeFailed);
}

HwCloudSettingsWidget::~HwCloudSettingsWidget()
{
}

HwCloudSettingsWidget::Config HwCloudSettingsWidget::config() const
{
    Config c;
    c.region         = m_region->text().trimmed();
    c.projectId      = m_projectId->text().trimmed();
    c.instancePrefix = m_prefix->text().trimmed();
    c.deviceId       = m_deviceId->text().trimmed();
    c.secret         = m_secret->text().trimmed();
    c.clientId       = m_clientId->text().trimmed();
    c.serviceId      = m_serviceId->text().trimmed();
    c.accessKey      = m_accessKey->text().trimmed();
    c.secretKey      = m_secretKey->text().trimmed();
    bool ok = false;
    const int port = m_port->text().toInt(&ok);
    c.port = (ok && port > 0 && port < 65536) ? port : 1883;
    return c;
}

QString HwCloudSettingsWidget::mqttHost() const
{
    const Config c = config();
    if (!c.instancePrefix.isEmpty())
        return QStringLiteral("%1.st1.iotda-device.%2.myhuaweicloud.com")
            .arg(c.instancePrefix, c.region);
    return QStringLiteral("iotda-device.%1.myhuaweicloud.com").arg(c.region);
}

void HwCloudSettingsWidget::buildUi()
{
    setObjectName(QStringLiteral("hwCloudCard"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    m_cardTitle = new QLabel(tr("华为云 / MQTT 连接"), this);
    m_cardTitle->setObjectName(QStringLiteral("hwCloudCardTitle"));
    m_cardSub = new QLabel(
        tr("配置终端接入华为云 IoTDA 的 MQTT 登录凭证（与小程序/固件参数一致），并可测试连通性；"
           "注意「测试连接」会以该设备ID登录，可能与下位机互踢（下位机心跳会自动重连）"),
        this);
    m_cardSub->setObjectName(QStringLiteral("hwCloudCardSub"));
    m_cardSub->setWordWrap(true);

    m_statusText = new QLabel(this);
    m_statusText->setObjectName(QStringLiteral("hwCloudCardStatus"));
    m_statusText->setWordWrap(true);

    QGridLayout *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    int row = 0;

    auto addField = [&](const QString &label, QLineEdit **edit, const QString &placeholder,
                        bool password = false) {
        QLabel *lbl = new QLabel(label, this);
        lbl->setObjectName(QStringLiteral("hwCloudFieldLabel"));
        *edit = new QLineEdit(this);
        (*edit)->setObjectName(QStringLiteral("hwCloudEdit"));
        (*edit)->setPlaceholderText(placeholder);
        if (password)
            (*edit)->setEchoMode(QLineEdit::Password);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(*edit, row, 1);
        ++row;
    };

    addField(tr("区域"), &m_region, QStringLiteral("cn-north-4"));
    addField(tr("项目ID"), &m_projectId, QStringLiteral("控制台-我的凭证-项目列表"));
    addField(tr("实例前缀"), &m_prefix, QStringLiteral("标准版前缀，如 ec2e895127；基础版留空"));
    addField(tr("设备ID(用户名)"), &m_deviceId, QStringLiteral("IoTDA 控制台设备详情页"));
    addField(tr("设备密钥"), &m_secret, QStringLiteral("设备详情页的密钥/重置密钥"), true);

    // ClientID 行：输入框 + 生成按钮
    QLabel *cidLbl = new QLabel(tr("ClientID"), this);
    cidLbl->setObjectName(QStringLiteral("hwCloudFieldLabel"));
    m_clientId = new QLineEdit(this);
    m_clientId->setObjectName(QStringLiteral("hwCloudEdit"));
    m_clientId->setPlaceholderText(tr("点「生成」自动生成（签名类型0）"));
    m_btnGenClientId = new QPushButton(tr("生成"), this);
    m_btnGenClientId->setObjectName(QStringLiteral("hwCloudBtnSmall"));
    QHBoxLayout *cidRow = new QHBoxLayout;
    cidRow->setSpacing(6);
    cidRow->addWidget(m_clientId, 1);
    cidRow->addWidget(m_btnGenClientId);
    grid->addWidget(cidLbl, row, 0);
    grid->addLayout(cidRow, row, 1);
    ++row;

    addField(tr("服务ID"), &m_serviceId, QStringLiteral("产品模型服务ID"));
    addField(tr("端口"), &m_port, QStringLiteral("1883"));
    // 应用侧（HTTPS 查影子）凭证：环境监测/设备控制的真实数据走这两个密钥签名
    addField(tr("访问密钥AK"), &m_accessKey, QStringLiteral("IAM 访问密钥 AK"));
    addField(tr("安全密钥SK"), &m_secretKey, QStringLiteral("IAM 安全访问密钥 SK"), true);

    // MQTT 登录密码行：只读显示 + 生成/复制
    QLabel *pwdLbl = new QLabel(tr("MQTT密码"), this);
    pwdLbl->setObjectName(QStringLiteral("hwCloudFieldLabel"));
    m_password = new QLineEdit(this);
    m_password->setObjectName(QStringLiteral("hwCloudEdit"));
    m_password->setReadOnly(true);
    m_password->setPlaceholderText(tr("点「生成密码」= HMAC-SHA256(设备密钥, ClientID)"));
    m_btnGenPwd = new QPushButton(tr("生成密码"), this);
    m_btnGenPwd->setObjectName(QStringLiteral("hwCloudBtnSmall"));
    m_btnCopyPwd = new QPushButton(tr("复制"), this);
    m_btnCopyPwd->setObjectName(QStringLiteral("hwCloudBtnSmall"));
    m_btnCopyPwd->setEnabled(false);
    QHBoxLayout *pwdRow = new QHBoxLayout;
    pwdRow->setSpacing(6);
    pwdRow->addWidget(m_password, 1);
    pwdRow->addWidget(m_btnGenPwd);
    pwdRow->addWidget(m_btnCopyPwd);
    grid->addWidget(pwdLbl, row, 0);
    grid->addLayout(pwdRow, row, 1);
    ++row;

    root->addWidget(m_cardTitle);
    root->addWidget(m_cardSub);
    root->addSpacing(2);
    root->addWidget(m_statusText);
    root->addLayout(grid);

    QHBoxLayout *actions = new QHBoxLayout;
    actions->setSpacing(10);
    m_btnSave = new QPushButton(tr("保存配置"), this);
    m_btnSave->setObjectName(QStringLiteral("hwCloudBtnPrimary"));
    m_btnTest = new QPushButton(tr("测试连接"), this);
    m_btnTest->setObjectName(QStringLiteral("hwCloudBtn"));
    m_btnReset = new QPushButton(tr("恢复预填"), this);
    m_btnReset->setObjectName(QStringLiteral("hwCloudBtn"));
    actions->addWidget(m_btnSave);
    actions->addWidget(m_btnTest);
    actions->addWidget(m_btnReset);
    actions->addStretch(1);
    root->addLayout(actions);

    m_msgLabel = new QLabel(this);
    m_msgLabel->setObjectName(QStringLiteral("hwCloudMsg"));
    m_msgLabel->setWordWrap(true);
    root->addWidget(m_msgLabel);

    connect(m_btnGenClientId, &QPushButton::clicked, this, &HwCloudSettingsWidget::onGenerateClientId);
    connect(m_btnGenPwd,      &QPushButton::clicked, this, &HwCloudSettingsWidget::onGeneratePassword);
    connect(m_btnCopyPwd,     &QPushButton::clicked, this, &HwCloudSettingsWidget::onCopyPassword);
    connect(m_btnSave,        &QPushButton::clicked, this, &HwCloudSettingsWidget::onSaveClicked);
    connect(m_btnTest,        &QPushButton::clicked, this, &HwCloudSettingsWidget::onTestClicked);
    connect(m_btnReset,       &QPushButton::clicked, this, &HwCloudSettingsWidget::onResetDefaults);

    // 软键盘：所有输入框点击时弹出（与 WiFi 密码框同款）
    m_keyboard = new SoftKeyboard(this);
    connect(m_keyboard, &SoftKeyboard::done, this, &HwCloudSettingsWidget::dismissKeyboard);
    const QList<QLineEdit *> edits = findChildren<QLineEdit *>();
    for (QLineEdit *e : edits)
        e->installEventFilter(this);
}

static QString rgbTriplet(const QString &hex)
{
    QColor c(hex);
    return QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
}

void HwCloudSettingsWidget::applyStyle()
{
    // 主题参数化：颜色随 m_* 成员（applyTheme 更新后重刷），字号统一为固定档位
    const QString cardBg = QStringLiteral(
        "background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:1 %2);"
        " border: 1px solid %3;").arg(m_card0, m_card1, m_border);
    const QString card1Rgb  = rgbTriplet(m_card1);
    const QString style = QString(
        "QFrame#hwCloudCard { %1 border-radius: 16px;"
        "  font-family: \"Microsoft YaHei\", \"PingFang SC\", sans-serif; }"
        "QLabel#hwCloudCardTitle { color: %2; font-size: 16px; font-weight: bold; }"
        "QLabel#hwCloudCardSub { color: %3; font-size: 12px; }"
        "QLabel#hwCloudCardStatus { color: %4; font-size: 14px; }"
        "QLabel#hwCloudFieldLabel { color: %3; font-size: 12px; min-width: 96px; }"
        "QLabel#hwCloudMsg { color: %3; font-size: 11px; }"
        "QLineEdit#hwCloudEdit {"
        "  background: rgba(%5, 0.9); color: %2; font-size: 14px;"
        "  border: 1px solid %6; border-radius: 8px; padding: 3px 8px;"
        "}"
        "QPushButton#hwCloudBtn, QPushButton#hwCloudBtnPrimary, QPushButton#hwCloudBtnSmall {"
        "  %1 border-radius: 10px; color: %2; font-size: 15px;"
        "  min-width: 72px; min-height: 44px; padding: 0 12px;"
        "}"
        "QPushButton#hwCloudBtnPrimary {"
        "  background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %4, stop:1 %7);"
        "  font-weight: bold;"
        "}"
        "QPushButton#hwCloudBtnSmall { min-width: 60px; min-height: 36px; font-size: 13px; }"
        "QPushButton#hwCloudBtn:pressed, QPushButton#hwCloudBtnPrimary:pressed { opacity: 0.85; }")
        .arg(cardBg, m_textMain, m_textSub, m_accent, card1Rgb, m_border, m_accent2);
    setStyleSheet(style);
}

void HwCloudSettingsWidget::loadConfig()
{
    QSettings s(QCoreApplication::applicationDirPath()
                    + QStringLiteral("/hotel_terminal.ini"), QSettings::IniFormat);
    // 默认值预填（本项目实测可用的云端参数）；用户「保存配置」后以 QSettings 为准覆盖
    m_region->setText(s.value(QStringLiteral("cloud/region"), QStringLiteral("cn-north-4")).toString());
    m_projectId->setText(s.value(QStringLiteral("cloud/projectId"),
        QStringLiteral("01a010b2f89272ae80e3c14bd8e6ab7c")).toString());
    m_prefix->setText(s.value(QStringLiteral("cloud/instancePrefix"),
        QStringLiteral("ec2e895127")).toString());
    m_deviceId->setText(s.value(QStringLiteral("cloud/deviceId"),
        QStringLiteral("6a83c964cbb0cf6bb97a573b_stm_32F407ZET6")).toString());
    m_secret->setText(s.value(QStringLiteral("cloud/secret")).toString());
    m_clientId->setText(s.value(QStringLiteral("cloud/clientId"),
        QStringLiteral("6a83c964cbb0cf6bb97a573b_stm_32F407ZET6_0_0_2026090400")).toString());
    m_serviceId->setText(s.value(QStringLiteral("cloud/serviceId"),
        QStringLiteral("jifengdaxunlin")).toString());
    m_port->setText(s.value(QStringLiteral("cloud/port"), 1883).toString());
    m_accessKey->setText(s.value(QStringLiteral("cloud/accessKey"),
        QStringLiteral("HPUAW7UIQVMKXGAOJABA")).toString());
    m_secretKey->setText(s.value(QStringLiteral("cloud/secretKey"),
        QStringLiteral("YXTVMQQgxL7Zqda4CI8fQgyXyZFvQVcH1k5ptP6B")).toString());
    // MQTT 登录密码预填 = STM32 固件 mqtt.h 里的 MQTT_Password（与下位机同凭证）
    m_password->setText(s.value(QStringLiteral("cloud/mqttPassword"),
        QStringLiteral("5cddb94cc6ab4eb9062a2e273f000f7c644592c77daa5959dbc3d02161ffcbb1")).toString());
    if (!m_password->text().isEmpty())
        m_btnCopyPwd->setEnabled(true);

    const Config c = config();
    if (c.valid()) {
        m_statusText->setText(tr("已保存：%1  @ %2:%3")
                              .arg(c.deviceId, mqttHost()).arg(c.port));
    } else {
        m_statusText->setText(tr("尚未保存有效配置：请填写设备ID、设备密钥并生成 ClientID"));
    }
}

void HwCloudSettingsWidget::saveConfig()
{
    const Config c = config();
    QSettings s(QCoreApplication::applicationDirPath()
                    + QStringLiteral("/hotel_terminal.ini"), QSettings::IniFormat);
    s.setValue(QStringLiteral("cloud/region"), c.region);
    s.setValue(QStringLiteral("cloud/projectId"), c.projectId);
    s.setValue(QStringLiteral("cloud/instancePrefix"), c.instancePrefix);
    s.setValue(QStringLiteral("cloud/deviceId"), c.deviceId);
    s.setValue(QStringLiteral("cloud/secret"), c.secret);
    s.setValue(QStringLiteral("cloud/clientId"), c.clientId);
    s.setValue(QStringLiteral("cloud/serviceId"), c.serviceId);
    s.setValue(QStringLiteral("cloud/port"), c.port);
    s.setValue(QStringLiteral("cloud/accessKey"), c.accessKey);
    s.setValue(QStringLiteral("cloud/secretKey"), c.secretKey);
    s.setValue(QStringLiteral("cloud/mqttPassword"), m_password->text().trimmed());
    s.sync();

    emit configSaved();

    m_statusText->setText(tr("已保存：%1  @ %2:%3")
                          .arg(c.deviceId, mqttHost()).arg(c.port));
    setMsg(tr("配置已保存到本机。"));
    emit logMessage(QString::fromUtf8("[cloud] 配置已保存，接入点 %1:%2").arg(mqttHost()).arg(c.port));
}

void HwCloudSettingsWidget::onGenerateClientId()
{
    const QString deviceId = m_deviceId->text().trimmed();
    if (deviceId.isEmpty()) {
        setMsg(tr("请先填写设备ID，再生成 ClientID。"), true);
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    // 签名类型 0：{设备ID}_0_0_{时间戳}（不校验时间戳，密码永不过期）
    const QString ts = QStringLiteral("%1%2%3%4")
        .arg(now.toString(QStringLiteral("yyyy")))
        .arg(pad2(now.date().month()))
        .arg(pad2(now.date().day()))
        .arg(pad2(now.time().hour()));
    m_clientId->setText(deviceId + QStringLiteral("_0_0_") + ts);
    setMsg(tr("ClientID 已生成（签名类型 0，不校验时间戳）。"));
}

void HwCloudSettingsWidget::onGeneratePassword()
{
    const QString secret = m_secret->text().trimmed();
    const QString clientId = m_clientId->text().trimmed();
    if (secret.isEmpty() || clientId.isEmpty()) {
        setMsg(tr("请先填写设备密钥与 ClientID，再生成密码。"), true);
        return;
    }
    m_generatedPwd = toHexLower(hmacSha256(secret.toUtf8(), clientId.toUtf8()));
    m_password->setText(m_generatedPwd);
    m_btnCopyPwd->setEnabled(true);
    setMsg(tr("MQTT 登录密码已生成（HMAC-SHA256(设备密钥, ClientID)），共 %1 位。").arg(m_generatedPwd.size()));
    emit logMessage(QString::fromUtf8("[cloud] MQTT 密码已生成（%1 位十六进制）").arg(m_generatedPwd.size()));
}

void HwCloudSettingsWidget::onCopyPassword()
{
    if (m_generatedPwd.isEmpty())
        return;
    QApplication::clipboard()->setText(m_generatedPwd);
    setMsg(tr("已复制到剪贴板。"));
}

void HwCloudSettingsWidget::onSaveClicked()
{
    saveConfig();
}

void HwCloudSettingsWidget::onResetDefaults()
{
    QSettings s(QCoreApplication::applicationDirPath()
                    + QStringLiteral("/hotel_terminal.ini"), QSettings::IniFormat);
    s.remove(QStringLiteral("cloud"));   // 清掉本机保存值，回到预填默认
    s.sync();
    loadConfig();
    setMsg(tr("已恢复为预填默认值（小程序/固件实测参数），轮询已热更新。"));
    emit configSaved();
    emit logMessage(QString::fromUtf8("[cloud] 已恢复预填默认配置"));
}

void HwCloudSettingsWidget::onTestClicked()
{
    if (m_busy) return;
    const Config c = config();
    if (c.deviceId.isEmpty()) {
        setMsg(tr("请先填写设备ID。"), true);
        return;
    }

    m_busy = true;
    m_btnTest->setText(tr("测试中..."));
    m_btnTest->setEnabled(false);
    m_mqttDone  = m_mqttOk  = false;
    m_probeDone = m_probeOk = false;
    m_mqttMsg   = tr("[未执行] 需设备密钥");
    m_probeMsg  = tr("[未执行] 需 AK/SK 与项目ID");

    // ClientID / MQTT 密码缺失时自动补全（保证"直接点"即可测试）
    if (!c.secret.isEmpty()) {
        if (c.clientId.isEmpty())
            onGenerateClientId();
        if (m_password->text().isEmpty())
            onGeneratePassword();
    }
    saveConfig();   // 自动补全的凭证一并落盘（后续保存配置可覆盖）

    bool any = false;

    // ---- 路 1：设备侧 MQTT CONNECT 握手（1883 可达 + 设备凭证）----
    // 凭证来源二选一：表单里的 MQTT 密码（预填固件值），或 设备密钥现算 HMAC
    if (!c.clientId.isEmpty()
        && (!c.secret.isEmpty() || !m_password->text().trimmed().isEmpty())) {
        QString pwd = m_password->text().trimmed();
        if (pwd.isEmpty()) {
            onGeneratePassword();          // 有密钥无密码：现算
            pwd = m_password->text().trimmed();
        }
        if (!pwd.isEmpty()) {
            setMsg(tr("正在测试设备侧 MQTT（%1:%2）与应用侧影子 API ...")
                   .arg(mqttHost()).arg(c.port));
            m_tester->start(mqttHost(), c.port,
                            c.clientId, c.deviceId, pwd);
            any = true;
        } else {
            m_mqttDone = true;
            m_mqttMsg  = tr("[失败] 无法生成 MQTT 密码（设备密钥无效）");
        }
    } else {
        m_mqttDone = true;                 // 未执行也必须置完成，否则汇总永久等待
        m_mqttMsg  = tr("[未执行] 需 ClientID + 设备密钥（或预填的 MQTT 密码）");
    }

    // ---- 路 2：应用侧影子 API（AK/SK 签名 + HTTPS + 设备数据）----
    if (!c.accessKey.isEmpty() && !c.secretKey.isEmpty() && !c.projectId.isEmpty()) {
        HwCloudClient::Config pc;
        pc.region         = c.region;
        pc.projectId      = c.projectId;
        pc.instancePrefix = c.instancePrefix;
        pc.deviceId       = c.deviceId;
        pc.serviceId      = c.serviceId;
        pc.accessKey      = c.accessKey;
        pc.secretKey      = c.secretKey;
        m_probe->setConfig(pc);
        m_probe->pollNow();
        any = true;
    } else {
        m_probeDone = true;                // 未执行也必须置完成，否则汇总永久等待
    }

    if (!any) {
        m_busy = false;
        m_btnTest->setText(tr("测试连接"));
        m_btnTest->setEnabled(true);
        setMsg(tr("没有可执行的测试：请填写设备密钥（MQTT 测试）或 AK/SK + 项目ID（影子 API 测试）。"), true);
        return;
    }
    finishTestIfDone();   // 两路都秒回失败时立即汇总
}

void HwCloudSettingsWidget::onTestFinished(bool ok, const QString &message)
{
    m_mqttDone = true;
    m_mqttOk   = ok;
    m_mqttMsg  = QString(ok ? QStringLiteral("[OK] %1") : QStringLiteral("[失败] %1"))
                 .arg(message);
    finishTestIfDone();
}

void HwCloudSettingsWidget::onProbeShadowUpdated()
{
    const HwCloudClient::Snapshot s = m_probe->snapshot();
    m_probeDone = true;
    m_probeOk   = true;
    m_probeMsg  = tr("[OK] 影子 API 联通（temp=%1℃ humi=%2%RH 更新于 %3）")
                      .arg(QString::number(s.temp, 'f', 1),
                           QString::number(s.humi, 'f', 1), s.eventTimeText);
    finishTestIfDone();
}

void HwCloudSettingsWidget::onProbeFailed(const QString &why)
{
    m_probeDone = true;
    m_probeOk   = false;
    m_probeMsg  = tr("[失败] 影子 API：%1").arg(why);
    finishTestIfDone();
}

void HwCloudSettingsWidget::finishTestIfDone()
{
    if (!m_busy || !m_mqttDone || !m_probeDone)
        return;
    m_busy = false;
    m_btnTest->setText(tr("测试连接"));
    m_btnTest->setEnabled(true);

    const bool allOk = m_mqttOk && m_probeOk;
    setMsg(tr("设备侧 MQTT：%1\n应用侧影子：%2").arg(m_mqttMsg, m_probeMsg), !allOk);
    emit logMessage(QString::fromUtf8("[cloud] 一键测试完成 => MQTT %1 / 影子API %2")
                    .arg(m_mqttOk ? QStringLiteral("OK") : QStringLiteral("失败"),
                         m_probeOk ? QStringLiteral("OK") : QStringLiteral("失败")));
}

void HwCloudSettingsWidget::setMsg(const QString &text, bool error)
{
    if (!m_msgLabel) return;
    m_msgLabel->setText(text);
    m_msgLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                              .arg(error ? QStringLiteral("#e74c3c") : m_textSub));
}

void HwCloudSettingsWidget::applyTheme(bool dark, const QString &accent, const QString &accent2,
                                       const QString &card0, const QString &card1, const QString &border,
                                       const QString &textMain, const QString &textSub)
{
    m_dark     = dark;
    m_accent   = accent;
    m_accent2  = accent2;
    m_card0    = card0;
    m_card1    = card1;
    m_border   = border;
    m_textMain = textMain;
    m_textSub  = textSub;
    applyStyle();
}

#include "hwcloud_settings_widget.moc"

// ---- 软键盘（所有输入框通用）----
bool HwCloudSettingsWidget::eventFilter(QObject *watched, QEvent *event)
{
    QLineEdit *edit = qobject_cast<QLineEdit *>(watched);
    // 仅用户主动点击输入框时弹键盘（不响应 FocusIn，避免页面切换时误弹）
    if (edit && event->type() == QEvent::MouseButtonPress)
        showKeyboardFor(edit);
    return QFrame::eventFilter(watched, event);
}

void HwCloudSettingsWidget::showKeyboardFor(QLineEdit *edit)
{
    if (!m_keyboard || !edit) return;
    QWidget *top = topLevelWidget();
    if (!top) return;
    // 键盘必须挂到顶层窗口：作为卡片子控件时 setGeometry 用顶层坐标会被裁剪
    if (m_keyboard->parentWidget() != top)
        m_keyboard->setParent(top);
    m_keyboard->setTarget(edit);

    QPoint p = edit->mapTo(top, QPoint(0, edit->height()));
    int kbW = qMax(560, edit->width() + 200);
    int kbH = 300;   // 5 行 44px 按键 + 顶部栏
    QRect geo = top->geometry();
    int x = qMin(p.x(), geo.width() - kbW - 8);
    if (x < 4) x = 4;
    // 优先放在输入框下方；下方放不下则改放到输入框上方，
    // 仍不行才底部对齐——保证正在输入的框尽量不被键盘盖住
    int y = p.y() + 6;
    if (y + kbH > geo.height() - 8) {
        const int aboveY = p.y() - edit->height() - kbH - 6;
        y = (aboveY >= 4) ? aboveY : (geo.height() - kbH - 8);
    }
    if (y < 4) y = 4;
    m_keyboard->setGeometry(x, y, kbW, kbH);
    m_keyboard->show();
    m_keyboard->raise();
    m_keyboard->setFocus();
}

void HwCloudSettingsWidget::dismissKeyboard()
{
    if (!m_keyboard) return;
    // 延迟 300ms：触摸屏上 touch release 会在键盘消失后穿透到下方导航按钮
    QTimer::singleShot(300, m_keyboard, &QWidget::hide);
}
