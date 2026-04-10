#include "capture/filecapture.h"
#include "utils/imageconvert.h"
#include <QDebug>
#include <QThread>
#include <opencv2/imgproc.hpp>

FileCapture::FileCapture(const std::string &filePath, int fps, QObject *parent)
    : VideoSource(parent), m_filePath(filePath), m_fps(fps),
      m_width(0), m_height(0) {}

FileCapture::~FileCapture() {
    stop();
    wait();
    close();
}

bool FileCapture::open() {
    m_cap.open(m_filePath);
    if (!m_cap.isOpened()) {
        emit errorOccurred(QString("无法打开视频文件: %1").arg(m_filePath.c_str()));
        return false;
    }
    m_width = static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_WIDTH));
    m_height = static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    qDebug() << "FileCapture opened:" << m_filePath.c_str()
             << m_width << "x" << m_height;
    return true;
}

void FileCapture::close() {
    if (m_cap.isOpened())
        m_cap.release();
}

void FileCapture::run() {
    m_running = true;
    int delayMs = 1000 / m_fps;

    while (!m_quit) {
        // 暂停时不处理帧
        if (!m_running) {
            QThread::msleep(50);
            continue;
        }

        cv::Mat frame;
        if (!m_cap.read(frame)) {
            m_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            if (!m_cap.read(frame)) {
                emit errorOccurred("读取视频帧失败");
                break;
            }
        }

        QImage image = ImageConvert::matToQImage(frame);
        if (m_pendingFrames.load() < 2) {
            m_pendingFrames.fetch_add(1);
            emit frameReady(image);
        }

        QThread::msleep(delayMs);
    }
}
