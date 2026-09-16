#include "face/lbph_engine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QTextStream>
#include <QtGlobal>

#include <cmath>

namespace {

// M_PI 在部分 MinGW/MSVC 头文件里不保证定义，这里自己定一份
const double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// 1) 工具
// ---------------------------------------------------------------------------

// OpenCV 灰度换算系数（BGR2GRAY: 0.114B + 0.587G + 0.299R）
inline int lumaOf(quint32 px)
{
    const int r = int((px >> 16) & 0xFF);
    const int g = int((px >> 8) & 0xFF);
    const int b = int(px & 0xFF);
    return (29 * b + 150 * g + 77 * r) >> 8;
}

// 双线性插值：坐标 (x, y) 可能是小数（ELBP 的圆形邻域采样点）
inline double bilinear(const double *img, int w, int h, double x, double y)
{
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;
    if (x > w - 1.0) x = w - 1.0;
    if (y > h - 1.0) y = h - 1.0;
    const int x1 = int(std::floor(x));
    const int y1 = int(std::floor(y));
    const int x2 = qMin(x1 + 1, w - 1);
    const int y2 = qMin(y1 + 1, h - 1);
    const double fx = x - x1;
    const double fy = y - y1;
    const double top = img[y1 * w + x1] * (1.0 - fx) + img[y1 * w + x2] * fx;
    const double bot = img[y2 * w + x1] * (1.0 - fx) + img[y2 * w + x2] * fx;
    return top * (1.0 - fy) + bot * fy;
}

// ---------------------------------------------------------------------------
// 2) OpenCV YAML 解析（只认 FileStorage 写出来的、或手工整理的等价格式）
//
//    - !!opencv-matrix 块：rows / cols / dt / data: [ ... ]（data 可折行）
//    - labelsInfo 两种写法都支持：
//        labelsInfo: { 1: "张三" }             （手工编辑，推荐）
//        labelsInfo:
//          1: 张三                              （OpenCV 展开写法）
//    - 解析只取数值 token，遇到任何异常都跳过而不是崩溃。
// ---------------------------------------------------------------------------
QString readTextFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    return ts.readAll();
}

// 把一段文本里的所有数值抽出来
QVector<double> numericsIn(const QString &text)
{
    QVector<double> out;
    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        const bool startsNumber = c.isDigit()
            || (c == QLatin1Char('-') && i + 1 < n && text.at(i + 1).isDigit())
            || (c == QLatin1Char('+') && i + 1 < n && text.at(i + 1).isDigit())
            || (c == QLatin1Char('.') && i + 1 < n && text.at(i + 1).isDigit());
        if (!startsNumber) { ++i; continue; }

        int j = i;
        if (text.at(j) == QLatin1Char('-') || text.at(j) == QLatin1Char('+')) ++j;
        bool dotSeen = false;
        bool expSeen = false;
        while (j < n) {
            const QChar d = text.at(j);
            if (d.isDigit()) { ++j; continue; }
            if (d == QLatin1Char('.') && !dotSeen && !expSeen) { dotSeen = true; ++j; continue; }
            if ((d == QLatin1Char('e') || d == QLatin1Char('E')) && !expSeen && j + 1 < n) {
                const QChar e = text.at(j + 1);
                if (e.isDigit() || ((e == QLatin1Char('-') || e == QLatin1Char('+'))
                                    && j + 2 < n && text.at(j + 2).isDigit())) {
                    expSeen = true;
                    j += (e.isDigit() ? 1 : 2);
                    continue;
                }
            }
            break;
        }
        bool ok = false;
        const QString token = text.mid(i, j - i);
        const double v = token.toDouble(&ok);
        if (ok) out.append(v);
        i = j;
    }
    return out;
}

// 取 "key:" 之后的第一个数值
double valueAfter(const QString &block, const QString &key, double fallback)
{
    const int k = block.indexOf(key);
    if (k < 0) return fallback;
    int nl = block.indexOf(QLatin1Char('\n'), k);
    if (nl < 0) nl = block.size();
    const QVector<double> v = numericsIn(block.mid(k + key.size(), nl - k - key.size()));
    return v.isEmpty() ? fallback : v.first();
}

// 对齐矩阵块块头里的整数
int intAfter(const QString &block, const QString &key, int fallback)
{
    return int(valueAfter(block, key, double(fallback)));
}

struct YamlMatrix {
    int rows;
    int cols;
    QString dt;
    QVector<double> data;
    YamlMatrix() : rows(0), cols(0) {}
};

// 解析指定 key 之后紧跟的一个 !!opencv-matrix
bool parseMatrix(const QString &text, const QString &key, YamlMatrix *out)
{
    const int k = text.indexOf(key);
    if (k < 0) return false;
    const int m = text.indexOf(QStringLiteral("!!opencv-matrix"), k);
    if (m < 0) return false;
    const int nextKey = text.indexOf(QStringLiteral("!!opencv-matrix"), m + 1);
    const int end = (nextKey < 0) ? text.size() : nextKey;
    const QString block = text.mid(m, end - m);

    out->rows = intAfter(block, QStringLiteral("rows:"), 0);
    out->cols = intAfter(block, QStringLiteral("cols:"), 0);

    const int dtPos = block.indexOf(QStringLiteral("dt:"));
    if (dtPos >= 0) {
        int e = block.indexOf(QLatin1Char('\n'), dtPos);
        if (e < 0) e = block.size();
        out->dt = block.mid(dtPos + 3, e - dtPos - 3).trimmed();
    }

    const int dPos = block.indexOf(QStringLiteral("data:"));
    if (dPos < 0) return false;
    int open = block.indexOf(QLatin1Char('['), dPos);
    int close = block.indexOf(QLatin1Char(']'), dPos);
    if (open < 0) return false;
    if (close < 0) close = block.size();
    out->data = numericsIn(block.mid(open + 1, close - open - 1));
    return true;
}

// labelsInfo: 里的 标签->姓名
QHash<int, QString> parseNames(const QString &text)
{
    QHash<int, QString> names;
    const int k = text.indexOf(QStringLiteral("labelsInfo:"));
    if (k < 0) return names;

    const QString after = text.mid(k + 11);
    const int open = after.indexOf(QLatin1Char('{'));
    if (open >= 0) {
        // 手工 JSON 风格：{ 1363479013: "张三", ... }
        const int close = after.indexOf(QLatin1Char('}'), open);
        const QString body = after.mid(open + 1, (close < 0 ? after.size() : close) - open - 1);
        const QStringList pairs = body.split(QLatin1Char(','), QString::SkipEmptyParts);
        for (int i = 0; i < pairs.size(); ++i) {
            const QString p = pairs.at(i);
            const int colon = p.indexOf(QLatin1Char(':'));
            if (colon <= 0) continue;
            bool ok = false;
            const int label = p.left(colon).trimmed().toInt(&ok);
            if (!ok) continue;
            QString name = p.mid(colon + 1).trimmed();
            name.remove(QLatin1Char('"'));
            name.remove(QLatin1Char('\''));
            if (!name.isEmpty()) names.insert(label, name);
        }
        return names;
    }

    // OpenCV 展开写法：一行一条 "  1363479013: 张三"
    const QStringList lines = after.split(QLatin1Char('\n'));
    for (int i = 1; i < lines.size(); ++i) {          // 跳过 labelsInfo: 自己那行
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) continue;
        bool ok = false;
        const int label = line.left(colon).trimmed().toInt(&ok);
        if (!ok) continue;
        QString name = line.mid(colon + 1).trimmed();
        if (name == QLatin1String("[]")) continue;
        name.remove(QLatin1Char('"'));
        name.remove(QLatin1Char('\''));
        if (!name.isEmpty()) names.insert(label, name);
    }
    return names;
}

} // namespace

// ---------------------------------------------------------------------------
// 构造 / 配置
// ---------------------------------------------------------------------------
LbphEngine::LbphEngine()
    : m_imgShape(92, 112),
      // OpenCV 的 threshold 默认是 DBL_MAX（等于不判定），所以这里给一个可用初值。
      // LBPH 卡方距离的经验量级：同一人通常 < 60，不同人明显更大；
      // 加载模型后 calibrate() 会用模型内模板间距给出建议值，可在 json 里覆盖。
      m_verifyDistance(60.0),
      m_cropScale(1.0),
      m_modelTemplateCount(0),
      m_verifyFromConfig(false)
{
}

void LbphEngine::setImageShape(int w, int h)
{
    if (w > 8 && h > 8) m_imgShape = QSize(w, h);
}

void LbphEngine::clearModel()
{
    m_histograms.clear();
    m_labels.clear();
    m_modelPath.clear();
}

int LbphEngine::labelForName(const QString &name)
{
    // FNV-1a 32bit 取低位，保证同名恒定、跨进程稳定（Qt 5.4 无 qHash(QString) 重载保证）
    quint32 h = 2166136261u;
    const QByteArray utf8 = name.toUtf8();
    for (int i = 0; i < utf8.size(); ++i) {
        h ^= quint8(utf8.at(i));
        h *= 16777619u;
    }
    int label = int(h & 0x7FFFFFFFu);
    if (label == 0) label = 1;
    return label;
}

// ---------------------------------------------------------------------------
// 模型读取：OpenCV YAML
// ---------------------------------------------------------------------------
bool LbphEngine::loadModel(const QString &yamlPath)
{
    const QString text = readTextFile(yamlPath);
    if (text.isEmpty()) {
        qWarning() << "LbphEngine: 模型文件读取失败或为空:" << yamlPath;
        return false;
    }

    // 分类器参数
    m_params.threshold = valueAfter(text, QStringLiteral("threshold:"), m_params.threshold);
    m_params.radius    = intAfter(text, QStringLiteral("radius:"), m_params.radius);
    m_params.neighbors = intAfter(text, QStringLiteral("neighbors:"), m_params.neighbors);
    m_params.gridX     = intAfter(text, QStringLiteral("grid_x:"), m_params.gridX);
    m_params.gridY     = intAfter(text, QStringLiteral("grid_y:"), m_params.gridY);
    if (m_params.gridX < 1) m_params.gridX = 1;
    if (m_params.gridY < 1) m_params.gridY = 1;
    if (m_params.neighbors < 1 || m_params.neighbors > 8) m_params.neighbors = 8;
    if (m_params.radius < 1) m_params.radius = 1;

    // 期望的特征维度：OpenCV 固定 256 bin
    const int want = m_params.gridX * m_params.gridY * 256;
    const double opencvThreshold = m_params.threshold;

    // 直方图：可能出现多个 !!opencv-matrix
    QVector<QVector<float> > hists;
    {
        int search = 0;
        while (true) {
            const int k = text.indexOf(QStringLiteral("- !!opencv-matrix"), search);
            if (k < 0) break;
            const int nextM = text.indexOf(QStringLiteral("- !!opencv-matrix"), k + 1);
            const QString block = text.mid(k, (nextM < 0 ? text.size() : nextM) - k);

            const int dPos = block.indexOf(QStringLiteral("data:"));
            if (dPos < 0) break;
            const int open = block.indexOf(QLatin1Char('['), dPos);
            if (open < 0) break;
            int close = block.indexOf(QLatin1Char(']'), dPos);
            if (close < 0) close = block.size();

            // 只看 data 数组内部：labels 段排在最后一个直方图之后，
            // 不能拿整块文本判断（否则最后一个直方图会被误当成 labels 段丢掉）
            const QString body = block.mid(open + 1, close - open - 1);
            if (body.contains(QStringLiteral("labels:"))) break;

            const QVector<double> raw = numericsIn(body);
            if (raw.size() != want) {
                qWarning() << "LbphEngine: 直方图维度不符，期望" << want
                           << "实际" << raw.size() << "，已跳过";
            } else {
                QVector<float> h(want);
                for (int i = 0; i < want; ++i) h[i] = float(raw.at(i));
                hists.append(h);
            }
            search = (nextM < 0) ? text.size() : nextM;
        }
    }

    if (hists.isEmpty()) {
        qWarning() << "LbphEngine: 模型里没有可用的直方图:" << yamlPath;
        return false;
    }

    // labels
    QVector<int> labels;
    {
        YamlMatrix lm;
        if (parseMatrix(text, QStringLiteral("labels:"), &lm) && !lm.data.isEmpty()) {
            for (int i = 0; i < lm.data.size(); ++i) labels.append(int(lm.data.at(i) + 0.5));
        }
    }
    while (labels.size() < hists.size()) labels.append(-1);

    m_histograms = hists;
    m_labels     = labels;
    m_modelPath  = yamlPath;
    m_modelTemplateCount = hists.size();

    // labelsInfo（模型里带名字时直接用）
    const QHash<int, QString> inModel = parseNames(text);
    for (QHash<int, QString>::const_iterator it = inModel.constBegin();
         it != inModel.constEnd(); ++it) {
        m_names.insert(it.key(), it.value());
    }

    // OpenCV 的 threshold 是"距离上限"；DBL_MAX 表示没设，按应用侧阈值走
    if (opencvThreshold < 1.0e30 && opencvThreshold > 0.0)
        m_verifyDistance = opencvThreshold;

    qDebug() << "LbphEngine: 载入" << m_histograms.size() << "个模板, grid"
             << m_params.gridX << "x" << m_params.gridY
             << "radius" << m_params.radius << "neighbors" << m_params.neighbors
             << "特征维度" << want;
    return true;
}

// ---------------------------------------------------------------------------
// 配置：faces_model.json
// {
//   "model":       "trainer.yml",
//   "img_width":   92,
//   "img_height":  112,
//   "crop_scale":  1.0,
//   "verify_distance": 12000,
//   "names":       { "1363479013": "张三" },
//   "templates":   [ { "label": 123, "name": "李四", "data": [ ...16384 个 ... ] } ]
// }
// ---------------------------------------------------------------------------
bool LbphEngine::loadConfig(const QString &jsonPath)
{
    m_configPath = jsonPath;
    m_warnings.clear();

    QFile f(jsonPath);
    if (!f.exists()) return false;
    if (!f.open(QIODevice::ReadOnly)) {
        m_warnings << QStringLiteral("无法读取 %1").arg(jsonPath);
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_warnings << QStringLiteral("%1 解析失败：%2")
                          .arg(QFileInfo(jsonPath).fileName(), err.errorString());
        return false;
    }

    const QJsonObject o = doc.object();

    if (o.contains(QStringLiteral("img_width")) && o.contains(QStringLiteral("img_height"))) {
        setImageShape(o.value(QStringLiteral("img_width")).toInt(m_imgShape.width()),
                      o.value(QStringLiteral("img_height")).toInt(m_imgShape.height()));
    }
    if (o.contains(QStringLiteral("crop_scale")))
        setCropScale(o.value(QStringLiteral("crop_scale")).toDouble(1.0));
    if (o.contains(QStringLiteral("verify_distance"))) {
        m_verifyDistance = o.value(QStringLiteral("verify_distance")).toDouble(m_verifyDistance);
        m_verifyFromConfig = true;
    }

    const QJsonObject names = o.value(QStringLiteral("names")).toObject();
    for (QJsonObject::const_iterator it = names.constBegin(); it != names.constEnd(); ++it) {
        bool ok = false;
        const int label = it.key().toInt(&ok);
        const QString name = it.value().toString().trimmed();
        if (ok && !name.isEmpty()) m_names.insert(label, name);
    }

    // 本地追加的模板（本程序里新登记的人脸）
    const QJsonArray tpls = o.value(QStringLiteral("templates")).toArray();
    int added = 0;
    for (int i = 0; i < tpls.size(); ++i) {
        const QJsonObject t = tpls.at(i).toObject();
        const int label = t.value(QStringLiteral("label")).toInt(-1);
        const QString name = t.value(QStringLiteral("name")).toString().trimmed();
        const QJsonArray data = t.value(QStringLiteral("data")).toArray();
        if (label < 0 || data.size() != featureSize()) continue;
        QVector<float> h(data.size());
        for (int k = 0; k < data.size(); ++k) h[k] = float(data.at(k).toDouble());
        m_histograms.append(h);
        m_labels.append(label);
        if (!name.isEmpty()) m_names.insert(label, name);
        ++added;
    }

    if (!m_warnings.isEmpty())
        qWarning() << "LbphEngine config:" << m_warnings;
    qDebug() << "LbphEngine: 配置载入完成，姓名" << m_names.size()
             << "条，本地模板" << added << "个";
    return true;
}

bool LbphEngine::saveConfig(const QString &jsonPath) const
{
    QJsonObject o;
    o.insert(QStringLiteral("img_width"), m_imgShape.width());
    o.insert(QStringLiteral("img_height"), m_imgShape.height());
    o.insert(QStringLiteral("crop_scale"), m_cropScale);
    o.insert(QStringLiteral("verify_distance"), m_verifyDistance);
    if (!m_modelPath.isEmpty())
        o.insert(QStringLiteral("model"), QFileInfo(m_modelPath).fileName());

    QJsonObject names;
    for (QHash<int, QString>::const_iterator it = m_names.constBegin();
         it != m_names.constEnd(); ++it) {
        names.insert(QString::number(it.key()), it.value());
    }
    o.insert(QStringLiteral("names"), names);

    // 模型文件里没有的模板（本程序登记的）才写进 json
    QJsonArray tpls;
    const int modelCount = qMax(0, m_modelTemplateCount);
    for (int i = modelCount; i < m_histograms.size(); ++i) {
        if (i >= m_labels.size()) break;
        QJsonObject t;
        t.insert(QStringLiteral("label"), m_labels.at(i));
        const QString nm = m_names.value(m_labels.at(i));
        if (!nm.isEmpty()) t.insert(QStringLiteral("name"), nm);
        const QVector<float> &h = m_histograms.at(i);
        QJsonArray data;
        for (int k = 0; k < h.size(); ++k) data.append(double(h.at(k)));
        t.insert(QStringLiteral("data"), data);
        tpls.append(t);
    }
    o.insert(QStringLiteral("templates"), tpls);

    QSaveFile f(jsonPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

QString LbphEngine::nameFor(int label) const
{
    const QString n = m_names.value(label);
    if (!n.isEmpty()) return n;
    if (label < 0) return QString();
    return QStringLiteral("贵宾 %1").arg(label);
}

void LbphEngine::setLabelName(int label, const QString &name)
{
    if (label < 0 || name.isEmpty()) return;
    m_names.insert(label, name);
}

int LbphEngine::labelForExistingName(const QString &name) const
{
    for (QHash<int, QString>::const_iterator it = m_names.constBegin();
         it != m_names.constEnd(); ++it) {
        if (it.value() == name) return it.key();
    }
    return -1;
}

void LbphEngine::addSample(int label, const QVector<float> &histogram, const QString &name)
{
    if (label < 0 || histogram.size() != featureSize()) return;
    m_histograms.append(histogram);
    m_labels.append(label);
    if (!name.isEmpty()) m_names.insert(label, name);
}

bool LbphEngine::saveLocalTemplates(const QString &jsonPath)
{
    return saveConfig(jsonPath);
}

QVector<int> LbphEngine::labels() const
{
    return m_labels;
}

// ---------------------------------------------------------------------------
// 自标定：模型内模板两两卡方距离
//   * 只有 >= 2 个模板才有意义；
//   * 同一个人有多个模板时，最小距离反映"同一人波动"，可作为阈值下界参考；
//   * 不同标签之间的最小距离则是"要区分开"的上界，阈值取两者的一个折中。
// ---------------------------------------------------------------------------
double LbphEngine::calibrate(double *minPair, double *worstPair) const
{
    double minD = 0.0, maxD = 0.0;
    int pairs = 0;
    for (int i = 0; i < m_histograms.size(); ++i) {
        for (int j = i + 1; j < m_histograms.size(); ++j) {
            const QVector<float> &a = m_histograms.at(i);
            const QVector<float> &b = m_histograms.at(j);
            if (a.size() != b.size() || a.isEmpty()) continue;
            double d = 0.0;
            for (int k = 0; k < a.size(); ++k) {
                if (a.at(k) == 0.0f) continue;
                const double diff = double(a.at(k)) - double(b.at(k));
                d += diff * diff / double(a.at(k));
            }
            if (pairs == 0 || d < minD) minD = d;
            if (pairs == 0 || d > maxD) maxD = d;
            ++pairs;
        }
    }
    if (minPair)  *minPair = minD;
    if (worstPair) *worstPair = maxD;
    if (pairs == 0) return m_verifyDistance;

    // 建议阈值：靠近"同人波动"的上沿，留一点余量但明显低于不同人的距离
    const double suggest = minD * 1.6 + 5.0;
    qDebug() << "LbphEngine: 模板两两距离 min" << minD << "max" << maxD
             << "对数" << pairs << "-> 建议 verify_distance" << suggest;
    return suggest;
}

// ---------------------------------------------------------------------------
// ELBP + 网格直方图（严格对照 OpenCV modules/face/src/lbph_faces.cpp）
//
//   ELBP:
//     for n in 0..neighbors-1:
//         ty = -radius * sin(2*pi*n/neighbors)
//         tx =  radius * cos(2*pi*n/neighbors)
//         cy = round(ty) + y ; cx = round(tx) + x      // 整数中心
//         ry = ty - round(ty); rx = tx - round(tx)     // 小数权重（插值）
//         取四邻域（含边界回退），按权重合成 a、b 两个"比较样本"
//         center >= sample 则该位为 1
//     模式码按 n 移位累加成 8bit（neighbors=8 时正好 256 种模式）
// ---------------------------------------------------------------------------
QVector<float> LbphEngine::computeHistogram(const QImage &frame, const QRect &faceRect) const
{
    QVector<float> out;
    if (frame.isNull() || !faceRect.isValid() || !modelLoaded()) return out;

    // 1) 取正方形人脸区域（按 cropScale 微调），裁到帧内
    QRect r = faceRect.normalized();
    const int side = qMax(r.width(), r.height());
    const QPoint c = r.center();
    int w = qMax(4, int(side * m_cropScale));
    int h = qMax(4, int(side * m_cropScale));
    QRect crop(c.x() - w / 2, c.y() - h / 2, w, h);
    crop = crop.intersected(frame.rect());
    if (crop.width() < 8 || crop.height() < 8) return out;

    // 2) 灰度 + 缩放到训练时的尺寸
    const QImage gray = frame.copy(crop)
        .convertToFormat(QImage::Format_RGB32)
        .scaled(m_imgShape, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int W = gray.width();
    const int H = gray.height();
    if (W < 16 || H < 16) return out;

    QVector<double> img(W * H, 0.0);
    {
        const quint32 *p = reinterpret_cast<const quint32 *>(gray.constBits());
        const int stride = gray.bytesPerLine() / 4;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                img[y * W + x] = double(lumaOf(p[y * stride + x]));
    }

    // 3) ELBP 模式码图
    //
    //    数值细节：neighbors=8, radius=1 时 sincos 表给出的偏移会算出
    //    ty = -2.449e-16 这种"本应为 0"的浮点残差，使得 ry=0、fy≈-2.4e-16。
    //    若照搬 OpenCV 的 4 邻居加权求和，采样值会比真实像素小约 1e-14，
    //    于是"所有像素相同"的邻域会因为 center >= sample 不成立而丢位
    //    （实测模式码会从 255 变成 213）。
    //    这里按 OpenCV 的取整语义把权重取整：fx/fy 归零时直接用该方向的整像素，
    //    既与 OpenCV 的结果一致，也不会引入浮点噪声。
    const int nb = m_params.neighbors;
    const double rad = double(m_params.radius);
    QVector<quint8> code(W * H, 0);
    for (int n = 0; n < nb; ++n) {
        const double ty = -rad * std::sin(2.0 * kPi * n / double(nb));
        const double tx =  rad * std::cos(2.0 * kPi * n / double(nb));
        // OpenCV 用 floor(x + 0.5) 做"四舍五入"，对 -0.5 附近的负数取整方向一致
        const int ry = int(std::floor(ty + 0.5));
        const int rx = int(std::floor(tx + 0.5));
        const double fyRaw = ty - double(ry);
        const double fxRaw = tx - double(rx);
        const double fy = (fyRaw > -1e-6 && fyRaw < 1e-6) ? 0.0 : fyRaw;
        const double fx = (fxRaw > -1e-6 && fxRaw < 1e-6) ? 0.0 : fxRaw;

        for (int y = rad; y < H - rad; ++y) {
            for (int x = rad; x < W - rad; ++x) {
                const int cy = y + ry;
                const int cx = x + rx;
                const double center = img[y * W + x];
                double sample = 0.0;

                if (cx >= 0 && cx < W && cy >= 0 && cy < H) {
                    const double v00 = img[cy * W + cx];
                    if (fx == 0.0 && fy == 0.0) {
                        sample = v00;                       // 正对整像素：不插值
                    } else if (fx == 0.0) {
                        sample = v00 * (1.0 - fy)
                               + ((cy + 1 < H) ? img[(cy + 1) * W + cx] * fy : v00 * fy);
                    } else if (fy == 0.0) {
                        sample = v00 * (1.0 - fx)
                               + ((cx + 1 < W) ? img[cy * W + (cx + 1)] * fx : v00 * fx);
                    } else {
                        const double v10 = (cx + 1 < W) ? img[cy * W + (cx + 1)] : v00;
                        const double v01 = (cy + 1 < H) ? img[(cy + 1) * W + cx] : v00;
                        const double v11 = (cx + 1 < W && cy + 1 < H)
                            ? img[(cy + 1) * W + (cx + 1)] : v00;
                        sample = v00 * (1.0 - fx) * (1.0 - fy)
                               + v10 * fx * (1.0 - fy)
                               + v01 * (1.0 - fx) * fy
                               + v11 * fx * fy;
                    }
                } else if (cx >= -1 && cx < W && cy >= -1 && cy < H) {
                    // 边界回退（OpenCV 对越界邻居的等价处理）
                    if (cx >= 0 && cy >= 0)          sample = img[cy * W + cx] * (1.0 - fx) * (1.0 - fy);
                    else if (cx + 1 < W && cy >= 0)  sample = img[cy * W + (cx + 1)] * fy;
                    else if (cy + 1 < H && cx >= 0)  sample = img[(cy + 1) * W + cx] * fx;
                }

                code[y * W + x] |= quint8((center >= sample ? 1 : 0) << n);
            }
        }
    }

    // 4) 网格划分 + 每格 256 bin 直方图 + 逐格归一化后拼接
    const int gx = m_params.gridX;
    const int gy = m_params.gridY;
    out.resize(gx * gy * 256);
    int offset = 0;

    for (int y = 0; y < gy; ++y) {
        for (int x = 0; x < gx; ++x) {
            // OpenCV: for(j=0;j<grid_y;j++) for(i=0;i<grid_x;i++)
            //   y = floor(j*height/grid_y) .. floor((j+1)*height/grid_y)
            const int yStart = int(double(y) * H / gy);
            const int yEnd   = (y == gy - 1) ? (H - 1) : int(double(y + 1) * H / gy);
            const int xStart = int(double(x) * W / gx);
            const int xEnd   = (x == gx - 1) ? (W - 1) : int(double(x + 1) * W / gx);

            for (int bin = 0; bin < 256; ++bin)
                out[offset + bin] = 0.0f;

            for (int yy = yStart; yy <= yEnd; ++yy) {
                if (yy < 0 || yy >= H) continue;
                for (int xx = xStart; xx <= xEnd; ++xx) {
                    if (xx < 0 || xx >= W) continue;
                    out[offset + code[yy * W + xx]] += 1.0f;
                }
            }

            // 每格直方图独立归一化（OpenCV: normalize(hist, temp_hist, 1, 0, NORM_L1)）
            float sum = 0.0f;
            for (int bin = 0; bin < 256; ++bin) sum += out[offset + bin];
            if (sum > 0.0f)
                for (int bin = 0; bin < 256; ++bin) out[offset + bin] /= sum;

            offset += 256;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 比对：卡方距离（OpenCV 的 compareHist(CHISQR)，越小越像）
// ---------------------------------------------------------------------------
LbphEngine::Predict LbphEngine::predict(const QVector<float> &histogram) const
{
    Predict best;
    if (histogram.isEmpty() || m_histograms.isEmpty()) return best;

    // 先按标签分组汇总距离：同一标签有多个模板时取平均（对同一人的多张样本更稳），
    // 再在所有标签里取最近的那个。单模板模型下等价于直接取最小距离。
    QHash<int, QPair<double, int> > perLabel;   // label -> (距离和, 模板数)
    for (int i = 0; i < m_histograms.size(); ++i) {
        const QVector<float> &t = m_histograms.at(i);
        if (t.size() != histogram.size()) continue;
        const int label = (i < m_labels.size()) ? m_labels.at(i) : -1;

        // CHISQR: sum( (a-b)^2 / a )，a 为 0 的 bin 跳过（该 bin 贡献为 0）
        double d = 0.0;
        const float *a = histogram.constData();
        const float *b = t.constData();
        for (int k = 0; k < histogram.size(); ++k) {
            if (a[k] == 0.0f) continue;
            const double diff = double(a[k]) - double(b[k]);
            d += diff * diff / double(a[k]);
        }

        QPair<double, int> acc = perLabel.value(label, qMakePair(0.0, 0));
        acc.first  += d;
        acc.second += 1;
        perLabel.insert(label, acc);
    }

    if (perLabel.isEmpty()) return best;

    double bestAvg = 1.0e30;
    for (QHash<int, QPair<double, int> >::const_iterator it = perLabel.constBegin();
         it != perLabel.constEnd(); ++it) {
        const double avg = it.value().first / double(qMax(1, it.value().second));
        if (avg < bestAvg) {
            bestAvg = avg;
            best.label = it.key();
        }
    }

    best.distance   = bestAvg;
    best.confidence = 1.0 / (1.0 + bestAvg);
    best.name       = nameFor(best.label);
    best.matched    = (bestAvg <= m_verifyDistance);
    return best;
}

LbphEngine::Predict LbphEngine::predict(const QImage &frame, const QRect &faceRect) const
{
    return predict(computeHistogram(frame, faceRect));
}
