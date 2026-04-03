#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QCheckBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QElapsedTimer>
#include <QStackedWidget>
#include <QComboBox>
#include <opencv2/videoio.hpp>
#include <deque>

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
    void onSwitchMode();          // 切换调试/临床模式
    void onPresetSelected(int index);  // 选择预设
    void onExportPreset();        // 导出当前参数为预设

private:
    void setupUi();
    void setupVideoSource();
    void loadPresets();           // 加载预设文件
    void applyConfig(const ImageProcessor::Config &cfg);  // 将 Config 同步到界面控件
    QImage processFrame(const QImage &input);

    // 模式: true=临床模式, false=调试模式
    bool m_clinicalMode;

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

    // 模式切换
    QPushButton *m_btnSwitchMode;     // 调试/临床 切换按钮
    QStackedWidget *m_ctrlStack;      // 右侧面板切换容器
    QWidget *m_debugPanel;            // 调试模式面板（现有控件）
    QWidget *m_clinicalPanel;         // 临床模式面板（预设选择）
    QComboBox *m_cboPreset;           // 预设下拉框
    QPushButton *m_btnExportPreset;   // 导出预设按钮

    // 预设数据
    QStringList m_presetNames;
    QList<ImageProcessor::Config> m_presetConfigs;

    // 状态
    QLabel *m_lblStatus;
    QLabel *m_lblFps;

    // 状态标志
    bool m_frozen;
    int m_rotateAngle;
    bool m_recording;
    cv::VideoWriter m_videoWriter;

    // 帧缓存（用于冻结时选最清晰帧）
    static const int FRAME_BUFFER_SIZE = 8;  // 缓存最近 8 帧
    std::deque<QImage> m_frameBuffer;

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
