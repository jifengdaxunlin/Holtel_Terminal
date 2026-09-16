#ifndef CAMERATHREAD_H
#define CAMERATHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstddef>

#ifdef Q_OS_LINUX
#include <linux/videodev2.h>
#else
// 非 Linux 平台编译占位（fourcc 与平面数上限）
#define VIDEO_MAX_PLANES 8
#define V4L2_PIX_FMT_YUYV   0x56595559UL
#define V4L2_PIX_FMT_UYVY   0x59565955UL
#define V4L2_PIX_FMT_YVYU   0x55595659UL
#define V4L2_PIX_FMT_NV12   0x3231564EUL
#define V4L2_PIX_FMT_NV21   0x3132564EUL
#define V4L2_PIX_FMT_MJPEG  0x47504A4DUL
#define V4L2_PIX_FMT_RGB565 0x50424752UL
#endif

/*
 * 基于 V4L2 的摄像头采集线程。
 * 支持：
 *   - 单平面 / 多平面(MPLANE) 采集；
 *   - YUYV / UYVY / YVYU / NV12 / NV21 / RGB565 / MJPEG；
 *   - 枚举驱动真实格式(ENUM_FMT)与分辨率(ENUM_FRAMESIZES)；
 *   - 请求帧率(VIDIOC_S_PARM)；
 *   - 转换时降采样 + 只渲染最新帧，保证预览流畅。
 */
class CameraThread : public QThread
{
    Q_OBJECT

public:
    explicit CameraThread(const QString &device, int width, int height,
                          QObject *parent = 0);
    ~CameraThread();

    // 线程安全的停止接口
    void stop();

    // 扫描 /dev/video* 设备列表
    static QStringList availableDevices();
    // 查询设备描述："设备路径 [能力] 卡名"，失败返回空串
    static QString queryDeviceName(const QString &device);

    // 取出最新一帧（取出后清空；"只渲染最新帧"策略，保证低延迟流畅）
    QImage takePending();

    // 运行时交换 U/V（红蓝反转时的快速修正）
    void setSwapUV(bool on);
    // 当前格式支持的分辨率列表（打开后有效）
    QList<QSize> supportedFrameSizes() const { return m_frameSizes; }

signals:
    void cameraOpened(const QString &info);
    void cameraError(const QString &message);
    void cameraClosed();
    void frameAvailable();   // 有新帧待取，配合 takePending()

protected:
    void run();

private:
    bool openDevice();
    void enumerateFormats();
    bool isSupportedFormat(quint32 fourcc) const;
    bool tryFormat(quint32 fourcc);
    bool initFormat();
    bool requestFps(int fps);
    void enumerateFrameSizes();
    bool initBuffers();
    bool startStream();
    void closeDevice();
    QImage convertFrame(const unsigned char *src, quint32 bytesused);
    QImage yuv422ToRgb32(const unsigned char *src, int w, int h, int layout);
    QImage nv12ToRgb32(const unsigned char *src, int w, int h, bool nv21);
    QImage rgb565ToRgb32(const unsigned char *src, int w, int h);

    // 一个缓冲区的所有平面（单平面设备 planeCount == 1）
    struct CamBuffer {
        unsigned char *addr[VIDEO_MAX_PLANES];
        size_t len[VIDEO_MAX_PLANES];
        CamBuffer() { memset(this, 0, sizeof(*this)); }
    };

    QString m_device;
    int m_reqWidth;
    int m_reqHeight;
    std::atomic<bool> m_running;

    int m_fd;
    bool m_isMplane;        // 是否多平面(MPLANE)设备
    int m_planeCount;       // 每个缓冲区的平面数
    int m_bufType;          // v4l2_buf_type
    CamBuffer *m_buffers;
    int m_bufferCount;
    quint32 m_pixelFormat;  // 协商后的像素格式
    int m_width;            // 实际生效的分辨率
    int m_height;
    int m_decim;            // 转换时降采样倍数（直接缩小输出，大幅省 CPU）
    QList<quint32> m_enumFormats;   // 驱动枚举到的格式
    QList<QSize> m_frameSizes;      // 当前格式支持的分辨率
    std::atomic<bool> m_swapUV;     // 运行时交换 U/V
    QImage m_pending;       // 最新待渲染帧（只保留一帧，旧的丢弃）
    QMutex m_pendingMutex;
};

#endif // CAMERATHREAD_H
