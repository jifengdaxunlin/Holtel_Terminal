#ifndef HWCLOUD_SETTINGS_WIDGET_H
#define HWCLOUD_SETTINGS_WIDGET_H

#include <QFrame>

class QLabel;
class QLineEdit;
class QPushButton;
class MqttTester;
class HwCloudClient;
class SoftKeyboard;

/*
 * 华为云 / MQTT 连接设置卡（设置页）
 *
 * 参考微信小程序 miniprogram/utils/config.ts + huawei.ts 中固件的 MQTT 参数：
 *   接入域名: {实例前缀}.st1.iotda-device.{区域}.myhuaweicloud.com:1883
 *   ClientID: {设备ID}_0_0_{时间戳}   （签名类型 0，不校验时间戳，密码永不过期）
 *   Username: {设备ID}（= MQTT 登录用户名）
 *   密码:     HMAC-SHA256(设备密钥, clientId) 的十六进制小写串
 *   上报主题: $oc/devices/{设备ID}/sys/properties/report
 *
 * 本卡用于：配置终端接入华为云 IoTDA 的 MQTT 登录凭证，并做连通性测试。
 * 「测试连接」一键两路并行：
 *   ① 设备侧：QTcpSocket 完成一次 MQTT 3.1.1 CONNECT 握手，以服务端
 *      CONNACK 判断 1883 可达性与设备凭证（ClientID/用户名/HMAC 密码）；
 *   ② 应用侧：HwCloudClient 走 HTTPS 设备影子查询，验证 AK/SK 签名、
 *      项目ID/设备ID 与数据通道。
 * ClientID 与 MQTT 密码缺失时自动生成，保证「直接点」即可测试。
 * 配置保存在本机 QSettings。
 */
class HwCloudSettingsWidget : public QFrame
{
    Q_OBJECT
public:
    explicit HwCloudSettingsWidget(QWidget *parent = 0);
    ~HwCloudSettingsWidget();

    struct Config {
        QString region;          // 区域，如 cn-north-4
        QString projectId;       // 项目 ID
        QString instancePrefix;  // 标准版实例域名前缀，如 ec2e895127（基础版留空）
        QString deviceId;        // 设备 ID（= MQTT 登录用户名）
        QString secret;          // 设备密钥
        QString clientId;        // ClientID（签名类型 0）
        QString serviceId;       // 服务 ID
        int     port = 1883;     // MQTT 端口
        QString accessKey;       // IAM 访问密钥 AK（应用侧 HTTPS 影子查询用）
        QString secretKey;       // IAM 安全访问密钥 SK

        bool valid() const {
            return !deviceId.trimmed().isEmpty() && !secret.trimmed().isEmpty()
                && !clientId.trimmed().isEmpty();
        }
    };

    Config config() const;
    bool isBusy() const { return m_busy; }

    // 供 MainWindow 在页面切换时调用，收起软键盘
    void dismissKeyboard();

    // 由 MainWindow 在主题切换时调用，让卡片跟随当前主题
    void applyTheme(bool dark, const QString &accent, const QString &accent2,
                    const QString &card0, const QString &card1, const QString &border,
                    const QString &textMain, const QString &textSub);

signals:
    void logMessage(const QString &text);
    /** 保存配置后发出，MainWindow 据此刷新华为云影子轮询参数 */
    void configSaved();

private slots:
    void onGenerateClientId();
    void onGeneratePassword();
    void onCopyPassword();
    void onSaveClicked();
    void onTestClicked();
    void onResetDefaults();
    void onTestFinished(bool ok, const QString &message);
    void onProbeShadowUpdated();
    void onProbeFailed(const QString &why);
    void finishTestIfDone();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void showKeyboardFor(QLineEdit *edit);
    void applyStyle();
    void loadConfig();
    void saveConfig();
    void setMsg(const QString &text, bool error = false);
    QString mqttHost() const;

    // ---- UI ----
    QLabel      *m_cardTitle;
    QLabel      *m_cardSub;
    QLabel      *m_statusText;   // 已保存配置摘要
    QLabel      *m_msgLabel;     // 操作 / 测试结果
    QLineEdit   *m_region;
    QLineEdit   *m_projectId;
    QLineEdit   *m_prefix;
    QLineEdit   *m_deviceId;
    QLineEdit   *m_secret;
    QLineEdit   *m_clientId;
    QLineEdit   *m_serviceId;
    QLineEdit   *m_port;
    QLineEdit   *m_accessKey;    // IAM AK（应用侧签名）
    QLineEdit   *m_secretKey;    // IAM SK（应用侧签名）
    QLineEdit   *m_password;     // 生成的 MQTT 登录密码（只读）
    QPushButton *m_btnGenClientId;
    QPushButton *m_btnGenPwd;
    QPushButton *m_btnCopyPwd;
    QPushButton *m_btnSave;
    QPushButton *m_btnTest;
    QPushButton *m_btnReset;

    MqttTester  *m_tester;
    HwCloudClient *m_probe;      // 应用侧影子 API 测试探针（一键测试第二路）
    SoftKeyboard *m_keyboard;
    bool         m_busy;
    // 一键测试：两路结果汇总
    bool    m_mqttDone,  m_mqttOk;
    bool    m_probeDone, m_probeOk;
    QString m_mqttMsg,   m_probeMsg;
    QString      m_generatedPwd;

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

#endif // HWCLOUD_SETTINGS_WIDGET_H
