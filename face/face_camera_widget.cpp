#include "face/face_camera_widget.h"
#include "face/camerathread.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QResizeEvent>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QDebug>

FaceCameraWidget::FaceCameraWidget(QWidget *parent)
    : QWidget(parent), m_camera(0), m_frameTimer(0), m_frames(0),
      m_nextLabel(1), m_liveFace(false), m_analyzeSkip(0), m_collectSkip(0)
{
    buildUi();

    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(50);       // 20fps preview
    connect(m_frameTimer, &QTimer::timeout, this, &FaceCameraWidget::onFrameTick);

    // 人脸库随程序目录走（与 captures/ 快照同一策略）
    m_faceDbPath = QCoreApplication::applicationDirPath() + QStringLiteral("/faces.db");
    loadFaceDb();

    // LBPH 模型（OpenCV trainer.yml + faces_model.json），有就优先用它识别
    loadLbphModel();
}

FaceCameraWidget::~FaceCameraWidget()
{
    close();
}

void FaceCameraWidget::buildUi()
{
    setObjectName(QStringLiteral("faceCameraWidget"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("faceLive"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(180, 120);
    m_preview->setText(tr("(摄像头未启动)"));
    m_preview->setScaledContents(false);

    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(8);
    m_btnOpen  = new QPushButton(tr("打开摄像头"), this);
    m_btnClose = new QPushButton(tr("关闭"), this);
    m_btnOpen->setObjectName(QStringLiteral("faceCamBtn"));
    m_btnClose->setObjectName(QStringLiteral("faceCamBtn"));
    m_btnClose->setEnabled(false);
    m_status = new QLabel(tr("就绪"), this);
    m_status->setObjectName(QStringLiteral("faceCamStatus"));
    m_deviceLabel = new QLabel(this);
    m_deviceLabel->setObjectName(QStringLiteral("faceCamDevice"));
    row->addWidget(m_btnOpen);
    row->addWidget(m_btnClose);
    row->addWidget(m_status, 1);
    row->addWidget(m_deviceLabel);

    root->addWidget(m_preview, 1);
    root->addLayout(row);

    connect(m_btnOpen,  &QPushButton::clicked, this, [this](){ open(); });
    connect(m_btnClose, &QPushButton::clicked, this, [this](){ close(); });

    // 卡片自身的样式集中在这里，颜色由 applyTheme() 覆盖
    applyStyle();
}

void FaceCameraWidget::applyStyle()
{
    m_preview->setStyleSheet(QStringLiteral(
        "background: #000000; color: %1; font-size: 12px;").arg(m_textSub));

    setStyleSheet(QString(
        "QLabel#faceCamStatus { color: %1; font-size: 11px; }"
        "QLabel#faceCamDevice { color: %1; font-size: 11px; }"
        "QPushButton#faceCamBtn {"
        "  background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 %3, stop:1 %4);"
        "  border-radius: 8px; border: 1px solid %5; color: %6;"
        "  font-size: 11px; min-width: 64px; min-height: 26px; padding: 0 8px;"
        "}"
        "QPushButton#faceCamBtn:disabled { color: %2; border: 1px solid %5; }"
        "QPushButton#faceCamBtn:hover { border: 1px solid %7; }"
    ).arg(m_textSub, m_textMain, m_card0, m_card1, m_border, m_textMain, m_accent));
}

void FaceCameraWidget::applyTheme(bool dark, const QString &accent, const QString &accent2,
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

bool FaceCameraWidget::open(const QString &devicePath)
{
    if (m_camera) {
        m_status->setText(tr("摄像头已在运行"));
        return true;
    }

    // Auto-detect the first V4L2 device if no path is given.
    // The X6818 board may enumerate /dev/video9 or similar instead of /dev/video0.
    QString path = devicePath;
    if (path.isEmpty()) {
        QStringList devs = CameraThread::availableDevices();
        // 优先选择 video9（常见于 X6818 平台的人脸识别摄像头）
        QString preferred = QStringLiteral("/dev/video9");
        if (devs.contains(preferred)) {
            path = preferred;
        } else {
            path = devs.isEmpty() ? QStringLiteral("/dev/video0") : devs.first();
        }
    }

    m_camera = new CameraThread(path, 640, 480, this);
    connect(m_camera, &CameraThread::cameraOpened, this, &FaceCameraWidget::onCameraOpened);
    connect(m_camera, &CameraThread::cameraError,  this, &FaceCameraWidget::onCameraError);
    connect(m_camera, &CameraThread::cameraClosed, this, &FaceCameraWidget::onCameraClosed);
    m_camera->start();
    m_status->setText(tr("正在打开 %1 ...").arg(path));
    m_deviceLabel->setText(path);
    return true;
}

void FaceCameraWidget::resizeEvent(QResizeEvent *)
{
    // Keep the placeholder text centered when no pixmap is shown.
    if (m_preview && m_preview->pixmap() == 0) {
        m_preview->setAlignment(Qt::AlignCenter);
    }
}

void FaceCameraWidget::close()
{
    if (!m_camera) return;
    CameraThread *cam = m_camera;
    m_camera = 0;
    cam->stop();
    // 线程结束后自动释放，不阻塞主线程
    QObject::connect(cam, &QThread::finished, cam, &QObject::deleteLater);
}

void FaceCameraWidget::onCameraOpened(const QString &info)
{
    m_btnOpen->setEnabled(false);
    m_btnClose->setEnabled(true);
    m_status->setText(tr("已打开：%1").arg(info));
    m_frameTimer->start();
    m_frames = 0;
    emit cameraOpened(info);
}

void FaceCameraWidget::onCameraError(const QString &message)
{
    m_lastError = message;
    m_lastFrame = QImage();          // 出错的帧不再用于人脸识别
    m_status->setText(tr("错误：%1").arg(message));
    m_preview->setText(tr("(摄像头错误)\n%1").arg(message));
    if (m_camera) {
        CameraThread *cam = m_camera;
        m_camera = 0;
        cam->stop();
        QObject::connect(cam, &QThread::finished, cam, &QObject::deleteLater);
    }
    m_btnOpen->setEnabled(true);
    m_btnClose->setEnabled(false);
    emit cameraError(message);
}

void FaceCameraWidget::onCameraClosed()
{
    m_btnOpen->setEnabled(true);
    m_btnClose->setEnabled(false);
    m_lastFrame = QImage();          // 关流后清空缓存帧
    m_status->setText(tr("已关闭"));
    m_preview->setText(tr("(摄像头未启动)"));
    m_frameTimer->stop();
    emit cameraClosed();
}

void FaceCameraWidget::onFrameTick()
{
    if (!m_camera) return;
    QImage img = m_camera->takePending();
    if (img.isNull()) return;

    // 缓存原始帧：人脸识别取帧与预览共用同一帧源
    m_lastFrame = img;

    // ---- 建档模式：节流采样（约 4Hz），画面里有人脸才收 ----
    if (m_engine.enrolling()) {
        if (++m_collectSkip >= 5) {
            m_collectSkip = 0;
            const FaceEngine::Analysis a = m_engine.analyze(img, false);
            if (a.faceFound) {
                // 两套后端同时攒样本：NCC 模板进 faces.db，LBPH 直方图进本地模板
                if (m_enrollToModel && m_engine.lbphReady()) {
                    const QVector<float> h = m_engine.lbph().computeHistogram(img, a.faceRect);
                    if (!h.isEmpty()) m_pendingLbph.append(h);
                }
                const bool done = m_engine.addEnrollSample(img, a.faceRect);
                emit enrollProgress(m_engine.enrollRemaining(), m_engine.enrollTotal());
                m_status->setText(tr("登记中... 还差 %1 张")
                                      .arg(qMax(0, m_engine.enrollRemaining())));
                if (done) {
                    const bool saved = saveFaceDb();
                    const int lbphAdded = m_enrollToModel
                        ? commitLbphTemplates(m_engine.enrollName()) : 0;
                    m_status->setText(saved
                        ? (m_enrollToModel
                               ? tr("已登记：%1（LBPH +%2）").arg(m_engine.enrollName()).arg(lbphAdded)
                               : tr("已登记：%1（仅本地）").arg(m_engine.enrollName()))
                        : tr("登记保存失败"));
                    emit enrollFinished(saved, m_engine.enrollName(),
                                        saved ? tr("建档完成") : tr("faces.db 写入失败"));
                }
            }
        }
    } else {
        // ---- 实时检测：约 7Hz，结果画在预览上 ----
        if (++m_analyzeSkip >= 3) {
            m_analyzeSkip = 0;
            const FaceEngine::Analysis a = m_engine.analyze(img, true);
            m_liveFace = a.faceFound;
            m_liveBox  = a.faceFound ? a.faceRect : QRect();
        }
    }

    renderPreview(img);

    m_frames++;
    if (m_frames % 20 == 0) {
        emit fpsUpdated(20.0 * 1000.0 / (50.0 * 1.0));   // simple 20fps estimate
    }
}

void FaceCameraWidget::renderPreview(const QImage &src)
{
    QImage im = (src.format() == QImage::Format_RGB32)
        ? src : src.convertToFormat(QImage::Format_RGB32);

    // 在画面上叠画实时人脸框
    if (m_liveFace && m_liveBox.isValid()
        && m_liveBox.right() < im.width() && m_liveBox.bottom() < im.height()) {
        QPainter p(&im);
        QPen pen(QColor(0x37, 0xe6, 0xa0), 3);
        p.setPen(pen);
        p.drawRect(m_liveBox);
        // 四角加亮短边，更像取景框
        p.setPen(QPen(QColor(0x9d, 0xff, 0xd9), 5));
        const int c = qMax(8, m_liveBox.width() / 5);
        p.drawLine(m_liveBox.topLeft(),     m_liveBox.topLeft()     + QPoint(c, 0));
        p.drawLine(m_liveBox.topLeft(),     m_liveBox.topLeft()     + QPoint(0, c));
        p.drawLine(m_liveBox.topRight(),    m_liveBox.topRight()    + QPoint(-c, 0));
        p.drawLine(m_liveBox.topRight(),    m_liveBox.topRight()    + QPoint(0, c));
        p.drawLine(m_liveBox.bottomLeft(),  m_liveBox.bottomLeft()  + QPoint(c, 0));
        p.drawLine(m_liveBox.bottomLeft(),  m_liveBox.bottomLeft()  + QPoint(0, -c));
        p.drawLine(m_liveBox.bottomRight(), m_liveBox.bottomRight() + QPoint(-c, 0));
        p.drawLine(m_liveBox.bottomRight(), m_liveBox.bottomRight() + QPoint(0, -c));
        p.end();
    }

    // Scale to preview label size, keep aspect ratio.
    QPixmap pix = QPixmap::fromImage(im);
    pix = pix.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_preview->setPixmap(pix);
}

FaceEngine::Analysis FaceCameraWidget::analyzeLatest(bool drawBox)
{
    FaceEngine::Analysis a;
    if (m_lastFrame.isNull()) return a;

    a = m_engine.analyze(m_lastFrame, true);
    m_liveFace = a.faceFound;
    m_liveBox  = a.faceFound ? a.faceRect : QRect();

    // 把这一帧的判定依据显示在预览下方，方便现场调阈值
    if (a.lbphUsed()) {
        m_status->setText(tr("LBPH 距离 %1（阈值 %2）· %3")
                              .arg(a.lbphDistance, 0, 'f', 1)
                              .arg(m_engine.lbph().verifyDistance(), 0, 'f', 1)
                              .arg(a.matched ? a.name : tr("未命中")));
    } else if (a.faceFound && a.backend == FaceEngine::BackendTemplate) {
        m_status->setText(tr("NCC 相似度 %1%").arg(qRound(a.score * 100.0)));
    }

    if (drawBox)
        renderPreview(m_lastFrame);
    return a;
}

void FaceCameraWidget::startEnrollment(const QString &name, int samples, bool registerToModel)
{
    m_collectSkip = 4;                   // 让第一帧尽快开始采集
    m_pendingLbph.clear();
    m_enrollToModel = registerToModel;
    m_engine.beginEnroll(name, samples);
    m_status->setText(registerToModel
        ? tr("登记中... 请正对摄像头")
        : tr("登记中（仅本地）... 请正对摄像头"));
}

void FaceCameraWidget::cancelEnrollment()
{
    if (!m_engine.enrolling()) return;
    m_engine.cancelEnroll();
    m_status->setText(tr("登记已取消"));
}

bool FaceCameraWidget::loadFaceDb()
{
    return m_engine.load(m_faceDbPath);
}

bool FaceCameraWidget::saveFaceDb() const
{
    return m_engine.save(m_faceDbPath);
}

// ---------------------------------------------------------------------------
// LBPH：模型加载 + 本地登记
// ---------------------------------------------------------------------------

// 在几个常见位置里找模型：程序目录（首选，与 faces.db 同处）、models/ 子目录、
// 再往上一层的 deploy/（开发期直接在工程目录里跑也能命中）
static QString findLbphFile(const QString &appDir, const QString &fileName)
{
    QStringList dirs;
    dirs << appDir
         << appDir + QStringLiteral("/models")
         << appDir + QStringLiteral("/../deploy")
         << appDir + QStringLiteral("/..");
    for (int i = 0; i < dirs.size(); ++i) {
        const QString p = QDir(dirs.at(i)).filePath(fileName);
        if (QFileInfo(p).exists()) return p;
    }
    return QString();
}

bool FaceCameraWidget::loadLbphModel()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    m_modelDir = appDir;      // 本地新登记的模板固定写回程序目录

    const QString modelFile = findLbphFile(appDir, QStringLiteral("trainer.yml"));
    if (modelFile.isEmpty()) {
        qWarning() << "FaceCameraWidget: 未找到 trainer.yml（已找过" << appDir
                   << "及其 models/、deploy/），识别走 NCC。"
                   << "把 deploy/trainer.yml 与本程序放同一目录即可启用 LBPH。";
        return false;
    }

    // 配置文件优先跟着模型走（同一目录），否则用程序目录里的
    const QString cfgNear = QFileInfo(modelFile).dir().filePath(QStringLiteral("faces_model.json"));
    const QString cfgApp  = QDir(appDir).filePath(QStringLiteral("faces_model.json"));

    // FaceEngine::loadLbphFiles 需要 (模型所在目录)，这里先把找到的模型路径交给它
    const bool ok = m_engine.loadLbphFrom(modelFile, QFileInfo(cfgNear).exists() ? cfgNear : cfgApp);
    if (ok) {
        qDebug() << "FaceCameraWidget: LBPH 就绪 —" << m_engine.lbphStatusText()
                 << "模型:" << modelFile;
    } else {
        qWarning() << "FaceCameraWidget: trainer.yml 存在但加载失败:" << modelFile;
    }
    return ok;
}

int FaceCameraWidget::commitLbphTemplates(const QString &name)
{
    if (!m_engine.lbphReady() || m_pendingLbph.isEmpty() || name.isEmpty()) {
        m_pendingLbph.clear();
        return 0;
    }

    LbphEngine &lbph = m_engine.lbph();

    // 同名复用已有标签；新面孔按名字哈希出稳定标签，并顺手拿一个没用过的号
    int label = lbph.labelForExistingName(name);
    if (label < 0) {
        label = LbphEngine::labelForName(name);
        const QVector<int> used = lbph.labels();
        QStringList taken;
        for (int i = 0; i < used.size(); ++i) taken << QString::number(used.at(i));
        while (taken.contains(QString::number(label)) || label < 0)
            label = m_nextLabel++;
        m_nextLabel = qMax(m_nextLabel, label + 1);
    }

    int added = 0;
    for (int i = 0; i < m_pendingLbph.size(); ++i) {
        lbph.addSample(label, m_pendingLbph.at(i), name);
        ++added;
    }
    m_pendingLbph.clear();

    // 本地登记的模板写进 faces_model.json（原始 trainer.yml 永不改动）
    const QString cfg = m_modelDir + QStringLiteral("/faces_model.json");
    if (!lbph.saveLocalTemplates(cfg))
        qWarning() << "FaceCameraWidget: 本地 LBPH 模板写入失败:" << cfg;
    return added;
}
