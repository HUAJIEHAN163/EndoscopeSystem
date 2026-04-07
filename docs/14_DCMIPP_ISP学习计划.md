# DCMIPP / ISP 学习与实践（建议插入 P8 与 P9 之间，作为 P8.5）

---

## 背景：当前流程 vs 加入 ISP 后的流程

### 当前项目流程（纯软件处理）

```
OV5640 Sensor
    ↓ YUYV 原始数据（未经任何处理）
DCMIPP（仅用采集接口，ISP 未启用）
    ↓
V4L2 驱动 → mmap 到用户空间
    ↓
OpenCV 软件处理（CPU 逐帧计算，占用高）
    ├── 白平衡（applyWhiteBalance）
    ├── CLAHE 增强
    ├── 去雾
    ├── 降噪
    ├── 锐化
    └── 畸变校正
    ↓
Qt 显示
```

**问题**：所有处理都靠 CPU，多个算法同时开启时帧率下降明显（临床模式卡顿就是这个原因）。

### 加入 ISP 后的流程（硬件 + 软件协作）

根据 OV5640 实际支持的参数（已在开发板上验证），实际协作流程为：

```
OV5640 Sensor
    ↓
硬件处理（Sensor 内部 ISP，不占 CPU）
    ├── 自动白平衡 (AWB)        ← 替代 applyWhiteBalance
    ├── 自动曝光 (AE)            ← 软件做不了
    ├── 自动增益 (AG)            ← 软件做不了
    └── 对比度/饱和度/色调        ← 基础色彩调节
    ↓ YUYV 数据
V4L2 → mmap → 用户空间
    ↓
OpenCV 软件处理（只做硬件做不了的）
    ├── CLAHE 增强
    ├── 去雾（暗通道先验）
    ├── 锐化                     ← OV5640 不支持硬件锐化
    ├── 降噪                     ← OV5640 不支持硬件降噪
    ├── 畸变校正
    └── 边缘检测 / 阈值分割
    ↓
Qt 显示
```

### 对比总结

| 对比项 | 当前（纯软件） | 加入 ISP 后 |
|--------|--------------|------------|
| 白平衡 | OpenCV 软件计算，每帧消耗 CPU | ISP 硬件实时完成，零 CPU 开销 |
| 曝光控制 | 无（固定曝光） | ISP 自动曝光，画面亮度稳定 |
| 增益控制 | 无 | ISP 自动增益，暗场景自动提亮 |
| 色彩调节 | 简单灰度世界法 | 硬件对比度/饱和度/色调，更精细 |
| 锐化 | GaussianBlur + addWeighted | 仍然全靠软件（OV5640 不支持硬件锐化） |
| 降噪 | bilateralFilter，非常慢 | 仍然全靠软件（OV5640 不支持硬件降噪） |
| CLAHE/去雾 | 软件实现 | 仍然需要软件实现（ISP 没有） |
| CPU 占用 | 高（所有算法都靠 CPU） | 降低（白平衡卸载给硬件） |

---

## 学习路径

### 阶段 1：了解 DCMIPP 架构（读文档）

STM32MP157 的 DCMIPP 模块包含：

```
DCMIPP 模块
├── DCMI 部分：图像采集接口（从 Sensor 接收数据）
└── ISP 部分：图像处理流水线
     ├── 去马赛克（Demosaic）
     ├── 黑电平校正（BLC）
     ├── 白平衡（AWB）
     ├── 色彩校正（CCM）
     ├── 伽马校正
     ├── 降噪（NR）
     ├── 锐化
     └── 自动曝光（AE）
```

注意：DCMIPP 的 ISP 能力和 Sensor 驱动暴露的参数不一定完全对应。实际可用的参数以 `v4l2-ctl --list-ctrls` 输出为准。

参考资料：
- ST 官方文档：STM32MP157 Reference Manual — DCMIPP 章节
- ST Wiki：https://wiki.st.com/stm32mpu/wiki/DCMIPP_internal_peripheral

### 阶段 2：命令行探索 ISP 参数（开发板实操）

#### OV5640 实际支持的参数（已在开发板上验证）

```bash
root@lubancat:~# v4l2-ctl -d /dev/video0 --list-ctrls
```

| 参数 | 范围 | 默认值 | 当前值 | 说明 |
|------|------|--------|--------|------|
| contrast | 0-255 | 0 | 0 | 对比度 |
| saturation | 0-255 | 64 | 64 | 饱和度 |
| hue | 0-359 | 0 | 0 | 色调 |
| white_balance_automatic | 0/1 | 1 | 1（开） | 自动白平衡 |
| red_balance | 0-4095 | 0 | 0 | 手动白平衡红色增益（关闭自动后可调） |
| blue_balance | 0-4095 | 0 | 0 | 手动白平衡蓝色增益（关闭自动后可调） |
| exposure | 0-65535 | 0 | 885 | 曝光值（关闭自动曝光后可调） |
| gain_automatic | 0/1 | 1 | 1（开） | 自动增益 |
| gain | 0-1023 | 0 | 176 | 增益（关闭自动增益后可调） |
| horizontal_flip | 0/1 | 0 | 0 | 水平翻转 |
| vertical_flip | 0/1 | 0 | 0 | 垂直翻转 |
| auto_exposure | 0/1 | 0 | 0 | 自动曝光 |
| power_line_frequency | 0-3 | 1 | 1 | 工频抗闪烁（50Hz/60Hz） |

**重要发现：**
- `flags=inactive` 表示当前不可调（因为自动模式开着）。关闭自动模式后才能手动调节
- **无 sharpness 参数** → OV5640 驱动不支持硬件锐化，锐化全靠软件
- **无硬件降噪参数** → 降噪也全靠软件

#### OV5640 能卸载给硬件的处理

| 功能 | 硬件支持 | 说明 |
|------|---------|------|
| 白平衡 | ✅ | 自动/手动红蓝增益，可替代软件 applyWhiteBalance |
| 曝光控制 | ✅ | 自动/手动，软件做不了 |
| 增益控制 | ✅ | 自动/手动，软件做不了 |
| 对比度/饱和度/色调 | ✅ | 基础色彩调节 |
| 翻转 | ✅ | 可替代软件旋转中的 180° 情况 |
| 锐化 | ❌ | 全靠软件 applySharpen |
| 降噪 | ❌ | 全靠软件 applyDenoise |
| CLAHE | ❌ | 全靠软件 |
| 去雾 | ❌ | 全靠软件 |
| 畸变校正 | ❌ | 全靠软件 |

#### 命令行调参示例

在开发板上试试这些命令，观察画面变化：

```bash
# 调对比度
v4l2-ctl -d /dev/video0 --set-ctrl=contrast=128

# 调饱和度
v4l2-ctl -d /dev/video0 --set-ctrl=saturation=128

# 关闭自动白平衡，手动调红蓝增益
v4l2-ctl -d /dev/video0 --set-ctrl=white_balance_automatic=0
v4l2-ctl -d /dev/video0 --set-ctrl=red_balance=2000
v4l2-ctl -d /dev/video0 --set-ctrl=blue_balance=2000

# 恢复自动白平衡
v4l2-ctl -d /dev/video0 --set-ctrl=white_balance_automatic=1

# 关闭自动增益，手动调增益
v4l2-ctl -d /dev/video0 --set-ctrl=gain_automatic=0
v4l2-ctl -d /dev/video0 --set-ctrl=gain=500

# 恢复自动增益
v4l2-ctl -d /dev/video0 --set-ctrl=gain_automatic=1

# 翻转画面
v4l2-ctl -d /dev/video0 --set-ctrl=horizontal_flip=1
v4l2-ctl -d /dev/video0 --set-ctrl=vertical_flip=1
```

**练习目标**：逐个调参，观察画面变化，理解每个参数的作用。

### 阶段 3：代码集成 — 在 Qt 界面中控制 ISP 参数

在 `v4l2capture.cpp` 中添加 ISP 参数控制方法：

```cpp
// 通过 V4L2 ioctl 设置 ISP 参数
void V4l2Capture::setExposure(int value) {
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = value;
    ioctl(m_fd, VIDIOC_S_CTRL, &ctrl);
}

void V4l2Capture::setAutoWhiteBalance(bool enable) {
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
    ctrl.value = enable ? 1 : 0;
    ioctl(m_fd, VIDIOC_S_CTRL, &ctrl);
}

void V4l2Capture::setGain(int value) {
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_GAIN;
    ctrl.value = value;
    ioctl(m_fd, VIDIOC_S_CTRL, &ctrl);
}

void V4l2Capture::setContrast(int value) {
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_CONTRAST;
    ctrl.value = value;
    ioctl(m_fd, VIDIOC_S_CTRL, &ctrl);
}

void V4l2Capture::setSaturation(int value) {
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_SATURATION;
    ctrl.value = value;
    ioctl(m_fd, VIDIOC_S_CTRL, &ctrl);
}
```

根据 OV5640 实际支持的参数，调试模式界面规划为：

```
┌─────────────────────────┐┌──────────────────────────┐
│ 硬件参数 (OV5640)       ││ 软件参数 (OpenCV)        │
│                         ││                          │
│ ☑ 自动曝光            ││ ☑ CLAHE 增强             │
│ 曝光 [====□===]       ││ [====□===] clipLimit   │
│ ☑ 自动增益            ││ ☑ 去雾                   │
│ 增益 [==□=====]       ││ ☑ 锐化                   │
│ ☑ 自动白平衡          ││ [====□===] 锐化强度   │
│ 红增益 [===□====]    ││ ☑ 降噪                   │
│ 蓝增益 [===□====]    ││ ☑ 畸变校正               │
│ 对比度 [===□====]    ││ ☐ 边缘检测               │
│ 饱和度 [===□====]    ││ ☐ 阈值分割               │
│ ☐ 水平翻转            ││                          │
│ ☐ 垂直翻转            ││                          │
└─────────────────────────┘└──────────────────────────┘
```

注意：硬件参数只在开发板 + 真实摄像头时可用，虚拟机 + 测试视频时硬件参数面板应置灰不可操作。

### 阶段 4：对比实验 — ISP 硬件 vs OpenCV 软件

| 实验 | 方法 | 观察点 |
|------|------|--------|
| 白平衡对比 | OV5640 AWB vs applyWhiteBalance | 色彩准确度、CPU 占用 |
| 综合对比 | 硬件白平衡+曝光 + 软件 CLAHE/去雾 vs 纯软件全做 | 帧率、画质、CPU 占用 |

注意：OV5640 不支持硬件锐化和降噪，这两项无法做对比实验，全靠软件。

```cpp
// 对比测试代码思路
// 方案 A：ISP 关闭，纯软件处理
setAutoWhiteBalance(false);
// processFrame 中开启所有 OpenCV 算法

// 方案 B：ISP 开启，软件只做高级算法
setAutoWhiteBalance(true);
// processFrame 中只开启 CLAHE、去雾、锐化、降噪，关闭白平衡
```

---

## 实施步骤

1. ~~在开发板上用 `v4l2-ctl --list-ctrls` 确认 DCMIPP 支持哪些参数~~ ✅ 已完成
2. 命令行逐个调参，截图记录效果
3. 在 `v4l2capture.h/cpp` 中封装 ISP 控制方法
4. 在调试模式界面添加 ISP 参数控件
5. 做对比实验，记录帧率和画质差异
6. 根据实验结果，确定最优的 ISP + 软件协作方案

---

## 目标要求

- [x] 能说清 DCMIPP 的组成（采集接口 + ISP 流水线）
- [x] 能用 v4l2-ctl 命令行查看支持的参数
- [ ] 能用 v4l2-ctl 命令行调节曝光、增益、白平衡并观察效果
- [ ] 能在代码中通过 ioctl 控制 ISP 参数
- [ ] 能解释 ISP 硬件处理和软件处理的分工与区别
- [ ] 完成 ISP vs OpenCV 的对比实验并记录结论

**建议时间：3-5 天**

---

## 两套参数调节界面设计

### 为什么需要两套参数

启用 ISP 后，图像处理分为硬件和软件两层，参数独立、互不干扰：

两套参数都支持动态调节，改了立刻看到效果：
- ISP 参数：通过 `ioctl(m_fd, VIDIOC_S_CTRL, &ctrl)` 实时生效
- 软件参数：通过现有的 `processFrame` 每帧读取控件状态，实时生效

### 实际产品中的意义

两套参数在实际产品中是标准做法，不仅仅是学习用途：

| 参数层 | 谁调 | 什么时候调 | 多久调一次 |
|--------|------|-----------|----------|
| ISP（曝光、增益、白平衡） | 硬件/BSP 工程师 | 出厂标定、换 Sensor 时 | 很少改 |
| 软件（CLAHE、去雾、降噪） | 算法/应用工程师 | 开发阶段调参 | 按场景切换预设 |

临床模式下两套参数都隐藏：
- ISP 用出厂默认值
- 软件用预设值（胃镜模式/肠镜模式等）

调试模式下两套参数都暴露：
- 硬件工程师调 ISP 参数，确保基础画质
- 算法工程师调软件参数，优化高级处理效果

这也是为什么预设文件后续可以扩展为两层：

```json
// config/presets/gastroscope.json 扩展版
{
    "name": "胃镜模式",
    "isp": {
        "autoExposure": true,
        "autoWhiteBalance": true,
        "gain": 40,
        "contrast": 64,
        "saturation": 80
    },
    "software": {
        "clahe": true,
        "claheClipLimit": 3.0,
        "dehaze": true,
        "dehazeOmega": 0.85,
        "sharpen": true,
        "sharpenAmount": 1.2,
        "denoise": true
    }
}
```

---

## ISP 与测试视频的关系

### ISP 无法处理测试视频

ISP 是硬件模块，只能处理从 Sensor 物理接口实时输入的数据流：

```
✅ 能处理： OV5640 Sensor → 物理接口 → DCMIPP ISP → 处理后的帧
❌ 不能处理：test.avi 文件 → 内存 → ？？？ ISP 接收不了文件数据
```

### 两种开发环境的差异

| 环境 | 视频源 | ISP | 软件处理 | 用途 |
|------|--------|-----|---------|------|
| 虚拟机 + 测试视频 | test.avi | ✖ 不可用 | ✔ 全部靠 OpenCV | 软件算法调参 |
| 开发板 + 摄像头 | OV5640 | ✔ 可用 | ✔ 只做高级算法 | 完整功能验证 |

### 参数迁移注意事项

在虚拟机上调好的软件参数，到开发板上可能需要微调：

```
虚拟机调参：
  输入 = 原始视频（未经 ISP 处理）
  软件白平衡 = 开
  软件降噪 = 开（强度高）

开发板调参：
  输入 = ISP 处理后的视频（已经做过白平衡和基础色彩调节）
  软件白平衡 = 关（ISP 已做）
  软件降噪 = 开（强度不变，因为 OV5640 没有硬件降噪）
  软件锐化 = 开（强度不变，因为 OV5640 没有硬件锐化）
```

所以预设文件建议区分环境：

```
config/presets/
├── gastroscope.json          ← 虚拟机用（纯软件处理）
├── gastroscope_isp.json      ← 开发板用（ISP + 软件协作）
├── colonoscope.json
├── colonoscope_isp.json
└── ...
```

---

## 开发工作流建议

```
第一步：虚拟机 + 测试视频
  └→ 调试软件算法参数（CLAHE、去雾、降噪等）
  └→ 导出预设文件

第二步：开发板 + 真实摄像头
  └→ 调试 ISP 参数（曝光、增益、白平衡、对比度、饱和度）
  └→ 微调软件参数（ISP 已做白平衡，软件白平衡可关闭；锐化和降噪强度不变）
  └→ 导出开发板专用预设文件

第三步：对比实验
  └→ 纯软件 vs ISP+软件，记录帧率和画质差异
  └→ 确定最优协作方案
```

---

## 整体规划总览更新建议

在原表格 P8 和 P9 之间插入：

```
| P8.5 | DCMIPP/ISP 学习与实践 | 3-5 天 | 硬件图像处理 | ⬜ 未开始 |
```
