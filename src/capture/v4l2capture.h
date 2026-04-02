#ifndef V4L2CAPTURE_H
#define V4L2CAPTURE_H

#ifdef ENABLE_V4L2

#include "capture/videosource.h"
#include <string>
#include <vector>
#include <linux/videodev2.h>

struct FrameBuffer {
    void *start;
    size_t length;
};

// V4L2 摄像头采集
// 借鉴 cap-v4l2 的健壮初始化流程 + select 等待机制
// 借鉴 VideoMonitor 的 QThread 封装 + 信号槽传递
class V4l2Capture : public VideoSource {
    Q_OBJECT
public:
    V4l2Capture(const std::string &device = "/dev/video0",
                int width = 640, int height = 480,
                int fps = 30, int bufferCount = 4,
                QObject *parent = nullptr);
    ~V4l2Capture();

    bool open() override;
    void close() override;
    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }

    // V4L2 Controls — 内窥镜硬件参数调节
    bool setExposure(int value);
    bool setAutoExposure(bool enable);
    bool setGain(int value);
    bool setAutoGain(bool enable);
    bool setContrast(int value);
    bool setSaturation(int value);
    bool setHue(int value);
    bool setHFlip(bool enable);
    bool setVFlip(bool enable);
    bool setAutoWhiteBalance(bool enable);
    bool setRedBalance(int value);
    bool setBlueBalance(int value);

protected:
    void run() override;

private:
    // V4L2 初始化流程
    bool queryCapability();
    bool enumFormats();
    bool tryAndSetFormat();
    bool setFrameRate();
    bool requestBuffers();
    bool startStreaming();
    bool stopStreaming();
    void releaseBuffers();

    // 帧采集
    bool grabFrame(void **data, int *size);

    // ioctl 封装 (处理 EINTR)
    static int xioctl(int fd, int request, void *arg);

    // V4L2 control 辅助
    bool setControl(uint32_t id, int value);
    int getControl(uint32_t id);

    std::string m_device;
    int m_fd;
    int m_width;
    int m_height;
    int m_fps;
    int m_bufferCount;
    uint32_t m_pixelFormat; // 实际使用的像素格式
    std::vector<FrameBuffer> m_buffers;
};

#endif // ENABLE_V4L2
#endif // V4L2CAPTURE_H
