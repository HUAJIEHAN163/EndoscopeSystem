// 条件编译：只有定义了 ENABLE_V4L2 才编译这个文件
// CMakeLists.txt 中 add_definitions(-DENABLE_V4L2) 控制
#ifdef ENABLE_V4L2

#include "capture/v4l2capture.h"

// === Linux 系统头文件 ===
#include <fcntl.h>        // open() 的标志位（O_RDWR、O_NONBLOCK）
#include <unistd.h>       // close()、read()、write()
#include <sys/ioctl.h>    // ioctl() — 设备控制的核心系统调用
#include <sys/mman.h>     // mmap() / munmap() — 内存映射
#include <sys/select.h>   // select() — 等待设备数据就绪
#include <cstring>        // memset()、strerror()

// === Qt / OpenCV ===
#include <QDebug>             // qDebug 调试输出
#include <QElapsedTimer>      // 性能计时
#include <opencv2/imgproc.hpp> // cvtColor 颜色空间转换

// =====================================================================
// xioctl — ioctl 的安全封装
// ioctl 可能被系统信号中断（返回 -1，errno == EINTR），需要重试
// 所有 V4L2 操作都通过这个函数调用 ioctl
// =====================================================================
int V4l2Capture::xioctl(int fd, int request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); }
    while (r == -1 && errno == EINTR);  // 被信号中断就重试
    return r;
}

// =====================================================================
// 构造函数
// 只保存参数，不做任何硬件操作（硬件操作在 open() 中）
// =====================================================================
V4l2Capture::V4l2Capture(const std::string &device, int width, int height,
                         int fps, int bufferCount, QObject *parent)
    : VideoSource(parent),          // 调用父类构造函数（设置 parent，m_running = false）
      m_device(device),             // 设备路径，如 "/dev/video0"
      m_fd(-1),                     // 文件描述符，-1 表示未打开
      m_width(width),               // 期望宽度
      m_height(height),             // 期望高度
      m_fps(fps),                   // 期望帧率
      m_bufferCount(bufferCount),   // 缓冲区数量（请求值，实际分配可能不同）
      m_pixelFormat(V4L2_PIX_FMT_YUYV) {} // 默认像素格式 YUYV

// =====================================================================
// 析构函数
// 确保线程停止、资源释放
// =====================================================================
V4l2Capture::~V4l2Capture() {
    stop();   // m_running = false，通知 run() 循环退出
    wait();   // 等待线程真正结束（QThread::wait）
    close();  // 关闭设备、释放 mmap 缓冲区
}

// =====================================================================
// open — 打开设备并完成初始化
//
// 完整流程：
//   open 设备 → 查询能力 → 枚举格式 → 设置格式 → 设置帧率 → 申请缓冲区
//
// 这是 V4L2 的标准初始化流程，几乎所有 V4L2 程序都是这个顺序
// =====================================================================
bool V4l2Capture::open() {
    // 打开设备文件
    // O_RDWR：可读可写（采集需要读，控制参数需要写）
    // O_NONBLOCK：非阻塞模式（配合 select 使用，避免 read 卡死）
    m_fd = ::open(m_device.c_str(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        emit errorOccurred(QString("无法打开设备: %1 (%2)")
                           .arg(m_device.c_str()).arg(strerror(errno)));
        return false;
    }

    // 按顺序执行初始化步骤，任何一步失败都返回 false
    if (!queryCapability()) return false;  // 1. 查询设备能力
    enumFormats();                          // 2. 枚举支持的格式（仅打印，不影响流程）
    if (!tryAndSetFormat()) return false;   // 3. 设置像素格式和分辨率
    if (!setFrameRate()) return false;      // 4. 设置帧率
    if (!requestBuffers()) return false;    // 5. 申请缓冲区并 mmap 映射

    qDebug() << QString("V4L2 就绪: %1x%2 @ %3fps, %4 buffers (实际分配)")
                .arg(m_width).arg(m_height).arg(m_fps).arg(m_bufferCount);
    return true;
}

// =====================================================================
// close — 关闭设备，释放所有资源
// =====================================================================
void V4l2Capture::close() {
    if (m_fd >= 0) {
        stopStreaming();    // 停止视频流
        releaseBuffers();   // 解除 mmap 映射
        ::close(m_fd);      // 关闭文件描述符（:: 表示调用全局的 close，不是类的 close）
        m_fd = -1;
    }
}

// =====================================================================
// V4L2 初始化流程 — 逐步详解
// =====================================================================

// --- 第 1 步：查询设备能力 ---
// 确认设备支持视频采集和 streaming I/O
bool V4l2Capture::queryCapability() {
    struct v4l2_capability cap;     // V4L2 能力结构体
    memset(&cap, 0, sizeof(cap));   // 清零（V4L2 要求结构体先清零）

    // VIDIOC_QUERYCAP：查询设备能力
    if (xioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        emit errorOccurred("VIDIOC_QUERYCAP 失败");
        return false;
    }

    // 打印设备信息（调试用）
    qDebug() << "Driver:" << (char*)cap.driver      // 驱动名，如 "ov5640"
             << "Card:" << (char*)cap.card           // 设备名
             << "Bus:" << (char*)cap.bus_info;       // 总线信息

    // 检查是否支持视频采集
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        emit errorOccurred("设备不支持视频采集");
        return false;
    }
    // 检查是否支持 streaming I/O（mmap 方式需要）
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        emit errorOccurred("设备不支持 streaming I/O");
        return false;
    }
    return true;
}

// --- 第 2 步：枚举支持的像素格式 ---
// 仅打印信息，不影响流程，帮助开发者了解设备支持什么格式
bool V4l2Capture::enumFormats() {
    struct v4l2_fmtdesc fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    qDebug() << "支持的像素格式:";
    // VIDIOC_ENUM_FMT：枚举格式，index 从 0 开始递增，直到返回失败
    while (xioctl(m_fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        qDebug() << QString("  [%1] %2 (0x%3)")
                    .arg(fmt.index)
                    .arg((char*)fmt.description)     // 格式描述，如 "YUYV 4:2:2"
                    .arg(fmt.pixelformat, 8, 16, QChar('0'));
        fmt.index++;
    }
    return true;
}

// --- 第 3 步：设置像素格式和分辨率 ---
bool V4l2Capture::tryAndSetFormat() {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = m_width;              // 期望宽度
    fmt.fmt.pix.height = m_height;            // 期望高度
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;  // YUYV 格式（内窥镜常用）
    fmt.fmt.pix.field = V4L2_FIELD_NONE;      // 逐行扫描（非隔行）

    // VIDIOC_TRY_FMT：试探性设置，驱动会调整到最接近的支持参数
    // 不会真正生效，只是让驱动告诉你它能支持什么
    if (xioctl(m_fd, VIDIOC_TRY_FMT, &fmt) < 0) {
        qDebug() << "TRY_FMT 失败，尝试直接设置";
    }

    // 驱动可能调整了分辨率（比如请求 640x480，驱动只支持 320x240）
    if ((int)fmt.fmt.pix.width != m_width || (int)fmt.fmt.pix.height != m_height) {
        qDebug() << QString("分辨率被调整: %1x%2 -> %3x%4")
                    .arg(m_width).arg(m_height)
                    .arg(fmt.fmt.pix.width).arg(fmt.fmt.pix.height);
        m_width = fmt.fmt.pix.width;    // 更新为实际分辨率
        m_height = fmt.fmt.pix.height;
    }

    // VIDIOC_S_FMT：正式设置格式
    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        emit errorOccurred("VIDIOC_S_FMT 失败");
        return false;
    }

    m_pixelFormat = fmt.fmt.pix.pixelformat;  // 记录实际使用的格式
    return true;
}

// --- 第 4 步：设置帧率 ---
bool V4l2Capture::setFrameRate() {
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // 帧率 = denominator / numerator，如 30/1 = 30fps
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = m_fps;

    // 帧率设置失败不致命，使用驱动默认帧率
    if (xioctl(m_fd, VIDIOC_S_PARM, &parm) < 0) {
        qDebug() << "设置帧率失败，使用默认帧率";
    }
    return true;
}

// --- 第 5 步：申请缓冲区并 mmap 映射 ---
// 这是 V4L2 最核心的部分：在内核中分配缓冲区，然后映射到用户空间
//
// 流程：
//   REQBUFS（申请 N 个缓冲区）
//     → QUERYBUF（查询每个缓冲区的地址和大小）
//       → mmap（映射到用户空间，用户可以直接读取像素数据）
//         → QBUF（把缓冲区放入采集队列，等待驱动填充数据）
bool V4l2Capture::requestBuffers() {
    // 1. 申请缓冲区
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = m_bufferCount;                // 请求的缓冲区数量（通常 4 个）
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;            // 使用 mmap 方式（零拷贝）

    // VIDIOC_REQBUFS：让内核分配缓冲区
    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) {
        emit errorOccurred("VIDIOC_REQBUFS 失败");
        return false;
    }

    // 【问题 #13 验证】检查驱动实际分配的 buffer 数量
    qWarning() << QString("[Buffer 验证] 请求 %1 个 buffer，驱动实际分配 %2 个")
                  .arg(m_bufferCount).arg(req.count);
    
    if (req.count != (unsigned)m_bufferCount) {
        qWarning() << QString("[Buffer 警告] 驱动分配的 buffer 数量少于请求数量！")
                   << "这可能导致 overrun 错误，建议优化处理速度。";
    }
    
    if (req.count < 2) {
        emit errorOccurred("驱动分配的 buffer 数量不足（< 2）");
        return false;
    }
    
    // 更新 m_bufferCount 为实际分配的数量
    m_bufferCount = req.count;

    // 2. 逐个查询并映射缓冲区
    m_buffers.resize(req.count);
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        // VIDIOC_QUERYBUF：查询缓冲区的偏移量和大小
        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            emit errorOccurred("VIDIOC_QUERYBUF 失败");
            return false;
        }

        // mmap：把内核缓冲区映射到用户空间
        // 映射后 m_buffers[i].start 直接指向像素数据，不需要 copy
        // PROT_READ | PROT_WRITE：可读可写
        // MAP_SHARED：与内核共享（驱动写入的数据用户立刻能看到）
        m_buffers[i].length = buf.length;
        m_buffers[i].start = mmap(NULL, buf.length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, m_fd, buf.m.offset);
        if (m_buffers[i].start == MAP_FAILED) {
            emit errorOccurred("mmap 失败");
            return false;
        }

        // VIDIOC_QBUF：把缓冲区放入采集队列
        // 驱动会从队列中取出空缓冲区，填充摄像头数据
        if (xioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            emit errorOccurred("VIDIOC_QBUF 初始入队失败");
            return false;
        }
    }
    return true;
}

// --- 开始/停止视频流 ---

bool V4l2Capture::startStreaming() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // VIDIOC_STREAMON：开始采集，驱动开始往缓冲区填数据
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        emit errorOccurred("VIDIOC_STREAMON 失败");
        return false;
    }
    return true;
}

bool V4l2Capture::stopStreaming() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // VIDIOC_STREAMOFF：停止采集
    return xioctl(m_fd, VIDIOC_STREAMOFF, &type) == 0;
}

// --- 释放 mmap 映射 ---
void V4l2Capture::releaseBuffers() {
    for (auto &b : m_buffers) {
        if (b.start && b.start != MAP_FAILED)
            munmap(b.start, b.length);  // 解除映射
    }
    m_buffers.clear();
}

// =====================================================================
// grabFrame — 采集一帧
//
// 流程：
//   select 等待数据就绪 → DQBUF 取出填好的缓冲区 → 读取数据 → QBUF 归还缓冲区
//
// 缓冲区轮转机制（以 4 个 buffer 为例）：
//   初始：[队列: buf0, buf1, buf2, buf3]  全部在队列中等待填充
//   驱动填充 buf0 → DQBUF 取出 buf0 → 用户读取 → QBUF 归还 buf0
//   驱动填充 buf1 → DQBUF 取出 buf1 → 用户读取 → QBUF 归还 buf1
//   ...循环往复，保证始终有 buffer 在队列中等待驱动填充
// =====================================================================
bool V4l2Capture::grabFrame(void **data, int *size, int *bufIndex) {
    // select：等待设备有数据可读（最多等 2 秒）
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int r = select(m_fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return false;

    // VIDIOC_DQBUF：取出已填充数据的缓冲区
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) return false;

    *data = m_buffers[buf.index].start;
    *size = buf.bytesused;
    *bufIndex = buf.index;  // 记录 buffer 索引，用完数据后再归还

    return true;
}

// 归还缓冲区 — 必须在数据拷贝完成后调用
bool V4l2Capture::returnBuffer(int bufIndex) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = bufIndex;
    return xioctl(m_fd, VIDIOC_QBUF, &buf) >= 0;
}

// =====================================================================
// run — 采集线程主循环（在独立线程中执行）
//
// 数据流：
//   摄像头 → YUYV 原始数据 → BGR → RGB → QImage → emit frameReady → 主线程
//
// 为什么要两次颜色转换：
//   YUYV → BGR：OpenCV 默认使用 BGR 格式（所有算法基于 BGR）
//   BGR → RGB：QImage 使用 RGB 格式（Qt 显示需要 RGB）
// =====================================================================
void V4l2Capture::run() {
    if (!startStreaming()) return;
    m_running = true;

    int frameCount = 0;
    qint64 totalProcessTime = 0;
    QElapsedTimer perfTimer;

    // m_running 控制帧处理，m_quit 控制线程退出
    while (!m_quit) {
        // 暂停时不处理帧，但仍然 grabFrame + returnBuffer 保持 V4L2 流活跃
        void *data = nullptr;
        int size = 0;
        int bufIndex = -1;

        if (!grabFrame(&data, &size, &bufIndex)) {
            if (!m_quit)
                qDebug() << "采集帧超时";
            continue;
        }

        // 暂停时只归还 buffer，不处理
        if (!m_running) {
            returnBuffer(bufIndex);
            continue;
        }

        perfTimer.start();

        // YUYV → BGR，在归还 buffer 之前完成拷贝
        cv::Mat yuyv(m_height, m_width, CV_8UC2, data);
        cv::Mat bgr;
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);

        // 数据已拷贝到 bgr，归还 buffer 给驱动
        returnBuffer(bufIndex);

        // 队列中已有 2 帧未处理时丢弃，避免堆积导致 OOM
        if (m_pendingFrames.load() >= 2) continue;

        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows,
                     rgb.step, QImage::Format_RGB888);
        m_pendingFrames.fetch_add(1);
        emit frameReady(image.copy());

        qint64 elapsed = perfTimer.elapsed();
        totalProcessTime += elapsed;
        frameCount++;

        // 每 100 帧输出一次统计
        if (frameCount % 100 == 0) {
            qreal avgTime = totalProcessTime / (qreal)frameCount;
            qWarning() << QString("[性能] 已处理 %1 帧，平均耗时 %2 ms/帧")
                          .arg(frameCount).arg(avgTime, 0, 'f', 1);
        }
    }

    // 输出最终统计
    if (frameCount > 0) {
        qreal avgTime = totalProcessTime / (qreal)frameCount;
        qWarning() << QString("[性能总结] 总帧数 %1，平均耗时 %2 ms/帧")
                      .arg(frameCount).arg(avgTime, 0, 'f', 1);
    }

    stopStreaming();
}

// =====================================================================
// V4L2 Controls — 硬件参数控制
//
// 通过 ioctl + VIDIOC_S_CTRL / VIDIOC_G_CTRL 控制摄像头硬件参数
// 这些参数由 Sensor（OV5640）的驱动提供，不同 Sensor 支持的参数不同
// =====================================================================

// 通用的设置/获取控制参数方法
bool V4l2Capture::setControl(uint32_t id, int value) {
    struct v4l2_control ctrl;
    ctrl.id = id;        // 参数 ID（如 V4L2_CID_GAIN）
    ctrl.value = value;  // 要设置的值
    return xioctl(m_fd, VIDIOC_S_CTRL, &ctrl) == 0;
}

int V4l2Capture::getControl(uint32_t id) {
    struct v4l2_control ctrl;
    ctrl.id = id;
    if (xioctl(m_fd, VIDIOC_G_CTRL, &ctrl) < 0) return -1;
    return ctrl.value;
}

// 各参数的快捷方法（内部都调用 setControl）
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
