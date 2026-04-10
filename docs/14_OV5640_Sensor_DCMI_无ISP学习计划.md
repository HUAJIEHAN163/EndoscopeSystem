# DCMIPP / ISP 学习与实践（建议插入 P8 与 P9 之间，作为 P8.5）

---

## 硬件验证结果（2025-01 实测）

### 实际硬件配置

通过系统命令验证，本项目使用的开发板配置为：

```
开发板: 野火鲁班猫 STM32MP157 (Embedfire LubanCat)
芯片: STM32MP157 (可能是 A/C 型号)
摄像头接口: DCMI (Digital Camera Memory Interface)
驱动: stm32-dcmi (不是 stm32-dcmipp)
摄像头: OV5640 (内置基础 ISP)
```

**关键发现：**
- ❌ 本开发板**没有 DCMIPP（带 ISP 的增强版）**，只有 DCMI（纯采集接口）
- ✅ 所有硬件图像处理能力来自 **OV5640 Sensor 内置 ISP**
- ✅ DCMI 只负责数据传输，不做任何图像处理

### 实际数据流

```
OV5640 Sensor (内置 ISP)
    ↓ 内部处理：白平衡、曝光、增益、对比度、饱和度
    ↓ 输出 YUYV 4:2:2
DCMI (纯采集接口，无任何图像处理)
    ↓ 直通传输
V4L2 驱动 (stm32-dcmi)
    ↓ mmap 到用户空间
用户程序 (endoscope)
    ↓ OpenCV 软件处理：CLAHE、去雾、锐化、降噪等
Qt 显示
```

### 如何验证硬件配置（调试方法论）

**为什么要验证？**
- 文档和芯片手册可能与实际硬件不符
- 不同开发板、不同内核版本配置不同
- 只有实测才能确定真实能力

**验证步骤：**

#### 1. 查看驱动名称（最直接的证据）

```bash
v4l2-ctl -d /dev/video0 --all | grep "Driver name"
```

**输出：**
```
Driver name      : stm32-dcmi
Card type        : STM32 Camera Memory Interface
```

**为什么重要？**
- `stm32-dcmi` = 只有 DCMI（无 ISP）
- `stm32-dcmipp` = 有 DCMIPP（带 ISP）
- 驱动名称直接反映硬件配置

#### 2. 查看设备树 compatible 字符串（硬件配置的源头）

```bash
cat /sys/firmware/devicetree/base/soc/dcmi@4c006000/compatible
```

**输出：**
```
st,stm32-dcmi
```

**为什么重要？**
- 设备树定义了硬件配置
- `st,stm32-dcmi` 确认是 DCMI，不是 DCMIPP
- 这是内核加载驱动的依据

#### 3. 检查内核配置（驱动是否编译）

```bash
zcat /proc/config.gz | grep -i dcmi
```

**输出：**
```
CONFIG_VIDEO_STM32_DCMI=y
```

```bash
zcat /proc/config.gz | grep -i dcmipp
```

**输出：**
```
(无输出)
```

**为什么重要？**
- `CONFIG_VIDEO_STM32_DCMI=y` = DCMI 驱动编译进内核
- 没有 `CONFIG_VIDEO_STM32_DCMIPP` = 内核不支持 DCMIPP
- 即使硬件有 DCMIPP，内核没编译也用不了

#### 4. 查看 video 设备数量（ISP 通常有多个输出）

```bash
ls -l /dev/video*
```

**输出：**
```
crw-rw---- 1 root video 81, 0 Apr  9 18:51 /dev/video0
```

**为什么重要？**
- 只有 1 个 video 设备 = 简单采集模式
- 如果有 DCMIPP ISP，通常会有多个设备：
  - `/dev/video0` - 原始输出
  - `/dev/video1` - ISP 处理后输出
  - `/dev/video2` - 统计信息输出

#### 5. 查看可用控制参数（确定硬件能力）

```bash
v4l2-ctl -d /dev/video0 --list-ctrls
```

**输出分析：**
```
User Controls
                       contrast 0x00980901 (int)    : min=0 max=255 ...
                     saturation 0x00980902 (int)    : min=0 max=255 ...
        white_balance_automatic 0x0098090c (bool)   : default=1 value=1
                       exposure 0x00980911 (int)    : min=0 max=65535 ...
                           gain 0x00980913 (int)    : min=0 max=1023 ...
```

**关键观察：**
- ✅ 有白平衡、曝光、增益 → OV5640 Sensor ISP 提供
- ❌ 没有 sharpness（锐化）→ 硬件不支持
- ❌ 没有 noise_reduction（降噪）→ 硬件不支持

**为什么重要？**
- 这些参数决定了哪些功能可以卸载给硬件
- 没有的参数必须用软件实现

#### 6. 查看开发板型号

```bash
cat /sys/firmware/devicetree/base/model
```

**输出：**
```
Embedfire STM32MP157 Star LubanCat Robot S1 Board
```

**为什么重要？**
- 确认开发板厂商和型号
- 不同厂商的 BSP 配置可能不同

#### 7. 查看内核日志（驱动加载信息）

```bash
dmesg | grep -i "dcmi\|ov5640" | head -20
```

**输出：**
```
[    2.482699] ov5640 1-003c: Linked as a consumer to regulator.6
[    2.558873] stm32-dcmi 4c006000.dcmi: Probe done
[    xxx.xxx] stm32-dcmi 4c006000.dcmi: Some errors found while streaming: errors=3721 (overrun=3724)
```

**为什么重要？**
- 确认驱动加载成功
- `overrun` 错误说明 CPU 处理速度跟不上采集速度（性能瓶颈）

### 验证结论

| 项目 | 预期（文档） | 实际（验证） | 结论 |
|------|------------|------------|------|
| 硬件模块 | DCMIPP（带 ISP） | DCMI（无 ISP） | ❌ 文档标题有误导 |
| 驱动名称 | stm32-dcmipp | stm32-dcmi | ✅ 实测为准 |
| ISP 能力 | DCMIPP 提供 | OV5640 提供 | ✅ 来自 Sensor |
| 硬件白平衡 | ✅ 支持 | ✅ 支持 | ✅ 可用 |
| 硬件锐化 | ❓ 不确定 | ❌ 不支持 | ✅ 必须软件实现 |
| 硬件降噪 | ❓ 不确定 | ❌ 不支持 | ✅ 必须软件实现 |

---

## 背景：当前流程 vs 加入 ISP 后的流程

### 当前项目流程（纯软件处理）

```
OV5640 Sensor
    ↓ YUYV 原始数据（实际已经过 Sensor 内部 ISP 处理）
DCMI（仅用采集接口，无任何图像处理）
    ↓ 直通传输
V4L2 驱动 (stm32-dcmi) → mmap 到用户空间
    ↓
OpenCV 软件处理（CPU 逐帧计算，占用高）
    ├── 白平衡（applyWhiteBalance）      ← 实际 OV5640 已做，软件重复处理
    ├── CLAHE 增强
    ├── 去雾
    ├── 降噪
    ├── 锐化
    └── 畸变校正
    ↓
Qt 显示
```

**问题**：
1. 所有高级处理都靠 CPU，多个算法同时开启时帧率下降明显（临床模式卡顿）
2. 软件白平衡与 OV5640 硬件白平衡重复，浪费 CPU（~9ms/帧）

### 优化后的流程（硬件 + 软件协作）

根据 OV5640 实际支持的参数（已在开发板上验证），优化后的协作流程为：

```
OV5640 Sensor
    ↓
硬件处理（Sensor 内部 ISP，不占 CPU）
    ├── 自动白平衡 (AWB)        ← 替代软件 applyWhiteBalance
    ├── 自动曝光 (AE)            ← 软件做不了
    ├── 自动增益 (AG)            ← 软件做不了
    └── 对比度/饱和度/色调        ← 基础色彩调节
    ↓ YUYV 数据（已经过基础 ISP 处理）
DCMI（纯采集接口，直通传输）
    ↓
V4L2 驱动 (stm32-dcmi) → mmap → 用户空间
    ↓
OpenCV 软件处理（只做硬件做不了的）
    ├── ❌ 白平衡（关闭，OV5640 已做）
    ├── CLAHE 增强               ← 硬件不支持，必须软件实现
    ├── 去雾（暗通道先验）        ← 硬件不支持，必须软件实现
    ├── 锐化                     ← OV5640 不支持，必须软件实现
    ├── 降噪                     ← OV5640 不支持，必须软件实现
    ├── 畸变校正                 ← 硬件不支持，必须软件实现
    └── 边缘检测 / 阈值分割       ← 硬件不支持，必须软件实现
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

**执行命令：**
```bash
root@lubancat:~# v4l2-ctl -d /dev/video0 --list-ctrls
```

**实际输出：**
```
User Controls

                       contrast 0x00980901 (int)    : min=0 max=255 step=1 default=0 value=0 flags=slider
                     saturation 0x00980902 (int)    : min=0 max=255 step=1 default=64 value=64 flags=slider
                            hue 0x00980903 (int)    : min=0 max=359 step=1 default=0 value=0 flags=slider
        white_balance_automatic 0x0098090c (bool)   : default=1 value=1 flags=update
                    red_balance 0x0098090e (int)    : min=0 max=4095 step=1 default=0 value=0 flags=inactive, slider
                   blue_balance 0x0098090f (int)    : min=0 max=4095 step=1 default=0 value=0 flags=inactive, slider
                       exposure 0x00980911 (int)    : min=0 max=65535 step=1 default=0 value=885 flags=inactive, volatile
                 gain_automatic 0x00980912 (bool)   : default=1 value=1 flags=update
                           gain 0x00980913 (int)    : min=0 max=1023 step=1 default=0 value=248 flags=inactive, volatile
                horizontal_flip 0x00980914 (bool)   : default=0 value=0
                  vertical_flip 0x00980915 (bool)   : default=0 value=0
           power_line_frequency 0x00980918 (menu)   : min=0 max=3 default=1 value=1

Camera Controls

                  auto_exposure 0x009a0901 (menu)   : min=0 max=1 default=0 value=0 flags=update

Image Processing Controls

                 link_frequency 0x009f0901 (intmenu): min=0 max=0 default=0 value=0
                   test_pattern 0x009f0903 (menu)   : min=0 max=4 default=0 value=0
```

**参数解读表格：**

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
| gain | 0-1023 | 0 | 248 | 增益（关闭自动增益后可调） |
| horizontal_flip | 0/1 | 0 | 0 | 水平翻转 |
| vertical_flip | 0/1 | 0 | 0 | 垂直翻转 |
| auto_exposure | 0/1 | 0 | 0 | 自动曝光 |
| power_line_frequency | 0-3 | 1 | 1 | 工频抗闪烁（50Hz/60Hz） |

**重要发现：**
- `flags=inactive` 表示当前不可调（因为自动模式开着）。关闭自动模式后才能手动调节
- **无 sharpness 参数** → OV5640 驱动不支持硬件锐化，锐化全靠软件
- **无硬件降噪参数** → 降噪也全靠软件

**为什么要关注 `flags` 字段？**

`flags` 字段告诉我们参数的当前状态：

| Flag | 含义 | 示例 |
|------|------|------|
| `inactive` | 当前不可调（被其他参数禁用） | `red_balance` 在 `white_balance_automatic=1` 时不可调 |
| `volatile` | 值会自动变化（由硬件控制） | `exposure` 在自动曝光开启时会自动调整 |
| `update` | 修改此参数会影响其他参数 | `white_balance_automatic=0` 会激活 `red_balance` 和 `blue_balance` |
| `slider` | 适合用滑块控件显示 | 连续值参数 |

**为什么要查找缺失的参数？**

如果某个功能在 `--list-ctrls` 中**没有对应参数**，说明：
1. 硬件不支持这个功能
2. 驱动没有暴露这个参数
3. 必须用软件实现

常见缺失的参数：
- `sharpness` - 锐化
- `noise_reduction` - 降噪
- `brightness` - 亮度
- `gamma` - 伽马校正

这就是为什么我们的项目中 CLAHE、去雾、锐化、降噪全部需要软件实现。

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
| P8.5 | OV5640 Sensor ISP 学习与实践 | 3-5 天 | 硬件参数调节 | ⬜ 未开始 |
```

注：标题从 "DCMIPP/ISP" 改为 "OV5640 Sensor ISP"，更准确反映实际情况。

---

## 学习总结

### 核心收获

1. **掌握了嵌入式 Linux 硬件验证方法**
   - 通过 `v4l2-ctl` 查看驱动和参数
   - 通过设备树确认硬件配置
   - 通过内核配置确认驱动支持
   - 通过 `dmesg` 查看运行状态

2. **理解了硬件 ISP 的分层架构**
   - Sensor 内置 ISP（OV5640）
   - SoC ISP（DCMIPP，本项目无）
   - 软件 ISP（OpenCV）

3. **明确了硬件和软件的分工**
   - 硬件：白平衡、曝光、增益、基础色彩
   - 软件：CLAHE、去雾、锐化、降噪、畸变校正

4. **学会了通过 V4L2 ioctl 控制硬件参数**
   - `VIDIOC_S_CTRL` 设置参数
   - `VIDIOC_G_CTRL` 读取参数
   - 理解 `flags` 字段的含义

### 常见误区

#### 误区 1：混淆 DCMI 和 DCMIPP

❌ **错误认知：**
- "STM32MP157 有 DCMIPP，所以我的开发板有 ISP"

✅ **正确理解：**
- STM32MP157 有多个子型号，A/C 只有 DCMI，D/F 才有 DCMIPP
- 即使芯片有 DCMIPP，设备树和内核也必须配置正确
- **必须通过实际命令验证**

#### 误区 2：认为所有 ISP 功能都在 SoC 上

❌ **错误认知：**
- "ISP 是 STM32MP157 提供的"

✅ **正确理解：**
- 大部分摄像头 Sensor 内置基础 ISP（白平衡、曝光、增益）
- SoC ISP 提供更高级的功能（锐化、降噪、畸变校正）
- 两者是协作关系，不是替代关系

#### 误区 3：相信文档而不验证

❌ **错误认知：**
- "手册说有 DCMIPP，所以一定有"
- "文档说支持锐化，所以一定支持"

✅ **正确理解：**
- 芯片手册描述的是理论能力
- 实际开发板可能用了低配型号
- BSP 可能没有启用所有功能
- **必须通过 `v4l2-ctl --list-ctrls` 实测**

#### 误区 4：认为启用硬件 ISP 后软件算法全部可以删除

❌ **错误认知：**
- "启用 ISP 后，OpenCV 的 CLAHE、去雾、锐化、降噪都可以删除"

✅ **正确理解：**
- 只有 `v4l2-ctl --list-ctrls` 中**有对应参数**的功能才能卸载给硬件
- 本项目只有白平衡可以卸载，其他全部保留
- 即使有 DCMIPP，CLAHE 和去雾也需要软件实现

### 实际产品中的区别

| 项目 | 本项目 | 医疗级内稥镜 |
|------|---------|---------------|
| Sensor | OV5640（消费级） | Sony IMX290/327（医疗级） |
| Sensor ISP | 白平衡、曝光、增益 | 白平衡、曝光、增益、锐化、降噪、3D 降噪 |
| SoC ISP | 无（DCMI） | Ambarella CV / TI DaVinci |
| 软件算法 | CLAHE、去雾、锐化、降噪 | 只做拍照后处理，实时显示不做 |
| 频闪照明 | 无 | M4 核心控制 LED PWM（微秒级同步） |
| 成本 | ~2000 元 | 10万 - 100万元 |

### 后续优化方向

在当前硬件配置下，性能优化的主要方向：

1. **启用 OV5640 自动白平衡**（立即做）
   - 省掉软件白平衡的 9ms
   - FPS 提升 20-25%

2. **NEON 指令集加速**（P5-P6）
   - YUYV→RGB 转换加速 3-5x
   - 高斯滞波加速 2-4x
   - CLAHE 直方图统计加速 2-3x

3. **多线程流水线**（P3）
   - 采集线程、处理线程、渲染线程分离
   - 环形 Buffer 队列

4. **内存池**（P4）
   - 预分配 cv::Mat，循环复用
   - 避免每帧动态分配

5. **解决 DCMI overrun 错误**
   - 增加 V4L2 buffer 数量（2 → 4）
   - 给 CPU 更多缓冲时间

**不要期望：**
- ❌ 通过软件升级获得 DCMIPP ISP（硬件不支持）
- ❌ 硬件锐化和降噪（OV5640 不支持）
- ❌ 达到医疗级内稥镜的性能（硬件差距太大）
