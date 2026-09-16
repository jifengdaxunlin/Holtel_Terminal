#ifndef FACE_CAMERA_WIDGET_H
#define FACE_CAMERA_WIDGET_H

#include <QWidget>
#include <QImage>
#include <QRect>
#include "face/face_engine.h"

class QLabel;
class QPushButton;
class QTimer;
class CameraThread;

/*
 * Face-camera preview widget.
 *
 * Hosts a CameraThread that captures frames from /dev/video0 (or
 * the default device on Windows). Each frame is scaled and shown
 * inside a QLabel that fills the entire widget area.
 *
 * Call open() / close() to start/stop the camera. When the widget
 * is hidden, the camera is released so the LED turns off.
 *
 * 内置 FaceEngine（纯 C++，零外部依赖）：
 *  - 预览时按节流频率做实时人脸检测，直接在画面上画出人脸框；
 *  - analyzeLatest() 供「开始人脸识别」按钮取最新一帧做检测+比对；
 *  - startEnrollment() 进入建档模式，在预览帧流中自动采集 N 张样本，
 *    完成后写入 faces.db 并发出 enrollFinished。
 */
class FaceCameraWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FaceCameraWidget(QWidget *parent = 0);
    ~FaceCameraWidget();

    bool open(const QString &devicePath = QString());
    void close();

    // 摄像头是否已开流
    bool isOpen() const { return m_camera != 0; }

    // 最近一帧（供人脸识别取帧）；未开流或无帧时返回空 QImage
    QImage currentFrame() const { return m_lastFrame; }

    // 对最新一帧做检测+识别，同时在预览上画出人脸框
    FaceEngine::Analysis analyzeLatest(bool drawBox = true);

    // 建档：在预览帧流中自动采集 samples 张，完成后保存并发出 enrollFinished
    // registerToModel=false 时不写入 LBPH 模型（用于"陌生人自动归档"，
    // 避免未经确认真实身份的样本污染训练好的模型）
    void startEnrollment(const QString &name, int samples = 4, bool registerToModel = true);
    void cancelEnrollment();                 // 取消进行中的建档（不发信号）
    bool isEnrolling() const { return m_engine.enrolling(); }

    // 人脸库存取（faces.db）
    bool        loadFaceDb();
    bool        saveFaceDb() const;
    QStringList enrolledPersons() const { return m_engine.persons(); }

    // LBPH（OpenCV trainer.yml）：启动时尝试加载，成功则识别优先走它
    bool    loadLbphModel();
    bool    lbphReady() const { return m_engine.lbphReady(); }
    QString lbphStatus() const { return m_engine.lbphStatusText(); }
    int     lbphTemplateCount() const { return m_engine.lbph().sampleCount(); }
    double  lbphVerifyDistance() const { return m_engine.lbph().verifyDistance(); }

    // 由 MainWindow 在主题切换时调用，让预览卡片跟随当前主题
    void applyTheme(bool dark, const QString &accent, const QString &accent2,
                    const QString &card0, const QString &card1, const QString &border,
                    const QString &textMain, const QString &textSub);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void cameraOpened(const QString &info);
    void cameraError(const QString &message);
    void cameraClosed();
    void fpsUpdated(double fps);
    void enrollProgress(int remaining, int total);
    void enrollFinished(bool ok, const QString &name, const QString &message);

private slots:
    void onFrameTick();
    void onCameraOpened(const QString &info);
    void onCameraError(const QString &message);
    void onCameraClosed();

private:
    void buildUi();
    void applyStyle();
    void renderPreview(const QImage &src);
    // 把攒下的 LBPH 直方图登记到模型里（同名复用标签）并落盘，返回新增数量
    int  commitLbphTemplates(const QString &name);

    QLabel      *m_preview;
    QLabel      *m_status;
    QLabel      *m_deviceLabel;
    QPushButton *m_btnOpen;
    QPushButton *m_btnClose;
    CameraThread *m_camera;
    QTimer      *m_frameTimer;
    int          m_frames;
    QString      m_lastError;
    QImage       m_lastFrame;   // 最近一帧（与预览同源）

    // ---- 人脸识别 ----
    FaceEngine   m_engine;
    QString      m_faceDbPath;  // faces.db 路径
    QString      m_modelDir;    // trainer.yml / faces_model.json 所在目录
    int          m_nextLabel;   // 本程序新登记的人脸分配的 LBPH 标签
    QVector<QVector<float> > m_pendingLbph;   // 本次建档攒下的 LBPH 直方图
    bool         m_enrollToModel;             // 本次建档是否写入 LBPH 模型
    QRect        m_liveBox;     // 实时人脸框（帧坐标系）
    bool         m_liveFace;
    int          m_analyzeSkip; // 实时检测节流
    int          m_collectSkip; // 建档采样节流

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

#endif // FACE_CAMERA_WIDGET_H
