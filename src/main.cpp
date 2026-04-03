#include <QApplication>    // Qt 应用程序核心类，管理事件循环和全局设置
#include "ui/mainwindow.h"  // 主窗口类

/*
# 例1：无额外参数
./build/endoscope
# argc = 1, argv[0] = "./build/endoscope"

# 例2：带参数
./build/endoscope --fullscreen --device /dev/video1
# argc = 4
# argv[0] = "./build/endoscope"
# argv[1] = "--fullscreen"
# argv[2] = "--device"
# argv[3] = "/dev/video1"
*/
int main(int argc, char *argv[]) {
    // 创建 Qt 应用实例，argc/argv 用于接收命令行参数
    QApplication app(argc, argv);
    app.setApplicationName("EndoscopeSystem");

    // 创建并显示主窗口，构造函数中会初始化 UI 和视频源
    MainWindow window;
    window.show();
/*
    // 进入 Qt 事件循环，程序在此阻塞，直到窗口关闭才返回
    // app.exec() 内部简化理解
    while (没有收到退出信号) {
    等待事件();        // 没事件时阻塞在这里
    处理事件();        // 鼠标点击、按键、定时器、帧到达...
}
return 退出码;
*/
    return app.exec();
}
