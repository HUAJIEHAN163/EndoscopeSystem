#ifndef V4L2CAPTURE_H
#define V4L2CAPTURE_H

#ifdef ENABLE_V4L2

// === 项目内部头文件 ===
#include "capture/videosource.h"  // 视频源抽象基类（QThread + 信号槽）

// === 标准库 ===
#include <string>                 // std::string（设备路径）
#include <vector>                 // std::vector（缓冲区数组）

// === Linux V4L2 API ===
#include <linux/videodev2.h>      // V4L2 结构体和常量定义（v4l2_buffer、VIDIOC_* 等）

// =====================================================================
// FrameBuffer — V4L2 mmap 缓冲区描述
//
// V4L2 使用 mmap 方式实现零拷贝采集：
//   内核分配缓冲区 → mmap 映射到用户空间 → 摄像头直接写入 → 用户直接读取
//
// 每个 FrameBuffer 对应一个内核缓冲区的用户空间映射：
//   start  — mmap 返回的指针，指向像素数据的起始地址
//   length — 缓冲区大小（字节），munmap 时需要
// =====================================================================
struct FrameBuffer {
    void *start;      // mmap 映射后的用户空间地址
    size_t length;    // 缓冲区大小（字节）
};

// =====================================================================
// V4l2Capture — V4L2 摄像头采集类
//
// 继承自 VideoSource（QThread），在独立线程中采集摄像头数据。
//
// 工作流程：
//   1. open()  — 打开设备、查询能力、设置格式/帧率、申请缓冲区
//   2. start() — 启动采集线程（QThread::start）
//   3. run()   — 线程主循环：grabFrame → cvtColor → emit frameReady
//   4. stop()  — 设置退出标志，线程循环结束
//   5. close() — 停止视频流、释放 mmap 缓冲区、关闭设备
//
// V4L2 缓冲区轮转机制（以 4 个 buffer 为例）：
//   初始：[队列: buf0, buf1, buf2, buf3]  全部在队列中等待填充
//   驱动填充 buf0 → DQBUF 取出 → 用户读取 → QBUF 归还 → 驱动继续填充
//   ...循环往复，保证始终有 buffer 在队列中等待驱动填充
//
// 硬件参数控制：
//   通过 V4L2 ioctl (VIDIOC_S_CTRL) 直接控制 OV5640 Sensor 寄存器，
//   支持白平衡、曝光、增益、对比度、饱和度、翻转等参数调节。
// =====================================================================
class V4l2Capture : public VideoSource {
    Q_OBJECT
public:
    // 构造函数：只保存参数，不做硬件操作（硬件操作在 open() 中）
    // device      — 设备路径，如 "/dev/video0"
    // width/height — 期望分辨率（驱动可能调整到最接近的支持值）
    // fps         — 期望帧率
    // bufferCount — 请求的 mmap 缓冲区数量（驱动实际分配可能不同）
    V4l2Capture(const std::string &device = "/dev/video0",
                int width = 640, int height = 480,
                int fps = 30, int bufferCount = 4,
                QObject *parent = nullptr);
    ~V4l2Capture();

    // --- VideoSource 接口实现 ---
    bool open() override;   // 打开设备并完成 V4L2 初始化流程
    void close() override;  // 关闭设备，释放所有资源
    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }

    // === 硬件参数控制（通过 V4L2 ioctl 直接控制 OV5640 Sensor）===
    //
    // 这些参数由 Sensor 驱动提供，不同 Sensor 支持的参数不同。
    // OV5640 实际支持的参数通过 v4l2-ctl --list-ctrls 查看。
    //
    // 返回值：true = 设置成功，false = 设置失败（参数不支持或值超范围）

    // 曝光控制
    bool setExposure(int value);       // 手动曝光值（0-65535，需先关闭自动曝光）
    bool setAutoExposure(bool enable); // 自动曝光开关

    // 增益控制
    bool setGain(int value);           // 手动增益值（0-1023，需先关闭自动增益）
    bool setAutoGain(bool enable);     // 自动增益开关

    // 色彩调节
    bool setContrast(int value);       // 对比度（0-255）
    bool setSaturation(int value);     // 饱和度（0-255）
    bool setHue(int value);            // 色调（0-359）

    // 画面翻转（根据摄像头安装方向设置）
    bool setHFlip(bool enable);        // 水平翻转
    bool setVFlip(bool enable);        // 垂直翻转

    // 白平衡控制
    bool setAutoWhiteBalance(bool enable);  // 自动白平衡开关
    bool setRedBalance(int value);     // 手动红色增益（0-4095，需先关闭自动白平衡）
    bool setBlueBalance(int value);    // 手动蓝色增益（0-4095，需先关闭自动白平衡）

protected:
    // 采集线程主循环（QThread::run 的重写）
    // 在独立线程中执行：startStreaming → 循环 grabFrame → stopStreaming
    void run() override;

private:
    // === V4L2 初始化流程（在 open() 中按顺序调用）===
    bool queryCapability();   // 1. 查询设备能力（是否支持采集和 streaming）
    bool enumFormats();       // 2. 枚举支持的像素格式（仅打印，不影响流程）
    bool tryAndSetFormat();   // 3. 设置像素格式和分辨率
    bool setFrameRate();      // 4. 设置帧率
    bool requestBuffers();    // 5. 申请 mmap 缓冲区并映射到用户空间
    bool startStreaming();    // 开始视频流（VIDIOC_STREAMON）
    bool stopStreaming();     // 停止视频流（VIDIOC_STREAMOFF）
    void releaseBuffers();    // 释放 mmap 映射

    // === 帧采集 ===
    // grabFrame    — DQBUF 取出已填充的缓冲区，返回数据指针和 buffer 索引
    // returnBuffer — QBUF 归还缓冲区给驱动（必须在数据拷贝完成后调用）
    //
    // 分离 DQBUF 和 QBUF 是为了避免读写冲突（见问题记录 #7）：
    //   grabFrame 取出 buffer → cvtColor 拷贝数据 → returnBuffer 归还 buffer
    bool grabFrame(void **data, int *size, int *bufIndex);
    bool returnBuffer(int bufIndex);

    // ioctl 安全封装：自动处理 EINTR（被信号中断时重试）
    static int xioctl(int fd, int request, void *arg);

    // V4L2 control 辅助方法（所有硬件参数控制最终调用这两个）
    bool setControl(uint32_t id, int value);  // VIDIOC_S_CTRL
    int getControl(uint32_t id);              // VIDIOC_G_CTRL

    // === 成员变量 ===
    std::string m_device;              // 设备路径，如 "/dev/video0"
    int m_fd;                          // 设备文件描述符（-1 表示未打开）
    int m_width;                       // 实际分辨率宽度（可能被驱动调整）
    int m_height;                      // 实际分辨率高度
    int m_fps;                         // 帧率
    int m_bufferCount;                 // 实际分配的缓冲区数量（可能少于请求值）
    uint32_t m_pixelFormat;            // 实际使用的像素格式（如 V4L2_PIX_FMT_YUYV）
    std::vector<FrameBuffer> m_buffers; // mmap 缓冲区数组
    int m_dqbufErrors = 0;              // DQBUF 失败计数（overrun 诊断）
};

#endif // ENABLE_V4L2
#endif // V4L2CAPTURE_H
