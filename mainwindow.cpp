#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "darktoast.h"
#include "wifi/wifi_settings_widget.h"
#include "face/face_camera_widget.h"
#include "cloud/hwcloud_settings_widget.h"
#include "cloud/hwcloud_client.h"
#include "time/ntpsync.h"

#include <QAbstractButton>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

// ---------------------------------------------------------------------------
// 本文件只依赖 mainwindow.h 声明的接口。
//
// 设计要点：
//   * 6 套主题由 THEMES[6] 描述，全部样式经 buildStyleSheet() 生成，
//     .ui 中不再保留任何硬编码颜色（否则会覆盖全局 QSS）。
//   * QSS 通过 objectName 钩子选中控件：panelCard / cardTitle / tileBtn ...
//     因此 .ui 里既有控件名（ui->xxx）保持不变，只是多了一个 objectName 属性。
//   * 样式切换时把主题参数下发给代码构建的卡片（WiFi / 华为云 / 人脸预览）。
// ---------------------------------------------------------------------------

namespace {

const char *const kSettingsOrg = "XihuHotel";
const char *const kSettingsApp = "HotelTerminal";
const char *const kThemeKey    = "ui/theme";

// "#rrggbb" -> "rgba(r, g, b, a)"，用于强调色的半透明底纹
QString rgba(const QString &hex, double alpha)
{
    const QColor c(hex);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(alpha, 0, 'f', 2);
}

// 设置页 6 个主题按钮上的主题色圆点
QPixmap themeDot(const QString &color, int size = 14)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(0, 0, 0, 70), 1));
    p.setBrush(QColor(color));
    p.drawEllipse(1, 1, size - 2, size - 2);
    p.end();
    return pm;
}

} // namespace

// ---------------------------------------------------------------------------
// 主题表：2 套 -> 6 套
// ---------------------------------------------------------------------------
const MainWindow::ThemeSpec MainWindow::THEMES[6] = {
    // id          name        dark   accent      accent2     card0       card1
    //                                                   checked0    checked1    border
    //                                                   sidebar0    sidebar1    sidebarBorder
    //                                                   textMain    textSub     bg
    { "deepsea",  "深海蓝",   true,  "#00d4ff", "#0077be", "#0f2a52", "#081a33",
      "#0a2245", "#061a36", "#1a3a6b", "#0a1a33", "#051020", "#132b4d",
      "#ffffff", "#8aa4c8", "#050d1a" },

    { "moonlight", "月光浅",  false, "#0077be", "#00d4ff", "#ffffff", "#e8eef5",
      "#e0f6ff", "#c8ecfc", "#c8d2dd", "#ffffff", "#e8eef5", "#c8d2dd",
      "#1a2332", "#5a6b82", "#eef2f6" },

    { "obsidian", "曜石金",   true,  "#f1c40f", "#b8860b", "#2a2416", "#1a1608",
      "#3a3116", "#241d0a", "#6b5a1a", "#1a1608", "#0d0b04", "#4a3d12",
      "#ffffff", "#c8b98a", "#0d0b04" },

    { "jade",     "墨玉绿",   true,  "#2ecc71", "#1e8449", "#10331f", "#081a10",
      "#14472a", "#0a2416", "#1a6b3d", "#0a2416", "#051009", "#124a2a",
      "#ffffff", "#8ac8a4", "#051009" },

    { "violet",   "幻紫",     true,  "#a569bd", "#6c3483", "#2a1a45", "#160d24",
      "#38215a", "#1e1230", "#4a2a6b", "#1e1230", "#100919", "#3a2154",
      "#ffffff", "#b89ac8", "#100919" },

    { "sunrise",  "暖阳橙",   false, "#e67e22", "#d35400", "#ffffff", "#fdf0e2",
      "#ffe8d0", "#ffd6ac", "#e8cdb0", "#ffffff", "#fdf0e2", "#e8cdb0",
      "#33231a", "#82624a", "#fdf6ee" }
};

const MainWindow::ThemeSpec &MainWindow::themeById(const QString &id)
{
    for (int i = 0; i < 6; ++i) {
        if (id == QString::fromLatin1(THEMES[i].id))
            return THEMES[i];
    }
    return THEMES[0];
}

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowTitle(tr("Shengjing Hotel · Smart Terminal"));
    // Default target resolution for the 7-inch X6818 panel.
    resize(800, 480);

    // 恢复上次选择的主题（QSettings）。
    // themeById() 对缺失/非法的 id 会回落到 THEMES[0]，因此这里无需额外校验；
    // 它返回的是 THEMES 数组内的引用，用指针差即可得到下标。
    QSettings settings(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/hotel_terminal.ini"), QSettings::IniFormat);
    QString savedId = settings.value(QString::fromLatin1(kThemeKey)).toString();
    if (savedId.isEmpty()) {
        // 首次使用新 ini：迁移旧存储（org/app 原生配置）里的历史主题，
        // 否则首次进入会回落默认主题，与用户的历史选择"错乱"
        QSettings legacy(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
        const QString legacyId = legacy.value(QString::fromLatin1(kThemeKey)).toString();
        if (!legacyId.isEmpty()) {
            settings.setValue(QString::fromLatin1(kThemeKey), legacyId);
            settings.sync();
            savedId = legacyId;
        }
    }
    m_themeIndex = int(&themeById(savedId) - THEMES);

    // 设置页 6 个主题按钮：带主题色圆点，当前主题高亮
    QHBoxLayout *themeLayout = ui->pageSettings
        ? ui->pageSettings->findChild<QHBoxLayout *>(QStringLiteral("themeBtnLayout"))
        : 0;
    if (themeLayout) {
        for (int i = 0; i < 6; ++i) {
            const ThemeSpec &t = THEMES[i];
            QPushButton *btn = new QPushButton(QString::fromUtf8(t.name), ui->themeCard);
            btn->setObjectName(QStringLiteral("themeBtn"));
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setMinimumHeight(64);
            btn->setIconSize(QSize(14, 14));
            btn->setIcon(QIcon(themeDot(QString::fromLatin1(t.accent))));
            btn->setToolTip(QString::fromUtf8(t.name));
            connect(btn, &QPushButton::clicked, this, [this, i]() { onThemeSelect(i); });
            themeLayout->addWidget(btn);
            m_themeButtons.append(btn);
        }
    }

    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::onTimeUpdate);
    m_clockTimer->start(1000);
    onTimeUpdate();

    // 音乐歌单：只从程序目录 music/ 读取真实存在的音频文件（按文件名排序）
    m_tracks = scanMusicFolder();

    setupConnections();
    setupIntegrationWidgets();
    applyTheme();                 // 内部会调用 setupIcons() + applyButtonStyles()

    // 天气（辽宁沈阳）：首次进入立即拉取一次，此后每小时自动刷新
    m_weatherNam = new QNetworkAccessManager(this);
    m_weatherTimer = new QTimer(this);
    connect(m_weatherTimer, &QTimer::timeout, this, &MainWindow::fetchWeather);
    m_weatherTimer->start(30 * 60 * 1000);   // 每半小时自动刷新
    fetchWeather();

    // 启用手机式滑动（触摸拖拽滚动）
    if (ui->settingsScroll) {
        // 垂直滚动条移到左侧（800x480 屏右侧触控不便）：滚动区整体 RTL 使滚动条靠左，
        // viewport 显式恢复 LTR，内容布局不受镜像影响
        ui->settingsScroll->setLayoutDirection(Qt::RightToLeft);
        ui->settingsScroll->viewport()->setLayoutDirection(Qt::LeftToRight);
        QScroller::grabGesture(ui->settingsScroll->viewport(), QScroller::TouchGesture);
    }

    // 未完成人脸识别前不允许确认入住
    ui->btnConfirmCheckin->setEnabled(false);
    setFaceStatus(tr("等待识别"), FaceState::Waiting);
    updateServiceSummary();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// 样式：全部由主题表参数化生成
// ---------------------------------------------------------------------------
QString MainWindow::buildStyleSheet(const ThemeSpec &t) const
{
    const QString accent   = QString::fromLatin1(t.accent);
    const QString accent2  = QString::fromLatin1(t.accent2);
    const QString card0    = QString::fromLatin1(t.card0);
    const QString card1    = QString::fromLatin1(t.card1);
    const QString checked0 = QString::fromLatin1(t.checked0);
    const QString checked1 = QString::fromLatin1(t.checked1);
    const QString border   = QString::fromLatin1(t.border);
    const QString sb0      = QString::fromLatin1(t.sidebar0);
    const QString sb1      = QString::fromLatin1(t.sidebar1);
    const QString sbBorder = QString::fromLatin1(t.sidebarBorder);
    const QString textMain = QString::fromLatin1(t.textMain);
    const QString textSub  = QString::fromLatin1(t.textSub);
    const QString bg       = QString::fromLatin1(t.bg);

    const QString cardGrad = QStringLiteral(
        "qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:1 %2)")
        .arg(card0, card1);
    const QString checkedGrad = QStringLiteral(
        "qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:1 %2)")
        .arg(checked0, checked1);
    const QString sidebarGrad = QStringLiteral(
        "qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
        .arg(sb0, sb1);
    const QString topBarGrad = QStringLiteral(
        "qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:1 %2)")
        .arg(card0, card1);
    const QString accentGrad = QStringLiteral(
        "qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:1 %2)")
        .arg(accent, accent2);
    const QString accentRadial = QStringLiteral(
        "qradialgradient(cx:0.5, cy:0.5, radius:0.5, stop:0 %1, stop:1 %2)")
        .arg(accent, accent2);

    const QString soft   = rgba(accent, 0.10);
    const QString softer = rgba(accent, 0.06);
    const QString strong = rgba(accent, 0.18);

    const QString okColor  = t.dark ? QStringLiteral("#2ecc71") : QStringLiteral("#27ae60");
    const QString badColor = t.dark ? QStringLiteral("#e74c3c") : QStringLiteral("#c0392b");

    QString s;

    // ---- 全局 ----
    s += QStringLiteral("QMainWindow { background-color: %1; }\n").arg(bg);
    s += QStringLiteral("QWidget { font-family: \"Microsoft YaHei\", \"PingFang SC\", sans-serif; }\n");
    s += QStringLiteral("QFrame { border: none; }\n");
    s += QStringLiteral("QFrame#centralwidget { background: transparent; }\n");
    s += QStringLiteral("QLabel { color: %1; background: transparent; }\n").arg(textMain);
    s += QStringLiteral("QPushButton { border: none; outline: none; }\n");
    s += QStringLiteral("QToolButton { border: none; outline: none; }\n");
    s += QStringLiteral("QScrollArea#settingsScroll { background: transparent; border: none; }\n");
    s += QStringLiteral("QWidget#settingsContent { background: transparent; }\n");

    // ---- 滚动条（设置页）----
    s += QStringLiteral("QScrollBar:vertical { background: transparent; width: 20px; margin: 0; }\n");
    s += QStringLiteral("QScrollBar::handle:vertical { background: %1; border-radius: 10px; min-height: 28px; }\n")
            .arg(rgba(textSub, 0.40));
    s += QStringLiteral("QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }\n");
    s += QStringLiteral("QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }\n");

    // ---- 侧栏 ----
    s += QStringLiteral("QFrame#sidebar { background: %1; border-right: 1px solid %2; }\n")
            .arg(sidebarGrad, sbBorder);
    s += QStringLiteral("QLabel#logoBadge { background: %1; border-radius: 20px; color: #ffffff;"
                        " font-size: 18px; font-weight: bold; }\n").arg(accentRadial);
    s += QStringLiteral("QToolButton#navBtn { background: transparent; color: %1; font-size: 12px;"
                        " border-left: 3px solid transparent; padding: 4px 0;"
                        " min-width: 76px; min-height: 74px; icon-size: 30px 30px; }\n").arg(textSub);
    s += QStringLiteral("QToolButton#navBtn:hover { color: %1; background: %2; }\n")
            .arg(textMain, softer);
    s += QStringLiteral("QToolButton#navBtn:checked { color: %1; background: %2;"
                        " border-left: 3px solid %1; }\n").arg(accent, soft);

    // ---- 顶栏 ----
    s += QStringLiteral("QFrame#topBar { background: %1; border-bottom: 1px solid %2; }\n")
            .arg(topBarGrad, sbBorder);
    s += QStringLiteral("QLabel#topTitle { color: %1; font-size: 16px; font-weight: bold; }\n").arg(textMain);
    s += QStringLiteral("QLabel#topSubTitle { color: %1; font-size: 11px; }\n").arg(textSub);
    s += QStringLiteral("QLabel#topInfoMain { color: %1; font-size: 12px; }\n").arg(textMain);
    s += QStringLiteral("QLabel#topTemp { color: %1; font-size: 20px; font-weight: bold; }\n").arg(accent);
    s += QStringLiteral("QLabel#topTime { color: %1; font-size: 22px; font-weight: bold; }\n").arg(accent);
    s += QStringLiteral("QLabel#topDateLab { color: %1; font-size: 11px; }\n").arg(textSub);
    s += QStringLiteral("QLabel#topChip { background: %1; border-radius: 12px; color: %2;"
                        " font-size: 12px; padding: 6px 14px; }\n").arg(soft, accent);
    s += QStringLiteral("QPushButton#topIconBtn { background: transparent; icon-size: 22px 22px; }\n");

    // ---- 卡片 ----
    s += QStringLiteral("QFrame#panelCard { background: %1; border-radius: 16px;"
                        " border: 1px solid %2; }\n").arg(cardGrad, border);
    s += QStringLiteral("QLabel#cardTitle { color: %1; font-size: 16px; font-weight: bold; }\n").arg(textMain);
    s += QStringLiteral("QLabel#cardSub { color: %1; font-size: 12px; }\n").arg(textSub);
    s += QStringLiteral("QLabel#bodyMain { color: %1; font-size: 14px; }\n").arg(textMain);
    s += QStringLiteral("QLabel#heroTitle { color: %1; font-size: 22px; font-weight: bold; }\n").arg(textMain);
    s += QStringLiteral("QLabel#heroAccent { color: %1; font-size: 22px; font-weight: bold; }\n").arg(accent);
    s += QStringLiteral("QLabel#pageTitle { color: %1; font-size: 20px; font-weight: bold; }\n").arg(accent);
    s += QStringLiteral("QLabel#pageHint { color: %1; font-size: 14px; }\n").arg(textSub);
    s += QStringLiteral("QLabel#sectionIcon { background: transparent; }\n");

    // ---- 传感器 / 概览小格 ----
    s += QStringLiteral("QFrame#sensorBox { background: %1; border-radius: 10px; }\n").arg(softer);
    s += QStringLiteral("QFrame#infoBox { background: %1; border-radius: 10px; }\n").arg(softer);
    s += QStringLiteral("QLabel#sensorVal { color: %1; font-size: 12px; font-weight: bold; }\n").arg(accent);
    s += QStringLiteral("QLabel#sensorLab { color: %1; font-size: 11px; }\n").arg(textSub);

    // ---- 图片 / 圆盘占位 ----
    s += QStringLiteral("QFrame#roomImageBox { background: %1; border-radius: 12px; }\n").arg(strong);
    s += QStringLiteral("QFrame#idImageBox { background: %1; border-radius: 8px; }\n").arg(textMain);
    s += QStringLiteral("QLabel#idImageText { color: %1; font-size: 20px; font-weight: bold; }\n").arg(bg);
    s += QStringLiteral("QLabel#musicDisc { background: %1; border: 2px solid %2;"
                        " border-radius: 28px; }\n").arg(soft, accent);

    // ---- 入住页文字 ----
    s += QStringLiteral("QLabel#idLabel { color: %1; font-size: 12px; }\n").arg(textSub);
    s += QStringLiteral("QLabel#idValue { color: %1; font-size: 15px; }\n").arg(textMain);
    s += QStringLiteral("QLabel#roomInfoLabel { color: %1; font-size: 12px; }\n").arg(textSub);
    s += QStringLiteral("QLabel#roomInfoValue { color: %1; font-size: 15px; }\n").arg(textMain);
    s += QStringLiteral("QLabel#resultOk { color: %1; font-size: 14px; }\n").arg(okColor);

    // ---- 状态胶囊 ----
    s += QStringLiteral("QLabel#statusPill { background: %1; color: %2; border-radius: 12px;"
                        " padding: 6px 16px; font-size: 14px; }\n").arg(rgba(textSub, 0.15), textSub);
    s += QStringLiteral("QLabel#statusPillBusy { background: %1; color: %2; border-radius: 12px;"
                        " padding: 6px 16px; font-size: 14px; }\n").arg(soft, accent);
    s += QStringLiteral("QLabel#statusPillOk { background: %1; color: %2; border-radius: 12px;"
                        " padding: 6px 16px; font-size: 14px; }\n").arg(rgba(okColor, 0.15), okColor);
    s += QStringLiteral("QLabel#statusPillFail { background: %1; color: %2; border-radius: 12px;"
                        " padding: 6px 16px; font-size: 14px; }\n").arg(rgba(badColor, 0.15), badColor);

    // ---- 大按钮（客房开关 / 服务页动作）----
    s += QStringLiteral("QPushButton#bigBtn { background: %1; border-radius: 14px; border: 1px solid %2;"
                        " color: %3; font-size: 14px; text-align: left; padding-left: 18px;"
                        " icon-size: 26px 26px; }\n").arg(cardGrad, border, textMain);
    s += QStringLiteral("QPushButton#bigBtn:hover { border: 1px solid %1; }\n").arg(accent);
    s += QStringLiteral("QPushButton#bigBtn:checked { background: %1; border: 1px solid %2;"
                        " color: %2; }\n").arg(checkedGrad, accent);

    // ---- 磁贴（首页 4 个快捷入口）----
    s += QStringLiteral("QPushButton#tileBtn { background: %1; border-radius: 14px; border: 1px solid %2;"
                        " color: %3; font-size: 14px; text-align: left; padding-left: 14px;"
                        " icon-size: 26px 26px; }\n").arg(cardGrad, border, textMain);
    s += QStringLiteral("QPushButton#tileBtn:hover { border: 1px solid %1; background: %2; }\n")
            .arg(accent, checkedGrad);
    s += QStringLiteral("QPushButton#tileBtn:pressed { border: 1px solid %1; }\n").arg(accent);

    // ---- 点单按钮（可多选）----
    s += QStringLiteral("QPushButton#svcBtn { background: %1; border-radius: 12px; border: 1px solid %2;"
                        " color: %3; font-size: 14px; text-align: left; padding-left: 12px;"
                        " icon-size: 22px 22px; }\n").arg(cardGrad, border, textMain);
    s += QStringLiteral("QPushButton#svcBtn:hover { border: 1px solid %1; }\n").arg(accent);
    s += QStringLiteral("QPushButton#svcBtn:checked { background: %1; border: 1px solid %2;"
                        " color: %2; font-weight: bold; }\n").arg(checkedGrad, accent);

    // ---- 电话按钮 ----
    s += QStringLiteral("QPushButton#phoneBtn { background: %1; border-radius: 12px; border: 1px solid %2;"
                        " color: %3; font-size: 14px; icon-size: 20px 20px; }\n").arg(cardGrad, border, textMain);
    s += QStringLiteral("QPushButton#phoneBtn:hover { border: 1px solid %1; color: %1; }\n").arg(accent);
    s += QStringLiteral("QPushButton#phoneBtn:pressed { background: %1; }\n").arg(checkedGrad);

    // ---- 主题按钮 ----
    s += QStringLiteral("QPushButton#themeBtn { background: %1; border: 1px solid %2; border-radius: 10px;"
                        " color: %3; font-size: 14px; padding: 6px 4px; }\n").arg(cardGrad, border, textMain);
    s += QStringLiteral("QPushButton#themeBtn:hover { border: 1px solid %1; }\n").arg(accent);
    s += QStringLiteral("QPushButton#themeBtn:checked { background: %1; border: 2px solid %2;"
                        " color: %2; font-weight: bold; }\n").arg(checkedGrad, accent);

    // ---- 圆按钮 ----
    s += QStringLiteral("QPushButton#circleBtn { background: %1; border-radius: 17px; color: %2;"
                        " icon-size: 18px 18px; }\n").arg(soft, accent);
    s += QStringLiteral("QPushButton#circleBtn:hover { background: %1; }\n").arg(strong);
    s += QStringLiteral("QPushButton#circleBtnPrimary { background: %1; border-radius: 20px;"
                        " color: #ffffff; icon-size: 22px 22px; }\n").arg(accentGrad);

    // ---- 入住操作按钮 ----
    s += QStringLiteral("QPushButton#actionBtn { background: %1; border-radius: 12px; border: 1px solid %2;"
                        " color: %3; font-size: 14px; icon-size: 22px 22px; }\n").arg(cardGrad, border, textMain);
    s += QStringLiteral("QPushButton#actionBtn:hover { border: 1px solid %1; }\n").arg(accent);
    s += QStringLiteral("QPushButton#actionBtnPrimary { background: %1; border-radius: 12px;"
                        " color: #ffffff; font-size: 14px; font-weight: bold;"
                        " icon-size: 22px 22px; }\n").arg(accentGrad);
    s += QStringLiteral("QPushButton#actionBtnPrimary:disabled { background: %1; color: %2; }\n")
            .arg(rgba(textSub, 0.18), textSub);

    // ---- 滑块 ----
    s += QStringLiteral("QSlider::groove:vertical { border-radius: 8px; width: 16px; background: %1; }\n")
            .arg(rgba(textSub, 0.25));
    s += QStringLiteral("QSlider::handle:vertical { height: 28px; border-radius: 14px;"
                        " background: %1; margin: -2px 0; }\n").arg(accent);
    s += QStringLiteral("QSlider::sub-page:vertical { border-radius: 8px; background: %1; }\n").arg(accentGrad);

    return s;
}

void MainWindow::applyTheme()
{
    const ThemeSpec &t = THEMES[qBound(0, m_themeIndex, 5)];

    setStyleSheet(buildStyleSheet(t));
    invalidateThemeBackground();   // 主题背景层随主题重建
    update();

    const QString accent   = QString::fromLatin1(t.accent);
    const QString accent2  = QString::fromLatin1(t.accent2);
    const QString card0    = QString::fromLatin1(t.card0);
    const QString card1    = QString::fromLatin1(t.card1);
    const QString border   = QString::fromLatin1(t.border);
    const QString textMain = QString::fromLatin1(t.textMain);
    const QString textSub  = QString::fromLatin1(t.textSub);

    // 代码构建的卡片不会自动套用上面的 QSS 钩子，需要显式下发主题参数
    if (m_wifiCard)
        m_wifiCard->applyTheme(t.dark, accent, accent2, card0, card1, border, textMain, textSub);
    if (m_cloudCard)
        m_cloudCard->applyTheme(t.dark, accent, accent2, card0, card1, border, textMain, textSub);
    if (m_faceCamView)
        m_faceCamView->applyTheme(t.dark, accent, accent2, card0, card1, border, textMain, textSub);

    // 图标按深/浅两套变体重载
    setupIcons();
    applyButtonStyles();
}

// ===================== 主题背景层 =====================
// 在窗口最底层绘制与当前主题匹配的渐变/光效图案；侧栏、顶栏、卡片都
// 绘制在它之上，所以只在页面留白处可见。基础层 + 装饰层分开缓存，
// 重绘时只做两次 drawPixmap，不占用 Cortex-A9 的 CPU。

void MainWindow::invalidateThemeBackground()
{
    m_bgBase    = QPixmap();
    m_bgOverlay = QPixmap();
    m_bgSize    = QSize();
    m_bgTheme   = -1;
}

void MainWindow::ensureThemeBackground()
{
    if (m_bgTheme == m_themeIndex && m_bgSize == size() && !m_bgBase.isNull())
        return;
    buildThemeBackground(m_bgBase, THEMES[qBound(0, m_themeIndex, 5)], size());
    m_bgTheme = m_themeIndex;
    m_bgSize  = size();
}

void MainWindow::buildThemeBackground(QPixmap &target, const ThemeSpec &theme,
                                      const QSize &size)
{
    if (size.isEmpty()) {
        target = QPixmap();
        m_bgOverlay = QPixmap();
        return;
    }

    const QColor bg      = QColor(QString::fromLatin1(theme.bg));
    const QColor accent  = QColor(QString::fromLatin1(theme.accent));
    const QColor accent2 = QColor(QString::fromLatin1(theme.accent2));

    // ---- 基础层：页面背景色对角渐变，终点混入 8% 强调色与主题联动 ----
    QColor bg2 = bg;
    bg2.setRed(  qBound(0, bg.red()   * 92 / 100 + accent.red()   * 8 / 100, 255));
    bg2.setGreen(qBound(0, bg.green() * 92 / 100 + accent.green() * 8 / 100, 255));
    bg2.setBlue( qBound(0, bg.blue()  * 92 / 100 + accent.blue()  * 8 / 100, 255));

    QPixmap base(size);
    base.fill(Qt::transparent);
    {
        QPainter p(&base);
        QLinearGradient diag(0.0, 0.0, size.width(), size.height());
        diag.setColorAt(0.0, bg);
        diag.setColorAt(1.0, bg2);
        p.fillRect(QRect(QPoint(0, 0), size), diag);

        // 右上 / 左下各一团很淡的光晕，只提示主题氛围
        QRadialGradient glow1(QPoint(size.width() * 5 / 6, size.height() / 8),
                              size.width() * 2 / 3);
        glow1.setColorAt(0.0, QColor(accent.red(),  accent.green(),  accent.blue(),  26));
        glow1.setColorAt(1.0, QColor(accent.red(),  accent.green(),  accent.blue(),  0));
        p.fillRect(QRect(QPoint(0, 0), size), glow1);

        QRadialGradient glow2(QPoint(size.width() / 6, size.height() * 7 / 8),
                              size.width() * 2 / 3);
        glow2.setColorAt(0.0, QColor(accent2.red(), accent2.green(), accent2.blue(), 20));
        glow2.setColorAt(1.0, QColor(accent2.red(), accent2.green(), accent2.blue(), 0));
        p.fillRect(QRect(QPoint(0, 0), size), glow2);
    }
    target = base;

    // ---- 装饰层：右上点阵 + 全屏星点/气泡（固定种子伪随机，位置稳定） ----
    QPixmap overlay(size);
    overlay.fill(Qt::transparent);
    {
        QPainter p(&overlay);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        const int dotAlpha = theme.dark ? 30 : 46;
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), dotAlpha));
        const int spacing = 22;
        const int cols = (size.width()  / spacing) / 4;   // 只铺右上 1/4 区域
        const int rows = (size.height() / spacing) / 3;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int x = size.width() - cols * spacing + c * spacing + 8;
                const int y = 10 + r * spacing;
                p.drawEllipse(x, y, 2, 2);
            }
        }

        quint32 seed = 20260915u;   // 固定种子：每次重建图案一致，不闪烁
        const int maxR = theme.dark ? 4 : 7;
        const int bubbleAlpha = theme.dark ? 36 : 52;
        p.setBrush(QColor(accent2.red(), accent2.green(), accent2.blue(), bubbleAlpha));
        for (int i = 0; i < 26; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const int x = int((seed >> 8) % quint32(size.width()));
            seed = seed * 1664525u + 1013904223u;
            const int y = int((seed >> 8) % quint32(size.height()));
            seed = seed * 1664525u + 1013904223u;
            const int rr = 2 + int((seed >> 8) % quint32(maxR));
            p.drawEllipse(QPoint(x, y), rr, rr);
        }
    }
    m_bgOverlay = overlay;
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    ensureThemeBackground();
    QPainter p(this);
    if (!m_bgBase.isNull())    p.drawPixmap(0, 0, m_bgBase);
    if (!m_bgOverlay.isNull()) p.drawPixmap(0, 0, m_bgOverlay);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    invalidateThemeBackground();
    QMainWindow::resizeEvent(event);
}

void MainWindow::applyButtonStyles()
{
    // 主题按钮：当前主题高亮（QSS 的 :checked 由此驱动）
    for (int i = 0; i < m_themeButtons.size(); ++i) {
        if (m_themeButtons[i]) {
            m_themeButtons[i]->setChecked(i == m_themeIndex);
            m_themeButtons[i]->setIconSize(QSize(14, 14));
        }
    }

    // QSS 的 icon-size 对 QPushButton 并非所有版本都生效，这里再兜一层
    if (ui->weatherIconBtn)
        ui->weatherIconBtn->setIconSize(QSize(22, 22));

    const QSize circle(18, 18);
    QList<QPushButton *> circles;
    circles << ui->btnAcMinus << ui->btnAcPlus << ui->btnPrev << ui->btnNext;
    for (QPushButton *b : circles) {
        if (b) b->setIconSize(circle);
    }
    if (ui->btnPlay) ui->btnPlay->setIconSize(QSize(22, 22));
}

// ---------------------------------------------------------------------------
// 图标
// ---------------------------------------------------------------------------
QPixmap MainWindow::loadIconPng(const QString &baseName, const QSize &size)
{
    // The .pro embeds pre-rendered PNG icons because QtSvg is unreliable
    // on the ARM board ("Cannot open file ':/icons/xxx.svg'"). Keep the
    // SVG files as editable source, but always load PNG at runtime.
    // 深色主题用白色图标（.png），浅色主题用深色描边变体（-light.png）。
    const ThemeSpec &t = THEMES[qBound(0, m_themeIndex, 5)];
    const QString suffix = t.dark ? QStringLiteral(".png") : QStringLiteral("-light.png");

    QPixmap pix(QStringLiteral(":/icons/") + baseName + suffix);
    if (!pix.isNull()) {
        return pix.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Fallback to a tiny blank pixmap so the UI still has stable geometry.
    QPixmap fallback(size);
    fallback.fill(Qt::transparent);
    return fallback;
}

void MainWindow::setupIcons()
{
    const ThemeSpec &t = THEMES[qBound(0, m_themeIndex, 5)];
    const QString iconSuffix = t.dark ? QStringLiteral(".png") : QStringLiteral("-light.png");

    auto setBtnIcon = [&](QAbstractButton *btn, const QString &name) {
        if (btn) btn->setIcon(QIcon(QStringLiteral(":/icons/") + name + iconSuffix));
    };

    // ---- 标题小图标 ----
    const int smallSize = 22;
    const int sectionSize = 24;
    const int largeSize = 32;
    ui->roomCardIcon->setPixmap(loadIconPng(QStringLiteral("home"), QSize(sectionSize, sectionSize)));
    ui->musicTitleIcon->setPixmap(loadIconPng(QStringLiteral("music"), QSize(smallSize, smallSize)));
    ui->acTitleIcon->setPixmap(loadIconPng(QStringLiteral("snowflake"), QSize(smallSize, smallSize)));
    ui->envTitleIcon->setPixmap(loadIconPng(QStringLiteral("leaf"), QSize(sectionSize, sectionSize)));
    ui->orderTitleIcon->setPixmap(loadIconPng(QStringLiteral("bell"), QSize(sectionSize, sectionSize)));
    ui->phoneTitleIcon->setPixmap(loadIconPng(QStringLiteral("phone"), QSize(sectionSize, sectionSize)));
    ui->checkinTitleIcon->setPixmap(loadIconPng(QStringLiteral("key"), QSize(largeSize, largeSize)));
    ui->faceTitleIcon->setPixmap(loadIconPng(QStringLiteral("face"), QSize(smallSize, smallSize)));
    ui->idTitleIcon->setPixmap(loadIconPng(QStringLiteral("id-card"), QSize(smallSize, smallSize)));
    ui->resultTitleIcon->setPixmap(loadIconPng(QStringLiteral("check"), QSize(smallSize, smallSize)));
    ui->roomInfoTitleIcon->setPixmap(loadIconPng(QStringLiteral("key"), QSize(smallSize, smallSize)));

    // ---- 传感器图标（与 STM32 上报的物理量一一对应）----
    const int sensorIconSize = 20;
    ui->sensorTempIcon->setPixmap(loadIconPng(QStringLiteral("thermometer"), QSize(sensorIconSize, sensorIconSize)));
    ui->sensorHumiIcon->setPixmap(loadIconPng(QStringLiteral("droplet"), QSize(sensorIconSize, sensorIconSize)));
    ui->sensorCo2Icon->setPixmap(loadIconPng(QStringLiteral("flame"), QSize(sensorIconSize, sensorIconSize)));
    ui->sensorPmIcon->setPixmap(loadIconPng(QStringLiteral("gas"), QSize(sensorIconSize, sensorIconSize)));
    ui->sensorHchoIcon->setPixmap(loadIconPng(QStringLiteral("fan"), QSize(sensorIconSize, sensorIconSize)));
    ui->sensorIonIcon->setPixmap(loadIconPng(QStringLiteral("pump"), QSize(sensorIconSize, sensorIconSize)));

    // ---- 首页装饰图案（欢迎卡/客房示意图/信息格子）----
    if (ui->welcomeIcon)
        ui->welcomeIcon->setPixmap(loadIconPng(QStringLiteral("home"), QSize(24, 24)));
    if (ui->roomImageIcon)
        ui->roomImageIcon->setPixmap(loadIconPng(QStringLiteral("bed"), QSize(44, 44)));
    const int infoIconSize = 16;
    if (ui->infoRoomIcon)
        ui->infoRoomIcon->setPixmap(loadIconPng(QStringLiteral("key"), QSize(infoIconSize, infoIconSize)));
    if (ui->infoLeaveIcon)
        ui->infoLeaveIcon->setPixmap(loadIconPng(QStringLiteral("door"), QSize(infoIconSize, infoIconSize)));
    if (ui->infoBreakfastIcon)
        ui->infoBreakfastIcon->setPixmap(loadIconPng(QStringLiteral("food"), QSize(infoIconSize, infoIconSize)));
    if (ui->infoWifiIcon)
        ui->infoWifiIcon->setPixmap(loadIconPng(QStringLiteral("info"), QSize(infoIconSize, infoIconSize)));
    if (ui->infoForecastIcon)
        ui->infoForecastIcon->setPixmap(loadIconPng(QStringLiteral("weather"), QSize(infoIconSize, infoIconSize)));

    // ---- 音乐卡中央圆盘 ----
    ui->musicIcon->setPixmap(loadIconPng(QStringLiteral("music"), QSize(30, 30)));

    // ---- 侧栏导航 ----
    setBtnIcon(ui->navHome, QStringLiteral("home"));
    setBtnIcon(ui->navRoom, QStringLiteral("bed"));
    setBtnIcon(ui->navCheckin, QStringLiteral("key"));
    setBtnIcon(ui->navService, QStringLiteral("bell"));
    setBtnIcon(ui->navSettings, QStringLiteral("gear"));

    // ---- 首页磁贴 ----
    setBtnIcon(ui->btnTileRoom, QStringLiteral("bed"));
    setBtnIcon(ui->btnTileService, QStringLiteral("bell"));
    setBtnIcon(ui->btnTileCheckin, QStringLiteral("key"));
    setBtnIcon(ui->btnTileSettings, QStringLiteral("gear"));

    // ---- 客房开关 ----
    setBtnIcon(ui->btnDnd, QStringLiteral("dnd"));
    setBtnIcon(ui->btnClean, QStringLiteral("broom"));
    setBtnIcon(ui->btnDoor, QStringLiteral("door"));
    setBtnIcon(ui->btnLight, QStringLiteral("bulb"));
    setBtnIcon(ui->btnScene, QStringLiteral("scene"));

    // ---- 服务页 ----
    setBtnIcon(ui->btnOrderTowel, QStringLiteral("towel"));
    setBtnIcon(ui->btnOrderWater, QStringLiteral("water"));
    setBtnIcon(ui->btnOrderLaundry, QStringLiteral("laundry"));
    setBtnIcon(ui->btnOrderAlarm, QStringLiteral("alarm"));
    setBtnIcon(ui->btnOrderFood, QStringLiteral("food"));
    setBtnIcon(ui->btnOrderExtraBed, QStringLiteral("extra-bed"));
    setBtnIcon(ui->btnPhoneFront, QStringLiteral("bell"));
    setBtnIcon(ui->btnPhoneRestaurant, QStringLiteral("food"));
    setBtnIcon(ui->btnPhoneClean, QStringLiteral("broom"));
    setBtnIcon(ui->btnPhoneSecurity, QStringLiteral("lock"));
    setBtnIcon(ui->btnCall, QStringLiteral("phone"));
    setBtnIcon(ui->btnService, QStringLiteral("bell"));
    setBtnIcon(ui->btnInfo, QStringLiteral("info"));
    setBtnIcon(ui->btnPhoneBook, QStringLiteral("phone"));

    // ---- 空调 / 音乐 ----
    setBtnIcon(ui->btnAcMinus, QStringLiteral("minus"));
    setBtnIcon(ui->btnAcPlus, QStringLiteral("plus"));
    setBtnIcon(ui->btnPrev, QStringLiteral("prev"));
    setBtnIcon(ui->btnNext, QStringLiteral("next"));
    setBtnIcon(ui->btnPlay, m_isPlaying ? QStringLiteral("pause") : QStringLiteral("play"));

    // ---- 入住操作 ----
    setBtnIcon(ui->btnReadId, QStringLiteral("id-card"));
    setBtnIcon(ui->btnStartFace, QStringLiteral("face"));
    setBtnIcon(ui->btnConfirmCheckin, QStringLiteral("check"));
    setBtnIcon(ui->btnPrint, QStringLiteral("printer"));
    setBtnIcon(ui->btnCardAuth, QStringLiteral("lock"));

    // ---- 顶栏 ----
    setBtnIcon(ui->weatherIconBtn, QStringLiteral("weather"));
}

// ---------------------------------------------------------------------------
// 信号连接
// ---------------------------------------------------------------------------
void MainWindow::setupConnections()
{
    // 导航切换 - QToolButton
    connect(ui->navHome, &QToolButton::clicked, this, &MainWindow::onNavHome);
    connect(ui->navRoom, &QToolButton::clicked, this, &MainWindow::onNavRoom);
    connect(ui->navCheckin, &QToolButton::clicked, this, &MainWindow::onNavCheckin);
    connect(ui->navService, &QToolButton::clicked, this, &MainWindow::onNavService);
    connect(ui->navSettings, &QToolButton::clicked, this, &MainWindow::onNavSettings);

    // 首页磁贴
    connect(ui->btnTileRoom, &QPushButton::clicked, this, &MainWindow::onTileRoom);
    connect(ui->btnTileService, &QPushButton::clicked, this, &MainWindow::onTileService);
    connect(ui->btnTileCheckin, &QPushButton::clicked, this, &MainWindow::onTileCheckin);
    connect(ui->btnTileSettings, &QPushButton::clicked, this, &MainWindow::onTileSettings);

    // 空调控制
    connect(ui->acSlider, &QSlider::valueChanged, this, &MainWindow::onAcSliderChanged);
    connect(ui->btnAcMinus, &QPushButton::clicked, this, &MainWindow::onAcMinus);
    connect(ui->btnAcPlus, &QPushButton::clicked, this, &MainWindow::onAcPlus);

    // 客房开关
    connect(ui->btnDnd, &QPushButton::toggled, this, &MainWindow::onDndToggled);
    connect(ui->btnClean, &QPushButton::toggled, this, &MainWindow::onCleanToggled);
    connect(ui->btnDoor, &QPushButton::pressed, this, &MainWindow::onDoorPressed);
    connect(ui->btnDoor, &QPushButton::released, this, &MainWindow::onDoorReleased);
    connect(ui->btnLight, &QPushButton::toggled, this, &MainWindow::onLightToggled);
    connect(ui->btnScene, &QPushButton::toggled, this, &MainWindow::onSceneToggled);

    // 服务页动作
    connect(ui->btnCall, &QPushButton::toggled, this, &MainWindow::onCallToggled);
    connect(ui->btnService, &QPushButton::clicked, this, &MainWindow::onServiceClicked);
    connect(ui->btnInfo, &QPushButton::clicked, this, &MainWindow::onInfoClicked);
    connect(ui->btnPhoneBook, &QPushButton::clicked, this, &MainWindow::onPhoneBookClicked);

    // 客房点单（多选，副标题实时汇总）
    QList<QPushButton *> orderBtns;
    orderBtns << ui->btnOrderTowel << ui->btnOrderWater << ui->btnOrderLaundry
              << ui->btnOrderAlarm << ui->btnOrderFood << ui->btnOrderExtraBed;
    for (QPushButton *b : orderBtns) {
        if (b) connect(b, &QPushButton::toggled, this, &MainWindow::onServiceOrderToggled);
    }

    // 点单确认/保存
    connect(ui->btnOrderSave, &QPushButton::clicked, this, &MainWindow::onOrderSaveClicked);

    // 常用电话（一键呼叫）
    const QStringList phoneNames = QStringList()
        << tr("前台") << tr("餐厅") << tr("保洁") << tr("安保");
    QList<QPushButton *> phoneBtns;
    phoneBtns << ui->btnPhoneFront << ui->btnPhoneRestaurant
              << ui->btnPhoneClean << ui->btnPhoneSecurity;
    for (int i = 0; i < phoneBtns.size() && i < phoneNames.size(); ++i) {
        QPushButton *b = phoneBtns.at(i);
        if (!b) continue;
        const QString name = phoneNames.at(i);
        connect(b, &QPushButton::clicked, this, [this, name]() {
            showToast(tr("呼叫"), tr("正在呼叫%1...").arg(name));
        });
    }

    // 音乐控制
    connect(ui->btnPlay, &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(ui->btnPrev, &QPushButton::clicked, this, &MainWindow::onPrevTrack);
    connect(ui->btnNext, &QPushButton::clicked, this, &MainWindow::onNextTrack);

    // 入住办理
    connect(ui->btnReadId, &QPushButton::clicked, this, &MainWindow::onReadIdClicked);
    connect(ui->btnStartFace, &QPushButton::clicked, this, &MainWindow::onStartFaceClicked);
    connect(ui->btnConfirmCheckin, &QPushButton::clicked, this, &MainWindow::onConfirmCheckinClicked);
    connect(ui->btnPrint, &QPushButton::clicked, this, &MainWindow::onPrintClicked);
    connect(ui->btnCardAuth, &QPushButton::clicked, this, &MainWindow::onCardAuthClicked);
}

// ---------------------------------------------------------------------------
// 顶栏时钟
// ---------------------------------------------------------------------------
QString MainWindow::currentDateTimeString() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy年M月d日 dddd"));
}

void MainWindow::onTimeUpdate()
{
    const QDateTime now = QDateTime::currentDateTime();
    ui->topTime->setText(now.toString(QStringLiteral("hh:mm")));
    ui->topDate->setText(currentDateTimeString());
}

// ---------------------------------------------------------------------------
// 页面导航（首页 / 客房 / 服务 / 入住 / 设置）
// ---------------------------------------------------------------------------
void MainWindow::onNavHome()
{
    if (ui->stackedWidget->currentWidget() == ui->pageCheckin)
        onCheckinPageLeft();
    if (ui->stackedWidget->currentWidget() == ui->pageSettings) {
        if (m_wifiCard)  m_wifiCard->dismissKeyboard();
        if (m_cloudCard) m_cloudCard->dismissKeyboard();
    }

    ui->stackedWidget->setCurrentWidget(ui->pageHome);
    ui->topTitle->setText(tr("欢迎入住 · 沈阳盛京酒店"));
    ui->topSubTitle->setText(tr("用心服务 · 让旅途更美好"));
    ui->navHome->setChecked(true);
}

void MainWindow::onNavRoom()
{
    if (ui->stackedWidget->currentWidget() == ui->pageCheckin)
        onCheckinPageLeft();
    if (ui->stackedWidget->currentWidget() == ui->pageSettings) {
        if (m_wifiCard)  m_wifiCard->dismissKeyboard();
        if (m_cloudCard) m_cloudCard->dismissKeyboard();
    }

    ui->stackedWidget->setCurrentWidget(ui->pageRoom);
    ui->topTitle->setText(tr("客房控制"));
    ui->topSubTitle->setText(tr("舒适每一刻"));
    ui->navRoom->setChecked(true);
}

void MainWindow::onNavCheckin()
{
    if (ui->stackedWidget->currentWidget() == ui->pageSettings) {
        if (m_wifiCard)  m_wifiCard->dismissKeyboard();
        if (m_cloudCard) m_cloudCard->dismissKeyboard();
    }

    ui->stackedWidget->setCurrentWidget(ui->pageCheckin);
    ui->topTitle->setText(tr("身份验证 / 入住办理"));
    ui->topSubTitle->setText(tr("快速核验 · 安心入住"));
    ui->navCheckin->setChecked(true);
    onCheckinPageEntered();
}

void MainWindow::onNavService()
{
    if (ui->stackedWidget->currentWidget() == ui->pageCheckin)
        onCheckinPageLeft();
    if (ui->stackedWidget->currentWidget() == ui->pageSettings) {
        if (m_wifiCard)  m_wifiCard->dismissKeyboard();
        if (m_cloudCard) m_cloudCard->dismissKeyboard();
    }

    ui->stackedWidget->setCurrentWidget(ui->pageService);
    ui->topTitle->setText(tr("酒店服务"));
    ui->topSubTitle->setText(tr("贴心服务"));
    ui->navService->setChecked(true);
}

void MainWindow::onNavSettings()
{
    if (ui->stackedWidget->currentWidget() == ui->pageCheckin)
        onCheckinPageLeft();

    ui->stackedWidget->setCurrentWidget(ui->pageSettings);
    ui->topTitle->setText(tr("系统设置"));
    ui->topSubTitle->setText(tr("系统管理与维护"));
    ui->navSettings->setChecked(true);

    // 进入设置页时回到顶部
    if (ui->settingsScroll)
        ui->settingsScroll->verticalScrollBar()->setValue(0);
}

// 首页 4 个磁贴
void MainWindow::onTileRoom()     { onNavRoom(); }
void MainWindow::onTileService()  { onNavService(); }
void MainWindow::onTileCheckin()  { onNavCheckin(); }
void MainWindow::onTileSettings() { onNavSettings(); }

// ---------------------------------------------------------------------------
// 客房控制
// ---------------------------------------------------------------------------
void MainWindow::onAcSliderChanged(int value)
{
    ui->acTempBig->setText(QString::number(value) + QStringLiteral("°C"));
}

void MainWindow::onAcMinus()
{
    const int v = ui->acSlider->value();
    if (v > ui->acSlider->minimum())
        ui->acSlider->setValue(v - 1);
}

void MainWindow::onAcPlus()
{
    const int v = ui->acSlider->value();
    if (v < ui->acSlider->maximum())
        ui->acSlider->setValue(v + 1);
}

void MainWindow::onDndToggled(bool checked)
{
    if (checked) {
        ui->btnClean->setChecked(false);
        ui->btnDnd->setText(tr("请勿打扰 - 已开启"));
    } else {
        ui->btnDnd->setText(tr("请勿打扰"));
    }
    sendRoomState();
}

void MainWindow::onCleanToggled(bool checked)
{
    if (checked) {
        ui->btnDnd->setChecked(false);
        ui->btnClean->setText(tr("请即打扫 - 已呼叫"));
    } else {
        ui->btnClean->setText(tr("请即打扫"));
    }
    sendRoomState();
}

// 房态写入云端影子 desired：dnd/clean 两个 0/1 属性（需在 IoTDA 产品模型中定义）
void MainWindow::sendRoomState()
{
    if (!m_cloudClient || !ui->btnDnd || !ui->btnClean)
        return;
    QVariantMap props;
    props.insert(QStringLiteral("dnd"),   ui->btnDnd->isChecked()   ? 1 : 0);
    props.insert(QStringLiteral("clean"), ui->btnClean->isChecked() ? 1 : 0);
    m_cloudClient->setDesired(props);
}

void MainWindow::onRoomStateWritten(bool ok, const QString &msg)
{
    qDebug().noquote() << "[roomstate]" << (ok ? "OK" : "FAIL") << msg;
    if (ok)
        showToast(tr("房态"), tr("已同步到云端，前台可见。"));
    else
        showToast(tr("房态"), tr("同步失败：%1").arg(msg));
}

void MainWindow::onDoorPressed()
{
    ui->btnDoor->setText(tr("正在开门..."));
}

void MainWindow::onDoorReleased()
{
    ui->btnDoor->setText(tr("长按开门"));
    showToast(tr("门锁"), tr("房门已开启，请随手关门。"));
}

void MainWindow::onLightToggled(bool checked)
{
    ui->btnLight->setText(checked ? tr("一键关灯") : tr("一键亮灯"));
}

void MainWindow::onSceneToggled(bool checked)
{
    ui->btnScene->setText(checked ? tr("场景模式 - 睡眠") : tr("场景模式"));
}

void MainWindow::onCallToggled(bool checked)
{
    ui->btnCall->setText(checked ? tr("正在呼叫...") : tr("呼叫前台"));
    if (checked)
        showToast(tr("呼叫前台"), tr("已为您接通前台，请稍候。"));
}

void MainWindow::onPlayClicked()
{
    if (m_tracks.isEmpty()) {         // music/ 目录没有音频文件
        showToast(tr("音乐"), tr("music 文件夹中没有歌曲文件"));
        return;
    }
    if (m_trackIndex < 0) {           // 未选曲：从第一首开始播放
        m_trackIndex = 0;
        m_isPlaying = true;
    } else {
        m_isPlaying = !m_isPlaying;   // 暂停 / 继续
    }
    updateMusicUi();
}

void MainWindow::onNextTrack()
{
    if (m_tracks.isEmpty()) return;
    m_trackIndex = (m_trackIndex < 0) ? 0 : (m_trackIndex + 1) % m_tracks.size();
    // 保持当前播放状态：未播放时切歌不自动开播
    updateMusicUi();
}

void MainWindow::onPrevTrack()
{
    if (m_tracks.isEmpty()) return;
    m_trackIndex = (m_trackIndex <= 0) ? m_tracks.size() - 1 : m_trackIndex - 1;
    // 保持当前播放状态
    updateMusicUi();
}

void MainWindow::updateMusicUi()
{
    if (m_trackIndex < 0 || m_trackIndex >= m_tracks.size()) {
        ui->musicSong->setText(tr("暂无歌曲"));
        ui->musicHint->setText(tr("请选择播放"));
    } else {
        ui->musicSong->setText(m_tracks.at(m_trackIndex));
        ui->musicHint->setText(m_isPlaying ? tr("正在播放 · 酒店甄选歌单")
                                           : tr("已暂停 · 点击继续"));
    }
    // 与 applyButtonStyles 同一套图标机制，状态/主题切换保持一致
    const ThemeSpec &t = THEMES[qBound(0, m_themeIndex, 5)];
    const QString suffix = t.dark ? QStringLiteral(".png") : QStringLiteral("-light.png");
    ui->btnPlay->setIcon(QIcon(QStringLiteral(":/icons/")
                               + (m_isPlaying ? QStringLiteral("pause") : QStringLiteral("play"))
                               + suffix));
}

// ---------------------------------------------------------------------------
// 酒店服务
// ---------------------------------------------------------------------------
void MainWindow::onServiceOrderToggled(bool)
{
    updateServiceSummary();
}

void MainWindow::onOrderSaveClicked()
{
    updateServiceSummary();
    if (m_orderPicked.isEmpty()) {
        showToast(tr("客房点单"), tr("请先选择需要的服务项目。"));
        return;
    }
    ui->orderSub->setText(tr("已保存 · 共 %1 项服务，可继续修改").arg(m_orderPicked.size()));
    showToast(tr("客房点单"), tr("已保存 %1 项服务：%2")
                                  .arg(m_orderPicked.size())
                                  .arg(m_orderPicked.join(QStringLiteral("、"))));
}

void MainWindow::updateServiceSummary()
{
    QList<QPushButton *> items;
    items << ui->btnOrderTowel << ui->btnOrderWater << ui->btnOrderLaundry
          << ui->btnOrderAlarm << ui->btnOrderFood << ui->btnOrderExtraBed;

    m_orderPicked.clear();
    for (QPushButton *b : items) {
        if (b && b->isChecked())
            m_orderPicked << b->text();
    }

    if (m_orderPicked.isEmpty()) {
        ui->orderSub->setText(tr("请选择需要的服务（可多选）"));
    } else {
        ui->orderSub->setText(tr("已选 %1 项：%2")
                              .arg(m_orderPicked.size())
                              .arg(m_orderPicked.join(QStringLiteral("、"))));
    }
}

void MainWindow::onServiceClicked()
{
    showToast(tr("酒店服务"), tr("已打开酒店服务菜单。"));
}

void MainWindow::onInfoClicked()
{
    showToast(tr("信息中心"), tr("已打开信息中心。"));
}

void MainWindow::onPhoneBookClicked()
{
    showToast(tr("常用电话"), tr("前台 8000 · 餐厅 8001 · 保洁 8002 · 安保 8003"));
}

// ---------------------------------------------------------------------------
// 入住办理：人脸识别状态机
// ---------------------------------------------------------------------------
void MainWindow::setFaceStatus(const QString &text, FaceState state)
{
    if (!ui->faceStatus) return;

    ui->faceStatus->setText(text);

    const char *hook = "statusPill";
    switch (state) {
    case FaceState::Busy:   hook = "statusPillBusy"; break;
    case FaceState::Passed: hook = "statusPillOk";   break;
    case FaceState::Failed: hook = "statusPillFail"; break;
    case FaceState::Waiting:
    default:                hook = "statusPill";     break;
    }

    // objectName 是 QSS 的选中依据，改了之后必须重新 polish 才会生效
    ui->faceStatus->setObjectName(QString::fromLatin1(hook));
    ui->faceStatus->style()->unpolish(ui->faceStatus);
    ui->faceStatus->style()->polish(ui->faceStatus);
    ui->faceStatus->update();
}

void MainWindow::onReadIdClicked()
{
    showToast(tr("身份证读取"), tr("正在读取身份证信息，请稍候...\n（演示模式：读取成功）"));

    // 已核验身份证：允许该客人的首次人脸建档
    m_idReadDone = true;

    // 新客人：重置人脸结果，必须重新识别
    m_facePassed = false;
    m_faceWorking = false;
    m_faceState = FaceState::Waiting;
    ui->btnConfirmCheckin->setEnabled(false);
    ui->similarityValue->setText(QStringLiteral("--"));
    ui->faceHint->setText(tr("请正对摄像头，保持面部在取景框内"));
    setFaceStatus(tr("等待识别"), FaceState::Waiting);
}

void MainWindow::onStartFaceClicked()
{
    if (m_faceWorking) return;

    m_faceWorking = true;
    m_facePassed = false;
    m_faceState = FaceState::Busy;
    ui->btnConfirmCheckin->setEnabled(false);
    ui->faceHint->setText(tr("正在识别，请保持不动..."));
    setFaceStatus(tr("识别中..."), FaceState::Busy);

    // 给摄像头一点时间产出新帧，再取帧比对。
    // 这里用旧式 SLOT() 重载：functor 版 singleShot() 是 Qt 5.4 才加入的，
    // 旧式重载在 Qt 5.x 全系可用，避免工具链版本差异。
    QTimer::singleShot(300, this, SLOT(tryCaptureForFace()));
}

void MainWindow::tryCaptureForFace()
{
    if (!m_faceCamView) {
        finishFaceCapture(QImage());
        return;
    }
    finishFaceCapture(m_faceCamView->currentFrame());
}

void MainWindow::finishFaceCapture(const QImage &frame)
{
    m_faceWorking = false;

    if (frame.isNull()) {
        m_facePassed = false;
        m_faceState = FaceState::Failed;
        ui->btnConfirmCheckin->setEnabled(false);
        ui->similarityValue->setText(QStringLiteral("--"));
        ui->faceHint->setText(tr("未获取到画面，请检查摄像头后重试"));
        setFaceStatus(tr("识别失败"), FaceState::Failed);
        showToast(tr("人脸识别"), tr("未获取到摄像头画面，请稍后重试。"));
        return;
    }

    // 1) 保存 BMP 快照到程序目录 captures/
    const QString dirPath = QCoreApplication::applicationDirPath() + QStringLiteral("/captures");
    QDir().mkpath(dirPath);
    const QString filePath = QStringLiteral("%1/face-%2.bmp")
        .arg(dirPath, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));
    if (!frame.save(filePath, "BMP"))
        qWarning() << "face snapshot save failed:" << filePath;

    // 2) 真实人脸比对：FaceEngine（肤色检测 + NCC 模板匹配，纯 C++ 零依赖）
    const FaceEngine::Analysis a =
        m_faceCamView ? m_faceCamView->analyzeLatest() : FaceEngine::Analysis();

    if (!a.faceFound) {
        m_facePassed = false;
        m_faceState = FaceState::Failed;
        ui->btnConfirmCheckin->setEnabled(false);
        ui->similarityValue->setText(QStringLiteral("--"));
        ui->faceHint->setText(tr("未检测到人脸，请正对摄像头重试"));
        setFaceStatus(tr("识别失败"), FaceState::Failed);
        showToast(tr("人脸识别"), tr("未检测到人脸，请正对摄像头。"));
        return;
    }

    const int similarity = qRound(a.score * 100.0);   // 真实 NCC 相似度
    ui->similarityValue->setText(QString::number(similarity) + QStringLiteral("%"));
    ui->faceHint->setText(tr("人脸识别完成"));

    // 3) 结果分支：命中 / 首次建档 / 未登记
    if (a.matched) {
        // 3a) 库中命中：点亮「确认入住」
        m_facePassed = true;
        m_faceState = FaceState::Passed;
        ui->btnConfirmCheckin->setEnabled(true);
        setFaceStatus(tr("识别通过"), FaceState::Passed);
        showToast(tr("人脸识别"), tr("识别通过：%1\n相似度 %2%。")
                  .arg(a.name).arg(similarity));
    } else if (m_idReadDone) {
        // 3b) 未建档但已核验身份证：自动登记（采集 4 帧建档，10 秒超时）
        m_faceWorking = true;
        m_faceState = FaceState::Busy;
        ui->btnConfirmCheckin->setEnabled(false);
        ui->similarityValue->setText(QStringLiteral("--"));
        ui->faceHint->setText(tr("首次登记，请保持正对摄像头..."));
        setFaceStatus(tr("登记中..."), FaceState::Busy);
        const QString guest = QStringLiteral("guest-")
            + QDateTime::currentDateTime().toString(QStringLiteral("MMdd-hhmmss"));
        m_faceCamView->startEnrollment(guest, 4);
        QTimer::singleShot(10 * 1000, this, SLOT(onEnrollTimeout()));
    } else {
        // 3c) 未建档且未核验身份证：拒绝
        m_facePassed = false;
        m_faceState = FaceState::Failed;
        ui->btnConfirmCheckin->setEnabled(false);
        setFaceStatus(tr("识别未通过"), FaceState::Failed);
        showToast(tr("人脸识别"), tr("该人脸未登记，请先读取身份证。"));
    }
}

void MainWindow::onFaceEnrollFinished(bool ok, const QString &name, const QString &message)
{
    m_faceWorking = false;
    if (ok) {
        m_facePassed = true;
        m_faceState = FaceState::Passed;
        ui->btnConfirmCheckin->setEnabled(true);
        ui->similarityValue->setText(tr("建档"));
        ui->faceHint->setText(tr("首次登记完成"));
        setFaceStatus(tr("登记通过"), FaceState::Passed);
        showToast(tr("人脸识别"), tr("已建档：%1\n下次识别将自动比对。").arg(name));
    } else {
        m_facePassed = false;
        m_faceState = FaceState::Failed;
        ui->btnConfirmCheckin->setEnabled(false);
        setFaceStatus(tr("登记失败"), FaceState::Failed);
        showToast(tr("人脸识别"), tr("登记失败：%1").arg(message));
    }
}

void MainWindow::onEnrollTimeout()
{
    // 建档仍在进行说明 10 秒内没采到人脸，取消并报失败
    if (!m_faceWorking || !m_faceCamView || !m_faceCamView->isEnrolling()) return;
    m_faceCamView->cancelEnrollment();
    onFaceEnrollFinished(false, QString(), tr("10 秒内未采集到人脸"));
}

void MainWindow::onConfirmCheckinClicked()
{
    if (!m_facePassed) {
        showToast(tr("入住办理"), tr("请先完成人脸识别。"));
        return;
    }
    showToast(tr("入住办理"), tr("入住办理成功！\n房号：1208\n祝您入住愉快！"));
}

void MainWindow::onPrintClicked()
{
    showToast(tr("打印凭条"), tr("正在打印入住凭条..."));
}

void MainWindow::onCardAuthClicked()
{
    showToast(tr("门锁授权"), tr("房卡已授权，门锁密码已下发。"));
}

// ---------------------------------------------------------------------------
// 主题切换
// ---------------------------------------------------------------------------
void MainWindow::onThemeSelect(int index)
{
    if (index < 0 || index >= 6) return;

    if (index == m_themeIndex) {
        applyButtonStyles();     // 保证高亮状态与新按钮一致
        return;
    }

    m_themeIndex = index;

    QSettings settings(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/hotel_terminal.ini"), QSettings::IniFormat);
    settings.setValue(QString::fromLatin1(kThemeKey), QString::fromLatin1(THEMES[index].id));

    applyTheme();
    showToast(tr("主题"), tr("已切换到「%1」。").arg(QString::fromUtf8(THEMES[index].name)));
}

// 保留的两个快捷槽（moc 需要定义）；主题按钮统一走 onThemeSelect(index)
void MainWindow::onThemeDark()  { onThemeSelect(0); }
void MainWindow::onThemeLight() { onThemeSelect(1); }

void MainWindow::showToast(const QString &title, const QString &message)
{
    const ThemeSpec &t = THEMES[qBound(0, m_themeIndex, 5)];
    DarkToast::showInformation(this, title, message,
                               t.dark ? DarkToast::Dark : DarkToast::Light,
                               QString::fromLatin1(t.accent));
}

// ---------------------------------------------------------------------------
// Day 2+ integration: WiFi / 华为云设置卡 + 人脸预览遮罩
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 音乐歌单：扫描程序目录 music/ 下的音频文件（按文件名排序）
// ---------------------------------------------------------------------------
QStringList MainWindow::scanMusicFolder() const
{
    QStringList names;
    QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/music"));
    if (!dir.exists())
        return names;
    QStringList filters;
    filters << QStringLiteral("*.mp3") << QStringLiteral("*.wav")
            << QStringLiteral("*.flac") << QStringLiteral("*.ogg")
            << QStringLiteral("*.m4a") << QStringLiteral("*.aac");
    const QFileInfoList files =
        dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot,
                          QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : files)
        names << fi.completeBaseName();
    return names;
}

// ---------------------------------------------------------------------------
// 天气（辽宁沈阳）：Open-Meteo 免密钥接口；启动拉取一次 + 每小时刷新
// ---------------------------------------------------------------------------
void MainWindow::fetchWeather()
{
    if (!m_weatherNam)
        return;
    // 沈阳坐标 41.8057N / 123.4315E，时区固定 Asia/Shanghai
    const QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"
                                  "?latitude=41.8057&longitude=123.4315"
                                  "&current=temperature_2m,weather_code"
                                  "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                                  "&forecast_days=3"
                                  "&timezone=Asia%2FShanghai"));
    QNetworkRequest req(url);
    QNetworkReply *reply = m_weatherNam->get(req);
    // 板端 Qt 无 CA 证书库：忽略 SSL 细节（与云影子接口同一策略）
    connect(reply, &QNetworkReply::sslErrors, reply, [reply]() { reply->ignoreSslErrors(); });
    connect(reply, &QNetworkReply::finished, this, &MainWindow::onWeatherReady);
}

void MainWindow::onWeatherReady()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        qDebug().noquote() << "[weather] fetch failed:" << reply->errorString()
                           << "- 保留上次显示，1 小时后重试";
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug().noquote() << "[weather] bad json:" << perr.errorString();
        return;
    }
    const QJsonObject cur = doc.object().value(QStringLiteral("current")).toObject();
    if (!cur.contains(QStringLiteral("temperature_2m")))
        return;
    const double temp = cur.value(QStringLiteral("temperature_2m")).toDouble();
    const int code = cur.value(QStringLiteral("weather_code")).toInt();
    ui->weatherLabel->setText(weatherCodeToText(code) + tr(" 沈阳"));
    ui->tempLabel->setText(QString::number(temp, 'f', 0) + QStringLiteral("°C"));

    // 三日预报：首页"三日预报"信息框，显示 今/明/后天 天气+最低~最高温（如 晴12~18°/多云14~20°/雨10~16°）
    const QJsonObject daily = doc.object().value(QStringLiteral("daily")).toObject();
    const QJsonArray dCodes = daily.value(QStringLiteral("weather_code")).toArray();
    const QJsonArray dTemps = daily.value(QStringLiteral("temperature_2m_max")).toArray();
    const QJsonArray dMins  = daily.value(QStringLiteral("temperature_2m_min")).toArray();
    if (dCodes.size() >= 3 && dTemps.size() >= 3 && dMins.size() >= 3 && ui->infoForecastVal) {
        QStringList parts;
        for (int i = 0; i < 3; ++i) {
            parts << weatherCodeToText(dCodes.at(i).toInt())
                    + QString::number(dMins.at(i).toDouble(), 'f', 0)
                    + QStringLiteral("~")
                    + QString::number(dTemps.at(i).toDouble(), 'f', 0)
                    + QStringLiteral("°");
        }
        ui->infoForecastVal->setText(parts.join(QStringLiteral("/")));
        qDebug().noquote() << "[weather] 3-day:" << parts.join(QStringLiteral("/"));
    }
    qDebug().noquote() << "[weather] Shenyang:" << temp << "C, code" << code;
}

QString MainWindow::weatherCodeToText(int code) const
{
    // WMO weather code -> 中文
    if (code == 0)  return tr("晴");
    if (code <= 2)  return tr("多云");
    if (code == 3)  return tr("阴");
    if (code == 45 || code == 48) return tr("雾");
    if (code >= 51 && code <= 67) return tr("雨");
    if (code >= 71 && code <= 77) return tr("雪");
    if (code >= 80 && code <= 82) return tr("阵雨");
    if (code >= 85 && code <= 86) return tr("阵雪");
    if (code >= 95) return tr("雷雨");
    return tr("多云");
}

void MainWindow::setupIntegrationWidgets()
{
    // ---- 1. 设置页：在主题卡之后插入 WiFi 与华为云/MQTT 卡片 ----
    QVBoxLayout *contentLayout = ui->pageSettings
        ? ui->pageSettings->findChild<QVBoxLayout *>(QStringLiteral("settingsContentLayout"))
        : 0;
    if (contentLayout) {
        m_wifiCard  = new WifiSettingsWidget(ui->pageSettings);
        m_cloudCard = new HwCloudSettingsWidget(ui->pageSettings);

        // 主题卡固定在 index 0，底部 spacer 保持在最后
        const int insertAt = qMin(1, contentLayout->count());
        contentLayout->insertWidget(insertAt, m_wifiCard);
        contentLayout->insertWidget(insertAt + 1, m_cloudCard);
    }

    // ---- 2. 人脸预览：覆盖整个 faceCard，取景面积最大 ----
    if (ui->faceCard) {
        m_faceCamView = new FaceCameraWidget(ui->faceCard);
        m_faceCamView->setObjectName(QStringLiteral("faceLiveOverlay"));
        m_faceCamView->hide();   // 仅在入住页开流后显示

        connect(m_faceCamView, &FaceCameraWidget::cameraOpened, this, [this](const QString &info) {
            qDebug() << "camera opened:" << info;
        });
        connect(m_faceCamView, &FaceCameraWidget::cameraError, this, [this](const QString &message) {
            setFaceStatus(tr("摄像头错误"), FaceState::Failed);
            showToast(tr("摄像头"), message);
        });
        // 首次建档完成 -> 刷新入住状态
        connect(m_faceCamView, &FaceCameraWidget::enrollFinished,
                this, &MainWindow::onFaceEnrollFinished);

        // 初始几何只是占位：遮罩在进入入住页前一直隐藏，真正显示前由
        // onCheckinPageEntered() 按 faceCard 的实际尺寸重设（6px 内边距）。
        m_faceCamView->setGeometry(6, 6, 200, 240);
    }

    // ---- 3. 华为云影子轮询：环境监测/设备控制真实数据 ----
    m_cloudClient = new HwCloudClient(this);
    applyCloudConfig();   // 从设置卡读取预填/已保存配置并启动轮询
    connect(m_cloudCard, &HwCloudSettingsWidget::configSaved,
            this, &MainWindow::applyCloudConfig);
    connect(m_cloudClient, &HwCloudClient::shadowUpdated,
            this, &MainWindow::onCloudShadowUpdated);
    connect(m_cloudClient, &HwCloudClient::requestFailed,
            this, &MainWindow::onCloudRequestFailed);
    connect(m_cloudClient, &HwCloudClient::desiredWritten,
            this, &MainWindow::onRoomStateWritten);

    // ---- 4. NTP 联网自动校时 ----
    m_ntpSync = new NtpSync(this);
    connect(m_ntpSync, &NtpSync::synced, this, [this](const QString &info, bool adjusted) {
        if (adjusted) showToast(tr("时间同步"), info);
    });
    connect(m_ntpSync, &NtpSync::failed, this, [](const QString &reason) {
        qDebug().noquote() << "[ntp]" << reason;
    });
    // WiFi 连接成功 / 刷新发现已联网 -> 立即校准
    connect(m_wifiCard, &WifiSettingsWidget::connectivityChanged, this,
            [this](bool up) { if (up && m_ntpSync && !m_ntpSync->isSynced()) m_ntpSync->syncNow(); });
    // 开机 3s 后先试一次（WiFi 已自动重连的场景）；之后每 30 分钟保底重校，防时钟漂移
    QTimer::singleShot(3000, m_ntpSync, &NtpSync::syncNow);
    QTimer *ntpKeep = new QTimer(this);
    connect(ntpKeep, &QTimer::timeout, m_ntpSync, &NtpSync::syncNow);
    ntpKeep->start(30 * 60 * 1000);
}

// ---------------------------------------------------------------------------
// 华为云影子数据 -> 客房页环境监测/设备控制
// ---------------------------------------------------------------------------
void MainWindow::applyCloudConfig()
{
    if (!m_cloudClient || !m_cloudCard) return;
    const HwCloudSettingsWidget::Config c = m_cloudCard->config();
    HwCloudClient::Config cc;
    cc.region         = c.region;
    cc.projectId      = c.projectId;
    cc.instancePrefix = c.instancePrefix;
    cc.deviceId       = c.deviceId;
    cc.serviceId      = c.serviceId;
    cc.accessKey      = c.accessKey;
    cc.secretKey      = c.secretKey;
    m_cloudClient->setConfig(cc);
    m_cloudClient->start(2000);   // 重启轮询（保存配置后热更新）并立即拉取一次；2s 刷新
}

void MainWindow::onCloudShadowUpdated()
{
    if (!m_cloudClient) return;
    const HwCloudClient::Snapshot s = m_cloudClient->snapshot();
    if (!s.hasData) return;

    // 与小程序显示口径一致：温度℃ / 湿度%RH / 火焰电阻Ω / 气体浓度%
    ui->sensorTempVal->setText(QString::number(s.temp, 'f', 1) + QStringLiteral("℃"));
    ui->sensorHumiVal->setText(QString::number(s.humi, 'f', 1) + QStringLiteral("%RH"));
    ui->sensorCo2Val->setText(QString::number(s.fire, 'f', 0) + QStringLiteral("Ω"));
    ui->sensorPmVal->setText(QString::number(s.gas, 'f', 1) + QStringLiteral("%"));

    // 执行器状态：排风扇 / 水泵（运行=绿色，停止=跟随主题）
    QLabel *fanVal = ui->sensorHchoVal;
    if (fanVal) {
        fanVal->setText(s.fan ? tr("运行") : tr("停止"));
        fanVal->setStyleSheet(s.fan ? QStringLiteral("color:#37e6a0;") : QString());
    }
    QLabel *pumpVal = ui->sensorIonVal;
    if (pumpVal) {
        pumpVal->setText(s.water ? tr("运行") : tr("停止"));
        pumpVal->setStyleSheet(s.water ? QStringLiteral("color:#37e6a0;") : QString());
    }

    if (ui->envCloudStatus)
        ui->envCloudStatus->setText(tr("云更新 %1").arg(s.eventTimeText));
}

void MainWindow::onCloudRequestFailed(const QString &why)
{
    if (ui->envCloudStatus)
        ui->envCloudStatus->setText(tr("云端未连接"));
    // 轮询失败属常态（未联网/设备离线），只打日志不弹窗
    qDebug().noquote() << "[cloud]" << why;
}

void MainWindow::onCheckinPageEntered()
{
    if (!m_faceCamView || !ui->faceCard) return;

    // 隐藏卡片静态内容，把面积让给实时预览
    if (ui->faceTitle)     ui->faceTitle->hide();
    if (ui->faceHint)      ui->faceHint->hide();
    if (ui->faceStatus)    ui->faceStatus->hide();
    if (ui->faceTitleIcon) ui->faceTitleIcon->hide();

    m_faceCamView->setGeometry(6, 6,
                               ui->faceCard->width() - 12,
                               ui->faceCard->height() - 12);
    m_faceCamView->raise();
    m_faceCamView->show();

    if (!m_faceCamStarted) {
        if (m_faceCamView->open())
            m_faceCamStarted = true;
    }
}

void MainWindow::onCheckinPageLeft()
{
    if (!m_faceCamView || !ui->faceCard) return;

    m_faceCamView->close();
    m_faceCamView->hide();
    m_faceCamStarted = false;

    // 恢复卡片静态内容
    if (ui->faceTitle)     ui->faceTitle->show();
    if (ui->faceHint)      ui->faceHint->show();
    if (ui->faceStatus)    ui->faceStatus->show();
    if (ui->faceTitleIcon) ui->faceTitleIcon->show();
}
