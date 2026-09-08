#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "darktoast.h"
#include <QPixmap>
#include <QPainter>
#include <QRegularExpression>
#include <QList>

static QPixmap loadIconPng(const QString &baseName, const QSize &size)
{
    // The .pro embeds pre-rendered PNG icons because QtSvg is unreliable
    // on the ARM board ("Cannot open file ':/icons/xxx.svg'"). Keep the
    // SVG files as editable source, but always load PNG at runtime.
    QPixmap pix(":/icons/" + baseName + ".png");
    if (!pix.isNull()) {
        return pix.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // Fallback to a tiny blank pixmap so the UI still has stable geometry.
    QPixmap fallback(size);
    fallback.fill(Qt::transparent);
    return fallback;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowTitle(tr("Xihu Hotel · Smart Terminal"));
    // Default target resolution for the 7-inch X6818 panel.
    // The UI is authored in 800x480 and scales down from the previous 1280x800.
    resize(800, 480);

    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::onTimeUpdate);
    m_clockTimer->start(1000);
    onTimeUpdate();

    setupIcons();
    setupConnections();
    applyTheme();
}

MainWindow::~MainWindow()
{
    delete ui;
}

QString MainWindow::darkStyleSheet() const
{
    return R"(QMainWindow {
    background-color: #050d1a;
}
QWidget {
    font-family: "Microsoft YaHei", "PingFang SC", sans-serif;
}
QFrame { border: none; }
QLabel { color: #ffffff; }
QPushButton { border: none; outline: none; }
QToolButton { border: none; outline: none; }
QPushButton:hover, QToolButton:hover { opacity: 0.92; }
QPushButton:pressed, QToolButton:pressed { opacity: 0.85; }

/* ===== Sidebar Navigation ===== */
QToolButton#navBtn {
    background: transparent;
    color: #8aa4c8;
    font-size: 13px;
    border-left: 3px solid transparent;
    padding: 6px 0 6px 0;
    min-width: 80px;
    min-height: 70px;
    icon-size: 28px 28px;
}
QToolButton#navBtn:hover {
    color: #ffffff;
    background: rgba(0, 212, 255, 0.08);
}
QToolButton#navBtn:checked {
    color: #00d4ff;
    background: rgba(0, 212, 255, 0.12);
    border-left: 3px solid #00d4ff;
}

/* ===== Home Big Buttons ===== */
QPushButton#bigBtn {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #0f2a52, stop:1 #081a33);
    border-radius: 14px;
    border: 1px solid #1a3a6b;
    color: #ffffff;
    font-size: 13px;
    text-align: left;
    padding-left: 22px;
    icon-size: 28px 28px;
}
QPushButton#bigBtn:checked {
    border: 1px solid #00d4ff;
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #0a2245, stop:1 #061a36);
}
QPushButton#bigBtnDanger:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #c0392b, stop:1 #8e1d1d);
    border: 1px solid #e74c3c;
}
QPushButton#bigBtnSuccess:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #27ae60, stop:1 #1e8449);
    border: 1px solid #2ecc71;
}
QPushButton#bigBtnWarning:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #f39c12, stop:1 #d35400);
    border: 1px solid #f1c40f;
}
QPushButton#bigBtnScene:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #8e44ad, stop:1 #6c3483);
    border: 1px solid #a569bd;
}
QPushButton#bigBtnCall:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #2980b9, stop:1 #1a5276);
    border: 1px solid #3498db;
}

/* ===== Circle Buttons ===== */
QPushButton#circleBtn {
    background: rgba(0, 212, 255, 0.1);
    border-radius: 20px;
    color: #00d4ff;
    font-size: 16px;
    icon-size: 18px 18px;
}
QPushButton#circleBtnPrimary {
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, stop:0 #00d4ff, stop:1 #0077be);
    border-radius: 24px;
    color: #ffffff;
    icon-size: 22px 22px;
}

/* ===== Action Buttons ===== */
QPushButton#actionBtn {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #0f2a52, stop:1 #081a33);
    border-radius: 12px;
    border: 1px solid #1a3a6b;
    color: #ffffff;
    font-size: 13px;
    text-align: center;
    padding-left: 4px;
    icon-size: 22px 22px;
}
QPushButton#actionBtnPrimary {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #00d4ff, stop:1 #0077be);
    border-radius: 12px;
    color: #ffffff;
    font-size: 13px;
    font-weight: bold;
    padding-left: 4px;
    icon-size: 22px 22px;
}

/* ===== Sliders ===== */
QSlider::groove:vertical {
    border-radius: 8px;
    width: 16px;
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #1a3a6b, stop:1 #0d1f3d);
}
QSlider::handle:vertical {
    height: 28px;
    border-radius: 14px;
    background: #00d4ff;
    margin: -2px 0;
}
QSlider::sub-page:vertical {
    border-radius: 8px;
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #00d4ff, stop:1 #0077be);
}

/* ===== Section Icons (in card titles) ===== */
QLabel#sectionIcon {
    background: transparent;
    icon-size: 18px 18px;
}
)";
}

QString MainWindow::lightStyleSheet() const
{
    return R"(QMainWindow {
    background-color: #eef2f6;
}
QWidget {
    font-family: "Microsoft YaHei", "PingFang SC", sans-serif;
}
QFrame { border: none; }
QLabel { color: #1a2332; }
QPushButton { border: none; outline: none; }
QToolButton { border: none; outline: none; }
QPushButton:hover, QToolButton:hover { opacity: 0.92; }
QPushButton:pressed, QToolButton:pressed { opacity: 0.85; }

/* ===== Sidebar Navigation ===== */
QToolButton#navBtn {
    background: transparent;
    color: #5a6b82;
    font-size: 13px;
    border-left: 3px solid transparent;
    padding: 6px 0 6px 0;
    min-width: 80px;
    min-height: 76px;
    icon-size: 32px 32px;
}
QToolButton#navBtn:hover {
    color: #0d1b2a;
    background: rgba(0, 212, 255, 0.08);
}
QToolButton#navBtn:checked {
    color: #0077be;
    background: rgba(0, 212, 255, 0.12);
    border-left: 3px solid #00d4ff;
}

/* ===== Home Big Buttons ===== */
QPushButton#bigBtn {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #ffffff, stop:1 #e8eef5);
    border-radius: 14px;
    border: 1px solid #c8d2dd;
    color: #1a2332;
    font-size: 13px;
    text-align: left;
    padding-left: 22px;
    icon-size: 28px 28px;
}
QPushButton#bigBtn:checked {
    border: 1px solid #00d4ff;
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #e0f6ff, stop:1 #c8ecfc);
}
QPushButton#bigBtnDanger:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #c0392b, stop:1 #8e1d1d);
    border: 1px solid #e74c3c;
}
QPushButton#bigBtnSuccess:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #27ae60, stop:1 #1e8449);
    border: 1px solid #2ecc71;
}
QPushButton#bigBtnWarning:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #f39c12, stop:1 #d35400);
    border: 1px solid #f1c40f;
}
QPushButton#bigBtnScene:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #8e44ad, stop:1 #6c3483);
    border: 1px solid #a569bd;
}
QPushButton#bigBtnCall:checked {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #2980b9, stop:1 #1a5276);
    border: 1px solid #3498db;
}

/* ===== Circle Buttons ===== */
QPushButton#circleBtn {
    background: rgba(0, 119, 190, 0.1);
    border-radius: 20px;
    color: #0077be;
    font-size: 16px;
    icon-size: 18px 18px;
}
QPushButton#circleBtnPrimary {
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, stop:0 #00d4ff, stop:1 #0077be);
    border-radius: 24px;
    color: #ffffff;
    icon-size: 22px 22px;
}

/* ===== Action Buttons ===== */
QPushButton#actionBtn {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #ffffff, stop:1 #e8eef5);
    border-radius: 12px;
    border: 1px solid #c8d2dd;
    color: #1a2332;
    font-size: 13px;
    text-align: center;
    padding-left: 4px;
    icon-size: 22px 22px;
}
QPushButton#actionBtnPrimary {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #00d4ff, stop:1 #0077be);
    border-radius: 12px;
    color: #ffffff;
    font-size: 13px;
    font-weight: bold;
    padding-left: 4px;
    icon-size: 22px 22px;
}

/* ===== Sliders ===== */
QSlider::groove:vertical {
    border-radius: 8px;
    width: 16px;
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #c8d2dd, stop:1 #eef2f6);
}
QSlider::handle:vertical {
    height: 28px;
    border-radius: 14px;
    background: #0077be;
    margin: -2px 0;
}
QSlider::sub-page:vertical {
    border-radius: 8px;
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #00d4ff, stop:1 #0077be);
}

/* ===== Section Icons (in card titles) ===== */
QLabel#sectionIcon {
    background: transparent;
    icon-size: 18px 18px;
}
)";
}

void MainWindow::applyTheme()
{
    setStyleSheet(m_theme == Theme::Dark ? darkStyleSheet() : lightStyleSheet());

    // Local style sheets embedded in mainwindow.ui override the global sheet.
    // For a clean light theme we must re-apply a few inline colors.
    const bool dark = (m_theme == Theme::Dark);
    const QString textMain = dark ? "#ffffff" : "#1a2332";
    const QString textSub = dark ? "#8aa4c8" : "#5a6b82";
    const QString accent = dark ? "#00d4ff" : "#0077be";

    // Sidebar gradients are inline; switch them here.
    ui->sidebar->setStyleSheet(dark
        ? "QFrame#sidebar { background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #0a1a33, stop:1 #051020); border-right: 1px solid #132b4d; }"
        : "QFrame#sidebar { background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #e8eef5); border-right: 1px solid #c8d2dd; }");

    // Navigation icons must switch colour so they stay visible in both themes.
    const QString navIconSuffix = dark ? ".png" : "-light.png";
    ui->navHome->setIcon(QIcon(":/icons/home" + navIconSuffix));
    ui->navRoom->setIcon(QIcon(":/icons/bed" + navIconSuffix));
    ui->navCheckin->setIcon(QIcon(":/icons/key" + navIconSuffix));
    ui->navService->setIcon(QIcon(":/icons/bell" + navIconSuffix));
    ui->navSettings->setIcon(QIcon(":/icons/gear" + navIconSuffix));

    // Theme choice buttons: dark mode shows the moon (white), light mode shows the sun (dark).
    ui->btnDarkTheme->setIcon(QIcon(":/icons/moon" + navIconSuffix));
    ui->btnLightTheme->setIcon(QIcon(":/icons/sun" + navIconSuffix));

    // Top bar gradient is inline.
    ui->topBar->setStyleSheet(dark
        ? "QFrame#topBar { background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #0c1d38, stop:1 #081426); border-bottom: 1px solid #132b4d; }\nQLabel#topTitle { color: #ffffff; font-size: 17px; font-weight: bold; }\nQLabel#topSubTitle { color: #8aa4c8; font-size: 11px; }\nQLabel#topInfo { color: #8aa4c8; font-size: 10px; }\nQLabel#topTime { color: #00d4ff; font-size: 20px; font-weight: bold; }\nQPushButton#weatherIconBtn { background: transparent; icon-size: 22px 22px; }\nQPushButton#tempIconBtn { background: transparent; icon-size: 22px 22px; }\nQPushButton#wifiIconBtn { background: transparent; icon-size: 20px 20px; }\nQPushButton#sysIconBtn { background: transparent; icon-size: 20px 20px; }"
        : "QFrame#topBar { background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #f5f8fa); border-bottom: 1px solid #c8d2dd; }\nQLabel#topTitle { color: #1a2332; font-size: 17px; font-weight: bold; }\nQLabel#topSubTitle { color: #5a6b82; font-size: 11px; }\nQLabel#topInfo { color: #5a6b82; font-size: 10px; }\nQLabel#topTime { color: #0077be; font-size: 20px; font-weight: bold; }\nQPushButton#weatherIconBtn { background: transparent; icon-size: 22px 22px; }\nQPushButton#tempIconBtn { background: transparent; icon-size: 22px 22px; }\nQPushButton#wifiIconBtn { background: transparent; icon-size: 20px 20px; }\nQPushButton#sysIconBtn { background: transparent; icon-size: 20px 20px; }");

    // Labels that only need a colour switch and keep their font sizes.
    auto setLabelColor = [&](QLabel *label, const QString &color) {
        if (!label) return;
        QString sheet = label->styleSheet();
        QRegularExpression re("color:\\s*#[0-9a-fA-F]+");
        sheet.replace(re, "color: " + color);
        if (!sheet.contains("color:"))
            sheet.append(QString(" color: %1;").arg(color));
        label->setStyleSheet(sheet);
    };

    setLabelColor(ui->weatherLabel, textMain);
    setLabelColor(ui->tempLabel, accent);
    setLabelColor(ui->topDate, textSub);
    setLabelColor(ui->roomLabel, accent);
    setLabelColor(ui->musicSong, textMain);
    setLabelColor(ui->musicHint, textSub);
    setLabelColor(ui->acMode, textSub);
    setLabelColor(ui->roomImageText, textSub);

    // Sensor value/label colours.
    QList<QLabel *> sensorVals = {ui->sensorTempVal, ui->sensorHumiVal, ui->sensorCo2Val,
                                  ui->sensorPmVal, ui->sensorHchoVal, ui->sensorIonVal};
    for (QLabel *l : sensorVals)
        setLabelColor(l, accent);
    QList<QLabel *> sensorLabs = {ui->sensorTempLabel, ui->sensorHumiLabel, ui->sensorCo2Label,
                                  ui->sensorPmLabel, ui->sensorHchoLabel, ui->sensorIonLabel};
    for (QLabel *l : sensorLabs)
        setLabelColor(l, textSub);

    // Card titles remain white text because their inline styles target
    // the specific QLabel IDs with white text. To make light mode fully
    // consistent you would need to remove those inline colour rules from
    // mainwindow.ui; the framework above already switches the vast majority.

    if (dark)
        ui->settingsHeader->setStyleSheet("color: #00d4ff; font-size: 20px; font-weight: bold;");
    else
        ui->settingsHeader->setStyleSheet("color: #0077be; font-size: 20px; font-weight: bold;");
}

void MainWindow::setupIcons()
{
    // 标题小图标
    const int smallSize = 20;
    const int sectionSize = 22;
    const int largeSize = 26;
    ui->roomCardIcon->setPixmap(loadIconPng("home", QSize(sectionSize, sectionSize)));
    ui->musicTitleIcon->setPixmap(loadIconPng("music", QSize(smallSize, smallSize)));
    ui->acTitleIcon->setPixmap(loadIconPng("snowflake", QSize(smallSize, smallSize)));
    ui->checkinTitleIcon->setPixmap(loadIconPng("key", QSize(largeSize, largeSize)));
    ui->faceTitleIcon->setPixmap(loadIconPng("face", QSize(smallSize, smallSize)));
    ui->idTitleIcon->setPixmap(loadIconPng("id-card", QSize(smallSize, smallSize)));
    ui->resultTitleIcon->setPixmap(loadIconPng("check", QSize(smallSize, smallSize)));
    ui->roomInfoTitleIcon->setPixmap(loadIconPng("key", QSize(smallSize, smallSize)));

    // 传感器图标
    const int sensorIconSize = 20;
    ui->sensorTempIcon->setPixmap(loadIconPng("thermometer", QSize(sensorIconSize, sensorIconSize)));
    ui->sensorHumiIcon->setPixmap(loadIconPng("droplet", QSize(sensorIconSize, sensorIconSize)));
    ui->sensorCo2Icon->setPixmap(loadIconPng("co2", QSize(sensorIconSize, sensorIconSize)));
    ui->sensorPmIcon->setPixmap(loadIconPng("dust", QSize(sensorIconSize, sensorIconSize)));
    ui->sensorHchoIcon->setPixmap(loadIconPng("flask", QSize(sensorIconSize, sensorIconSize)));
    ui->sensorIonIcon->setPixmap(loadIconPng("leaf", QSize(sensorIconSize, sensorIconSize)));

    // 音乐卡中央圆形图标
    ui->musicIcon->setPixmap(loadIconPng("music", QSize(44, 44)));
}

void MainWindow::setupConnections()
{
    // 导航切换 - QToolButton
    connect(ui->navHome, &QToolButton::clicked, this, &MainWindow::onNavHome);
    connect(ui->navRoom, &QToolButton::clicked, this, &MainWindow::onNavRoom);
    connect(ui->navCheckin, &QToolButton::clicked, this, &MainWindow::onNavCheckin);
    connect(ui->navService, &QToolButton::clicked, this, &MainWindow::onNavService);
    connect(ui->navSettings, &QToolButton::clicked, this, &MainWindow::onNavSettings);

    // 空调控制
    connect(ui->acSlider, &QSlider::valueChanged, this, &MainWindow::onAcSliderChanged);
    connect(ui->btnAcMinus, &QPushButton::clicked, this, &MainWindow::onAcMinus);
    connect(ui->btnAcPlus, &QPushButton::clicked, this, &MainWindow::onAcPlus);

    // 房间快捷按钮
    connect(ui->btnDnd, &QPushButton::toggled, this, &MainWindow::onDndToggled);
    connect(ui->btnClean, &QPushButton::toggled, this, &MainWindow::onCleanToggled);
    connect(ui->btnDoor, &QPushButton::pressed, this, &MainWindow::onDoorPressed);
    connect(ui->btnDoor, &QPushButton::released, this, &MainWindow::onDoorReleased);
    connect(ui->btnLight, &QPushButton::toggled, this, &MainWindow::onLightToggled);
    connect(ui->btnScene, &QPushButton::toggled, this, &MainWindow::onSceneToggled);
    connect(ui->btnCall, &QPushButton::toggled, this, &MainWindow::onCallToggled);
    connect(ui->btnService, &QPushButton::clicked, this, &MainWindow::onServiceClicked);
    connect(ui->btnInfo, &QPushButton::clicked, this, &MainWindow::onInfoClicked);

    // 音乐控制
    connect(ui->btnPlay, &QPushButton::clicked, this, &MainWindow::onPlayClicked);

    // 入住办理
    connect(ui->btnReadId, &QPushButton::clicked, this, &MainWindow::onReadIdClicked);
    connect(ui->btnStartFace, &QPushButton::clicked, this, &MainWindow::onStartFaceClicked);
    connect(ui->btnConfirmCheckin, &QPushButton::clicked, this, &MainWindow::onConfirmCheckinClicked);
    connect(ui->btnPrint, &QPushButton::clicked, this, &MainWindow::onPrintClicked);
    connect(ui->btnCardAuth, &QPushButton::clicked, this, &MainWindow::onCardAuthClicked);

    // 主题切换
    connect(ui->btnDarkTheme, &QPushButton::clicked, this, &MainWindow::onThemeDark);
    connect(ui->btnLightTheme, &QPushButton::clicked, this, &MainWindow::onThemeLight);
}

void MainWindow::onTimeUpdate()
{
    QDateTime now = QDateTime::currentDateTime();
    ui->topTime->setText(now.toString("hh:mm"));
    ui->topDate->setText(now.toString("yyyy年M月d日 dddd"));
}

void MainWindow::onNavHome()
{
    ui->stackedWidget->setCurrentWidget(ui->pageHome);
    ui->topTitle->setText(tr("尊敬的先生/女士，欢迎入住西湖山庄酒店"));
    ui->topSubTitle->setText(tr("用心服务 · 让旅途更美好"));
}

void MainWindow::onNavRoom()
{
    ui->stackedWidget->setCurrentWidget(ui->pageHome);
    ui->topTitle->setText(tr("客房控制"));
    ui->topSubTitle->setText(tr("舒适每一刻"));
}

void MainWindow::onNavCheckin()
{
    ui->stackedWidget->setCurrentWidget(ui->pageCheckin);
    ui->topTitle->setText(tr("身份验证 / 入住办理"));
    ui->topSubTitle->setText(tr("快速核验 · 安心入住"));
}

void MainWindow::onNavService()
{
    ui->stackedWidget->setCurrentWidget(ui->pageHome);
    ui->topTitle->setText(tr("酒店服务"));
    ui->topSubTitle->setText(tr("贴心服务"));
}

void MainWindow::onNavSettings()
{
    ui->stackedWidget->setCurrentWidget(ui->pageSettings);
    ui->topTitle->setText(tr("系统设置"));
    ui->topSubTitle->setText(tr("系统管理与维护"));
}

void MainWindow::onAcSliderChanged(int value)
{
    ui->acTempBig->setText(QString::number(value) + "°C");
}

void MainWindow::onAcMinus()
{
    int v = ui->acSlider->value();
    if (v > ui->acSlider->minimum()) {
        ui->acSlider->setValue(v - 1);
    }
}

void MainWindow::onAcPlus()
{
    int v = ui->acSlider->value();
    if (v < ui->acSlider->maximum()) {
        ui->acSlider->setValue(v + 1);
    }
}

void MainWindow::onDndToggled(bool checked)
{
    if (checked) {
        ui->btnClean->setChecked(false);
        ui->btnDnd->setText(tr("请勿打扰 - 已开启"));
    } else {
        ui->btnDnd->setText(tr("请勿打扰"));
    }
}

void MainWindow::onCleanToggled(bool checked)
{
    if (checked) {
        ui->btnDnd->setChecked(false);
        ui->btnClean->setText(tr("请即打扫 - 已呼叫"));
    } else {
        ui->btnClean->setText(tr("请即打扫"));
    }
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
}

void MainWindow::onServiceClicked()
{
    showToast(tr("酒店服务"), tr("已打开酒店服务菜单。"));
}

void MainWindow::onInfoClicked()
{
    showToast(tr("信息中心"), tr("已打开信息中心。"));
}

void MainWindow::onPlayClicked()
{
    m_isPlaying = !m_isPlaying;
    ui->musicSong->setText(m_isPlaying ? tr("静谧时光") : tr("暂无歌曲"));
    ui->musicHint->setText(m_isPlaying ? tr("酒店甄选歌单") : tr("请选择播放"));
}

void MainWindow::onReadIdClicked()
{
    showToast(tr("身份证读取"), tr("正在读取身份证信息，请稍候...\n（演示模式：读取成功）"));
}

void MainWindow::onStartFaceClicked()
{
    ui->faceHint->setText(tr("正在识别，请保持不动..."));
    QTimer::singleShot(1500, this, [this]() {
        ui->faceHint->setText(tr("人脸识别完成"));
        ui->faceStatus->setText(tr("识别通过"));
    });
}

void MainWindow::onConfirmCheckinClicked()
{
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

void MainWindow::onThemeDark()
{
    m_theme = Theme::Dark;
    applyTheme();
    showToast(tr("主题"), tr("已切换到深色模式。"));
}

void MainWindow::onThemeLight()
{
    m_theme = Theme::Light;
    applyTheme();
    showToast(tr("主题"), tr("已切换到浅色模式。"));
}

void MainWindow::showToast(const QString &title, const QString &message)
{
    DarkToast::showInformation(this, title, message,
                               m_theme == Theme::Dark ? DarkToast::Dark : DarkToast::Light);
}
