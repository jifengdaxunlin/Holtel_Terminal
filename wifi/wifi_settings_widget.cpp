#include "wifi/wifi_settings_widget.h"
#include "wifi/wifi_manager.h"
#include "wifi/softkeyboard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QThread>
#include <QEvent>
#include <QTimer>
#include <QDebug>
#include <QColor>

// #rrggbb -> "r, g, b"（供 QSS rgba() 半透明底使用）
static QString rgbTriplet(const QString &hex)
{
    QColor c(hex);
    return QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
}

WifiSettingsWidget::WifiSettingsWidget(QWidget *parent)
    : QFrame(parent)
    , m_cardTitle(0), m_cardSub(0), m_statusText(0), m_msgLabel(0)
    , m_btnRefresh(0), m_btnScan(0), m_btnDisconnect(0), m_btnConnect(0)
    , m_list(0), m_pwdEdit(0), m_keyboard(0)
    , m_wifi(0), m_worker(0), m_busy(false), m_opRunning(false), m_available(false)
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

    // 跨线程排队调用 connectTo 需要 WifiInfo 已注册（Qt::QueuedConnection 按值拷贝）
    qRegisterMetaType<WifiInfo>();

    // Worker thread for WifiManager; signals are queued across thread boundary.
    m_worker = new QThread(this);
    m_wifi   = new WifiManager(QStringLiteral("wlan0"));   // ignored on Windows
    m_wifi->moveToThread(m_worker);
    connect(m_worker, &QThread::finished, m_wifi, &QObject::deleteLater);

    connect(m_wifi, &WifiManager::scanStarted,        this, &WifiSettingsWidget::onScanStarted);
    connect(m_wifi, &WifiManager::scanFinished,       this, &WifiSettingsWidget::onScanFinished);
    connect(m_wifi, &WifiManager::connectFinished,    this, &WifiSettingsWidget::onConnectFinished);
    connect(m_wifi, &WifiManager::stateRefreshed,     this, &WifiSettingsWidget::onStateRefreshed);
    connect(m_wifi, &WifiManager::busyChanged,        this, &WifiSettingsWidget::onBusyChanged);
    connect(m_wifi, &WifiManager::operationFailed,    this, &WifiSettingsWidget::onOperationFailed);
    connect(m_wifi, &WifiManager::logMessage,         this, &WifiSettingsWidget::onLog);

    connect(m_btnScan,       &QPushButton::clicked, this, &WifiSettingsWidget::onScanClicked);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &WifiSettingsWidget::onDisconnectClicked);
    connect(m_btnRefresh,    &QPushButton::clicked, this, &WifiSettingsWidget::onRefreshClicked);
    connect(m_btnConnect,    &QPushButton::clicked, this, &WifiSettingsWidget::onConnectClicked);
    connect(m_list,          &QListWidget::itemSelectionChanged,
            this, &WifiSettingsWidget::onItemSelectionChanged);

    m_worker->start();

    // Show initial status; ask the worker for an authoritative state right away.
    updateCurrentStatus(false, QString(), QString());
    QTimer::singleShot(200, this, [this]() {
        if (m_wifi) QMetaObject::invokeMethod(m_wifi, "refreshState", Qt::QueuedConnection);
    });

    // On Windows hosts there is no wpa_supplicant; treat as "unavailable" rather
    // than throwing. The card stays visible so the UI is identical to the device.
#ifdef Q_OS_WIN
    m_available = true;          // netsh backend works on Windows
#else
    m_available = true;          // wpa_cli assumed present on ARM Linux
#endif
}

WifiSettingsWidget::~WifiSettingsWidget()
{
    // 排队到 worker 线程执行（析构在主线程，直接调用会跨线程创建 QProcess 子对象）
    if (m_wifi)  QMetaObject::invokeMethod(m_wifi, "disconnectWifi", Qt::QueuedConnection);
    if (m_worker) {
        m_worker->quit();
        m_worker->wait(2000);
    }
}

void WifiSettingsWidget::buildUi()
{
    setObjectName(QStringLiteral("wifiCard"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    m_cardTitle = new QLabel(tr("WiFi 连接"), this);
    m_cardTitle->setObjectName(QStringLiteral("wifiCardTitle"));
    m_cardSub   = new QLabel(tr("扫描附近网络并连接到客房 WiFi"), this);
    m_cardSub->setObjectName(QStringLiteral("wifiCardSub"));

    m_statusText = new QLabel(tr("当前：未连接"), this);
    m_statusText->setObjectName(QStringLiteral("wifiCardStatus"));

    QHBoxLayout *row1 = new QHBoxLayout;
    row1->setSpacing(8);
    m_btnRefresh    = new QPushButton(tr("刷新"), this);
    m_btnScan       = new QPushButton(tr("扫描"), this);
    m_btnDisconnect = new QPushButton(tr("断开"), this);
    m_btnRefresh->setObjectName(QStringLiteral("wifiBtn"));
    m_btnScan->setObjectName(QStringLiteral("wifiBtnPrimary"));
    m_btnDisconnect->setObjectName(QStringLiteral("wifiBtn"));
    row1->addWidget(m_btnRefresh);
    row1->addWidget(m_btnScan);
    row1->addWidget(m_btnDisconnect);
    row1->addStretch(1);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("wifiList"));
    m_list->setMinimumHeight(110);
    m_list->setMaximumHeight(160);

    QHBoxLayout *row2 = new QHBoxLayout;
    row2->setSpacing(8);
    QLabel *pwdLbl = new QLabel(tr("密码"), this);
    m_pwdEdit = new QLineEdit(this);
    m_pwdEdit->setObjectName(QStringLiteral("wifiPwd"));
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setPlaceholderText(tr("点击输入密码"));
    m_btnConnect = new QPushButton(tr("连接选中"), this);
    m_btnConnect->setObjectName(QStringLiteral("wifiBtnPrimary"));
    row2->addWidget(pwdLbl);
    row2->addWidget(m_pwdEdit, 1);
    row2->addWidget(m_btnConnect);

    m_msgLabel = new QLabel(this);
    m_msgLabel->setObjectName(QStringLiteral("wifiMsg"));
    m_msgLabel->setWordWrap(true);

    root->addWidget(m_cardTitle);
    root->addWidget(m_cardSub);
    root->addSpacing(4);
    root->addWidget(m_statusText);
    root->addLayout(row1);
    root->addWidget(m_list);
    root->addLayout(row2);
    root->addWidget(m_msgLabel);

    // SoftKeyboard: lives hidden, pops up over the password field.
    m_keyboard = new SoftKeyboard(this);
    m_keyboard->setObjectName(QStringLiteral("wifiKeyboard"));
    m_keyboard->hide();
    m_keyboard->setTarget(m_pwdEdit);
    // 点「完成/收起」隐藏键盘
    connect(m_keyboard, &SoftKeyboard::done, this, &WifiSettingsWidget::hideKeyboard);

    // Touch the password box -> show keyboard.
    m_pwdEdit->installEventFilter(this);
    // 只接受点击获焦：避免页面切换/程序调用 setFocus 时误弹键盘
    m_pwdEdit->setFocusPolicy(Qt::ClickFocus);
}

void WifiSettingsWidget::applyStyle()
{
    // 主题参数化：颜色随 m_* 成员（applyTheme 更新后重刷），字号统一为固定档位
    const QString cardBg = QStringLiteral(
        "background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:1 %2);"
        " border: 1px solid %3;").arg(m_card0, m_card1, m_border);
    const QString card1Rgb   = rgbTriplet(m_card1);
    const QString accentRgb  = rgbTriplet(m_accent);
    const QString style = QString(
        "QFrame#wifiCard { %1 border-radius: 16px;"
        "  font-family: \"Microsoft YaHei\", \"PingFang SC\", sans-serif; }"
        "QLabel#wifiCardTitle { color: %2; font-size: 16px; font-weight: bold; }"
        "QLabel#wifiCardSub { color: %3; font-size: 12px; }"
        "QLabel#wifiCardStatus { color: %4; font-size: 14px; }"
        "QLabel#wifiMsg { color: %3; font-size: 11px; }"
        "QListWidget#wifiList {"
        "  background: rgba(%5, 0.6); color: %2; font-size: 14px;"
        "  border: 1px solid %6; border-radius: 8px;"
        "}"
        "QListWidget#wifiList::item { padding: 4px 6px; }"
        "QListWidget#wifiList::item:selected { background: rgba(%7, 0.18); color: %4; }"
        "QLineEdit#wifiPwd {"
        "  background: rgba(%5, 0.9); color: %2; font-size: 14px;"
        "  border: 1px solid %6; border-radius: 8px; padding: 4px 8px;"
        "}"
        "QPushButton#wifiBtn, QPushButton#wifiBtnPrimary {"
        "  %1 border-radius: 10px; color: %2; font-size: 15px;"
        "  min-width: 64px; min-height: 44px; padding: 0 12px;"
        "}"
        "QPushButton#wifiBtnPrimary {"
        "  background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %4, stop:1 %8);"
        "  font-weight: bold;"
        "}"
        "QPushButton#wifiBtn:pressed, QPushButton#wifiBtnPrimary:pressed { opacity: 0.85; }")
        .arg(cardBg, m_textMain, m_textSub, m_accent, card1Rgb, m_border, accentRgb, m_accent2);
    setStyleSheet(style);
}

bool WifiSettingsWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_pwdEdit) {
        // 仅用户主动点击密码框时弹键盘；去掉 FocusIn 触发，
        // 避免页面切换/程序设置焦点时键盘莫名其妙弹出。
        if (event->type() == QEvent::MouseButtonPress)
            showKeyboardForPassword();
    }
    return QFrame::eventFilter(watched, event);
}

void WifiSettingsWidget::showKeyboardForPassword()
{
    if (!m_keyboard) return;
    QWidget *top = topLevelWidget();
    if (!top) return;
    // 键盘必须挂到顶层窗口：作为卡片子控件时 setGeometry 用顶层坐标会被裁剪，
    // 导致点击密码框"看不到键盘"。
    if (m_keyboard->parentWidget() != top)
        m_keyboard->setParent(top);
    QPoint p = m_pwdEdit->mapTo(top, QPoint(0, m_pwdEdit->height()));
    int kbW = qMax(560, m_pwdEdit->width() + 200);
    int kbH = 300;   // 5 行 44px 按键 + 顶部栏
    QRect geo = top->geometry();
    int x = qMin(p.x(), geo.width() - kbW - 8);
    if (x < 4) x = 4;
    // 优先放在密码框下方；下方放不下则改放到密码框上方，
    // 仍不行才底部对齐——保证正在输入的密码框尽量不被键盘盖住
    int y = p.y() + 6;
    if (y + kbH > geo.height() - 8) {
        const int aboveY = p.y() - m_pwdEdit->height() - kbH - 6;
        y = (aboveY >= 4) ? aboveY : (geo.height() - kbH - 8);
    }
    if (y < 4) y = 4;
    m_keyboard->setGeometry(x, y, kbW, kbH);
    m_keyboard->show();
    m_keyboard->raise();
    m_keyboard->setFocus();
}

void WifiSettingsWidget::hideKeyboard()
{
    if (!m_keyboard) return;
    // 延迟到事件循环空闲时再 hide：clicked 信号在 mousePress 时触发，
    // 若立即 hide，mouseRelease 会穿透到下方的导航按钮（如入住办理），
    // 导致"点收起却跳转到其他页面"。
    // 延迟 300ms：触摸屏上 touch release 会在键盘消失后穿透到下方导航按钮，
    // 等手指离开屏幕后再隐藏，避免"点收起却跳转到其他页面"。
    QTimer::singleShot(300, m_keyboard, &QWidget::hide);
    // 注意：不要把焦点设回密码框——FocusIn 会再次触发键盘弹出。
}

void WifiSettingsWidget::onScanClicked()
{
    setMsg(tr("正在扫描附近 WiFi..."));
    if (m_wifi) QMetaObject::invokeMethod(m_wifi, "scan", Qt::QueuedConnection);
}

void WifiSettingsWidget::onRefreshClicked()
{
    setMsg(tr("正在查询当前连接..."));
    if (m_wifi) QMetaObject::invokeMethod(m_wifi, "refreshState", Qt::QueuedConnection);
}

void WifiSettingsWidget::onDisconnectClicked()
{
    setMsg(tr("正在断开..."));
    if (m_wifi) QMetaObject::invokeMethod(m_wifi, "disconnectWifi", Qt::QueuedConnection);
}

void WifiSettingsWidget::onConnectClicked()
{
    WifiInfo info = selectedInfo();
    if (!info.isValid()) {
        setMsg(tr("请先在列表中选择一个 WiFi。"), true);
        return;
    }
    QString pwd = m_pwdEdit->text();
    if (info.secured && pwd.isEmpty()) {
        setMsg(tr("该网络需要密码，请在密码框中输入后再次连接。"), true);
        return;
    }
    setMsg(tr("正在连接到 %1 ...").arg(info.ssid));
    m_pendingPwd = pwd;
    m_connecting = true;
    // WifiManager::connectTo is a public slot - cross-thread invocation by name.
    QMetaObject::invokeMethod(m_wifi, "connectTo", Qt::QueuedConnection,
                              Q_ARG(WifiInfo, info),
                              Q_ARG(QString, pwd));
}

void WifiSettingsWidget::onItemSelectionChanged()
{
    WifiInfo info = selectedInfo();
    if (!info.isValid()) return;
    // Pre-fill password only if previously typed this session (cache).
    // (We keep no on-disk cache by default for security on a shared terminal.)
    if (m_pendingPwd.isEmpty()) m_pwdEdit->clear();
}

WifiInfo WifiSettingsWidget::selectedInfo() const
{
    QListWidgetItem *it = m_list ? m_list->currentItem() : 0;
    if (!it) return WifiInfo();
    return it->data(Qt::UserRole).value<WifiInfo>();
}

void WifiSettingsWidget::onScanStarted()
{
    setMsg(tr("扫描中..."));
}

void WifiSettingsWidget::onScanFinished(const QList<WifiInfo> &list)
{
    m_networks = list;
    m_list->clear();
    for (int i = 0; i < list.size(); ++i) {
        const WifiInfo &w = list.at(i);
        if (w.ssid.trimmed().isEmpty()) continue;
        QString label = QStringLiteral("[%1%]  %2   %3")
                            .arg(w.signal, 3)
                            .arg(w.ssid)
                            .arg(w.secured ? tr("(加密)") : tr("(开放)"));
        QListWidgetItem *it = new QListWidgetItem(label, m_list);
        it->setData(Qt::UserRole, QVariant::fromValue(w));
    }
    setMsg(tr("扫描完成，共 %1 个网络。").arg(m_list->count()));
}

void WifiSettingsWidget::onConnectFinished(bool ok, const QString &ssid,
                                           const QString &ip, const QString &message)
{
    m_connecting = false;
    emit connectivityChanged(ok);
    if (ok) {
        updateCurrentStatus(true, ssid, ip);
        setMsg(tr("已连接到 %1 （%2）。").arg(ssid, ip));
    } else {
        updateCurrentStatus(false, QString(), QString());
        setMsg(tr("连接失败：%1").arg(message), true);
    }
}

void WifiSettingsWidget::onStateRefreshed(bool connected, const QString &ssid,
                                          const QString &ip)
{
    updateCurrentStatus(connected, ssid, ip);
    emit connectivityChanged(connected);   // 开机刷新发现已联网 -> 触发 NTP 校时
}

void WifiSettingsWidget::onBusyChanged(bool busy)
{
    m_busy = busy;
    m_btnScan->setEnabled(!busy);
    m_btnConnect->setEnabled(!busy);
    m_btnDisconnect->setEnabled(!busy);
    m_btnRefresh->setEnabled(!busy);
}

void WifiSettingsWidget::onOperationFailed(const QString &reason)
{
    setMsg(tr("错误：%1").arg(reason), true);
}

void WifiSettingsWidget::onLog(const QString &text)
{
    qDebug().noquote() << "[wifi]" << text;
    emit logMessage(text);
}

void WifiSettingsWidget::setMsg(const QString &text, bool error)
{
    if (!m_msgLabel) return;
    m_msgLabel->setText(text);
    m_msgLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
                              .arg(error ? QStringLiteral("#e74c3c") : m_textSub));
}

void WifiSettingsWidget::updateCurrentStatus(bool connected, const QString &ssid,
                                             const QString &ip)
{
    if (!m_statusText) return;
    if (connected)
        m_statusText->setText(tr("当前：%1   %2").arg(ssid, ip));
    else
        m_statusText->setText(tr("当前：未连接"));
}

void WifiSettingsWidget::applyTheme(bool dark, const QString &accent, const QString &accent2,
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
void WifiSettingsWidget::dismissKeyboard()
{
    hideKeyboard();
}
