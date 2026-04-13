#ifndef PROCESSTHREAD_H
#define PROCESSTHREAD_H

#include <QThread>
#include <QImage>
#include <atomic>
#include "utils/framequeue.h"
#include "utils/imageconvert.h"
#include "processing/imageprocessor.h"

// 图像处理线程
// 从 captureQueue 取帧 → 执行算法 → 写入 displayQueue
// 主线程从 displayQueue 取最新帧显示，不再被算法阻塞
class ProcessThread : public QThread {
    Q_OBJECT
public:
    explicit ProcessThread(FrameQueue<4> *captureQueue,
                           FrameQueue<4> *displayQueue,
                           QObject *parent = nullptr)
        : QThread(parent),
          m_captureQueue(captureQueue),
          m_displayQueue(displayQueue),
          m_running(false),
          m_quit(false) {}

    void pause() { m_running = false; }
    void resume() { m_running = true; }
    void stop() { m_quit = true; m_running = false; }

    // 算法配置（主线程通过 UI 控件更新，处理线程读取）
    ImageProcessor::Config config;

    // 畸变校正映射表（只读，主线程初始化后不再修改）
    cv::Mat undistortMap1, undistortMap2;

    // 显示区域尺寸（用于 resize 到显示大小）
    int displayWidth = 640;
    int displayHeight = 480;

protected:
    void run() override {
        m_running = true;

        while (!m_quit) {
            if (!m_running) {
                QThread::msleep(10);
                continue;
            }

            QImage frame;
            if (!m_captureQueue->pop(frame)) {
                QThread::msleep(1);  // 队列空，短暂等待
                continue;
            }

            // 检查是否有算法启用
            bool anyEnabled = config.clahe || config.undistort ||
                              config.dehaze || config.sharpen ||
                              config.denoise || config.edgeDetect ||
                              config.threshold;

            QImage result;
            if (!anyEnabled) {
                // 不开算法，直接缩放到显示尺寸
                result = frame.scaled(displayWidth, displayHeight,
                                      Qt::KeepAspectRatio, Qt::FastTransformation);
            } else {
                // 执行算法处理
                cv::Mat src = ImageConvert::qimageToMat(frame);
                if (src.empty()) continue;

                cv::Mat dst;
                ImageProcessor::process(src, dst, config,
                                        undistortMap1, undistortMap2);

                // resize 到显示尺寸
                cv::Mat display;
                cv::resize(dst, display, cv::Size(displayWidth, displayHeight),
                           0, 0, cv::INTER_LINEAR);
                result = ImageConvert::matToQImage(display);
            }

            m_displayQueue->push(result);
        }
    }

private:
    FrameQueue<4> *m_captureQueue;   // 输入队列（采集线程写入）
    FrameQueue<4> *m_displayQueue;   // 输出队列（主线程读取）
    std::atomic<bool> m_running;
    std::atomic<bool> m_quit;
};

#endif // PROCESSTHREAD_H
