#include "face/face_engine.h"

#include <QDataStream>
#include <QIODevice>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QtGlobal>
#include <cmath>

const double FaceEngine::kVerify = 0.60;
const double FaceEngine::kKnown  = 0.50;

namespace {

// 取 RGB32 像素的三个分量
inline void rgbOf(quint32 px, int &r, int &g, int &b)
{
    r = int((px >> 16) & 0xFF);
    g = int((px >> 8)  & 0xFF);
    b = int(px & 0xFF);
}

inline quint8 lumaOf(quint32 px)
{
    return quint8((77  * int((px >> 16) & 0xFF)
                 + 150 * int((px >> 8)  & 0xFF)
                 +  29 * int(px & 0xFF)) >> 8);
}

// 判定肤色：YCbCr 经典阈值 + 基础 RGB 约束
inline bool isSkin(int r, int g, int b)
{
    const int y  =  (77 * r + 150 * g + 29 * b) >> 8;
    const int cb = 128 - int(0.168736 * r) - int(0.331264 * g) + int(0.5 * b);
    const int cr = 128 + int(0.5 * r) - int(0.418688 * g) - int(0.081312 * b);
    return (y >= 55) && (r >= 55) && (r >= b)
            && (cb >= 77 && cb <= 130)
            && (cr >= 133 && cr <= 175);
}

// 把检测结果映射回原始帧坐标并补全额头/下巴，取正方形
QRect expandFaceRect(const QRect &skin, int frameW, int frameH)
{
    // 收紧取景框（用户反馈原来 1.35 倍框太松）：少量上补额头、下补下巴
    const int h = int(skin.height() * 1.12);
    const int w = qMax(skin.width(), int(skin.width() * 1.02));
    const int cx = skin.center().x();
    const int cy = skin.y() + h / 2 + int(skin.height() * 0.01);
    int side = qMax(w, h);

    side = qMin(side, qMin(frameW, frameH));
    int x = cx - side / 2;
    int y = cy - side / 2;
    x = qBound(0, x, frameW - side);
    y = qBound(0, y, frameH - side);
    return QRect(x, y, side, side);
}

double ncc(const float *a, const float *b, int n)
{
    // a、b 都已零均值单位方差，NCC = dot / n
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += double(a[i]) * double(b[i]);
    return qBound(-1.0, s / n, 1.0);
}

} // namespace

// ---------------------------------------------------------------------------

FaceEngine::FaceEngine()
    : m_enrolling(false), m_enrollRemaining(0), m_enrollTotal(0)
{
}

FaceEngine::Analysis FaceEngine::analyze(const QImage &frame, bool identify)
{
    Analysis a;
    if (frame.isNull()) return a;

    QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    QImage small = (rgb.width() > 256)
        ? rgb.scaledToWidth(256, Qt::SmoothTransformation) : rgb;

    int rawScore = 0;
    const QRect d = detectSkinFace(small, &rawScore);
    if (d.isNull()) return a;                       // 未检测到人脸

    const double fx = double(rgb.width())  / small.width();
    const double fy = double(rgb.height()) / small.height();
    const QRect skinFull(qRound(d.x() * fx), qRound(d.y() * fy),
                         qRound(d.width() * fx), qRound(d.height() * fy));
    a.faceFound = true;
    a.faceRect  = expandFaceRect(skinFull, rgb.width(), rgb.height());

    if (!identify) return a;

    // ---- 后端 1：LBPH（OpenCV trainer.yml，纯 C++ 复刻）----
    if (m_lbph.modelLoaded()) {
        const LbphEngine::Predict p = m_lbph.predict(rgb, a.faceRect);
        if (p.label >= 0) {
            a.backend      = BackendLbph;
            a.lbphLabel    = p.label;
            a.lbphDistance = p.distance;
            a.score        = qBound(0.0, p.confidence, 1.0);
            a.bestScore    = a.score;
            a.bestName     = p.name;
            if (p.matched) {
                a.matched = true;
                a.name    = p.name;
            }
            return a;
        }
        // 模型在但特征算不出来（人脸框太小等）-> 落到 NCC 兜底
    }

    // ---- 后端 2：NCC 模板（faces.db）----
    if (m_persons.isEmpty()) return a;

    QVector<float> t = alignTemplate(rgb, a.faceRect);
    if (t.isEmpty()) return a;

    const float *tp = t.constData();
    double best = -1.0;
    QString bestName;
    for (int i = 0; i < m_persons.size(); ++i) {
        const double s = ncc(tp, m_persons[i].mean.constData(), FACE * FACE);
        if (s > best) { best = s; bestName = m_persons[i].name; }
    }
    a.backend   = BackendTemplate;
    a.bestScore = qMax(0.0, best);
    a.bestName  = bestName;
    a.score     = a.bestScore;
    if (best >= kVerify) {
        a.matched = true;
        a.name    = bestName;
    }
    return a;
}

// ---------------------------------------------------------------------------
// LBPH 后端：模型 + 姓名映射配置
// ---------------------------------------------------------------------------
bool FaceEngine::loadLbphFiles(const QString &dirPath)
{
    m_lbphDir = dirPath;
    QDir dir(dirPath);
    return loadLbphFrom(dir.filePath(QStringLiteral("trainer.yml")),
                        dir.filePath(QStringLiteral("faces_model.json")));
}

bool FaceEngine::loadLbphFrom(const QString &modelPath, const QString &configPath)
{
    if (!QFileInfo(modelPath).exists()) {
        qWarning() << "FaceEngine: 未找到 LBPH 模型" << modelPath << "，识别使用 NCC 模板";
        return false;
    }

    // 先读配置（模型里 labelsInfo 常为空，靠它做 标签->姓名 映射并覆盖阈值）
    if (!configPath.isEmpty() && QFileInfo(configPath).exists())
        m_lbph.loadConfig(configPath);
    m_lbphDir = QFileInfo(modelPath).absolutePath();

    const bool ok = m_lbph.loadModel(modelPath);
    if (!ok) return false;

    // 阈值优先级：faces_model.json 里显式给了 verify_distance 就用它，
    // 否则用模型内模板间距自标定（OpenCV 的 threshold 常是 DBL_MAX，等于没有阈值）
    if (!m_lbph.verifyFromConfig()) {
        double minPair = 0.0, maxPair = 0.0;
        const double suggest = m_lbph.calibrate(&minPair, &maxPair);
        m_lbph.setVerifyDistance(suggest);
        qDebug() << "FaceEngine: LBPH 阈值自标定为" << suggest
                 << "(模型内模板间距" << minPair << "~" << maxPair << ")";
    }
    return true;
}

QString FaceEngine::lbphStatusText() const
{
    if (!m_lbph.modelLoaded())
        return QStringLiteral("未加载 LBPH 模型");
    return QStringLiteral("LBPH 模型 %1 个模板，判定距离 ≤ %2")
        .arg(m_lbph.sampleCount())
        .arg(m_lbph.verifyDistance(), 0, 'f', 1);
}

// ---------------------------------------------------------------------------
// 检测：肤色掩码 -> 3x3 多数滤波 -> 连通域 -> 几何过滤 + 中心加权
// ---------------------------------------------------------------------------
QRect FaceEngine::detectSkinFace(const QImage &small, int *scoreOut)
{
    const int W = small.width();
    const int H = small.height();
    if (W < 32 || H < 32) return QRect();

    const quint32 *px = reinterpret_cast<const quint32 *>(small.constBits());
    const int stride = small.bytesPerLine() / 4;

    // 1) 肤色掩码
    QVector<quint8> mask(W * H, 0);
    for (int y = 0; y < H; ++y) {
        const quint32 *line = px + y * stride;
        quint8 *m = mask.data() + y * W;
        for (int x = 0; x < W; ++x) {
            int r, g, b;
            rgbOf(line[x], r, g, b);
            m[x] = isSkin(r, g, b) ? 1 : 0;
        }
    }

    // 2) 3x3 多数滤波（去掉孤立噪点、填补小洞）
    QVector<quint8> clean(W * H, 0);
    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            int cnt = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    cnt += mask[(y + dy) * W + (x + dx)];
            clean[y * W + x] = (cnt >= 5) ? 1 : 0;
        }
    }

    // 3) 连通域（4 邻接 BFS，取满足几何条件的最高分块）
    const int minW   = qMax(24, W / 10);           // 脸至少占画面宽度 1/10
    const int minArea = qMax(minW * minW / 2, W * H / 200);
    QVector<int> label(W * H, -1);
    QVector<int> queue;
    queue.reserve(W * H / 4);

    QRect bestRect;
    double bestScore = 0.0;
    int nextLabel = 0;

    for (int start = 0; start < W * H; ++start) {
        if (clean[start] == 0 || label[start] >= 0) continue;

        queue.clear();
        queue.append(start);
        label[start] = nextLabel;
        int area = 0;
        int x0 = W, y0 = H, x1 = 0, y1 = 0;
        int head = 0;
        while (head < queue.size()) {
            const int idx = queue.at(head++);
            const int yy = idx / W, xx = idx % W;
            ++area;
            if (xx < x0) x0 = xx;
            if (yy < y0) y0 = yy;
            if (xx > x1) x1 = xx;
            if (yy > y1) y1 = yy;
            // 4 邻接
            if (xx > 0    && clean[idx - 1] && label[idx - 1] < 0) { label[idx - 1] = nextLabel; queue.append(idx - 1); }
            if (xx < W-1  && clean[idx + 1] && label[idx + 1] < 0) { label[idx + 1] = nextLabel; queue.append(idx + 1); }
            if (yy > 0    && clean[idx - W] && label[idx - W] < 0) { label[idx - W] = nextLabel; queue.append(idx - W); }
            if (yy < H-1  && clean[idx + W] && label[idx + W] < 0) { label[idx + W] = nextLabel; queue.append(idx + W); }
        }
        ++nextLabel;

        const int bw = x1 - x0 + 1;
        const int bh = y1 - y0 + 1;
        if (bw < minW || bh < minW * 3 / 4 || area < minArea) continue;

        const double fill = double(area) / double(bw * bh);
        if (fill < 0.38) continue;

        const double ar = double(bw) / double(bh);
        if (ar < 0.55 || ar > 1.8) continue;

        // 中心加权：越靠画面中心越可信
        const double dx = qAbs((x0 + x1) / 2.0 - W / 2.0) / (W / 2.0);
        const double dy = qAbs((y0 + y1) / 2.0 - H / 2.0) / (H / 2.0);
        const double score = fill * (1.0 - 0.25 * qMin(1.0, dx + dy));
        if (score > bestScore) {
            bestScore = score;
            bestRect = QRect(x0, y0, bw, bh);
        }
    }

    if (scoreOut) *scoreOut = int(bestScore * 100);
    return bestRect;
}

// ---------------------------------------------------------------------------
// 对齐 + 归一化：裁剪 -> 缩放 48x48 灰度 -> 3x3 盒滤波 -> 零均值单位方差
// ---------------------------------------------------------------------------
QVector<float> FaceEngine::alignTemplate(const QImage &frame, const QRect &faceRect)
{
    QVector<float> out;
    if (frame.isNull() || !faceRect.isValid()) return out;

    QRect r = faceRect.normalized();
    const int side = qMax(r.width(), r.height());
    const QPoint c = r.center();
    r = QRect(c.x() - side / 2, c.y() - side / 2, side, side);
    r = r.intersected(frame.rect());
    if (r.width() < FACE / 2 || r.height() < FACE / 2) return out;

    QImage small = frame.copy(r).convertToFormat(QImage::Format_RGB32)
                       .scaled(FACE, FACE, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    if (small.isNull()) return out;

    // 灰度
    float raw[FACE * FACE];
    const quint32 *p = reinterpret_cast<const quint32 *>(small.constBits());
    const int stride = small.bytesPerLine() / 4;
    for (int y = 0; y < FACE; ++y)
        for (int x = 0; x < FACE; ++x)
            raw[y * FACE + x] = float(lumaOf(p[y * stride + x]));

    // 3x3 盒滤波去噪
    float blur[FACE * FACE];
    for (int y = 0; y < FACE; ++y) {
        for (int x = 0; x < FACE; ++x) {
            int cnt = 0; double sum = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = qBound(0, y + dy, FACE - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = qBound(0, x + dx, FACE - 1);
                    sum += raw[yy * FACE + xx]; ++cnt;
                }
            }
            blur[y * FACE + x] = float(sum / cnt);
        }
    }

    // 零均值 / 单位方差（抗光照变化）
    double mean = 0.0;
    for (int i = 0; i < FACE * FACE; ++i) mean += blur[i];
    mean /= FACE * FACE;
    double var = 0.0;
    for (int i = 0; i < FACE * FACE; ++i) {
        const double d = blur[i] - mean;
        var += d * d;
    }
    var = std::sqrt(var / (FACE * FACE));
    if (var < 1e-6) return out;

    out.resize(FACE * FACE);
    for (int i = 0; i < FACE * FACE; ++i)
        out[i] = float((blur[i] - mean) / var);
    return out;
}

// ---------------------------------------------------------------------------
// 建档
// ---------------------------------------------------------------------------
void FaceEngine::beginEnroll(const QString &name, int samples)
{
    m_enrolling      = true;
    m_enrollName     = name;
    m_enrollTotal    = qMax(1, samples);
    m_enrollRemaining = m_enrollTotal;
    m_enrollBuf.clear();
}

void FaceEngine::cancelEnroll()
{
    m_enrolling      = false;
    m_enrollRemaining = 0;
    m_enrollTotal    = 0;
    m_enrollBuf.clear();
}

bool FaceEngine::addEnrollSample(const QImage &frame, const QRect &faceRect)
{
    if (!m_enrolling) return false;
    const QVector<float> t = alignTemplate(frame, faceRect);
    if (t.isEmpty()) return false;                  // 本帧不可用，不计入

    m_enrollBuf.append(t);
    if (m_enrollBuf.size() >= m_enrollTotal) {
        // 求均值模板
        const int n = FACE * FACE;
        QVector<float> mean(n, 0.0f);
        for (int s = 0; s < m_enrollBuf.size(); ++s) {
            const float *sp = m_enrollBuf.at(s).constData();
            for (int i = 0; i < n; ++i) mean[i] += sp[i];
        }
        for (int i = 0; i < n; ++i) mean[i] /= float(m_enrollBuf.size());

        // 重新归一化到单位方差
        double var = 0.0;
        for (int i = 0; i < n; ++i) var += double(mean[i]) * mean[i];
        var = std::sqrt(var / n);
        if (var < 1e-6) return false;
        for (int i = 0; i < n; ++i) mean[i] = float(mean[i] / var);

        // 同名覆盖（重新登记）
        for (int i = 0; i < m_persons.size(); ++i) {
            if (m_persons[i].name == m_enrollName) { m_persons.removeAt(i); break; }
        }
        Person p;
        p.name    = m_enrollName;
        p.mean    = mean;
        p.secs    = QDateTime::currentMSecsSinceEpoch() / 1000;   // 兼容 Qt 5.4（secs 接口 5.8 才有）
        p.samples = m_enrollBuf.size();
        m_persons.append(p);

        m_enrolling = false;
        m_enrollBuf.clear();
        return true;                                // 建档完成
    }
    --m_enrollRemaining;
    return false;
}

// ---------------------------------------------------------------------------
// 人脸库持久化：magic 'HFDB' + version + 记录
// ---------------------------------------------------------------------------
bool FaceEngine::save(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "FaceEngine::save open failed:" << path;
        return false;
    }
    QDataStream ds(&file);
    ds << quint32(0x48464442u)          // 'HFDB'
       << quint32(2)                    // version
       << qint32(FACE)
       << qint32(m_persons.size());
    for (int i = 0; i < m_persons.size(); ++i) {
        const Person &p = m_persons.at(i);
        ds << p.name << p.secs << qint32(p.samples);
        ds.writeRawData(reinterpret_cast<const char *>(p.mean.constData()),
                        FACE * FACE * int(sizeof(float)));
    }
    if (!file.commit()) {
        qWarning() << "FaceEngine::save commit failed:" << path;
        return false;
    }
    return true;
}

bool FaceEngine::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QDataStream ds(&file);
    quint32 magic = 0, ver = 0;
    qint32 face = 0, count = 0;
    ds >> magic >> ver >> face >> count;
    if (magic != 0x48464442u || ver != 2 || face != FACE || count < 0) {
        qWarning() << "FaceEngine::load bad file:" << path;
        return false;
    }

    QVector<Person> loaded;
    for (int i = 0; i < count; ++i) {
        Person p;
        qint32 samples = 0;
        ds >> p.name >> p.secs >> samples;
        p.samples = samples;
        p.mean.resize(FACE * FACE);
        if (ds.readRawData(reinterpret_cast<char *>(p.mean.data()),
                           FACE * FACE * int(sizeof(float)))
                != FACE * FACE * int(sizeof(float))) {
            qWarning() << "FaceEngine::load truncated at" << i;
            return false;
        }
        if (!p.name.isEmpty()) loaded.append(p);
    }

    m_persons = loaded;
    qDebug() << "FaceEngine::load" << m_persons.size() << "person(s) from" << path;
    return true;
}

QStringList FaceEngine::persons() const
{
    QStringList out;
    for (int i = 0; i < m_persons.size(); ++i)
        out.append(m_persons.at(i).name);
    return out;
}

bool FaceEngine::remove(const QString &name)
{
    for (int i = 0; i < m_persons.size(); ++i) {
        if (m_persons.at(i).name == name) {
            m_persons.removeAt(i);
            return true;
        }
    }
    return false;
}

void FaceEngine::clear()
{
    m_persons.clear();
}
