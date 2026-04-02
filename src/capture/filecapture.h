#ifndef FILECAPTURE_H
#define FILECAPTURE_H

#include "capture/videosource.h"
#include <opencv2/videoio.hpp>
#include <string>

// 从视频文件或图片序列读取帧
// 虚拟机开发时替代真实摄像头
class FileCapture : public VideoSource {
    Q_OBJECT
public:
    // filePath: 视频文件路径，或图片目录路径
    // fps: 模拟帧率，控制读取速度
    explicit FileCapture(const std::string &filePath, int fps = 30,
                         QObject *parent = nullptr);
    ~FileCapture();

    bool open() override;
    void close() override;
    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }

protected:
    void run() override;

private:
    std::string m_filePath;
    int m_fps;
    int m_width;
    int m_height;
    cv::VideoCapture m_cap;
};

#endif // FILECAPTURE_H
