#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QDateTime>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QList>
#include <QAbstractButton>

class QImage;
class QPixmap;
class QPushButton;
class QPaintEvent;
class QResizeEvent;
class NtpSync;
class HwCloudClient;
class QNetworkAccessManager;
class QNetworkReply;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 入住人脸识别状态（需在引用它的槽之前声明，否则报未声明错误）
    enum class FaceState { Waiting, Busy, Passed, Failed };

protected:
    // 主题背景层：在窗口最底层绘制与当前主题匹配的渐变/光效图案
    // （侧栏、顶栏、卡片都绘制在它之上，所以只看得见页面留白处）
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onTimeUpdate();
    void onNavHome();
    void onNavRoom();
    void onNavCheckin();
    void onNavService();
    void onNavSettings();
    void onAcSliderChanged(int value);
    void onAcMinus();
    void onAcPlus();
    void onDndToggled(bool checked);
    void onCleanToggled(bool checked);
    void onDoorPressed();
    void onDoorReleased();
    void onLightToggled(bool checked);
    void onSceneToggled(bool checked);
    void onCallToggled(bool checked);
    void onServiceClicked();
    void onInfoClicked();
    void onPlayClicked();
    void onNextTrack();
    void onPrevTrack();
    void onOrderSaveClicked();
    void onReadIdClicked();
    void onStartFaceClicked();
    void onConfirmCheckinClicked();
    void onPrintClicked();
    void onCardAuthClicked();

    // 天气（辽宁沈阳）：启动立即拉取一次，此后每小时刷新
    void onWeatherReady();

    // 首页快捷入口与服务页
    void onTileRoom();
    void onTileService();
    void onTileCheckin();
    void onTileSettings();
    void onServiceOrderToggled(bool checked);
    void onPhoneBookClicked();

    void onThemeDark();
    void onThemeLight();
    void onThemeSelect(int index);

    // 人脸识别流程
    void setFaceStatus(const QString &text, FaceState state);
    void tryCaptureForFace();
    void finishFaceCapture(const QImage &frame);
    void onFaceEnrollFinished(bool ok, const QString &name, const QString &message);
    void onEnrollTimeout();

    // 华为云影子数据（环境监测/设备控制真实数据）
    void applyCloudConfig();
    void onCloudShadowUpdated();
    void onCloudRequestFailed(const QString &why);
    void onRoomStateWritten(bool ok, const QString &msg);

private:
    // ---- 主题定义（6 套）----
    struct ThemeSpec {
        const char *id;        // 唯一 ID，持久化用
        const char *name;      // 中文名
        bool        dark;      // 深色主题（用白色图标集）
        const char *accent;    // 主强调色
        const char *accent2;   // 强调渐变终点
        const char *card0;     // 卡片渐变起点
        const char *card1;     // 卡片渐变终点
        const char *checked0;  // 选中态渐变起点
        const char *checked1;  // 选中态渐变终点
        const char *border;    // 卡片边框
        const char *sidebar0;  // 侧栏渐变起点
        const char *sidebar1;  // 侧栏渐变终点
        const char *sidebarBorder;
        const char *textMain;  // 主要文字
        const char *textSub;   // 次要文字
        const char *bg;        // 页面背景
    };

    static const ThemeSpec THEMES[6];
    static const ThemeSpec &themeById(const QString &id);

    Ui::MainWindow *ui;
    QTimer *m_clockTimer;
    bool m_isPlaying = false;
    int  m_trackIndex = -1;              // 当前曲目下标（-1 = 未选曲）
    QStringList m_tracks;                // 酒店甄选歌单
    int  m_themeIndex = 0;

    // ---- New integration widgets (Day 2+) ----
    class WifiSettingsWidget    *m_wifiCard      = nullptr;
    class FaceCameraWidget      *m_faceCamView   = nullptr;
    class HwCloudSettingsWidget *m_cloudCard     = nullptr;
    class HwCloudClient         *m_cloudClient   = nullptr;   // 影子轮询（环境数据）
    class NtpSync               *m_ntpSync       = nullptr;   // 联网自动校时
    class QNetworkAccessManager *m_weatherNam    = nullptr;   // 天气 HTTP 客户端
    QTimer                      *m_weatherTimer  = nullptr;   // 每小时刷新定时器
    bool                          m_faceCamStarted = false;

    // 入住人脸识别状态
    bool m_facePassed  = false;   // 本次入住是否已通过人脸识别
    bool m_faceWorking = false;   // 识别流程进行中（防重复点击）
    bool m_idReadDone  = false;   // 已读取身份证（作为首次建档授权）
    FaceState m_faceState = FaceState::Waiting;
    QList<QPushButton *> m_themeButtons;   // 设置页 6 个主题按钮
    QStringList m_orderPicked;             // 服务页当前已选点单条目

    void updateServiceSummary();         // 刷新服务页已选点单摘要
    void updateMusicUi();                // 同步歌名/提示/播放图标
    void fetchWeather();                 // 拉取沈阳实况天气（Open-Meteo 免密钥）
    QString weatherCodeToText(int code) const;
    QStringList scanMusicFolder() const;  // 扫描程序目录 music/ 下的音频文件
    void sendRoomState();                 // 房态（请勿打扰/请即打扫）写入云端影子 desired

    void setupConnections();
    void setupIcons();
    void applyButtonStyles();
    void applyTheme();
    QString buildStyleSheet(const ThemeSpec &theme) const;

    // ---- 主题背景 ----
    void ensureThemeBackground();                              // 按需重建背景缓存
    void buildThemeBackground(QPixmap &target, const ThemeSpec &theme,
                              const QSize &size);              // 纯色底 + 装饰层
    void invalidateThemeBackground();                          // 主题/尺寸变化时作废缓存
    QPixmap m_bgBase;        // 缓存：基础渐变（每次重绘只做一次 drawPixmap）
    QPixmap m_bgOverlay;     // 缓存：点阵/星点/气泡等装饰（半透明）
    QSize   m_bgSize;        // 缓存对应的窗口尺寸
    int     m_bgTheme = -1;  // 缓存对应的主题下标

    QString currentDateTimeString() const;

    void showToast(const QString &title, const QString &message);
    QPixmap loadIconPng(const QString &baseName, const QSize &size);

    void setupIntegrationWidgets();
    void onCheckinPageEntered();
    void onCheckinPageLeft();
};
#endif // MAINWINDOW_H
