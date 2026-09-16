#ifndef LBPH_ENGINE_H
#define LBPH_ENGINE_H

#include <QImage>
#include <QHash>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

/*
 * LbphEngine —— 纯 C++/Qt 复刻 OpenCV 的 LBPH（局部二值模式直方图）人脸识别
 *
 * 为什么要复刻而不是链接 OpenCV：
 *   X6818（Cortex-A9 / Qt 5.4.1）整机零第三方依赖，交叉编译一整套 OpenCV
 *   （core+imgproc+face）会显著增加体积与启动开销。LBPH 本身算法很小，
 *   直接按 OpenCV modules/face/src/lbph_faces.cpp 的实现复刻即可，
 *   板端不加任何库，识别数值与 OpenCV 完全同源。
 *
 * 算法（与 OpenCV 一一对应）：
 *   1. 人脸区域裁剪 -> 灰度 -> 缩放到 _img_shape（默认 92x112，与 OpenCV
 *      samples/cpp/facerec_demo.cpp 一致）；
 *   2. ELBP：对每个像素，按圆形邻域取 neighbors 个采样点（半径 radius），
 *      双线性插值取灰度，与中心像素比较得 8 bit 模式码（含插值权重合并）；
 *   3. 把模式码图按 grid_x x grid_y 切成网格，每格统计 256 bin 直方图，
 *      拼接成 grid_x*grid_y*256 维（8x8x256 = 16384）特征；
 *      每格直方图独立归一化（sum=1）。
 *   4. 比对：卡方距离（直方图间），越小越像；置信度取 1/(1+distance)。
 *
 * 模型文件：OpenCV 训练脚本产出的 YAML（opencv_lbphfaces:
 *   threshold / radius / neighbors / grid_x / grid_y / histograms / labels / labelsInfo）
 * 这里只读不写，原始模型文件永不被改动。
 */
class LbphEngine
{
public:
    // 分类器参数（与 OpenCV 默认值一致，模型文件里有则覆盖）
    struct Params {
        int    radius;
        int    neighbors;
        int    gridX;
        int    gridY;
        double threshold;      // OpenCV 语义：距离小于它才算命中（默认 DBL_MAX）
        Params() : radius(1), neighbors(8), gridX(8), gridY(8), threshold(1.7976931348623157e+308) {}
    };

    struct Predict {
        int     label;         // 命中标签（-1 = 未命中）
        QString name;          // 映射后的姓名（无映射时为「贵宾 <label>」）
        double  distance;      // 最近卡方距离（越小越像）
        double  confidence;    // 1/(1+distance) -> 0..1（越大越像）
        bool    matched;       // distance <= threshold
        Predict() : label(-1), distance(0.0), confidence(0.0), matched(false) {}
    };

    LbphEngine();

    // ---- 模型 ----
    // 读取 OpenCV YAML 模型（trainer.yml）。成功返回 true。
    bool loadModel(const QString &yamlPath);
    bool modelLoaded() const { return !m_histograms.isEmpty(); }
    QString modelPath() const { return m_modelPath; }

    // 姓名映射 / 阈值（faces_model.json）
    bool loadConfig(const QString &jsonPath);      // 不存在不算错误
    bool saveConfig(const QString &jsonPath) const;
    QString configPath() const { return m_configPath; }
    QStringList configWarnings() const { return m_warnings; }

    // label -> 姓名；无映射时给出占位名
    QString nameFor(int label) const;
    bool    hasNameFor(int label) const { return m_names.contains(label); }
    void    setLabelName(int label, const QString &name);

    // 姓名 -> 已有标签；模型里还没这个人时返回 -1
    int     labelForExistingName(const QString &name) const;

    // 本程序内新登记的人脸：追加直方图（label 可用 labelForName 生成）
    void    addSample(int label, const QVector<float> &histogram, const QString &name = QString());
    bool    saveLocalTemplates(const QString &jsonPath);
    int     sampleCount() const { return m_histograms.size(); }
    QVector<int> labels() const;

    // 名字 -> 稳定 label（同名总是同一个 label，无需时间戳）
    static int labelForName(const QString &name);

    // ---- 特征与比对 ----
    QVector<float> computeHistogram(const QImage &frame, const QRect &faceRect) const;

    // 对已算好的特征做比对
    Predict predict(const QVector<float> &histogram) const;

    // 便捷：直接从画面 + 人脸框识别
    Predict predict(const QImage &frame, const QRect &faceRect) const;

    int featureSize() const { return m_params.gridX * m_params.gridY * 256; }

    // 阈值（可由 faces_model.json 覆盖 OpenCV 文件里的 threshold）
    double verifyDistance() const { return m_verifyDistance; }
    void   setVerifyDistance(double d) { m_verifyDistance = d; }
    bool   verifyFromConfig() const { return m_verifyFromConfig; }

    // 自标定：用模型内自带模板两两算距离，给出建议阈值（距离越小越像）。
    // OpenCV 的 threshold=DBL_MAX 等于"不做判定"，必须靠这个量级来定阈值。
    // 返回建议的 verify_distance；minPair/worstPair 分别是最像/最不像的一对。
    double calibrate(double *minPair = 0, double *worstPair = 0) const;

    // 裁剪缩放系数：人脸框按该系数扩展后再对齐到 _img_shape。
    // 训练图片如果本身就是"脸部特写"，用 1.0 最匹配。
    double cropScale() const { return m_cropScale; }
    void   setCropScale(double s) { m_cropScale = s > 0.1 ? s : 1.0; }

    // 训练时的图像尺寸（_img_shape），默认 92x112
    void setImageShape(int w, int h);
    QSize imageShape() const { return m_imgShape; }

private:
    void clearModel();
    int  gridCoord(int i, int n, int length) const;   // OpenCV 的 floor(i*len/n) 网格边界

    Params  m_params;
    QSize   m_imgShape;
    QString m_modelPath;
    QString m_configPath;
    double  m_verifyDistance;    // 应用侧判定阈值（距离）
    double  m_cropScale;

    QVector<QVector<float> > m_histograms;
    QVector<int>             m_labels;
    QHash<int, QString>      m_names;
    QStringList              m_warnings;
    int                      m_modelTemplateCount;   // 模型文件自带的模板数（其余为本地登记）
    bool                     m_verifyFromConfig;     // faces_model.json 里是否显式给了阈值
};

#endif // LBPH_ENGINE_H
