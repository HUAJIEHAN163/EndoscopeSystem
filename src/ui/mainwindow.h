#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QCheckBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QElapsedTimer>
#include <opencv2/videoio.hpp>

#include "capture/videosource.h"
#include "processing/imageprocessor.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onFrameReady(const QImage &image);
    void onCapturePhoto();
    void onToggleRecord();
    void onToggleFreeze();
    void onRotateImage();
    void updateFps();

private:
    void setupUi();
    void setupVideoSource();
    QImage processFrame(const QImage &input);

    // 视频源 (V4L2 或文件)
    VideoSource *m_source;

    // 显示
    QImage m_displayImage;
    QRect m_videoRect;

    // 图像处理开关
    QCheckBox *m_chkWhiteBalance;
    QCheckBox *m_chkClahe;
    QCheckBox *m_chkUndistort;
    QCheckBox *m_chkDehaze;
    QCheckBox *m_chkSharpen;
    QCheckBox *m_chkDenoise;
    QCheckBox *m_chkEdgeDetect;
    QCheckBox *m_chkThreshold;

    // 参数滑块
    QSlider *m_sliderClaheClip;
    QSlider *m_sliderSharpen;
    QSlider *m_sliderThreshold;

    // 操作按钮
    QPushButton *m_btnCapture;
    QPushButton *m_btnRecord;
    QPushButton *m_btnFreeze;
    QPushButton *m_btnRotate;

    // 状态
    QLabel *m_lblStatus;
    QLabel *m_lblFps;

    // 状态标志
    bool m_frozen;
    int m_rotateAngle;
    bool m_recording;
    cv::VideoWriter m_videoWriter;

    // FPS
    int m_frameCount;
    QElapsedTimer m_fpsTimer;

    // 畸变校正 (标定后加载)
    cv::Mat m_undistortMap1;
    cv::Mat m_undistortMap2;
    bool m_undistortReady;

    // 算法配置
    ImageProcessor::Config m_procConfig;
};

#endif // MAINWINDOW_H
