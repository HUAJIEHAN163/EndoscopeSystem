#ifndef VIDEOSOURCE_H  // 头文件保护，防止重复包含
#define VIDEOSOURCE_H

#include <QThread>
#include <QImage>
#include <atomic>
#include "utils/framequeue.h"
#include <opencv2/core.hpp>

// =====================================================================
// VideoSource — 视频源抽象基类
//
// 设计模式：抽象接口（类似 C 语言的函数指针表）
// 定义了视频源的统一接口，具体实现由子类完成：
//   - V4l2Capture：从摄像头采集（开发板用）
//   - FileCapture：从视频文件读取（虚拟机测试用）
//
// 继承自 QThread，每个视频源在独立线程中运行，不阻塞主界面
//
// 数据流：
//   子类的 run() 循环读帧 → emit frameReady(image) → 主线程 onFrameReady 处理
//
// 为什么用抽象基类：
//   MainWindow 只持有 VideoSource* 指针，不关心具体是摄像头还是文件
//   切换视频源时只需 new 不同的子类，其他代码不用改（多态）
// =====================================================================
class VideoSource : public QThread {
    // Q_OBJECT：启用信号槽机制，frameReady 和 errorOccurred 信号需要它
    Q_OBJECT

public:
    // 构造函数
    // parent：Qt 父对象，用于内存管理（父对象销毁时自动删除子对象）
    // m_running 初始化为 false，调用 start() 后在 run() 中设为 true
    explicit VideoSource(QObject *parent = nullptr) : QThread(parent), m_running(false) {}

    // 虚析构函数
    // 基类析构函数必须是 virtual，否则通过基类指针 delete 子类对象时
    // 子类的析构函数不会被调用，导致资源泄漏
    // 例如：VideoSource *src = new V4l2Capture(...); delete src;
    //       如果不是 virtual，V4l2Capture 的析构函数不会执行
    virtual ~VideoSource() {}

    // ========== 纯虚函数（= 0）==========
    // 纯虚函数没有实现，子类必须重写，否则编译报错
    // 这就是"抽象接口"的含义——基类定义接口，子类提供实现

    // 打开视频源（摄像头设备 或 视频文件）
    // 返回 true 表示成功，false 表示失败
    virtual bool open() = 0;

    // 关闭视频源，释放资源
    virtual void close() = 0;

    // 获取视频宽高（录像时需要知道分辨率）
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;

    // ========== 普通方法（基类提供默认实现）==========

    // 暂停采集：线程保持运行但不处理帧
    void pause() { m_running = false; }

    // 恢复采集
    void resume() { m_running = true; }

    // 停止线程：线程完全退出
    void stop() { m_quit = true; m_running = false; }

    bool isRunning() const { return m_running; }

signals:
    // ========== 信号（由子类在 run() 中 emit）==========
    // 信号只有声明，没有实现，MOC 工具会自动生成实现代码

    // 新帧就绪信号
    // 子类每读到一帧就 emit frameReady(image)
    // MainWindow 通过 connect 连接到 onFrameReady 槽函数处理
    // const QImage& ：引用传递，避免拷贝大图像数据
    void frameReady(const QImage &image);

    // 错误信号
    // 采集出错时 emit errorOccurred("错误描述")
    // MainWindow 收到后显示在状态栏
    void errorOccurred(const QString &msg);

protected:
    // ========== 受保护成员（子类可访问，外部不可访问）==========

    // 运行标志：控制采集线程的启停
    // std::atomic<bool>：线程安全的 bool
    //   - 主线程调 stop() 写 false
    //   - 采集线程的 run() 循环读取
    //   - atomic 保证写入后采集线程立刻能读到新值
    //
    // 子类 run() 的典型用法：
    //   void V4l2Capture::run() {
    //       m_running = true;
    //       while (m_running) {   // 每次循环检查
    //           读一帧 → emit frameReady(image);
    //       }
    //   }
    std::atomic<bool> m_running;
    std::atomic<bool> m_quit{false};

public:
    // 旧接口（兼容，后续可删除）
    std::atomic<int> m_pendingFrames{0};

    // 三线程管线：采集线程写入此队列，处理线程读取
    FrameQueue<4> *m_captureQueue = nullptr;
};

#endif // VIDEOSOURCE_H
