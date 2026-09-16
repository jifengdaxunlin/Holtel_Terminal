#include "camerathread.h"

#include <QDir>
#include <QByteArray>
#include <algorithm>
#include <cstring>
#include <cerrno>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

// 饱和处理，防止颜色溢出
static inline int clamp255(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

CameraThread::CameraThread(const QString &device, int width, int height, QObject *parent)
    : QThread(parent)
    , m_device(device)
    , m_reqWidth(width)
    , m_reqHeight(height)
    , m_running(false)
    , m_fd(-1)
    , m_isMplane(false)
    , m_planeCount(1)
    , m_bufType(0)
    , m_buffers(0)
    , m_bufferCount(0)
    , m_pixelFormat(0)
    , m_width(width)
    , m_height(height)
    , m_decim(1)
    , m_swapUV(false)
{
}

CameraThread::~CameraThread()
{
    stop();
    wait(3000);
}

void CameraThread::stop()
{
    m_running = false;
}

QImage CameraThread::takePending()
{
    m_pendingMutex.lock();
    QImage frame = m_pending;
    m_pending = QImage();
    m_pendingMutex.unlock();
    return frame;
}

void CameraThread::setSwapUV(bool on)
{
    m_swapUV = on;
}

void CameraThread::enumerateFormats()
{
#ifdef Q_OS_LINUX
    m_enumFormats.clear();
    struct v4l2_fmtdesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.type = (v4l2_buf_type)m_bufType;
    while (::ioctl(m_fd, VIDIOC_ENUM_FMT, &desc) == 0) {
        m_enumFormats.append(desc.pixelformat);
        desc.index++;
    }
#endif
}

bool CameraThread::isSupportedFormat(quint32 fourcc) const
{
    return fourcc == V4L2_PIX_FMT_YUYV || fourcc == V4L2_PIX_FMT_UYVY
        || fourcc == V4L2_PIX_FMT_YVYU || fourcc == V4L2_PIX_FMT_NV12
        || fourcc == V4L2_PIX_FMT_NV21 || fourcc == V4L2_PIX_FMT_MJPEG
        || fourcc == V4L2_PIX_FMT_RGB565;
}

QStringList CameraThread::availableDevices()
{
    QStringList list;
#ifdef Q_OS_LINUX
    QDir devDir("/dev");
    QStringList names = devDir.entryList(QStringList("video*"), QDir::System, QDir::Name);
    foreach (const QString &n, names)
        list << "/dev/" + n;
#else
    list << "/dev/video0";
#endif
    return list;
}

QString CameraThread::queryDeviceName(const QString &device)
{
#ifdef Q_OS_LINUX
    int fd = ::open(device.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0)
        return QString();

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        ::close(fd);
        return QString();
    }
    ::close(fd);

    quint32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                       ? cap.device_caps : cap.capabilities;
    QString capStr;
    if (caps & V4L2_CAP_VIDEO_CAPTURE)
        capStr = QString::fromUtf8("采集");
    else if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        capStr = QString::fromUtf8("采集MPLANE");
    else if (caps & V4L2_CAP_VIDEO_OUTPUT)
        capStr = QString::fromUtf8("输出");
    else
        capStr = QString::fromUtf8("其他");

    QString card = QString::fromLocal8Bit(
        QByteArray(reinterpret_cast<const char *>(cap.card)).trimmed());
    if (card.isEmpty())
        card = QString::fromLocal8Bit(
            QByteArray(reinterpret_cast<const char *>(cap.driver)).trimmed());

    return QString("%1 [%2] %3").arg(device, capStr, card);
#else
    Q_UNUSED(device);
    return QString();
#endif
}

void CameraThread::run()
{
#ifdef Q_OS_LINUX
    m_running = true;

    bool ok = openDevice() && initFormat() && requestFps(30)
              && initBuffers() && startStream();
    if (ok) {
        QString fmtName;
        if (m_pixelFormat == V4L2_PIX_FMT_MJPEG)
            fmtName = "MJPEG";
        else if (m_pixelFormat == V4L2_PIX_FMT_YUYV)
            fmtName = "YUYV";
        else if (m_pixelFormat == V4L2_PIX_FMT_UYVY)
            fmtName = "UYVY";
        else if (m_pixelFormat == V4L2_PIX_FMT_YVYU)
            fmtName = "YVYU";
        else if (m_pixelFormat == V4L2_PIX_FMT_NV12)
            fmtName = "NV12";
        else if (m_pixelFormat == V4L2_PIX_FMT_NV21)
            fmtName = "NV21";
        else if (m_pixelFormat == V4L2_PIX_FMT_RGB565)
            fmtName = "RGB565";
        else
            fmtName = "UNKNOWN";

        emit cameraOpened(QString("%1  %2x%3  %4%5  预览%6x%7")
                          .arg(m_device).arg(m_width).arg(m_height).arg(fmtName)
                          .arg(m_isMplane ? QString::fromUtf8("  MPLANE") : QString())
                          .arg(m_width / m_decim).arg(m_height / m_decim));

        int timeoutCount = 0;
        while (m_running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_fd, &fds);
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int r = ::select(m_fd + 1, &fds, 0, 0, &tv);
            if (r == 0) {
                // 超时：设备长时间没有出图
                if (++timeoutCount % 5 == 0)
                    emit cameraError(QString::fromUtf8(
                        "等待图像数据超时，传感器可能没出图（检查排线/供电/驱动探测）"));
                continue;
            }
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                emit cameraError(QString::fromUtf8("select 失败: %1").arg(strerror(errno)));
                break;
            }

            struct v4l2_buffer buf;
            struct v4l2_plane planes[VIDEO_MAX_PLANES];
            memset(&buf, 0, sizeof(buf));
            memset(planes, 0, sizeof(planes));
            buf.type = (v4l2_buf_type)m_bufType;
            buf.memory = V4L2_MEMORY_MMAP;
            if (m_isMplane) {
                buf.length = m_planeCount;
                buf.m.planes = planes;
            }

            if (::ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN)
                    continue;
                emit cameraError(QString::fromUtf8("取帧失败(DQBUF): %1").arg(strerror(errno)));
                break;
            }

            quint32 bytesused = m_isMplane ? planes[0].bytesused : buf.bytesused;
            if (bytesused > 0) {
                QImage frame = convertFrame(m_buffers[buf.index].addr[0], bytesused);
                if (!frame.isNull()) {
                    // 只保留最新一帧：渲染跟不上时自动丢旧帧，延迟不累积
                    m_pendingMutex.lock();
                    m_pending = frame;
                    m_pendingMutex.unlock();
                    emit frameAvailable();
                }
            }

            if (::ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
                emit cameraError(QString::fromUtf8("缓冲区重新入队失败(QBUF): %1").arg(strerror(errno)));
                break;
            }
        }
    }

    closeDevice();
    emit cameraClosed();
#else
    // 非 Linux 平台（如 Windows 桌面预览编译）：给出提示即可
    Q_UNUSED(m_reqWidth);
    Q_UNUSED(m_reqHeight);
    Q_UNUSED(m_device);
    emit cameraError(QString::fromUtf8("当前平台不支持 V4L2 摄像头，请在 Linux / ARM 目标板上运行本程序"));
    emit cameraClosed();
#endif
}

bool CameraThread::openDevice()
{
#ifdef Q_OS_LINUX
    m_fd = ::open(m_device.toLocal8Bit().constData(), O_RDWR);
    if (m_fd < 0) {
        emit cameraError(QString::fromUtf8("无法打开设备 %1: %2")
                         .arg(m_device, QString::fromLocal8Bit(strerror(errno))));
        return false;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (::ioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        emit cameraError(QString::fromUtf8("QUERYCAP 失败：这可能不是 V4L2 设备"));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    // 多功能设备需优先看 device_caps
    quint32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                       ? cap.device_caps : cap.capabilities;

    // 单平面 / 多平面(MPLANE) 采集都支持
    if (caps & V4L2_CAP_VIDEO_CAPTURE) {
        m_isMplane = false;
    } else if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        m_isMplane = true;
    } else {
        emit cameraError(QString::fromUtf8(
            "该节点不是采集设备（可能是输出/ISP/M2M 节点），请在下拉框中选择带[采集]的节点"));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    if (!(caps & V4L2_CAP_STREAMING)) {
        emit cameraError(QString::fromUtf8("设备不支持 mmap 流式采集(V4L2_CAP_STREAMING)"));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_bufType = (int)(m_isMplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                 : V4L2_BUF_TYPE_VIDEO_CAPTURE);

    // 枚举驱动真实支持的像素格式
    enumerateFormats();
    return true;
#else
    return false;
#endif
}

bool CameraThread::tryFormat(quint32 fourcc)
{
#ifdef Q_OS_LINUX
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = (v4l2_buf_type)m_bufType;

    if (m_isMplane) {
        fmt.fmt.pix_mp.width = (unsigned int)m_reqWidth;
        fmt.fmt.pix_mp.height = (unsigned int)m_reqHeight;
        fmt.fmt.pix_mp.pixelformat = fourcc;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes = 1;
    } else {
        fmt.fmt.pix.width = (unsigned int)m_reqWidth;
        fmt.fmt.pix.height = (unsigned int)m_reqHeight;
        fmt.fmt.pix.pixelformat = fourcc;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }

    if (::ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0)
        return false;

    // 驱动会回填实际生效的格式，必须比对
    quint32 got = m_isMplane ? fmt.fmt.pix_mp.pixelformat : fmt.fmt.pix.pixelformat;
    if (got != fourcc)
        return false;

    if (m_isMplane) {
        m_width = (int)fmt.fmt.pix_mp.width;
        m_height = (int)fmt.fmt.pix_mp.height;
        m_planeCount = (int)fmt.fmt.pix_mp.num_planes;
        if (m_planeCount < 1 || m_planeCount > VIDEO_MAX_PLANES)
            return false;
    } else {
        m_width = (int)fmt.fmt.pix.width;
        m_height = (int)fmt.fmt.pix.height;
        m_planeCount = 1;
    }

    // 预览按需降采样：宽 >1280 的帧转换时直接 1/2 缩小，像素处理量降到 1/4
    m_decim = (m_width > 1280) ? 2 : 1;

    m_pixelFormat = got;

    // 枚举该格式实际支持的分辨率，供界面刷新下拉框
    enumerateFrameSizes();
    return true;
#else
    Q_UNUSED(fourcc);
    return false;
#endif
}

bool CameraThread::initFormat()
{
    // 优先尝试驱动声明支持的格式（ENUM_FMT），再补充常见格式兜底
    QList<quint32> tryList;
    for (int i = 0; i < m_enumFormats.size(); ++i) {
        quint32 f = m_enumFormats.at(i);
        if (isSupportedFormat(f) && !tryList.contains(f))
            tryList.append(f);
    }

    static const quint32 common[] = {
        V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_UYVY, V4L2_PIX_FMT_NV12,
        V4L2_PIX_FMT_NV21, V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_RGB565
    };
    const int commonCount = (int)(sizeof(common) / sizeof(common[0]));
    for (int i = 0; i < commonCount; ++i) {
        if (!tryList.contains(common[i]))
            tryList.append(common[i]);
    }

    for (int i = 0; i < tryList.size(); ++i) {
        if (tryFormat(tryList.at(i)))
            return true;
    }

    emit cameraError(QString::fromUtf8(
        "驱动支持的格式均无法协商成功，请把串口日志发给开发者分析"));
    return false;
}

bool CameraThread::requestFps(int fps)
{
#ifdef Q_OS_LINUX
    // 部分驱动默认帧率偏低，必须显式请求（不支持时静默忽略）
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = (v4l2_buf_type)m_bufType;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = (unsigned int)fps;
    ::ioctl(m_fd, VIDIOC_S_PARM, &parm);
    return true;
#else
    Q_UNUSED(fps);
    return true;
#endif
}

void CameraThread::enumerateFrameSizes()
{
#ifdef Q_OS_LINUX
    m_frameSizes.clear();

    struct v4l2_frmsizeenum fs;
    memset(&fs, 0, sizeof(fs));
    fs.pixel_format = m_pixelFormat;
    if (::ioctl(m_fd, VIDIOC_ENUM_FRAMESIZES, &fs) != 0)
        return;   // 驱动不支持枚举，界面沿用静态列表

    if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        for (;;) {
            m_frameSizes.append(QSize((int)fs.discrete.width, (int)fs.discrete.height));
            fs.index++;
            if (::ioctl(m_fd, VIDIOC_ENUM_FRAMESIZES, &fs) != 0)
                break;
        }
    } else {
        // STEPWISE / CONTINUOUS：取 min 到 max 的 5 档
        int minw = (int)fs.stepwise.min_width;
        int maxw = (int)fs.stepwise.max_width;
        int minh = (int)fs.stepwise.min_height;
        int maxh = (int)fs.stepwise.max_height;
        for (int i = 0; i < 5; ++i) {
            int wq = minw + (maxw - minw) * i / 4;
            int hq = minh + (maxh - minh) * i / 4;
            m_frameSizes.append(QSize(wq, hq));
        }
    }

    // 去重并按像素数从大到小排序
    QList<QSize> uniq;
    for (int i = 0; i < m_frameSizes.size(); ++i)
        if (!uniq.contains(m_frameSizes.at(i)))
            uniq.append(m_frameSizes.at(i));
    std::sort(uniq.begin(), uniq.end(),
              [](const QSize &a, const QSize &b) {
                  return a.width() * a.height() > b.width() * b.height();
              });
    m_frameSizes = uniq;
#endif
}

bool CameraThread::initBuffers()
{
#ifdef Q_OS_LINUX
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = (v4l2_buf_type)m_bufType;
    req.memory = V4L2_MEMORY_MMAP;

    if (::ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        emit cameraError(QString::fromUtf8("申请内核缓冲区失败(VIDIOC_REQBUFS)"));
        return false;
    }

    m_bufferCount = (int)req.count;
    m_buffers = new CamBuffer[m_bufferCount];

    for (int i = 0; i < m_bufferCount; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = (v4l2_buf_type)m_bufType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = (unsigned int)i;
        if (m_isMplane) {
            buf.length = m_planeCount;
            buf.m.planes = planes;
        }

        if (::ioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            emit cameraError(QString::fromUtf8("查询缓冲区失败(VIDIOC_QUERYBUF)"));
            return false;
        }

        for (int p = 0; p < m_planeCount; ++p) {
            size_t len;
            quint32 offset;
            if (m_isMplane) {
                len = planes[p].length;
                offset = planes[p].m.mem_offset;
            } else {
                len = buf.length;
                offset = buf.m.offset;
            }

            m_buffers[i].addr[p] = (unsigned char *)::mmap(0, len,
                                                           PROT_READ | PROT_WRITE,
                                                           MAP_SHARED, m_fd, offset);
            m_buffers[i].len[p] = len;
            if (m_buffers[i].addr[p] == MAP_FAILED) {
                m_buffers[i].addr[p] = 0;
                emit cameraError(QString::fromUtf8("内存映射失败(mmap)"));
                return false;
            }
        }
    }
    return true;
#else
    return false;
#endif
}

bool CameraThread::startStream()
{
#ifdef Q_OS_LINUX
    for (int i = 0; i < m_bufferCount; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = (v4l2_buf_type)m_bufType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = (unsigned int)i;
        if (m_isMplane) {
            buf.length = m_planeCount;
            buf.m.planes = planes;
        }
        if (::ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            emit cameraError(QString::fromUtf8("缓冲区入队失败(VIDIOC_QBUF)"));
            return false;
        }
    }

    v4l2_buf_type type = (v4l2_buf_type)m_bufType;
    if (::ioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        emit cameraError(QString::fromUtf8("启动视频流失败(VIDIOC_STREAMON): %1").arg(strerror(errno)));
        return false;
    }
    return true;
#else
    return false;
#endif
}

void CameraThread::closeDevice()
{
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        v4l2_buf_type type = (v4l2_buf_type)m_bufType;
        ::ioctl(m_fd, VIDIOC_STREAMOFF, &type);
    }

    if (m_buffers) {
        for (int i = 0; i < m_bufferCount; ++i) {
            for (int p = 0; p < m_planeCount && p < VIDEO_MAX_PLANES; ++p) {
                if (m_buffers[i].addr[p])
                    ::munmap(m_buffers[i].addr[p], m_buffers[i].len[p]);
                m_buffers[i].addr[p] = 0;
                m_buffers[i].len[p] = 0;
            }
        }
        delete [] m_buffers;
        m_buffers = 0;
    }
    m_bufferCount = 0;

    if (m_fd >= 0) {
        // 归还内核缓冲区
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 0;
        req.type = (v4l2_buf_type)m_bufType;
        req.memory = V4L2_MEMORY_MMAP;
        ::ioctl(m_fd, VIDIOC_REQBUFS, &req);

        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

// 按协商好的格式转换为一帧 QImage
QImage CameraThread::convertFrame(const unsigned char *src, quint32 bytesused)
{
    if (m_pixelFormat == V4L2_PIX_FMT_MJPEG)
        return QImage::fromData(src, (int)bytesused, "JPEG");
    if (m_pixelFormat == V4L2_PIX_FMT_YUYV)
        return yuv422ToRgb32(src, m_width, m_height, 0);
    if (m_pixelFormat == V4L2_PIX_FMT_UYVY)
        return yuv422ToRgb32(src, m_width, m_height, 1);
    if (m_pixelFormat == V4L2_PIX_FMT_YVYU)
        return yuv422ToRgb32(src, m_width, m_height, 2);
    if (m_pixelFormat == V4L2_PIX_FMT_NV12)
        return nv12ToRgb32(src, m_width, m_height, false);
    if (m_pixelFormat == V4L2_PIX_FMT_NV21)
        return nv12ToRgb32(src, m_width, m_height, true);
    if (m_pixelFormat == V4L2_PIX_FMT_RGB565)
        return rgb565ToRgb32(src, m_width, m_height);
    return QImage();
}

// 打包 YUV 4:2:2 -> RGB32，layout: 0=YUYV 1=UYVY 2=YVYU，转换时按 m_decim 降采样
QImage CameraThread::yuv422ToRgb32(const unsigned char *src, int w, int h, int layout)
{
    const int d = m_decim > 0 ? m_decim : 1;
    const int ow = w / d;
    const int oh = h / d;
    QImage img(ow, oh, QImage::Format_RGB32);

    for (int oy = 0; oy < oh; ++oy) {
        const unsigned char *line = src + (size_t)oy * d * w * 2;
        QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(oy));

        for (int ox = 0; ox < ow; ++ox) {
            int xs = (ox * d) & ~1;   // 以像素对为单位，取偶数列
            const unsigned char *p = line + xs * 2;

            int yy, u, v;
            if (layout == 0) {        // Y0 U Y1 V
                yy = p[0]; u = p[1] - 128; v = p[2] - 128;
            } else if (layout == 1) { // U Y0 V Y1
                u = p[0] - 128; yy = p[1]; v = p[2] - 128;
            } else {                  // Y0 V Y1 U
                yy = p[0]; v = p[1] - 128; u = p[2] - 128;
            }
            if (m_swapUV)
                qSwap(u, v);

            int rv = (91881 * v) >> 16;   // 1.402
            int gu = (22554 * u) >> 16;   // 0.344
            int gv = (46802 * v) >> 16;   // 0.714
            int bu = (116130 * u) >> 16;  // 1.772

            dst[ox] = qRgb(clamp255(yy + rv), clamp255(yy - gu - gv), clamp255(yy + bu));
        }
    }
    return img;
}

// NV12/NV21(4:2:0 半平面) -> RGB32，转换时按 m_decim 降采样
QImage CameraThread::nv12ToRgb32(const unsigned char *src, int w, int h, bool nv21)
{
    const int d = m_decim > 0 ? m_decim : 1;
    const int ow = w / d;
    const int oh = h / d;
    QImage img(ow, oh, QImage::Format_RGB32);

    const unsigned char *yPlane  = src;
    const unsigned char *uvPlane = src + (size_t)w * h;

    for (int oy = 0; oy < oh; ++oy) {
        int ys = oy * d;
        const unsigned char *yLine  = yPlane + (size_t)ys * w;
        const unsigned char *uvLine = uvPlane + (size_t)(ys >> 1) * w;
        QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(oy));

        for (int ox = 0; ox < ow; ++ox) {
            int xs = ox * d;
            int yy = yLine[xs];
            int u  = (int)uvLine[(xs >> 1) * 2 + (nv21 ? 1 : 0)] - 128;
            int v  = (int)uvLine[(xs >> 1) * 2 + (nv21 ? 0 : 1)] - 128;
            if (m_swapUV)
                qSwap(u, v);

            int rv = (91881 * v) >> 16;
            int gu = (22554 * u) >> 16;
            int gv = (46802 * v) >> 16;
            int bu = (116130 * u) >> 16;

            dst[ox] = qRgb(clamp255(yy + rv), clamp255(yy - gu - gv), clamp255(yy + bu));
        }
    }
    return img;
}

// RGB565 -> RGB32，转换时按 m_decim 降采样
QImage CameraThread::rgb565ToRgb32(const unsigned char *src, int w, int h)
{
    const int d = m_decim > 0 ? m_decim : 1;
    const int ow = w / d;
    const int oh = h / d;
    QImage img(ow, oh, QImage::Format_RGB32);

    for (int oy = 0; oy < oh; ++oy) {
        const quint16 *line = reinterpret_cast<const quint16 *>(
            src + (size_t)oy * d * w * 2);
        QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(oy));

        for (int ox = 0; ox < ow; ++ox) {
            quint16 px = line[ox * d];

            int r = (px >> 11) & 0x1f;
            int g = (px >> 5) & 0x3f;
            int b = px & 0x1f;
            r = (r << 3) | (r >> 2);   // 扩展到 8bit
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);

            dst[ox] = qRgb(r, g, b);
        }
    }
    return img;
}
