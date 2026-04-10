#ifndef MAINWINDOW_H  // 头文件保护：防止同一个文件被 #include 多次导致重复定义
#define MAINWINDOW_H

// === Qt 头文件 ===
#include <QMainWindow>    // 主窗口基类，提供标题栏、菜单栏等框架
#include <QImage>         // Qt 图像类，用于显示视频帧
#include <QCheckBox>      // 复选框控件（算法开关）
#include <QSlider>        // 滑块控件（参数调节）
#include <QPushButton>    // 按钮控件（拍照、录像等）
#include <QLabel>         // 文本标签（FPS、状态显示）
#include <QElapsedTimer>  // 高精度计时器（FPS 统计）
#include <QStackedWidget> // 堆叠容器（调试/临床面板切换）
#include <QComboBox>      // 下拉框（预设选择）
#include <QGroupBox>      // 分组框（硬件参数区）
#include <QScrollArea>    // 滚动区域（参数面板）
#include <QScroller>      // 触屏滑动手势

// === 第三方库 ===
#include <opencv2/videoio.hpp>  // OpenCV 视频写入（录像功能）
#include <deque>                // 双端队列（帧缓存，用于冻结选最清晰帧）

// === 项目内部头文件 ===
#include "capture/videosource.h"        // 视频源抽象基类
#include "processing/imageprocessor.h"  // 图像处理算法集合

// =====================================================================
// MainWindow — 主窗口类
// 继承自 QMainWindow，是整个应用的核心，负责：
// 1. 界面搭建（setupUi）
// 2. 视频源管理（setupVideoSource）
// 3. 帧处理流水线（onFrameReady → processFrame → paintEvent）
// 4. 用户交互（拍照、录像、冻结、旋转、模式切换、预设管理）
// =====================================================================
/*基本结构说明：
class MainWindow : public QMainWindow {
    Q_OBJECT                          // 1. 必须放最前面

public:
    explicit MainWindow(QWidget *parent = nullptr);  // 2. 构造函数
    ~MainWindow();                                    //    析构函数

protected:
    // 3. 重写父类的虚函数
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    // 4. 自定义信号（本项目没用到，但大项目常见）
    void someSignal();

private slots:
    // 5. 槽函数（响应信号的函数）
    void onFrameReady(const QImage &image);
    void onCapturePhoto();

private:
    // 6. 私有方法
    void setupUi();
    void setupVideoSource();

    // 7. 成员变量
    VideoSource *m_source;
    bool m_frozen;
};
*/
class MainWindow : public QMainWindow {
    // Q_OBJECT 宏：启用 Qt 的信号槽机制和元对象系统
    // 只要类里用了 signals、slots 或 connect，就必须加这个宏
    // 编译时 MOC 工具会扫描它，自动生成信号槽所需的额外代码
    Q_OBJECT

public:
    // 构造函数：parent 默认为 nullptr，表示顶层窗口
    // explicit 防止隐式类型转换（Qt 编码规范）
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();  // 析构函数：释放视频源和录像资源

protected:
    // --- 重写父类虚函数（override 让编译器检查函数签名是否正确）---
    // 窗口绘制：每次调用 update() 后由 Qt 自动触发，负责画视频帧和 OSD
    // override 告诉编译器：这个函数是重写父类的虚函数。它的作用是让编译器帮你检查，防止写错：
    /*调用链：
    用 update()
    ↓
    Qt 事件循环收到重绘请求
    ↓
    Qt 内部自动创建 QPaintEvent 对象（包含需要重绘的区域）
    ↓
    Qt 自动调用 MainWindow::paintEvent(event)  ← event 是 Qt 传进来的
    */
    void paintEvent(QPaintEvent *event) override;
    // 键盘事件：实现快捷键（Esc/Space/C/R/T）
    /*调用链
    用户按下键盘
    ↓
    操作系统通知 Qt
    ↓
    Qt 自动创建 QKeyEvent 对象（包含按了哪个键）
    ↓
    Qt 自动调用 MainWindow::keyPressEvent(event)
    */
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    //slots 在编译时会被替换为空，对 C++ 编译器来说就是普通的 private:。
    //但 MOC 工具会识别它，知道下面的函数是槽函数，需要生成信号槽连接的代码。
    // --- 槽函数：响应信号的回调函数，命名以 on 开头 ---

    void onFrameReady(const QImage &image);  // 视频源每产出一帧触发
    void onCapturePhoto();                    // 拍照
    void onToggleRecord();                    // 开始/停止录像
    void onToggleFreeze();                    // 冻结/继续画面（选最清晰帧）
    void onRotateImage();                     // 旋转 90°
    void updateFps();                         // 每秒刷新帧率显示
    void onSwitchMode();                      // 切换调试/临床模式
    void onEnterEditMode();                    // 进入图像编辑模式
    void onSelectImage();                      // 打开图像选择弹窗
    void onSaveEditedImage();                  // 保存编辑后的图像
    void onDeleteImage();                      // 删除当前图像
    void onEditParamChanged();                 // 编辑参数变化时实时预览
    void onPresetSelected(int index);         // 临床模式下选择预设
    void onExportPreset();                    // 调试模式下导出当前参数为预设

private:
    // --- 私有方法 ---

    void setupUi();
    void setupVideoSource();
    void loadPresets();
    void applyConfig(const ImageProcessor::Config &cfg);
    QImage processFrame(const QImage &input);
    void switchToMode(int mode);  // 0=调试, 1=临床, 2=图像编辑

    // --- 成员变量（m_ 前缀区分局部变量）---

    // 模式标志：true=临床模式（医生用），false=调试模式（工程师用）
    bool m_clinicalMode;

    // --- 当前模式：0=调试, 1=临床, 2=图像编辑 ---
    int m_currentMode;

    // 视频源：指向 V4l2Capture 或 FileCapture（多态，基类指针）
    VideoSource *m_source;

    // 显示相关
    QImage m_displayImage;  // 当前显示的图像（缩放后）
    QRect m_videoRect;      // 视频绘制区域的坐标和大小

    // --- 硬件参数控件（OV5640，仅开发板可用）---
    QGroupBox *m_hwGroup;              // 硬件参数分组框（虚拟机时置灰）
    QCheckBox *m_chkAutoWhiteBalance;  // 自动白平衡
    QCheckBox *m_chkAutoExposure;      // 自动曝光
    QCheckBox *m_chkHFlip;             // 水平翻转
    QCheckBox *m_chkVFlip;             // 垂直翻转

    // --- 调试模式控件：算法开关（复选框）---
    QCheckBox *m_chkClahe;         // CLAHE 增强
    QCheckBox *m_chkUndistort;     // 畸变校正
    QCheckBox *m_chkDehaze;        // 去雾
    QCheckBox *m_chkSharpen;       // 锐化（待移至图像编辑页）
    QCheckBox *m_chkDenoise;       // 降噪（待移至图像编辑页）
    QCheckBox *m_chkEdgeDetect;    // 边缘检测（待移至图像编辑页）
    QCheckBox *m_chkThreshold;     // 阈值分割（待移至图像编辑页）

    // --- 调试模式控件：参数滑块 ---
    QSlider *m_sliderClaheClip;    // CLAHE 对比度限制（10~80 → 1.0~8.0）
    QSlider *m_sliderSharpen;      // 锐化强度（待移至图像编辑页）
    QSlider *m_sliderThreshold;    // 阈值（待移至图像编辑页）

    // --- 拍照用原始帧缓存 ---
    QImage m_latestRawFrame;       // 最新原始帧（全分辨率，未经软件处理）
    QString m_savePath;             // 存储路径（开发板: /mnt/nfs，虚拟机: .）

    // --- 操作按钮（两种模式共用）---
    QPushButton *m_btnCapture;     // 拍照
    QPushButton *m_btnRecord;      // 录像
    QPushButton *m_btnFreeze;      // 冻结
    QPushButton *m_btnRotate;      // 旋转

    // --- 模式切换相关控件 ---
    QPushButton *m_btnSwitchMode;     // "切换到临床模式"/"切换到调试模式"按钮
    QStackedWidget *m_ctrlStack;      // 面板容器：index 0=调试, 1=临床, 2=图像编辑
    QWidget *m_debugPanel;            // 调试面板
    QWidget *m_clinicalPanel;         // 临床面板
    QWidget *m_editPanel;             // 图像编辑面板

    // --- 模式切换按钮（三个固定按钮）---
    QPushButton *m_btnModeDebug;
    QPushButton *m_btnModeClinical;
    QPushButton *m_btnModeEdit;

    // --- 图像编辑模式控件 ---
    QPushButton *m_btnSelectImage;    // 图像选择
    QPushButton *m_btnSaveEdit;       // 保存
    QPushButton *m_btnDeleteImage;    // 删除
    QCheckBox *m_chkEditSharpen;      // 锐化
    QSlider *m_sliderEditSharpen;     // 锐化强度
    QCheckBox *m_chkEditDenoise;      // 降噪
    QCheckBox *m_chkEditEdge;         // 边缘检测
    QCheckBox *m_chkEditThreshold;    // 阈值分割
    QSlider *m_sliderEditThreshold;   // 阈值
    QImage m_editSourceImage;         // 编辑源图（原始全分辨率）
    QString m_editImagePath;          // 当前编辑图像的文件路径
    QComboBox *m_cboPreset;           // 预设下拉框（胃镜/肠镜/支气管镜）
    QPushButton *m_btnExportPreset;   // "导出为预设"按钮

    // --- 预设数据 ---
    QStringList m_presetNames;                  // 预设名称列表
    QList<ImageProcessor::Config> m_presetConfigs;  // 预设参数列表（与名称一一对应）

    // --- 状态显示 ---
    QLabel *m_lblStatus;  // 底部状态文字（"就绪"/"录像中..."/"预设: 胃镜模式"等）
    QLabel *m_lblFps;     // FPS 显示

    // --- 状态标志 ---
    bool m_frozen;              // 画面是否冻结
    int m_rotateAngle;          // 旋转角度（0/90/180/270）
    bool m_recording;           // 是否正在录像
    cv::VideoWriter m_videoWriter;  // OpenCV 视频写入器

    // --- 帧缓存（冻结时选最清晰帧）---
    static const int FRAME_BUFFER_SIZE = 8;  // 缓存最近 8 帧（约 0.27 秒 @30fps）
    std::deque<QImage> m_frameBuffer;        // 双端队列，新帧从尾部进，超出容量从头部丢弃

    // --- FPS 统计 ---
    int m_frameCount;           // 当前秒内的帧计数
    QElapsedTimer m_fpsTimer;   // 计时器，每秒重置一次

    // --- 畸变校正（需要 camera_calib.yml 标定文件）---
    cv::Mat m_undistortMap1;    // 畸变校正映射表 X（预计算，每帧查表）
    cv::Mat m_undistortMap2;    // 畸变校正映射表 Y
    bool m_undistortReady;      // 标定文件是否加载成功

    // --- 算法配置 ---
    // 调试模式：每帧从界面控件读取
    // 临床模式：由 onPresetSelected 一次性设置
    ImageProcessor::Config m_procConfig;
};

#endif // MAINWINDOW_H
