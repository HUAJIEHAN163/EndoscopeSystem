#ifndef PROCESSTHREAD_H
#define PROCESSTHREAD_H

#include <QThread>
#include <QDebug>
#include <QElapsedTimer>
#include <atomic>
#include <opencv2/imgproc.hpp>
#include "utils/framequeue.h"
#include "processing/imageprocessor.h"

// 图像处理线程
// 从 captureQueue 取 BGR Mat → 执行算法 → resize → 写入 displayQueue
// 主线程从 displayQueue 取 BGR Mat，转 QImage 显示
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
        int frameCount = 0;
        QElapsedTimer totalTimer;
        totalTimer.start();

        while (!m_quit) {
            if (!m_running) {
                QThread::msleep(10);
                continue;
            }

            cv::Mat frame;
            if (!m_captureQueue->pop(frame)) {
                QThread::msleep(1);
                continue;
            }

            QElapsedTimer t; t.start();

            bool anyEnabled = config.clahe || config.undistort ||
                              config.dehaze || config.sharpen ||
                              config.denoise || config.edgeDetect ||
                              config.threshold;

            cv::Mat result;
            if (!anyEnabled) {
                // P7.1: 不开算法时直接 cv::resize（全程 BGR，无格式转换）
                cv::resize(frame, result, cv::Size(displayWidth, displayHeight),
                           0, 0, cv::INTER_LINEAR);
                qint64 resizeTime = t.elapsed();

                frameCount++;
                if (frameCount % 100 == 0) {
                    qDebug() << QString("[PROC 无算法] 帧%1 resize:%2ms")
                                .arg(frameCount).arg(resizeTime);
                }
            } else {
                cv::Mat dst;
                ImageProcessor::process(frame, dst, config,
                                        undistortMap1, undistortMap2);
                qint64 t2 = t.restart();  // 算法处理

                cv::resize(dst, result, cv::Size(displayWidth, displayHeight),
                           0, 0, cv::INTER_LINEAR);
                qint64 t3 = t.elapsed();  // resize

                frameCount++;
                if (frameCount % 50 == 0) {
                    qDebug() << QString("[PROC 帧%1] %2 算法:%3ms resize:%4ms 合计:%5ms")
                                .arg(frameCount)
                                .arg([this]()                                    
                                {   QStringList algs;
                                    if (config.clahe) algs << "CLAHE";
                                    if (config.undistort) algs << "Undistort";
                                    if (config.dehaze) algs << "Dehaze";
                                    if (config.sharpen) algs << "Sharpen";
                                    if (config.denoise) algs << "Denoise";
                                    if (config.edgeDetect) algs << "EdgeDetect";
                                    if (config.threshold) algs << "Threshold";
                                    return algs.join("+");
                                }())
                                .arg(t2).arg(t3)
                                .arg(t2+t3);
                }
            }

            m_displayQueue->push(std::move(result));  // P16: 移动语义，零拷贝

            // 每 200 帧输出总体统计
            if (frameCount % 200 == 0) {
                double elapsed = totalTimer.elapsed() / 1000.0;
                qDebug() << QString("[PROC 统计] %1帧/%2秒 = %3fps 队列剩余:%4")
                            .arg(frameCount)
                            .arg(elapsed, 0, 'f', 1)
                            .arg(frameCount / elapsed, 0, 'f', 1)
                            .arg(m_captureQueue->size());
            }
        }
    }

private:
    FrameQueue<4> *m_captureQueue;   // 输入队列（采集线程写入）
    FrameQueue<4> *m_displayQueue;   // 输出队列（主线程读取）
    std::atomic<bool> m_running;
    std::atomic<bool> m_quit;
};

#endif // PROCESSTHREAD_H
