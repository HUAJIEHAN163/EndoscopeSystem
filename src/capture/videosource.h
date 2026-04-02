#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include <QThread>
#include <QImage>
#include <atomic>

// 视频源抽象接口
// V4l2Capture 和 FileCapture 都继承此类
// 通过 frameReady 信号向外传递帧数据
class VideoSource : public QThread {
    Q_OBJECT
public:
    explicit VideoSource(QObject *parent = nullptr) : QThread(parent), m_running(false) {}
    virtual ~VideoSource() {}

    virtual bool open() = 0;
    virtual void close() = 0;

    void stop() { m_running = false; }
    bool isRunning() const { return m_running; }

    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;

signals:
    void frameReady(const QImage &image);
    void errorOccurred(const QString &msg);

protected:
    std::atomic<bool> m_running;
};

#endif // VIDEOSOURCE_H
