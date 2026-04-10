// === 项目内部头文件 ===
#include "ui/mainwindow.h"      // 主窗口类声明
#include "ui/presetmanager.h"   // 预设管理器
#include "capture/filecapture.h" // 文件视频源（虚拟机测试用）
#include "utils/imageconvert.h"  // QImage ↔ cv::Mat 转换工具

// 条件编译：只有在 Linux 且开启 V4L2 时才引入摄像头采集
#ifdef ENABLE_V4L2
#include "capture/v4l2capture.h"
#endif

// === Qt 头文件 ===
#include <QPainter>     // 绘图（在窗口上画视频帧和 OSD）
#include <QVBoxLayout>  // 垂直布局管理器
#include <QHBoxLayout>  // 水平布局管理器
#include <QGroupBox>    // 分组框（界面上的"内窥镜算法""通用算法"等框）
#include <QDateTime>    // 日期时间（拍照/录像文件名、OSD 时间戳）
#include <QKeyEvent>    // 键盘事件（快捷键）
#include <QDir>         // 目录操作（创建 captures/videos 文件夹）
#include <QTimer>       // 定时器（每秒刷新 FPS）
#include <QSettings>    // 配置文件读取（endoscope.conf）
#include <QDebug>       // 调试输出（qDebug）
#include <QFileInfo>    // 文件信息（判断设备/视频文件是否存在）
#include <QInputDialog> // 输入对话框（导出预设时输入名称）

// ===================== 构造函数 =====================
// 程序启动时 main.cpp 中 MainWindow window; 会调用这里
// 初始化列表：先初始化所有成员变量的默认值
MainWindow::MainWindow(QWidget *parent)         //parent = nullptr 表示这个窗口没有父窗口，它就是顶层窗口。
    : QMainWindow(parent), m_source(nullptr),   // 视频源指针，初始为空
      m_frozen(false), m_rotateAngle(0),         // 画面未冻结，旋转角度 0
      m_recording(false), m_frameCount(0),       // 未在录像，帧计数归零
      m_undistortReady(false),                    // 畸变校正未就绪
      m_clinicalMode(false)                       // 默认调试模式
{
    setWindowTitle("内窥镜图像处理系统");
    setupUi();            // 创建界面控件（按钮、滑块、复选框等）
    loadPresets();        // 加载预设文件
    setupVideoSource();   // 初始化视频源（V4L2 或文件）

    // 创建定时器，每 1000ms（1秒）触发一次 updateFps 刷新帧率显示
    m_fpsTimer.start();
    QTimer *fpsTimer = new QTimer(this);  // this 作为父对象，窗口销毁时自动释放
    connect(fpsTimer, &QTimer::timeout, this, &MainWindow::updateFps);  // 信号槽连接
    fpsTimer->start(1000);

    // 创建拍照和录像的保存目录（已存在则不重复创建）
    QDir().mkpath("./captures");
    QDir().mkpath("./videos");

    // 尝试加载相机标定文件（用于镜头畸变校正）
    // 没有这个文件不影响运行，只是畸变校正功能不可用
    cv::FileStorage fs("camera_calib.yml", cv::FileStorage::READ);
    if (fs.isOpened()) {
        cv::Mat cameraMatrix, distCoeffs;          // 相机内参矩阵和畸变系数
        fs["camera_matrix"] >> cameraMatrix;
        fs["dist_coeffs"] >> distCoeffs;
        fs.release();
        // 预计算畸变校正映射表（只算一次，后续每帧直接查表，提高效率）
        ImageProcessor::initUndistortMap(cameraMatrix, distCoeffs,
                                         cv::Size(640, 480),
                                         m_undistortMap1, m_undistortMap2);
        m_undistortReady = true;
        qDebug() << "镜头标定数据加载成功";
    }

    // CLAHE 预热：首次调用 createCLAHE 耗时 ~3s，启动时做一次避免运行时卡顿
    {
        cv::Mat dummy = cv::Mat::zeros(8, 8, CV_8UC3);
        cv::Mat out;
        ImageProcessor::applyCLAHE(dummy, out);
        qDebug() << "CLAHE 预热完成";
    }
}

// ===================== 析构函数 =====================
// 窗口关闭时自动调用，负责释放资源
MainWindow::~MainWindow() {
    if (m_source) {
        m_source->stop();   // 通知采集线程停止
        m_source->wait();   // 等待线程真正结束，防止野指针
    }
    if (m_videoWriter.isOpened())
        m_videoWriter.release();  // 关闭录像文件，确保数据写完
}

// ===================== 界面搭建 =====================
// 整体布局：左侧视频画面 + 右侧控制面板（可切换调试/临床模式）
// ┌──────────────────┬────────────┐
// │                  │ [切换模式]  │
// │   视频显示区域    │ 调试面板    │
// │   640 x 480      │  或        │
// │                  │ 临床面板    │
// └──────────────────┴────────────┘
void MainWindow::setupUi() {
    // 固定窗口大小 960x560，不允许用户拖拽缩放
    // 窗口大小适配：开发板屏幕 800x480，虚拟机用更大尺寸
#ifdef __arm__
    setFixedSize(800, 480);
#else
    setFixedSize(960, 660);
#endif

    // QMainWindow 要求必须有一个 centralWidget 作为内容容器
    // central 本身不可见（透明），所有控件都放在它上面
    // parent 设为 this（MainWindow），窗口销毁时自动释放 central
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    // 根布局：垂直排列（上方内容区 + 下方按钮区）
    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(2, 2, 2, 2);
    rootLayout->setSpacing(2);

    // 上方：水平排列（视频 + 参数面板）
    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    // ==============================================================
    // 左侧: 视频显示区域
    // 这个 QWidget 只是占位用，实际画面在 paintEvent 中绘制
    // addWidget 会自动把 central 设为 videoArea 的 parent
    // ==============================================================
    QWidget *videoArea = new QWidget;
#ifdef __arm__
    videoArea->setMinimumSize(480, 360);
#else
    videoArea->setMinimumSize(640, 480);
#endif
    videoArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);  // 允许拉伸
    videoArea->setStyleSheet("");  // 不设样式，背景在 paintEvent 中绘制
    mainLayout->addWidget(videoArea, 1);  // 拉伸因子 1，优先占据剩余空间
#ifdef __arm__
    m_videoRect = QRect(4, 4, 480, 360);
#else
    m_videoRect = QRect(4, 4, 640, 480);
#endif

    // ==============================================================
    // 右侧: 控制面板（垂直排列）
    // 从上到下：模式切换按钮 → 调试/临床面板 → 操作按钮 → 状态信息
    // ==============================================================
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->setSpacing(4);

    // --- 模式切换按钮（最顶部）---
    m_btnSwitchMode = new QPushButton("切换到临床模式");
    // 点击按钮 → 触发 onSwitchMode 槽函数
    connect(m_btnSwitchMode, &QPushButton::clicked, this, &MainWindow::onSwitchMode);
    rightLayout->addWidget(m_btnSwitchMode);

    // --- QStackedWidget: 面板切换容器 ---
    // 两个面板叠放在同一位置，同一时间只显示一个
    // index 0 = 调试面板，index 1 = 临床面板
    m_ctrlStack = new QStackedWidget;

    // ==============================================================
    // 调试面板（index 0）— 工程师用，所有参数可调
    // 父子关系：m_debugPanel → debugLayout → endoGroup/generalGroup → 各控件
    // ==============================================================
    m_debugPanel = new QWidget;
    QVBoxLayout *debugLayout = new QVBoxLayout(m_debugPanel);  // 布局绑定到 m_debugPanel
    debugLayout->setSpacing(4);
    debugLayout->setContentsMargins(0, 0, 0, 0);  // 无额外边距

    // -- 内窥镜算法分组框 --
    // QGroupBox 显示为带标题的边框，视觉上把相关控件归为一组
    QGroupBox *endoGroup = new QGroupBox("内窥镜算法");
    QVBoxLayout *endoLayout = new QVBoxLayout(endoGroup);  // endoGroup 内部垂直排列
    endoLayout->setSpacing(2);

    // 四个复选框：勾选后对应算法在 processFrame 中生效
    m_chkWhiteBalance = new QCheckBox("白平衡校正");
    m_chkClahe = new QCheckBox("CLAHE增强");
    m_chkUndistort = new QCheckBox("畸变校正");
    m_chkDehaze = new QCheckBox("去雾");

    // addWidget 把控件加入布局，同时自动设置 parent 为 endoGroup
    endoLayout->addWidget(m_chkWhiteBalance);
    endoLayout->addWidget(m_chkClahe);

    // CLAHE 参数滑块：整数 10~80，processFrame 中除以 10 得到实际值 1.0~8.0
    m_sliderClaheClip = new QSlider(Qt::Horizontal);  // 水平滑块
    m_sliderClaheClip->setRange(10, 80);
    m_sliderClaheClip->setValue(30);  // 默认 3.0
    endoLayout->addWidget(m_sliderClaheClip);

    endoLayout->addWidget(m_chkUndistort);
    endoLayout->addWidget(m_chkDehaze);
    debugLayout->addWidget(endoGroup);  // 把整个分组框加入调试面板布局

    // -- 通用算法分组框 --
    QGroupBox *generalGroup = new QGroupBox("通用算法");
    QVBoxLayout *generalLayout = new QVBoxLayout(generalGroup);
    generalLayout->setSpacing(2);

    m_chkSharpen = new QCheckBox("锐化");
    // 锐化强度滑块：整数 5~30 → 实际值 0.5~3.0
    m_sliderSharpen = new QSlider(Qt::Horizontal);
    m_sliderSharpen->setRange(5, 30);
    m_sliderSharpen->setValue(15);  // 默认 1.5

    m_chkDenoise = new QCheckBox("降噪");
    m_chkEdgeDetect = new QCheckBox("边缘检测");
    m_chkThreshold = new QCheckBox("阈值分割");
    // 阈值滑块：0~255，直接对应灰度阈值
    m_sliderThreshold = new QSlider(Qt::Horizontal);
    m_sliderThreshold->setRange(0, 255);
    m_sliderThreshold->setValue(128);  // 默认中间值

    generalLayout->addWidget(m_chkSharpen);
    generalLayout->addWidget(m_sliderSharpen);
    generalLayout->addWidget(m_chkDenoise);
    generalLayout->addWidget(m_chkEdgeDetect);
    generalLayout->addWidget(m_chkThreshold);
    generalLayout->addWidget(m_sliderThreshold);
    debugLayout->addWidget(generalGroup);

    // 导出预设按钮：把当前调好的参数保存为 JSON 文件
    m_btnExportPreset = new QPushButton("导出为预设");
    connect(m_btnExportPreset, &QPushButton::clicked, this, &MainWindow::onExportPreset);
    debugLayout->addWidget(m_btnExportPreset);

    // addStretch 添加弹簧，把上面的控件推到顶部，下面留空
    debugLayout->addStretch();
    // 把调试面板加入 QStackedWidget，index = 0
    m_ctrlStack->addWidget(m_debugPanel);

    // ==============================================================
    // 临床面板（index 1）— 医生用，只有预设选择
    // ==============================================================
    m_clinicalPanel = new QWidget;
    QVBoxLayout *clinicalLayout = new QVBoxLayout(m_clinicalPanel);
    clinicalLayout->setSpacing(8);
    clinicalLayout->setContentsMargins(0, 0, 0, 0);

    // 提示文字
    QLabel *lblPreset = new QLabel("选择检查模式:");
    clinicalLayout->addWidget(lblPreset);

    // 预设下拉框：选项由 loadPresets() 填充（胃镜/肠镜/支气管镜）
    m_cboPreset = new QComboBox;
    m_cboPreset->setMinimumHeight(36);  // 加大高度，方便触屏操作
    // 下拉框选项变化 → 触发 onPresetSelected，加载对应参数
    // QOverload 用于区分同名但参数不同的信号（Qt5 的写法）
    connect(m_cboPreset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPresetSelected);
    clinicalLayout->addWidget(m_cboPreset);

    clinicalLayout->addStretch();  // 弹簧，把下拉框推到顶部
    // 把临床面板加入 QStackedWidget，index = 1
    m_ctrlStack->addWidget(m_clinicalPanel);

    // 把 QStackedWidget 加入右侧布局
    // 固定右侧控制面板宽度，防止切换模式时变窄
    m_ctrlStack->setMinimumHeight(300);
    m_ctrlStack->setFixedWidth(250);
    rightLayout->addWidget(m_ctrlStack);

    // 把右侧整体布局加入上方水平布局
    mainLayout->addLayout(rightLayout);

    // ==============================================================
    // 下方：操作按钮（两行水平排列）+ 状态信息
    // 适配开发板 800x480 小屏幕
    // ==============================================================
    m_btnCapture = new QPushButton("拍照(C)");
    m_btnRecord = new QPushButton("录像(R)");
    m_btnFreeze = new QPushButton("冻结(Space)");
    m_btnRotate = new QPushButton("旋转(T)");

    connect(m_btnCapture, &QPushButton::clicked, this, &MainWindow::onCapturePhoto);
    connect(m_btnRecord, &QPushButton::clicked, this, &MainWindow::onToggleRecord);
    connect(m_btnFreeze, &QPushButton::clicked, this, &MainWindow::onToggleFreeze);
    connect(m_btnRotate, &QPushButton::clicked, this, &MainWindow::onRotateImage);

    // 第一行：拍照 + 录像 + 冻结 + 旋转
    QHBoxLayout *btnRow1 = new QHBoxLayout;
    btnRow1->setSpacing(4);
    btnRow1->addWidget(m_btnCapture);
    btnRow1->addWidget(m_btnRecord);
    btnRow1->addWidget(m_btnFreeze);
    btnRow1->addWidget(m_btnRotate);

    // 第二行：FPS + 状态
    QHBoxLayout *btnRow2 = new QHBoxLayout;
    btnRow2->setSpacing(8);
    m_lblFps = new QLabel("FPS: --");
    m_lblStatus = new QLabel("就绪");
    btnRow2->addWidget(m_lblFps);
    btnRow2->addWidget(m_lblStatus);
    btnRow2->addStretch();

    // 组装根布局
    rootLayout->addLayout(mainLayout, 1);  // 上方内容区占据剩余空间
    rootLayout->addLayout(btnRow1);
    rootLayout->addLayout(btnRow2);

    // 如果没有标定文件，畸变校正复选框置灰，鼠标悬停显示提示
    m_chkUndistort->setEnabled(m_undistortReady);
    if (!m_undistortReady)
        m_chkUndistort->setToolTip("未找到 camera_calib.yml 标定文件");

    // 默认显示调试面板（index 0）
    m_ctrlStack->setCurrentIndex(0);
}

// ===================== 视频源初始化 =====================
// 策略：优先尝试 V4L2 摄像头（开发板），失败则降级为文件模式（虚拟机测试）
// 视频源选择流程：
//   有摄像头设备 → V4L2Capture
//   无摄像头设备 → FileCapture（读取测试视频）
//   都没有      → 显示错误
void MainWindow::setupVideoSource() {
#ifdef ENABLE_V4L2
    // 从配置文件 config/endoscope.conf 读取摄像头参数
    QSettings settings("config/endoscope.conf", QSettings::IniFormat);
    QString device = settings.value("camera/device", "/dev/video0").toString();
    int width = settings.value("camera/width", 640).toInt();
    int height = settings.value("camera/height", 480).toInt();
    int fps = settings.value("camera/fps", 30).toInt();
    int buffers = settings.value("camera/buffers", 4).toInt();

    // 检查摄像头设备节点是否存在（虚拟机上通常不存在）
    if (QFileInfo(device).exists()) {
        auto *v4l2 = new V4l2Capture(device.toStdString(), width, height,
                                     fps, buffers, this);
        if (v4l2->open()) {
            m_source = v4l2;  // 成功，使用 V4L2 采集
            m_lblStatus->setText(QString("V4L2: %1").arg(device));
            qDebug() << "使用 V4L2 采集:" << device;
        } else {
            delete v4l2;      // 打开失败，释放资源，继续尝试文件模式
            qDebug() << "V4L2 打开失败，降级为文件模式";
        }
    }
#endif

    // 降级: 文件模式（V4L2 不可用时走这里）
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

    // 连接信号槽：视频源每产出一帧 → 调用 onFrameReady 处理
    // Qt::QueuedConnection 表示跨线程投递（采集线程 → 主线程）
    connect(m_source, &VideoSource::frameReady,
            this, &MainWindow::onFrameReady, Qt::QueuedConnection);

    // 错误信号 → 显示在状态栏（lambda 写法，匿名函数）
    connect(m_source, &VideoSource::errorOccurred, [this](const QString &msg) {
        m_lblStatus->setText("错误: " + msg);
    });

    m_source->start();  // 启动采集线程，开始产出视频帧
}

// ===================== 帧到达回调 =====================
// 每当视频源产出一帧，就会通过信号槽调用这里
// 这是整个数据流的核心：采集 → 处理 → 旋转 → 录像 → 显示
void MainWindow::onFrameReady(const QImage &image) {
    if (m_frozen) {
        m_source->m_pendingFrames.fetch_sub(1);
        return;
    }
    m_frameCount++;

    // 1. 图像处理
    QImage processed = processFrame(image);

    // 2. 缓存最近 N 帧（QImage 隐式共享，赋值不拷贝数据）
    m_frameBuffer.push_back(processed);
    if (static_cast<int>(m_frameBuffer.size()) > FRAME_BUFFER_SIZE)
        m_frameBuffer.pop_front();

    // 3. 旋转
    if (m_rotateAngle != 0) {
        QTransform transform;
        transform.rotate(m_rotateAngle);
        processed = processed.transformed(transform, Qt::FastTransformation);
    }

    // 4. 录像
    if (m_recording && m_videoWriter.isOpened()) {
        cv::Mat mat = ImageConvert::qimageToMat(processed);
        if (!mat.empty())
            m_videoWriter.write(mat);
    }

    // 5. 直接显示（已在 processFrame 中缩放到显示尺寸）
    m_displayImage = processed;
    update();
    m_source->m_pendingFrames.fetch_sub(1);
}

// ===================== 帧处理 =====================
// 读取界面上所有复选框和滑块的状态，组装成 Config，交给 ImageProcessor 处理
// 数据流：QImage → cv::Mat → 算法处理 → cv::Mat → QImage
QImage MainWindow::processFrame(const QImage &input) {
    // 从界面控件读取当前算法配置
    if (!m_clinicalMode) {
        // 调试模式：从界面控件读取参数
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
    }
    // 临床模式：m_procConfig 已在 onPresetSelected 中设置好，直接使用

    // 没有任何算法启用时，跳过处理直接返回原图（省 CPU）
    bool anyEnabled = m_procConfig.whiteBalance || m_procConfig.clahe ||
                      m_procConfig.undistort || m_procConfig.dehaze ||
                      m_procConfig.sharpen || m_procConfig.denoise ||
                      m_procConfig.edgeDetect || m_procConfig.threshold;
    if (!anyEnabled) {
        // 不开处理时，直接缩放到显示尺寸
        return input.scaled(m_videoRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    }

    cv::Mat src = ImageConvert::qimageToMat(input);
    if (src.empty()) return input;

    cv::Mat dst;
    ImageProcessor::process(src, dst, m_procConfig,
                            m_undistortMap1, m_undistortMap2);

    // process() 输出半分辨率，resize 到显示尺寸
    cv::Mat display;
    cv::resize(dst, display, cv::Size(m_videoRect.width(), m_videoRect.height()),
               0, 0, cv::INTER_LINEAR);

    return ImageConvert::matToQImage(display);
}

// ===================== 窗口绘制 =====================
// Qt 的绘制机制：调用 update() 后，Qt 会在合适时机自动调用 paintEvent
// 这里负责把视频帧画到窗口上，并叠加 OSD（屏幕显示信息）
void MainWindow::paintEvent(QPaintEvent *event) {
    QMainWindow::paintEvent(event);       // 先调用父类绘制（背景等）

    QPainter painter(this);     // 创建画笔，绑定到当前窗口

    // 视频区域黑色背景
    painter.fillRect(m_videoRect, Qt::black);

    if (m_displayImage.isNull()) return;  // 还没有图像时只画黑色背景

    // 计算居中位置（图像可能比显示区域小，需要居中放置）
    int x = m_videoRect.x() + (m_videoRect.width() - m_displayImage.width()) / 2;
    int y = m_videoRect.y() + (m_videoRect.height() - m_displayImage.height()) / 2;
    painter.drawImage(x, y, m_displayImage);  // 画视频帧

    // OSD: 左上角显示当前时间（黄色字体）
    painter.setPen(Qt::yellow);
    painter.setFont(QFont("Monospace", 9));
    painter.drawText(x + 8, y + 16,
                     QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    // OSD: 录像时显示红色圆点 + "REC" 标识
    if (m_recording) {
        painter.setPen(Qt::red);
        painter.setBrush(Qt::red);
        painter.drawEllipse(x + 8, y + 24, 8, 8);   // 红色圆点
        painter.drawText(x + 20, y + 32, "REC");     // 文字标识
    }
}

// ===================== 键盘事件 =====================
// 重写父类的键盘事件处理，实现快捷键操作
void MainWindow::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Escape: close(); break;           // Esc → 关闭窗口退出程序
    case Qt::Key_Space:  onToggleFreeze(); break;  // 空格 → 冻结/继续画面
    case Qt::Key_C:      onCapturePhoto(); break;  // C → 拍照
    case Qt::Key_R:      onToggleRecord(); break;  // R → 开始/停止录像
    case Qt::Key_T:      onRotateImage(); break;   // T → 旋转 90°
    default: QMainWindow::keyPressEvent(event);     // 其他按键交给父类处理
    }
}

// ===================== 拍照 =====================
// 把当前显示的图像保存为 JPEG 文件，文件名用时间戳命名避免重复
void MainWindow::onCapturePhoto() {
    if (m_displayImage.isNull()) return;  // 没有图像时不操作
    QString filename = QString("./captures/%1.jpg")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    m_displayImage.save(filename, "JPEG", 95);  // 质量 95%
    m_lblStatus->setText("拍照: " + filename);
}

// ===================== 录像 =====================
// 切换录像状态：未录像 → 开始录像，已录像 → 停止并保存
void MainWindow::onToggleRecord() {
    if (!m_recording) {
        // 开始录像：创建视频文件
        int w = m_source ? m_source->getWidth() : 640;
        int h = m_source ? m_source->getHeight() : 480;
        QString filename = QString("./videos/%1.avi")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        // MJPG 编码，25fps，适合嵌入式设备（CPU 占用低）
        m_videoWriter.open(filename.toStdString(),
                           cv::VideoWriter::fourcc('M','J','P','G'),
                           25, cv::Size(w, h));
        if (m_videoWriter.isOpened()) {
            m_recording = true;
            m_btnRecord->setText("停止录像 (R)");
            m_lblStatus->setText("录像中...");
        }
    } else {
        // 停止录像：关闭文件，确保数据写入磁盘
        m_recording = false;
        m_videoWriter.release();
        m_btnRecord->setText("录像 (R)");
        m_lblStatus->setText("录像已保存");
    }
}

// ===================== 冻结画面 =====================
// 冻结时从缓存的最近 N 帧中选取最清晰的一帧显示
void MainWindow::onToggleFreeze() {
    m_frozen = !m_frozen;

    if (m_frozen && !m_frameBuffer.empty()) {
        // 遍历缓存帧，用拉普拉斯方差评估清晰度，选得分最高的
        double bestScore = -1;
        int bestIdx = 0;
        for (int i = 0; i < static_cast<int>(m_frameBuffer.size()); i++) {
            cv::Mat mat = ImageConvert::qimageToMat(m_frameBuffer[i]);
            if (mat.empty()) continue;
            double score = ImageProcessor::calcSharpness(mat);
            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
            }
        }

        // 用最清晰帧更新显示
        QImage best = m_frameBuffer[bestIdx];
        if (m_rotateAngle != 0) {
            QTransform transform;
            transform.rotate(m_rotateAngle);
            best = best.transformed(transform, Qt::SmoothTransformation);
        }
        m_displayImage = best.scaled(m_videoRect.size(),
                                     Qt::KeepAspectRatio,
                                     Qt::FastTransformation);
        update();

        m_btnFreeze->setText("继续 (Space)");
        m_lblStatus->setText(QString("已冻结 (最佳帧 %1/%2, 清晰度 %3)")
                             .arg(bestIdx + 1)
                             .arg(m_frameBuffer.size())
                             .arg(bestScore, 0, 'f', 1));
    } else {
        m_frameBuffer.clear();  // 解冻时清空缓存
        m_btnFreeze->setText("冻结 (Space)");
        m_lblStatus->setText("运行中");
    }
}

// ===================== 旋转图像 =====================
// 每次顺时针旋转 90°，转完 360° 回到原始方向
void MainWindow::onRotateImage() {
    m_rotateAngle = (m_rotateAngle + 90) % 360;  // 0 → 90 → 180 → 270 → 0
    m_lblStatus->setText(QString("旋转: %1°").arg(m_rotateAngle));
}

// ===================== FPS 统计 =====================
// 每秒被定时器调用一次，计算过去 1 秒内的平均帧率
void MainWindow::updateFps() {
    double elapsed = m_fpsTimer.elapsed() / 1000.0;  // 毫秒 → 秒
    if (elapsed > 0) {
        double fps = m_frameCount / elapsed;  // 帧数 / 时间 = 帧率
        m_lblFps->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));  // 保留 1 位小数
    }
    m_frameCount = 0;       // 帧计数归零
    m_fpsTimer.restart();   // 重新计时
}

// ===================== 加载预设 =====================
// 从 config/presets/ 目录加载所有 .json 预设文件
void MainWindow::loadPresets() {
    QMap<QString, ImageProcessor::Config> presets =
        PresetManager::loadAll("config/presets");

    m_presetNames.clear();
    m_presetConfigs.clear();
    m_cboPreset->clear();

    for (auto it = presets.begin(); it != presets.end(); ++it) {
        m_presetNames.append(it.key());
        m_presetConfigs.append(it.value());
        m_cboPreset->addItem(it.key());
    }

    if (!m_presetConfigs.isEmpty())
        m_procConfig = m_presetConfigs.first();
}

// ===================== 模式切换 =====================
// 调试模式 ↔ 临床模式
void MainWindow::onSwitchMode() {
    m_clinicalMode = !m_clinicalMode;

    if (m_clinicalMode) {
        // 切换到临床模式：显示预设选择面板
        m_ctrlStack->setCurrentIndex(1);
        m_btnSwitchMode->setText("切换到调试模式");
        // 应用当前选中的预设
        if (!m_presetConfigs.isEmpty())
            onPresetSelected(m_cboPreset->currentIndex());
        m_lblStatus->setText("临床模式");
    } else {
        // 切换到调试模式：显示参数调节面板
        m_ctrlStack->setCurrentIndex(0);
        m_btnSwitchMode->setText("切换到临床模式");
        m_lblStatus->setText("调试模式");
    }
}

// ===================== 预设选择 =====================
// 临床模式下，医生选择检查模式后，加载对应的参数
void MainWindow::onPresetSelected(int index) {
    if (index < 0 || index >= m_presetConfigs.size()) return;
    m_procConfig = m_presetConfigs[index];
    m_lblStatus->setText(QString("预设: %1").arg(m_presetNames[index]));
}

// ===================== 导出预设 =====================
// 调试模式下，把当前调好的参数保存为新的预设文件
void MainWindow::onExportPreset() {
    // 弹出输入框让用户输入预设名称
    bool ok;
    QString name = QInputDialog::getText(this, "导出预设",
                                         "预设名称:", QLineEdit::Normal,
                                         "自定义模式", &ok);
    if (!ok || name.isEmpty()) return;

    // 从当前界面控件读取参数
    ImageProcessor::Config cfg;
    cfg.whiteBalance   = m_chkWhiteBalance->isChecked();
    cfg.clahe          = m_chkClahe->isChecked();
    cfg.claheClipLimit = m_sliderClaheClip->value() / 10.0;
    cfg.undistort      = m_chkUndistort->isChecked();
    cfg.dehaze         = m_chkDehaze->isChecked();
    cfg.sharpen        = m_chkSharpen->isChecked();
    cfg.sharpenAmount  = m_sliderSharpen->value() / 10.0;
    cfg.denoise        = m_chkDenoise->isChecked();
    cfg.edgeDetect     = m_chkEdgeDetect->isChecked();
    cfg.threshold      = m_chkThreshold->isChecked();
    cfg.thresholdValue = m_sliderThreshold->value();

    // 保存到文件
    QString filename = name;
    filename.replace(" ", "_");
    QString path = QString("config/presets/%1.json").arg(filename);

    if (PresetManager::save(path, name, cfg)) {
        m_lblStatus->setText("预设已导出: " + name);
        loadPresets();  // 重新加载预设列表
    } else {
        m_lblStatus->setText("导出失败");
    }
}

// ===================== 同步 Config 到界面控件 =====================
// 切换预设或加载参数时，把 Config 的值反映到界面上
void MainWindow::applyConfig(const ImageProcessor::Config &cfg) {
    m_chkWhiteBalance->setChecked(cfg.whiteBalance);
    m_chkClahe->setChecked(cfg.clahe);
    m_sliderClaheClip->setValue(static_cast<int>(cfg.claheClipLimit * 10));
    m_chkUndistort->setChecked(cfg.undistort && m_undistortReady);
    m_chkDehaze->setChecked(cfg.dehaze);
    m_chkSharpen->setChecked(cfg.sharpen);
    m_sliderSharpen->setValue(static_cast<int>(cfg.sharpenAmount * 10));
    m_chkDenoise->setChecked(cfg.denoise);
    m_chkEdgeDetect->setChecked(cfg.edgeDetect);
    m_chkThreshold->setChecked(cfg.threshold);
    m_sliderThreshold->setValue(cfg.thresholdValue);
}
