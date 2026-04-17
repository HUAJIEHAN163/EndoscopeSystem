#include "ui/mainwindow.h"      // 主窗口类声明
#include "ui/presetmanager.h"   // 预设管理器
#include "capture/filecapture.h" // 文件视频源（虚拟机测试用）
#include "utils/imageconvert.h"  // QImage ↔ cv::Mat 转换工具

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
#include <QInputDialog>
#include <QDialog>
#include <QFile>
#include <opencv2/imgcodecs.hpp>

// ===================== 构造函数 =====================
// 程序启动时 main.cpp 中 MainWindow window; 会调用这里
// 初始化列表：先初始化所有成员变量的默认值
MainWindow::MainWindow(QWidget *parent)         //parent = nullptr 表示这个窗口没有父窗口，它就是顶层窗口。
    : QMainWindow(parent), m_source(nullptr),   // 视频源指针，初始为空
      m_frozen(false), m_rotateAngle(0),         // 画面未冻结，旋转角度 0
      m_recording(false), m_frameCount(0),       // 未在录像，帧计数归零
      m_undistortReady(false),                    // 畸变校正未就绪
      m_clinicalMode(false),
      m_currentMode(0)
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
    // 存储路径：开发板用 NFS 共享目录，虚拟机用本地目录
    if (QDir("/mnt/nfs").exists()) {
        m_savePath = "/mnt/nfs";
    } else {
        m_savePath = ".";
    }
    QDir().mkpath(m_savePath + "/captures");
    QDir().mkpath(m_savePath + "/videos");

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
    if (m_processThread) {
        m_processThread->stop();
        m_processThread->wait();
    }
    if (m_source) {
        m_source->stop();
        m_source->wait();
    }
    if (m_videoWriter.isOpened())
        m_videoWriter.release();
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
    videoArea->setMinimumSize(460, 360);
#else
    videoArea->setMinimumSize(640, 480);
#endif
    videoArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);  // 允许拉伸
    videoArea->setStyleSheet("");  // 不设样式，背景在 paintEvent 中绘制
    mainLayout->addWidget(videoArea, 1);  // 拉伸因子 1，优先占据剩余空间
#ifdef __arm__
    m_videoRect = QRect(4, 4, 460, 360);
#else
    m_videoRect = QRect(4, 4, 640, 480);
#endif

    // ==============================================================
    // 右侧: 控制面板（垂直排列）
    // 从上到下：模式切换按钮 → 调试/临床面板 → 操作按钮 → 状态信息
    // ==============================================================
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->setSpacing(4);

    // --- 模式切换按钮（三个固定按钮，当前模式置灰）---
    QHBoxLayout *modeLayout = new QHBoxLayout;
    modeLayout->setSpacing(2);
    m_btnModeDebug = new QPushButton("调试");
    m_btnModeClinical = new QPushButton("临床");
    m_btnModeEdit = new QPushButton("编辑");
    QString modeStyle = "QPushButton { font-size: 12px; }";
    m_btnModeDebug->setStyleSheet(modeStyle);
    m_btnModeClinical->setStyleSheet(modeStyle);
    m_btnModeEdit->setStyleSheet(modeStyle);
    modeLayout->addWidget(m_btnModeDebug);
    modeLayout->addWidget(m_btnModeClinical);
    modeLayout->addWidget(m_btnModeEdit);
    rightLayout->addLayout(modeLayout);

    connect(m_btnModeDebug, &QPushButton::clicked, [this]() { switchToMode(0); });
    connect(m_btnModeClinical, &QPushButton::clicked, [this]() { switchToMode(1); });
    connect(m_btnModeEdit, &QPushButton::clicked, [this]() { switchToMode(2); });

    // --- QStackedWidget: 面板切换容器 ---
    // index 0 = 调试, index 1 = 临床, index 2 = 图像编辑
    m_ctrlStack = new QStackedWidget;

    // ==============================================================
    // 调试面板（index 0）— 工程师用，所有参数可调
    // 分组框可折叠：点击标题展开/收起，节省 800x480 小屏空间
    // ==============================================================
    m_debugPanel = new QWidget;
    QVBoxLayout *debugLayout = new QVBoxLayout(m_debugPanel);
    debugLayout->setSpacing(2);
    debugLayout->setContentsMargins(0, 0, 0, 0);

    // 折叠辅助函数：点击标题时显示/隐藏内容
    auto makeCollapsible = [](QGroupBox *group, bool expanded) {
        group->setCheckable(true);
        group->setChecked(expanded);
        // 勾选 = 展开，取消 = 收起（隐藏内部控件）
        QObject::connect(group, &QGroupBox::toggled, [group](bool checked) {
            for (auto *child : group->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                child->setVisible(checked);
        });
        // 初始状态
        if (!expanded) {
            for (auto *child : group->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                child->setVisible(false);
        }
    };

    // -- 硬件参数分组框（OV5640）--
    m_hwGroup = new QGroupBox("硬件参数 (OV5640)");
    QGridLayout *hwLayout = new QGridLayout(m_hwGroup);
    hwLayout->setSpacing(2);

    m_chkAutoWhiteBalance = new QCheckBox("自动白平衡");
    m_chkAutoWhiteBalance->setChecked(true);
    m_chkAutoExposure = new QCheckBox("自动曝光");
    m_chkAutoExposure->setChecked(true);
    m_chkHFlip = new QCheckBox("水平翻转");
    m_chkVFlip = new QCheckBox("垂直翻转");

    hwLayout->addWidget(m_chkAutoWhiteBalance, 0, 0);
    hwLayout->addWidget(m_chkAutoExposure,     0, 1);
    hwLayout->addWidget(m_chkHFlip,            1, 0);
    hwLayout->addWidget(m_chkVFlip,            1, 1);

    // 对比度和饱和度滑块
    QLabel *lblContrast = new QLabel("对比度");
    m_sliderContrast = new QSlider(Qt::Horizontal);
    m_sliderContrast->setRange(0, 255);
    m_sliderContrast->setValue(0);
    QLabel *lblSaturation = new QLabel("饱和度");
    m_sliderSaturation = new QSlider(Qt::Horizontal);
    m_sliderSaturation->setRange(0, 255);
    m_sliderSaturation->setValue(64);

    hwLayout->addWidget(lblContrast,       2, 0);
    hwLayout->addWidget(m_sliderContrast,  2, 1);
    hwLayout->addWidget(lblSaturation,     3, 0);
    hwLayout->addWidget(m_sliderSaturation,3, 1);

    makeCollapsible(m_hwGroup, false);
    debugLayout->addWidget(m_hwGroup);

    // -- 内窥镜算法分组框 --
    QGroupBox *endoGroup = new QGroupBox("内窥镜算法");
    QVBoxLayout *endoLayout = new QVBoxLayout(endoGroup);
    endoLayout->setSpacing(2);

    m_chkClahe = new QCheckBox("CLAHE增强");
    m_chkUndistort = new QCheckBox("畸变校正");
    m_chkDehaze = new QCheckBox("去雾");

    endoLayout->addWidget(m_chkClahe);

    m_sliderClaheClip = new QSlider(Qt::Horizontal);
    m_sliderClaheClip->setRange(10, 80);
    m_sliderClaheClip->setValue(30);
    endoLayout->addWidget(m_sliderClaheClip);

    endoLayout->addWidget(m_chkUndistort);

    endoLayout->addWidget(m_chkDehaze);
    m_sliderDehazeOmega = new QSlider(Qt::Horizontal);
    m_sliderDehazeOmega->setRange(50, 100);
    m_sliderDehazeOmega->setValue(95);
    endoLayout->addWidget(m_sliderDehazeOmega);

    makeCollapsible(endoGroup, true);  // 默认展开
    debugLayout->addWidget(endoGroup);

    // -- 通用算法分组框（已移至图像编辑页，保留控件但不显示）--
    m_chkSharpen = new QCheckBox;
    m_sliderSharpen = new QSlider;
    m_chkDenoise = new QCheckBox;
    m_chkEdgeDetect = new QCheckBox;
    m_chkThreshold = new QCheckBox;
    m_sliderThreshold = new QSlider;

    // 导出预设按钮
    m_btnExportPreset = new QPushButton("导出为预设");
    connect(m_btnExportPreset, &QPushButton::clicked, this, &MainWindow::onExportPreset);
    debugLayout->addWidget(m_btnExportPreset);

    debugLayout->addStretch();

    // 用 QScrollArea 包裹调试面板，内容超出时可滚动
    QScrollArea *debugScroll = new QScrollArea;
    debugScroll->setWidget(m_debugPanel);
    debugScroll->setWidgetResizable(true);
    debugScroll->setFrameShape(QFrame::NoFrame);
    debugScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // 禁用水平滚动条
    debugScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);     // 垂直滚动条按需显示
    // 触屏滑动：手指拖动即可滚动，不需要点击滚动条
    QScroller::grabGesture(debugScroll->viewport(), QScroller::TouchGesture);
    m_ctrlStack->addWidget(debugScroll);

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

    // ==============================================================
    // 图像编辑面板（index 2）
    // ==============================================================
    m_editPanel = new QWidget;
    QVBoxLayout *editLayout = new QVBoxLayout(m_editPanel);
    editLayout->setSpacing(4);
    editLayout->setContentsMargins(0, 0, 0, 0);

    m_btnSelectImage = new QPushButton("图像选择");
    connect(m_btnSelectImage, &QPushButton::clicked, this, &MainWindow::onSelectImage);
    editLayout->addWidget(m_btnSelectImage);

    // 编辑算法控件
    QGroupBox *editGroup = new QGroupBox("图像处理");
    QVBoxLayout *editAlgoLayout = new QVBoxLayout(editGroup);
    editAlgoLayout->setSpacing(2);

    m_chkEditSharpen = new QCheckBox("锐化");
    m_sliderEditSharpen = new QSlider(Qt::Horizontal);
    m_sliderEditSharpen->setRange(5, 30);
    m_sliderEditSharpen->setValue(15);
    m_chkEditDenoise = new QCheckBox("降噪");
    m_chkEditEdge = new QCheckBox("边缘检测");
    m_chkEditThreshold = new QCheckBox("阈值分割");
    m_sliderEditThreshold = new QSlider(Qt::Horizontal);
    m_sliderEditThreshold->setRange(0, 255);
    m_sliderEditThreshold->setValue(128);

    editAlgoLayout->addWidget(m_chkEditSharpen);
    editAlgoLayout->addWidget(m_sliderEditSharpen);
    editAlgoLayout->addWidget(m_chkEditDenoise);
    editAlgoLayout->addWidget(m_chkEditEdge);
    editAlgoLayout->addWidget(m_chkEditThreshold);
    editAlgoLayout->addWidget(m_sliderEditThreshold);
    editLayout->addWidget(editGroup);

    // 参数变化时实时预览
    connect(m_chkEditSharpen, &QCheckBox::toggled, this, &MainWindow::onEditParamChanged);
    connect(m_sliderEditSharpen, &QSlider::valueChanged, this, &MainWindow::onEditParamChanged);
    connect(m_chkEditDenoise, &QCheckBox::toggled, this, &MainWindow::onEditParamChanged);
    connect(m_chkEditEdge, &QCheckBox::toggled, this, &MainWindow::onEditParamChanged);
    connect(m_chkEditThreshold, &QCheckBox::toggled, this, &MainWindow::onEditParamChanged);
    connect(m_sliderEditThreshold, &QSlider::valueChanged, this, &MainWindow::onEditParamChanged);

    // 保存和删除按钮
    QHBoxLayout *editBtnLayout = new QHBoxLayout;
    m_btnSaveEdit = new QPushButton("保存");
    m_btnDeleteImage = new QPushButton("删除");
    connect(m_btnSaveEdit, &QPushButton::clicked, this, &MainWindow::onSaveEditedImage);
    connect(m_btnDeleteImage, &QPushButton::clicked, this, &MainWindow::onDeleteImage);
    editBtnLayout->addWidget(m_btnSaveEdit);
    editBtnLayout->addWidget(m_btnDeleteImage);
    editLayout->addLayout(editBtnLayout);

    editLayout->addStretch();
    m_ctrlStack->addWidget(m_editPanel);

    // 把 QStackedWidget 加入右侧布局
    // 固定右侧控制面板宽度，防止切换模式时变窄
    m_ctrlStack->setMinimumHeight(300);
#ifdef __arm__
    m_ctrlStack->setFixedWidth(300);  // 开发板加宽，避免文字截断
#else
    m_ctrlStack->setFixedWidth(250);
#endif
    rightLayout->addWidget(m_ctrlStack);

    // --- 操作按钮（放在右侧面板底部，2x2 网格布局）---
    m_btnCapture = new QPushButton("拍照");
    m_btnRecord = new QPushButton("录像");
    m_btnFreeze = new QPushButton("冻结");
    m_btnRotate = new QPushButton("旋转");

    QString btnStyle = "QPushButton { font-size: 15px; }";
    m_btnCapture->setStyleSheet(btnStyle);
    m_btnRecord->setStyleSheet(btnStyle);
    m_btnFreeze->setStyleSheet(btnStyle);
    m_btnRotate->setStyleSheet(btnStyle);

    connect(m_btnCapture, &QPushButton::clicked, this, &MainWindow::onCapturePhoto);
    connect(m_btnRecord, &QPushButton::clicked, this, &MainWindow::onToggleRecord);
    connect(m_btnFreeze, &QPushButton::clicked, this, &MainWindow::onToggleFreeze);
    connect(m_btnRotate, &QPushButton::clicked, this, &MainWindow::onRotateImage);

    QGridLayout *btnGrid = new QGridLayout;
    btnGrid->setSpacing(4);
    btnGrid->addWidget(m_btnCapture, 0, 0);
    btnGrid->addWidget(m_btnRecord,  0, 1);
    btnGrid->addWidget(m_btnFreeze,  1, 0);
    btnGrid->addWidget(m_btnRotate,  1, 1);
    rightLayout->addLayout(btnGrid);

    // 把右侧整体布局加入上方水平布局
    mainLayout->addLayout(rightLayout);

    // 底部状态栏
    QHBoxLayout *btnRow2 = new QHBoxLayout;
    btnRow2->setSpacing(8);
    m_lblFps = new QLabel("FPS: --");
    m_lblStatus = new QLabel("就绪");
    m_lblFps->setStyleSheet("font-size: 17px;");
    m_lblStatus->setStyleSheet("font-size: 17px;");
    btnRow2->addWidget(m_lblFps);
    btnRow2->addWidget(m_lblStatus);
    btnRow2->addStretch();

    rootLayout->addLayout(mainLayout, 1);
    rootLayout->addLayout(btnRow2);

    // 如果没有标定文件，畸变校正复选框置灰，鼠标悬停显示提示
    m_chkUndistort->setEnabled(m_undistortReady);
    if (!m_undistortReady)
        m_chkUndistort->setToolTip("未找到 camera_calib.yml 标定文件");

    // 默认调试模式
    m_currentMode = -1;  // 强制首次切换生效
    switchToMode(0);
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
        //打开 config/endoscope.conf 文件,告诉 QSettings 按 INI 格式解析
        QSettings settings("config/endoscope.conf", QSettings::IniFormat);
        //从配置文件中读取 [file] 组下的 path 键的值
        //如果不存在就用 "test_data/test.mp4" 作为默认值，转成 QString 赋给 testFile。
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

    // 三线程管线初始化
    m_source->m_captureQueue = &m_captureQueue;

    m_processThread = new ProcessThread(&m_captureQueue, &m_displayQueue, this);
    m_processThread->displayWidth = m_videoRect.width();
    m_processThread->displayHeight = m_videoRect.height();
    m_processThread->undistortMap1 = m_undistortMap1;
    m_processThread->undistortMap2 = m_undistortMap2;

    // 定时从 displayQueue 取最新帧显示（16ms ≈ 60fps 刷新率）
    m_displayTimer = new QTimer(this);
    //信号槽在m_displayTimer->start(16);中被触发，每16ms调用一次lambda函数，lambda函数中从displayQueue获取最新的帧数据，并进行处理和显示。
    connect(m_displayTimer, &QTimer::timeout, [this]() {
        // 同步 UI 参数到处理线程
        if (m_processThread && !m_clinicalMode) {
            m_processThread->config.whiteBalance = false;
            m_processThread->config.clahe = m_chkClahe->isChecked();
            m_processThread->config.claheClipLimit = m_sliderClaheClip->value() / 10.0;
            m_processThread->config.undistort = m_chkUndistort->isChecked() && m_undistortReady;
            m_processThread->config.dehaze = m_chkDehaze->isChecked();
            m_processThread->config.dehazeOmega = m_sliderDehazeOmega->value() / 100.0;
            // 通用算法已移至图像编辑页，实时流中固定关闭
            m_processThread->config.sharpen = false;
            m_processThread->config.denoise = false;
            m_processThread->config.edgeDetect = false;
            m_processThread->config.threshold = false;
        } else if (m_processThread && m_clinicalMode) {
            m_processThread->config = m_procConfig;
        }

        // P7.1: 从 displayQueue 取 BGR Mat，在主线程做唯一一次 BGR→RGB 转换
        cv::Mat mat;
        if (m_displayQueue.latest(mat)) {
            m_latestRawMat = mat;  // 保存原始 BGR 帧用于拍照/录像
            m_frameCount++;

            // BGR → RGB → QImage（整个管线唯一的颜色转换）
            cv::Mat rgb;
            cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
            QImage frame(rgb.data, rgb.cols, rgb.rows, rgb.step,
                         QImage::Format_RGB888);
            frame = frame.copy();  // 深拷贝，脱离 rgb 内存

            // 旋转
            if (m_rotateAngle != 0) {
                QTransform transform;
                transform.rotate(m_rotateAngle);
                frame = frame.transformed(transform, Qt::FastTransformation);
                frame = frame.scaled(m_videoRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
            }

            // 录像（直接用 BGR Mat，避免 QImage→Mat 转换）
            if (m_recording && m_videoWriter.isOpened()) {
                cv::Mat resized;
                cv::resize(mat, resized, cv::Size(m_videoRect.width(), m_videoRect.height()));
                m_videoWriter.write(resized);
            }

            m_displayImage = frame;
            update();
        }
    });
    m_displayTimer->start(16);

    // 启动采集线程和处理线程
    m_source->start();
    m_processThread->start();

    // 错误信号
    connect(m_source, &VideoSource::errorOccurred, [this](const QString &msg) {
        m_lblStatus->setText("错误: " + msg);
    });

    // 硬件参数区：只有 V4L2 摄像头时才可用
#ifdef ENABLE_V4L2
    bool hasHardware = (dynamic_cast<V4l2Capture*>(m_source) != nullptr);
#else
    bool hasHardware = false;
#endif
    m_hwGroup->setEnabled(hasHardware);
    if (!hasHardware)
        m_hwGroup->setToolTip("仅开发板摄像头可用");

    // 开发板上默认开启硬件白平衡和自动曝光
#ifdef ENABLE_V4L2
    if (hasHardware) {
        auto *v4l2 = dynamic_cast<V4l2Capture*>(m_source);
        v4l2->setAutoWhiteBalance(true);
        v4l2->setAutoExposure(true);

        // 连接硬件参数控件信号槽
        connect(m_chkAutoWhiteBalance, &QCheckBox::toggled, [v4l2](bool checked) {
            v4l2->setAutoWhiteBalance(checked);
        });
        connect(m_chkAutoExposure, &QCheckBox::toggled, [v4l2](bool checked) {
            v4l2->setAutoExposure(checked);
        });
        connect(m_chkHFlip, &QCheckBox::toggled, [v4l2](bool checked) {
            v4l2->setHFlip(checked);
        });
        connect(m_chkVFlip, &QCheckBox::toggled, [v4l2](bool checked) {
            v4l2->setVFlip(checked);
        });
        connect(m_sliderContrast, &QSlider::valueChanged, [v4l2](int value) {
            v4l2->setContrast(value);
        });
        connect(m_sliderSaturation, &QSlider::valueChanged, [v4l2](int value) {
            v4l2->setSaturation(value);
        });
    }
#endif
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

    // 保存原始帧（全分辨率，用于拍照）
    m_latestRawFrame = image;

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
        // 旋转后图像尺寸变化（如 640x480 → 480x640），重新缩放到显示区域
        processed = processed.scaled(m_videoRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    }

    // 4. 录像（写入前统一 resize 到 VideoWriter 尺寸，避免旋转后尺寸不匹配崩溃）
    if (m_recording && m_videoWriter.isOpened()) {
        cv::Mat mat = ImageConvert::qimageToMat(processed);
        if (!mat.empty()) {
            cv::Mat resized;
            cv::resize(mat, resized, cv::Size(m_videoRect.width(), m_videoRect.height()));
            m_videoWriter.write(resized);
        }
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
        m_procConfig.whiteBalance = false;  // 白平衡已由硬件处理
        m_procConfig.clahe = m_chkClahe->isChecked();
        m_procConfig.claheClipLimit = m_sliderClaheClip->value() / 10.0;
        m_procConfig.undistort = m_chkUndistort->isChecked() && m_undistortReady;
        m_procConfig.dehaze = m_chkDehaze->isChecked();
        m_procConfig.dehazeOmega = m_sliderDehazeOmega->value() / 100.0;
        // 通用算法已移至图像编辑页，实时流中不启用
        m_procConfig.sharpen = false;
        m_procConfig.denoise = false;
        m_procConfig.edgeDetect = false;
        m_procConfig.threshold = false;
    }
    // 临床模式：m_procConfig 已在 onPresetSelected 中设置好，直接使用

    // 没有任何算法启用时，跳过处理直接返回原图（省 CPU）
    bool anyEnabled = m_procConfig.clahe ||
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
    case Qt::Key_Escape: close(); break;
    default: QMainWindow::keyPressEvent(event);
    }
}

// ===================== 拍照 =====================
// 把当前显示的图像保存为 JPEG 文件，文件名用时间戳命名避免重复
void MainWindow::onCapturePhoto() {
    qDebug() << "[BTN] onCapturePhoto";
    if (m_displayImage.isNull() && m_latestRawMat.empty()) return;
    QString filename = QString("%1/captures/%2.jpg")
        .arg(m_savePath)
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    // P7.1: 直接用 BGR Mat 保存，避免 QImage 转换
    if (!m_latestRawMat.empty()) {
        cv::imwrite(filename.toStdString(), m_latestRawMat);
    } else {
        m_displayImage.save(filename, "JPEG", 95);
    }
    m_lblStatus->setText("拍照: " + filename);
}

// ===================== 录像 =====================
// 切换录像状态：未录像 → 开始录像，已录像 → 停止并保存
void MainWindow::onToggleRecord() {
    qDebug() << "[BTN] onToggleRecord";
    if (!m_recording) {
        // 开始录像：创建视频文件，尺寸与显示区域一致
        int w = m_videoRect.width();
        int h = m_videoRect.height();
        QString filename = QString("%1/videos/%2.avi")
            .arg(m_savePath)
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
    qDebug() << "[BTN] onToggleFreeze";
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
    qDebug() << "[BTN] onRotateImage";
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
// 0=调试, 1=临床, 2=图像编辑
void MainWindow::switchToMode(int mode) {
    if (mode == m_currentMode) return;

    // 离开图像编辑模式时恢复采集
    if (m_currentMode == 2 && mode != 2) {
        if (m_source)
            m_source->resume();
            if (m_processThread) m_processThread->resume();
            if (m_displayTimer) m_displayTimer->start(16);
    }

    m_currentMode = mode;
    m_ctrlStack->setCurrentIndex(mode);

    // 更新按钮状态：当前模式置灰
    m_btnModeDebug->setEnabled(mode != 0);
    m_btnModeClinical->setEnabled(mode != 1);
    m_btnModeEdit->setEnabled(mode != 2);

    // 图像编辑模式下隐藏拍照等按钮，其他模式显示
    bool showCaptureButtons = (mode != 2);
    m_btnCapture->setVisible(showCaptureButtons);
    m_btnRecord->setVisible(showCaptureButtons);
    m_btnFreeze->setVisible(showCaptureButtons);
    m_btnRotate->setVisible(showCaptureButtons);

    switch (mode) {
    case 0:  // 调试模式
        m_clinicalMode = false;
        m_lblStatus->setText("调试模式");
        break;
    case 1:  // 临床模式
        m_clinicalMode = true;
        if (!m_presetConfigs.isEmpty())
            onPresetSelected(m_cboPreset->currentIndex());
        m_lblStatus->setText("临床模式");
        break;
    case 2:  // 图像编辑模式
        m_clinicalMode = false;
        if (m_source)
            m_source->pause();
            if (m_processThread) m_processThread->pause();
            if (m_displayTimer) m_displayTimer->stop();
        m_lblStatus->setText("图像编辑");
        m_lblFps->setText("FPS: --");
        break;
    }
}

// 兼容旧接口
void MainWindow::onSwitchMode() {
    switchToMode(m_currentMode == 0 ? 1 : 0);
}

void MainWindow::onEnterEditMode() {
    switchToMode(2);
}

// ===================== 图像选择弹窗 =====================
void MainWindow::onSelectImage() {
    QString dir = m_savePath + "/captures";
    QDir captureDir(dir);
    QStringList files = captureDir.entryList(QStringList() << "*.jpg" << "*.png",
                                              QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        m_lblStatus->setText("无图像文件");
        return;
    }

    // 创建全屏弹窗
    QDialog dlg(this);
    dlg.setWindowTitle("选择图像");
    dlg.setFixedSize(this->size());

    QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);

    // 缩略图网格
    QScrollArea *scroll = new QScrollArea;
    QWidget *grid = new QWidget;
    QGridLayout *gridLayout = new QGridLayout(grid);
    gridLayout->setSpacing(8);

    int col = 0, row = 0;
    QString selectedFile;

    for (const QString &file : files) {
        QString path = dir + "/" + file;
        QPixmap thumb(path);
        thumb = thumb.scaled(120, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        QPushButton *btn = new QPushButton;
        btn->setIcon(QIcon(thumb));
        btn->setIconSize(thumb.size());
        btn->setFixedSize(130, 100);

        connect(btn, &QPushButton::clicked, [&dlg, &selectedFile, path]() {
            selectedFile = path;
            dlg.accept();
        });

        gridLayout->addWidget(btn, row, col);
        col++;
        if (col >= 4) { col = 0; row++; }
    }

    scroll->setWidget(grid);
    scroll->setWidgetResizable(true);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    dlgLayout->addWidget(scroll);

    QPushButton *btnCancel = new QPushButton("取消");
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlgLayout->addWidget(btnCancel);

    if (dlg.exec() == QDialog::Accepted && !selectedFile.isEmpty()) {
        m_editSourceImage = QImage(selectedFile);
        m_editImagePath = selectedFile;
        m_displayImage = m_editSourceImage.scaled(m_videoRect.size(),
                                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
        update();
        m_lblStatus->setText("编辑: " + QFileInfo(selectedFile).fileName());
    }
}

// ===================== 编辑参数变化 — 实时预览 =====================
void MainWindow::onEditParamChanged() {
    if (m_editSourceImage.isNull()) return;

    cv::Mat src = ImageConvert::qimageToMat(m_editSourceImage);
    if (src.empty()) return;

    ImageProcessor::Config cfg;
    cfg.sharpen = m_chkEditSharpen->isChecked();
    cfg.sharpenAmount = m_sliderEditSharpen->value() / 10.0;
    cfg.denoise = m_chkEditDenoise->isChecked();
    cfg.edgeDetect = m_chkEditEdge->isChecked();
    cfg.threshold = m_chkEditThreshold->isChecked();
    cfg.thresholdValue = m_sliderEditThreshold->value();

    cv::Mat dst;
    ImageProcessor::process(src, dst, cfg);

    // 显示处理结果
    cv::Mat display;
    cv::resize(dst, display, cv::Size(m_videoRect.width(), m_videoRect.height()));
    m_displayImage = ImageConvert::matToQImage(display);
    update();
}

// ===================== 保存编辑后的图像 =====================
void MainWindow::onSaveEditedImage() {
    if (m_editSourceImage.isNull()) {
        m_lblStatus->setText("无图像可保存");
        return;
    }

    // 对原图应用处理并保存
    cv::Mat src = ImageConvert::qimageToMat(m_editSourceImage);
    ImageProcessor::Config cfg;
    cfg.sharpen = m_chkEditSharpen->isChecked();
    cfg.sharpenAmount = m_sliderEditSharpen->value() / 10.0;
    cfg.denoise = m_chkEditDenoise->isChecked();
    cfg.edgeDetect = m_chkEditEdge->isChecked();
    cfg.threshold = m_chkEditThreshold->isChecked();
    cfg.thresholdValue = m_sliderEditThreshold->value();

    cv::Mat dst;
    ImageProcessor::process(src, dst, cfg);

    // 保存到原文件同目录，文件名加 _edited 后缀
    QFileInfo fi(m_editImagePath);
    QString savePath = fi.path() + "/" + fi.baseName() + "_edited.jpg";
    QImage result = ImageConvert::matToQImage(dst);
    result.save(savePath, "JPEG", 95);
    m_lblStatus->setText("已保存: " + QFileInfo(savePath).fileName());
}

// ===================== 删除当前图像 =====================
void MainWindow::onDeleteImage() {
    if (m_editImagePath.isEmpty()) {
        m_lblStatus->setText("无图像可删除");
        return;
    }
    QFile::remove(m_editImagePath);
    m_editSourceImage = QImage();
    m_editImagePath.clear();
    m_displayImage = QImage();
    update();
    m_lblStatus->setText("图像已删除");
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
    cfg.whiteBalance   = false;
    cfg.clahe          = m_chkClahe->isChecked();
    cfg.claheClipLimit = m_sliderClaheClip->value() / 10.0;
    cfg.undistort      = m_chkUndistort->isChecked();
    cfg.dehaze         = m_chkDehaze->isChecked();
    cfg.dehazeOmega    = m_sliderDehazeOmega->value() / 100.0;
    cfg.sharpen        = false;
    cfg.denoise        = false;
    cfg.edgeDetect     = false;
    cfg.threshold      = false;

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
    m_chkClahe->setChecked(cfg.clahe);
    m_sliderClaheClip->setValue(static_cast<int>(cfg.claheClipLimit * 10));
    m_sliderDehazeOmega->setValue(static_cast<int>(cfg.dehazeOmega * 100));
    m_chkUndistort->setChecked(cfg.undistort && m_undistortReady);
    m_chkDehaze->setChecked(cfg.dehaze);
}
