#ifndef FACE_ENGINE_H
#define FACE_ENGINE_H

#include <QImage>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

#include "face/lbph_engine.h"

/*
 * FaceEngine —— 纯 C++/Qt 人脸检测 + 识别引擎（零外部依赖）
 *
 * 设计目标：在 X6818 (Cortex-A9, Qt 5.4.1) 上无 OpenCV 也可编译运行。
 *
 *  - 检测  ：YCbCr 肤色分割 + 3x3 多数滤波 + 连通域标记 + 几何过滤
 *            （宽高比 / 填充率 / 面积 / 中心加权），返回最可信人脸矩形。
 *  - 特征  ：人脸区域对齐为 48x48 灰度，3x3 盒滤波去噪后做
 *            零均值 / 单位方差光照归一化。
 *  - 识别  ：两套后端，谁可用用谁
 *              LBPH  —— 载入 OpenCV 训练出的 trainer.yml（LbphEngine 纯 C++ 复刻），
 *                       卡方距离越小越像，阈值来自 faces_model.json；
 *              NCC   —— 本引擎自带的 48x48 模板归一化互相关，得分 0..1。
 *  - 建档  ：beginEnroll() -> addEnrollSample()xN -> 自动求均值入库。
 *  - 持久化：二进制 faces.db（QSaveFile 原子写），load()/save()。
 */
class FaceEngine
{
public:
    enum { FACE = 48 };                 // 对齐后的模板边长

    // 识别阈值（NCC 分数 0..1）
    static const double kVerify;        // >= 判定为库中人（0.60）
    static const double kKnown;         // >= 视作"疑似库中人但不够像"（0.50）

    // 识别后端：用于界面提示"这一分是谁给的"
    enum Backend { BackendNone = 0, BackendTemplate, BackendLbph };

    struct Analysis {
        bool    faceFound;              // 是否检测到人脸
        QRect   faceRect;               // 原始帧坐标系下的人脸矩形
        bool    matched;                // 是否通过识别
        QString name;                   // 命中的人名（未命中为空）
        double  score;                  // 匹配得分 0..1（越大越像，两种后端统一口径）
        QString bestName;               // 库中最高分者（即便未命中）
        double  bestScore;
        // ---- LBPH 后端信息（未启用时为默认值）----
        Backend backend;
        double  lbphDistance;           // 卡方距离（越小越像）
        int     lbphLabel;              // 命中标签，-1 = 无模型
        Analysis() : faceFound(false), matched(false),
                     score(0.0), bestScore(0.0),
                     backend(BackendNone), lbphDistance(0.0), lbphLabel(-1) {}
        bool lbphUsed() const { return backend == BackendLbph; }
    };

    FaceEngine();

    // ---- 检测 + 识别（同步，单帧几 ms @Cortex-A9）----
    Analysis analyze(const QImage &frame, bool identify = true);

    // ---- 建档（多帧采集由调用方驱动，例如预览定时器里逐帧喂）----
    void     beginEnroll(const QString &name, int samples = 4);
    bool     addEnrollSample(const QImage &frame, const QRect &faceRect);
    void     cancelEnroll();
    bool     enrolling() const       { return m_enrolling; }
    QString  enrollName() const      { return m_enrollName; }
    int      enrollRemaining() const { return m_enrollRemaining; }
    int      enrollTotal() const     { return m_enrollTotal; }

    // ---- 人脸库 ----
    bool        save(const QString &path) const;
    bool        load(const QString &path);
    QStringList persons() const;
    int         personCount() const   { return m_persons.size(); }
    bool        remove(const QString &name);
    void        clear();

    // ---- 工具 ----
    // 从 frame 的 faceRect 抠出对齐 + 归一化的 48x48 模板
    static QVector<float> alignTemplate(const QImage &frame, const QRect &faceRect);

    // ---- LBPH 后端（OpenCV trainer.yml）----
    // loadLbphFiles() 会自动找目录下的 trainer.yml / faces_model.json，
    // 找到模型就启用 LBPH（优先于 NCC），否则静默回落到 NCC。
    bool        loadLbphFiles(const QString &dirPath);
    // 显式指定模型与配置文件路径（调用方自己找好路径时用）
    bool        loadLbphFrom(const QString &modelPath, const QString &configPath);
    bool        lbphReady() const { return m_lbph.modelLoaded(); }
    LbphEngine &lbph() { return m_lbph; }
    const LbphEngine &lbph() const { return m_lbph; }
    QString     lbphStatusText() const;      // 供界面显示的一行说明

private:
    struct Person {
        QString        name;
        QVector<float> mean;            // 48x48 归一化模板
        qint64         secs;            // 建档时间
        int            samples;         // 采集帧数
    };

    QVector<Person> m_persons;
    LbphEngine      m_lbph;             // OpenCV LBPH 模型（未加载时 modelLoaded()==false）
    QString         m_lbphDir;          // 模型所在目录

    // 建档状态
    bool             m_enrolling;
    QString          m_enrollName;
    int              m_enrollRemaining;
    int              m_enrollTotal;
    QVector<QVector<float> > m_enrollBuf;

    // 检测内部实现：返回缩放后小图坐标系里的候选框
    static QRect detectSkinFace(const QImage &small, int *scoreOut);
};

#endif // FACE_ENGINE_H
