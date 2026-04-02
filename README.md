# EndoscopeSystem

基于 STM32MP157 + OV5640 的单机内窥镜图像处理系统。

## 技术栈

Qt5 + OpenCV + V4L2，运行于嵌入式 Linux (Cortex-A7)。

## 功能

- V4L2 摄像头实时采集 (640x480 YUYV @30fps)
- 内窥镜图像处理: CLAHE增强、畸变校正、去雾、白平衡
- 通用图像处理: 锐化、降噪、边缘检测、阈值分割
- 拍照、录像、冻结画面、图像旋转
- 硬件参数调节: 曝光、增益、翻转、白平衡

## 构建

```bash
# 虚拟机本地构建
bash scripts/build_local.sh

# 交叉编译 (ARM)
mkdir build_arm && cd build_arm
cmake .. -DCMAKE_TOOLCHAIN_FILE=../scripts/arm_toolchain.cmake \
         -DQt5_DIR=/path/to/qt5_arm/lib/cmake/Qt5 \
         -DOpenCV_DIR=/path/to/opencv_arm/lib/cmake/opencv4
make -j$(nproc)
```

## 运行

```bash
# 有摄像头时自动使用 V4L2
./endoscope

# 无摄像头时使用测试视频 (配置 config/endoscope.conf)
./endoscope
```

## 项目结构

```
src/
├── main.cpp                    # 入口
├── capture/
│   ├── videosource.h           # 视频源抽象接口
│   ├── v4l2capture.h/cpp       # V4L2 采集 (开发板)
│   └── filecapture.h/cpp       # 视频文件输入 (虚拟机测试)
├── processing/
│   └── imageprocessor.h/cpp    # 图像处理算法集合
├── ui/
│   └── mainwindow.h/cpp        # Qt 主界面
└── utils/
    └── imageconvert.h/cpp      # QImage ↔ cv::Mat 转换
```
