#include "ui/mainwindow.h"
#include "capture/filecapture.h"
#include "utils/imageconvert.h"

#ifdef ENABLE_V4L2
#include "capture/v4l2capture.h"
#endif

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QKeyEvent>
#include <QDir>
#include <QTimer>
#include <QSettings>
#include <QDebug>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_source(nullptr),
      m_frozen(false), m_rotateAngle(0),
      m_recording(false), m_frameCount(0),
      m_undistortReady(false)
{
    setWindowTitle("内窥镜图像处理系统");
    setupUi();
    setupVideoSource();

    // FPS 计时
    m_fpsTimer.start();
    QTimer *fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &MainWindow::updateFps);
    fpsTimer->start(1000);

    QDir().mkpath("./captures");
    QDir().mkpath("./videos");

    // 加载畸变校正标定文件
    cv::FileStorage fs("camera_calib.yml", cv::FileStorage::READ);
    if (fs.isOpened()) {
        cv::Mat cameraMatrix, distCoeffs;
        fs["camera_matrix"] >> cameraMatrix;
        fs["dist_coeffs"] >> distCoeffs;
        fs.release();
        ImageProcessor::initUndistortMap(cameraMatrix, distCoeffs,
                                         cv::Size(640, 480),
                                         m_undistortMap1, m_undistortMap2);
        m_undistortReady = true;
        qDebug() << "镜头标定数据加载成功";
    }
}

MainWindow::~MainWindow() {
    if (m_source) {
        m_source->stop();
        m_source->wait();
    }
    if (m_videoWriter.isOpened())
        m_videoWriter.release();
}

void MainWindow::setupUi() {
    setFixedSize(960, 560);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // 左侧: 视频显示 (paintEvent 绘制)
    QWidget *videoArea = new QWidget;
    videoArea->setMinimumSize(640, 480);
    videoArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(videoArea, 1);
    m_videoRect = QRect(4, 4, 640, 480);

    // 右侧: 控制面板
    QVBoxLayout *ctrlLayout = new QVBoxLayout;
    ctrlLayout->setSpacing(4);

    // -- 内窥镜算法 --
    QGroupBox *endoGroup = new QGroupBox("内窥镜算法");
    QVBoxLayout *endoLayout = new QVBoxLayout(endoGroup);
    endoLayout->setSpacing(2);

    m_chkWhiteBalance = new QCheckBox("白平衡校正");
    m_chkClahe = new QCheckBox("CLAHE增强");
    m_chkUndistort = new QCheckBox("畸变校正");
    m_chkDehaze = new QCheckBox("去雾");

    endoLayout->addWidget(m_chkWhiteBalance);
    endoLayout->addWidget(m_chkClahe);

    // CLAHE 参数滑块
    m_sliderClaheClip = new QSlider(Qt::Horizontal);
    m_sliderClaheClip->setRange(10, 80);  // 1.0 ~ 8.0
    m_sliderClaheClip->setValue(30);
    endoLayout->addWidget(m_sliderClaheClip);

    endoLayout->addWidget(m_chkUndistort);
    endoLayout->addWidget(m_chkDehaze);
    ctrlLayout->addWidget(endoGroup);

    // -- 通用算法 --
    QGroupBox *generalGroup = new QGroupBox("通用算法");
    QVBoxLayout *generalLayout = new QVBoxLayout(generalGroup);
    generalLayout->setSpacing(2);

    m_chkSharpen = new QCheckBox("锐化");
    m_sliderSharpen = new QSlider(Qt::Horizontal);
    m_sliderSharpen->setRange(5, 30);  // 0.5 ~ 3.0
    m_sliderSharpen->setValue(15);

    m_chkDenoise = new QCheckBox("降噪");
    m_chkEdgeDetect = new QCheckBox("边缘检测");
    m_chkThreshold = new QCheckBox("阈值分割");
    m_sliderThreshold = new QSlider(Qt::Horizontal);
    m_sliderThreshold->setRange(0, 255);
    m_sliderThreshold->setValue(128);

    generalLayout->addWidget(m_chkSharpen);
    generalLayout->addWidget(m_sliderSharpen);
    generalLayout->addWidget(m_chkDenoise);
    generalLayout->addWidget(m_chkEdgeDetect);
    generalLayout->addWidget(m_chkThreshold);
    generalLayout->addWidget(m_sliderThreshold);
    ctrlLayout->addWidget(generalGroup);

    // -- 操作 --
    QGroupBox *opGroup = new QGroupBox("操作");
    QVBoxLayout *opLayout = new QVBoxLayout(opGroup);
    opLayout->setSpacing(2);

    m_btnCapture = new QPushButton("拍照 (C)");
    m_btnRecord = new QPushButton("录像 (R)");
    m_btnFreeze = new QPushButton("冻结 (Space)");
    m_btnRotate = new QPushButton("旋转 (T)");

    connect(m_btnCapture, &QPushButton::clicked, this, &MainWindow::onCapturePhoto);
    connect(m_btnRecord, &QPushButton::clicked, this, &MainWindow::onToggleRecord);
    connect(m_btnFreeze, &QPushButton::clicked, this, &MainWindow::onToggleFreeze);
    connect(m_btnRotate, &QPushButton::clicked, this, &MainWindow::onRotateImage);

    opLayout->addWidget(m_btnCapture);
    opLayout->addWidget(m_btnRecord);
    opLayout->addWidget(m_btnFreeze);
    opLayout->addWidget(m_btnRotate);
    ctrlLayout->addWidget(opGroup);

    ctrlLayout->addStretch();

    // -- 状态 --
    m_lblFps = new QLabel("FPS: --");
    m_lblStatus = new QLabel("就绪");
    ctrlLayout->addWidget(m_lblFps);
    ctrlLayout->addWidget(m_lblStatus);

    mainLayout->addLayout(ctrlLayout);

    // 畸变校正按钮状态
    m_chkUndistort->setEnabled(m_undistortReady);
    if (!m_undistortReady)
        m_chkUndistort->setToolTip("未找到 camera_calib.yml 标定文件");
}

void MainWindow::setupVideoSource() {
    // 优先尝试 V4L2，失败则降级为文件模式
#ifdef ENABLE_V4L2
    // 读取配置
    QSettings settings("config/endoscope.conf", QSettings::IniFormat);
    QString device = settings.value("camera/device", "/dev/video0").toString();
    int width = settings.value("camera/width", 640).toInt();
    int height = settings.value("camera/height", 480).toInt();
    int fps = settings.value("camera/fps", 30).toInt();
    int buffers = settings.value("camera/buffers", 4).toInt();

    if (QFileInfo(device).exists()) {
        auto *v4l2 = new V4l2Capture(device.toStdString(), width, height,
                                     fps, buffers, this);
        if (v4l2->open()) {
            m_source = v4l2;
            m_lblStatus->setText(QString("V4L2: %1").arg(device));
            qDebug() << "使用 V4L2 采集:" << device;
        } else {
            delete v4l2;
            qDebug() << "V4L2 打开失败，降级为文件模式";
        }
    }
#endif

    // 降级: 文件模式
    if (!m_source) {
        QSettings settings("config/endoscope.conf", QSettings::IniFormat);
        QString testFile = settings.value("file/path", "test_data/test.mp4").toString();

        if (QFileInfo(testFile).exists()) {
            m_source = new FileCapture(testFile.toStdString(), 30, this);
            if (!m_source->open()) {
                m_lblStatus->setText("错误: 无法打开测试视频");
                return;
            }
            m_lblStatus->setText(QString("文件模式: %1").arg(testFile));
        } else {
            m_lblStatus->setText("错误: 无视频源 (无摄像头且无测试视频)");
            return;
        }
    }

    connect(m_source, &VideoSource::frameReady,
            this, &MainWindow::onFrameReady, Qt::QueuedConnection);
    connect(m_source, &VideoSource::errorOccurred, [this](const QString &msg) {
        m_lblStatus->setText("错误: " + msg);
    });

    m_source->start();
}

void MainWindow::onFrameReady(const QImage &image) {
    if (m_frozen) return;
    m_frameCount++;

    QImage processed = processFrame(image);

    // 旋转
    if (m_rotateAngle != 0) {
        QTransform transform;
        transform.rotate(m_rotateAngle);
        processed = processed.transformed(transform, Qt::SmoothTransformation);
    }

    // 录像
    if (m_recording && m_videoWriter.isOpened()) {
        cv::Mat mat = ImageConvert::qimageToMat(processed);
        if (!mat.empty())
            m_videoWriter.write(mat);
    }

    // 缩放显示
    m_displayImage = processed.scaled(m_videoRect.size(),
                                      Qt::KeepAspectRatio,
                                      Qt::FastTransformation);
    update();
}

QImage MainWindow::processFrame(const QImage &input) {
    // 更新算法配置
    m_procConfig.whiteBalance = m_chkWhiteBalance->isChecked();
    m_procConfig.clahe = m_chkClahe->isChecked();
    m_procConfig.claheClipLimit = m_sliderClaheClip->value() / 10.0;
    m_procConfig.undistort = m_chkUndistort->isChecked() && m_undistortReady;
    m_procConfig.dehaze = m_chkDehaze->isChecked();
    m_procConfig.sharpen = m_chkSharpen->isChecked();
    m_procConfig.sharpenAmount = m_sliderSharpen->value() / 10.0;
    m_procConfig.denoise = m_chkDenoise->isChecked();
    m_procConfig.edgeDetect = m_chkEdgeDetect->isChecked();
    m_procConfig.threshold = m_chkThreshold->isChecked();
    m_procConfig.thresholdValue = m_sliderThreshold->value();

    // 如果没有任何算法启用，直接返回
    bool anyEnabled = m_procConfig.whiteBalance || m_procConfig.clahe ||
                      m_procConfig.undistort || m_procConfig.dehaze ||
                      m_procConfig.sharpen || m_procConfig.denoise ||
                      m_procConfig.edgeDetect || m_procConfig.threshold;
    if (!anyEnabled) return input;

    cv::Mat src = ImageConvert::qimageToMat(input);
    if (src.empty()) return input;

    cv::Mat dst;
    ImageProcessor::process(src, dst, m_procConfig,
                            m_undistortMap1, m_undistortMap2);

    return ImageConvert::matToQImage(dst);
}

void MainWindow::paintEvent(QPaintEvent *event) {
    QMainWindow::paintEvent(event);
    if (m_displayImage.isNull()) return;

    QPainter painter(this);

    // 居中绘制
    int x = m_videoRect.x() + (m_videoRect.width() - m_displayImage.width()) / 2;
    int y = m_videoRect.y() + (m_videoRect.height() - m_displayImage.height()) / 2;
    painter.drawImage(x, y, m_displayImage);

    // OSD: 时间戳
    painter.setPen(Qt::yellow);
    painter.setFont(QFont("Monospace", 9));
    painter.drawText(x + 8, y + 16,
                     QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    // OSD: 录像指示
    if (m_recording) {
        painter.setPen(Qt::red);
        painter.setBrush(Qt::red);
        painter.drawEllipse(x + 8, y + 24, 8, 8);
        painter.drawText(x + 20, y + 32, "REC");
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Escape: close(); break;
    case Qt::Key_Space:  onToggleFreeze(); break;
    case Qt::Key_C:      onCapturePhoto(); break;
    case Qt::Key_R:      onToggleRecord(); break;
    case Qt::Key_T:      onRotateImage(); break;
    default: QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::onCapturePhoto() {
    if (m_displayImage.isNull()) return;
    QString filename = QString("./captures/%1.jpg")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    m_displayImage.save(filename, "JPEG", 95);
    m_lblStatus->setText("拍照: " + filename);
}

void MainWindow::onToggleRecord() {
    if (!m_recording) {
        int w = m_source ? m_source->getWidth() : 640;
        int h = m_source ? m_source->getHeight() : 480;
        QString filename = QString("./videos/%1.avi")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        m_videoWriter.open(filename.toStdString(),
                           cv::VideoWriter::fourcc('M','J','P','G'),
                           25, cv::Size(w, h));
        if (m_videoWriter.isOpened()) {
            m_recording = true;
            m_btnRecord->setText("停止录像 (R)");
            m_lblStatus->setText("录像中...");
        }
    } else {
        m_recording = false;
        m_videoWriter.release();
        m_btnRecord->setText("录像 (R)");
        m_lblStatus->setText("录像已保存");
    }
}

void MainWindow::onToggleFreeze() {
    m_frozen = !m_frozen;
    m_btnFreeze->setText(m_frozen ? "继续 (Space)" : "冻结 (Space)");
    m_lblStatus->setText(m_frozen ? "画面已冻结" : "运行中");
}

void MainWindow::onRotateImage() {
    m_rotateAngle = (m_rotateAngle + 90) % 360;
    m_lblStatus->setText(QString("旋转: %1°").arg(m_rotateAngle));
}

void MainWindow::updateFps() {
    double elapsed = m_fpsTimer.elapsed() / 1000.0;
    if (elapsed > 0) {
        double fps = m_frameCount / elapsed;
        m_lblFps->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
    }
    m_frameCount = 0;
    m_fpsTimer.restart();
}
