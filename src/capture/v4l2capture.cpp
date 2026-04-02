#ifdef ENABLE_V4L2

#include "capture/v4l2capture.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <cstring>
#include <QDebug>
#include <opencv2/imgproc.hpp>

int V4l2Capture::xioctl(int fd, int request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); }
    while (r == -1 && errno == EINTR);
    return r;
}

V4l2Capture::V4l2Capture(const std::string &device, int width, int height,
                         int fps, int bufferCount, QObject *parent)
    : VideoSource(parent), m_device(device), m_fd(-1),
      m_width(width), m_height(height), m_fps(fps),
      m_bufferCount(bufferCount), m_pixelFormat(V4L2_PIX_FMT_YUYV) {}

V4l2Capture::~V4l2Capture() {
    stop();
    wait();
    close();
}

bool V4l2Capture::open() {
    m_fd = ::open(m_device.c_str(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        emit errorOccurred(QString("无法打开设备: %1 (%2)")
                           .arg(m_device.c_str()).arg(strerror(errno)));
        return false;
    }

    if (!queryCapability()) return false;
    enumFormats(); // 仅打印信息，不影响流程
    if (!tryAndSetFormat()) return false;
    if (!setFrameRate()) return false;
    if (!requestBuffers()) return false;

    qDebug() << QString("V4L2 就绪: %1x%2 @ %3fps, %4 buffers")
                .arg(m_width).arg(m_height).arg(m_fps).arg(m_bufferCount);
    return true;
}

void V4l2Capture::close() {
    if (m_fd >= 0) {
        stopStreaming();
        releaseBuffers();
        ::close(m_fd);
        m_fd = -1;
    }
}

// === V4L2 初始化流程 ===

bool V4l2Capture::queryCapability() {
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));

    if (xioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        emit errorOccurred("VIDIOC_QUERYCAP 失败");
        return false;
    }

    qDebug() << "Driver:" << (char*)cap.driver
             << "Card:" << (char*)cap.card
             << "Bus:" << (char*)cap.bus_info;

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        emit errorOccurred("设备不支持视频采集");
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        emit errorOccurred("设备不支持 streaming I/O");
        return false;
    }
    return true;
}

bool V4l2Capture::enumFormats() {
    struct v4l2_fmtdesc fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    qDebug() << "支持的像素格式:";
    while (xioctl(m_fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        qDebug() << QString("  [%1] %2 (0x%3)")
                    .arg(fmt.index)
                    .arg((char*)fmt.description)
                    .arg(fmt.pixelformat, 8, 16, QChar('0'));
        fmt.index++;
    }
    return true;
}

bool V4l2Capture::tryAndSetFormat() {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = m_width;
    fmt.fmt.pix.height = m_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    // 先 TRY，让驱动调整到最接近的参数
    if (xioctl(m_fd, VIDIOC_TRY_FMT, &fmt) < 0) {
        qDebug() << "TRY_FMT 失败，尝试直接设置";
    }

    // 驱动可能调整了分辨率
    if ((int)fmt.fmt.pix.width != m_width || (int)fmt.fmt.pix.height != m_height) {
        qDebug() << QString("分辨率被调整: %1x%2 -> %3x%4")
                    .arg(m_width).arg(m_height)
                    .arg(fmt.fmt.pix.width).arg(fmt.fmt.pix.height);
        m_width = fmt.fmt.pix.width;
        m_height = fmt.fmt.pix.height;
    }

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        emit errorOccurred("VIDIOC_S_FMT 失败");
        return false;
    }

    m_pixelFormat = fmt.fmt.pix.pixelformat;
    return true;
}

bool V4l2Capture::setFrameRate() {
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = m_fps;

    if (xioctl(m_fd, VIDIOC_S_PARM, &parm) < 0) {
        qDebug() << "设置帧率失败，使用默认帧率";
    }
    return true;
}

bool V4l2Capture::requestBuffers() {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = m_bufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) {
        emit errorOccurred("VIDIOC_REQBUFS 失败");
        return false;
    }

    m_buffers.resize(req.count);
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            emit errorOccurred("VIDIOC_QUERYBUF 失败");
            return false;
        }

        m_buffers[i].length = buf.length;
        m_buffers[i].start = mmap(NULL, buf.length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, m_fd, buf.m.offset);
        if (m_buffers[i].start == MAP_FAILED) {
            emit errorOccurred("mmap 失败");
            return false;
        }

        if (xioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            emit errorOccurred("VIDIOC_QBUF 初始入队失败");
            return false;
        }
    }
    return true;
}

bool V4l2Capture::startStreaming() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        emit errorOccurred("VIDIOC_STREAMON 失败");
        return false;
    }
    return true;
}

bool V4l2Capture::stopStreaming() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return xioctl(m_fd, VIDIOC_STREAMOFF, &type) == 0;
}

void V4l2Capture::releaseBuffers() {
    for (auto &b : m_buffers) {
        if (b.start && b.start != MAP_FAILED)
            munmap(b.start, b.length);
    }
    m_buffers.clear();
}

bool V4l2Capture::grabFrame(void **data, int *size) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int r = select(m_fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return false;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) return false;

    *data = m_buffers[buf.index].start;
    *size = buf.bytesused;

    if (xioctl(m_fd, VIDIOC_QBUF, &buf) < 0) return false;

    return true;
}

// === 采集线程主循环 ===

void V4l2Capture::run() {
    if (!startStreaming()) return;
    m_running = true;

    while (m_running) {
        void *data = nullptr;
        int size = 0;

        if (!grabFrame(&data, &size)) {
            if (m_running)
                qDebug() << "采集帧超时";
            continue;
        }

        // YUYV → BGR → QImage
        cv::Mat yuyv(m_height, m_width, CV_8UC2, data);
        cv::Mat bgr;
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);

        // BGR → RGB → QImage (深拷贝，因为 data 指向 mmap 缓冲区)
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows,
                     rgb.step, QImage::Format_RGB888);
        emit frameReady(image.copy());
    }

    stopStreaming();
}

// === V4L2 Controls ===

bool V4l2Capture::setControl(uint32_t id, int value) {
    struct v4l2_control ctrl;
    ctrl.id = id;
    ctrl.value = value;
    return xioctl(m_fd, VIDIOC_S_CTRL, &ctrl) == 0;
}

int V4l2Capture::getControl(uint32_t id) {
    struct v4l2_control ctrl;
    ctrl.id = id;
    if (xioctl(m_fd, VIDIOC_G_CTRL, &ctrl) < 0) return -1;
    return ctrl.value;
}

bool V4l2Capture::setExposure(int value)        { return setControl(V4L2_CID_EXPOSURE, value); }
bool V4l2Capture::setAutoExposure(bool enable)  { return setControl(V4L2_CID_EXPOSURE_AUTO, enable ? 0 : 1); }
bool V4l2Capture::setGain(int value)            { return setControl(V4L2_CID_GAIN, value); }
bool V4l2Capture::setAutoGain(bool enable)      { return setControl(V4L2_CID_AUTOGAIN, enable ? 1 : 0); }
bool V4l2Capture::setContrast(int value)        { return setControl(V4L2_CID_CONTRAST, value); }
bool V4l2Capture::setSaturation(int value)      { return setControl(V4L2_CID_SATURATION, value); }
bool V4l2Capture::setHue(int value)             { return setControl(V4L2_CID_HUE, value); }
bool V4l2Capture::setHFlip(bool enable)         { return setControl(V4L2_CID_HFLIP, enable ? 1 : 0); }
bool V4l2Capture::setVFlip(bool enable)         { return setControl(V4L2_CID_VFLIP, enable ? 1 : 0); }
bool V4l2Capture::setAutoWhiteBalance(bool en)  { return setControl(V4L2_CID_AUTO_WHITE_BALANCE, en ? 1 : 0); }
bool V4l2Capture::setRedBalance(int value)      { return setControl(V4L2_CID_RED_BALANCE, value); }
bool V4l2Capture::setBlueBalance(int value)     { return setControl(V4L2_CID_BLUE_BALANCE, value); }

#endif // ENABLE_V4L2
