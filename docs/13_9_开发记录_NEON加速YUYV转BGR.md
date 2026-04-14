# 13_9 开发记录：NEON 加速 YUYV→BGR（P5）

## 一、背景

### 问题来源

FPS 性能总跟踪（13_8）第五章根因分析表明，当前 FPS 瓶颈是 **Cortex-A7 单核 CPU 上三个线程争抢时间片**。要突破瓶颈必须降低 CPU 总负载，而不是在线程间挪移工作量。

采集线程的 `cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV)` 是 CPU 占用最大的单一操作：

| 场景 | YUYV→BGR 耗时 | 数据来源 |
|---|---|---|
| 不开算法 | 17-33ms | 13_6 第九章 |
| 开 CLAHE | 41-47ms（被处理线程拖慢） | 13_6 第九章 |
| P7.1 后 | 17-55ms（平均 ~40ms） | 13_7 开发板日志 |

640×480 YUYV = 614400 字节输入，逐像素做浮点 YUV→BGR 转换，在 650MHz Cortex-A7 上非常慢。

### 为什么 P5 是最高优先级

1. 采集线程是最上游，它变快后释放的 CPU 时间片惠及所有线程
2. YUYV→BGR 是纯像素操作（逐像素乘加），天然适合 SIMD 向量化
3. 同时解决 overrun 问题（采集线程更快归还 V4L2 buffer）
4. 预计从 ~40ms 降到 ~10ms，释放 ~30ms CPU/帧

### 预期收益

| 场景 | 当前 FPS | P5 后预期 |
|---|---|---|
| 不开算法 | 17-20fps | ~25fps |
| 开 CLAHE | ~13fps | ~15fps |
| 开 CLAHE+去雾 | ~5fps | ~6fps |

---

## 二、设计方案

### 2.1 YUYV 格式与转换公式

YUYV（YUV 4:2:2 packed）每 4 字节编码 2 个像素：

```
字节:  Y0  U0  Y1  V0  Y2  U1  Y3  V1  ...
像素:  [pix0]  [pix1]  [pix2]  [pix3]  ...

pix0 = (Y0, U0, V0)
pix1 = (Y1, U0, V0)  ← 共享 U0, V0
pix2 = (Y2, U1, V1)
pix3 = (Y3, U1, V1)  ← 共享 U1, V1
```

BT.601 YUV→RGB 转换公式：

```
R = Y + 1.402 × (V - 128)
G = Y - 0.344 × (U - 128) - 0.714 × (V - 128)
B = Y + 1.772 × (U - 128)
```

### 2.2 定点化

浮点乘法在 Cortex-A7 上很慢。将系数 ×128 转为整数运算，最后右移 7 位：

```
R = Y + (179 × (V - 128)) >> 7      // 1.402 × 128 ≈ 179
G = Y - (44 × (U - 128) + 91 × (V - 128)) >> 7   // 0.344×128≈44, 0.714×128≈91
B = Y + (227 × (U - 128)) >> 7      // 1.772 × 128 ≈ 227
```

### 2.3 NEON 向量化策略

每次处理 8 像素 = 16 字节 YUYV 输入，24 字节 BGR 输出：

```
1. vld1q_u8:    加载 16 字节 YUYV
2. vuzp_u8:     分离 Y（偶数位）和 UV（奇数位）
3. 手动展开:    U/V 各复制到相邻像素位置（每个 U/V 对应 2 像素）
4. vmovl_u8:    uint8 → int16（扩展到 16 位做乘法）
5. vmulq_n_s16: 定点乘法（×179, ×44, ×91, ×227）
6. vshrq_n_s16: 右移 7 位
7. vqmovun_s16: clamp 到 [0,255] 并转回 uint8
8. vst3_u8:     交织存储为 BGR
```

### 2.4 改动范围

| 文件 | 改动 |
|---|---|
| `src/processing/neon_accel.h` | 新增：NEON 加速函数声明 |
| `src/processing/neon_accel.cpp` | 新增：NEON YUYV→BGR 实现 |
| `src/capture/v4l2capture.cpp` | cvtColor 替换为 NeonAccel::yuyvToBGR |
| `CMakeLists.txt` | 添加 neon_accel.h/cpp |

---

## 三、实施步骤

- [x] Step 1：新增 neon_accel.h/cpp
- [x] Step 2：实现 NEON YUYV→BGR
- [x] Step 3：v4l2capture.cpp 替换 cvtColor
- [x] Step 4：虚拟机编译验证（回退路径）
- [x] Step 5：ARM 交叉编译验证
- [x] Step 6：部署到 NFS
- [ ] Step 7：开发板验证

---

## 四、代码修改详细记录

### 4.1 Step 1：新增 neon_accel.h

新增文件：`src/processing/neon_accel.h`

```cpp
#ifndef NEON_ACCEL_H
#define NEON_ACCEL_H

#include <opencv2/core.hpp>

namespace NeonAccel {

// NEON 加速 YUYV→BGR 转换
// src: 640×480 CV_8UC2 (YUYV packed)
// dst: 640×480 CV_8UC3 (BGR)
void yuyvToBGR(const cv::Mat &src, cv::Mat &dst);

}

#endif // NEON_ACCEL_H
```

### 4.2 Step 2：新增 neon_accel.cpp

新增文件：`src/processing/neon_accel.cpp`

**平台分支：**
- `#ifdef __ARM_NEON`：ARM 平台使用 NEON intrinsic 实现
- `#else`：非 ARM 平台（虚拟机）回退到 `cv::cvtColor`

**NEON 实现核心循环（每次 8 像素）：**

```cpp
// 加载 16 字节 YUYV
uint8x16_t raw = vld1q_u8(yuyv);

// 分离 Y 和 UV
uint8x8x2_t deinterleaved = vuzp_u8(vget_low_u8(raw), vget_high_u8(raw));
uint8x8_t y_vals = deinterleaved.val[0];   // 8 个 Y
uint8x8_t uv_vals = deinterleaved.val[1];  // 4 个 U + 4 个 V 交替

// U/V 展开到 8 像素（每个 U/V 复制到相邻 2 像素）
// ... 手动展开 ...

// 定点 YUV→BGR
int16x8_t r = vaddq_s16(yy, vshrq_n_s16(vmulq_n_s16(v_off, 179), 7));
int16x8_t g = vsubq_s16(yy, vshrq_n_s16(
    vaddq_s16(vmulq_n_s16(u_off, 44), vmulq_n_s16(v_off, 91)), 7));
int16x8_t b = vaddq_s16(yy, vshrq_n_s16(vmulq_n_s16(u_off, 227), 7));

// clamp + 交织存储 BGR
uint8x8x3_t bgr_out = { vqmovun_s16(b), vqmovun_s16(g), vqmovun_s16(r) };
vst3_u8(bgr, bgr_out);
```

**尾部处理（不足 8 像素）：**

用标量代码处理，同样使用定点公式，保证结果一致。

**完整代码见 `src/processing/neon_accel.cpp`。**

### 4.3 Step 3：v4l2capture.cpp 替换 cvtColor

**新增 include：**

```cpp
#include "processing/neon_accel.h" // P5 NEON 加速
```

**改动前：**

```cpp
        // YUYV → BGR
        cv::Mat yuyv(m_height, m_width, CV_8UC2, data);
        cv::Mat bgr;
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
```

**改动后：**

```cpp
        // YUYV → BGR（P5: NEON 加速，非 ARM 平台回退到 OpenCV）
        cv::Mat yuyv(m_height, m_width, CV_8UC2, data);
        cv::Mat bgr;
        NeonAccel::yuyvToBGR(yuyv, bgr);
```

### 4.4 Step 4-5：CMakeLists.txt

**SOURCES 新增：**

```cmake
    src/processing/neon_accel.cpp
```

**HEADERS 新增：**

```cmake
    src/processing/neon_accel.h
```

### 4.5 编译验证

| 平台 | 结果 | 说明 |
|---|---|---|
| 虚拟机本地（x86） | ✅ 编译通过 | 走 `#else` 回退路径，调用 cv::cvtColor |
| ARM 交叉编译 | ✅ 编译通过 | 走 `#ifdef __ARM_NEON` 路径，使用 NEON intrinsic |

---

## 五、设计决策记录

### 5.1 为什么用 NEON intrinsic 而不是内联汇编

NEON intrinsic（`arm_neon.h` 提供的 C 函数）vs 内联汇编：

| | intrinsic | 内联汇编 |
|---|---|---|
| 可读性 | 好，类似 C 函数调用 | 差，汇编语法 |
| 编译器优化 | 编译器可以做寄存器分配和指令调度 | 完全手动控制 |
| 可维护性 | 好 | 差 |
| 性能 | 接近手写汇编（编译器优化后） | 理论最优 |
| 可移植性 | AArch32/AArch64 通用 | 需要分别写 |

对于本项目，intrinsic 的性能已经足够（预期 3-5x 提升），不需要追求极致的内联汇编。

### 5.2 为什么 U/V 展开用标量而不是 NEON

当前实现中，U/V 从 4 个值展开到 8 个值（每个复制到相邻像素）使用了 `vst1_u8` + 标量数组 + `vld1_u8` 的方式，中间经过了内存。

更优的方式是用 `vzip_u8` 在寄存器内完成展开，避免内存往返。但当前实现的 U/V 交替排列（U0 V0 U1 V1...）需要先分离 U 和 V 再各自 zip，指令数不一定更少。

**先用当前方案上板测试，如果 U/V 展开成为瓶颈再优化。**

### 5.3 定点精度分析

| 系数 | 精确值 | ×128 定点 | 误差 |
|---|---|---|---|
| 1.402 | 179.456 | 179 | -0.25% |
| 0.344 | 44.032 | 44 | -0.07% |
| 0.714 | 91.392 | 91 | -0.43% |
| 1.772 | 226.816 | 227 | +0.08% |

最大误差 0.43%，对应像素值偏差 ≤1，肉眼不可见。

### 5.4 风险评估

**风险 1：颜色偏差**

NEON 定点公式与 OpenCV cvtColor 的浮点公式可能有 ±1 的像素值差异。在内窥镜场景下不影响诊断，但如果后续需要精确颜色匹配（如与标准色卡对比），需要验证。

**风险 2：U/V 展开的内存往返开销**

当前 U/V 展开经过 `vst1_u8` → 标量数组 → `vld1_u8`，有一次内存写+读。如果 L1 cache 命中（大概率），开销约 2-3 个周期/次，8 像素一次，可接受。如果实测发现这里是瓶颈，可改用纯寄存器操作。

---

## 六、开发板验证结果

### 6.1 v1 实测结果（失败，已撤回）

v1 用 `vld1q_u8` + `vuzp` 分离 Y/UV，U/V 展开用标量内存往返（`vst1_u8` → 标量数组 → `vld1_u8`）。

| 指标 | OpenCV cvtColor | NEON v1 | 变化 |
|---|---|---|---|
| YUYV→BGR 平均耗时 | ~40ms | ~65ms | ✗ 慢 63% |
| 不开算法 FPS | 17-20fps | ~14.5fps | ✗ 下降 |

**失败原因**：U/V 展开的标量内存往返打断了 NEON 流水线。Cortex-A7 的 NEON 单元是单发射、无乱序，标量/NEON 混合执行的惩罚很大。

### 6.2 v2 实测结果（仍不及 OpenCV）

v2 用 `vld4_u8` 硬件解交织，偶数/奇数像素分别计算共享 U/V，`vzip_u8` 寄存器内交织，一次 16 像素。

| 指标 | OpenCV cvtColor | NEON v2 | 变化 |
|---|---|---|---|
| YUYV→BGR 平均耗时 | ~40ms | ~57ms | ✗ 慢 43% |
| 不开算法 FPS | 17-20fps | ~16.4fps | ✗ 下降 |

比 v1 有改善（65ms→57ms），但仍然比 OpenCV cvtColor 慢。

### 6.3 分析：为什么手写 NEON 比 OpenCV 慢

**可能原因 1：OpenCV 已经内置 NEON 优化**

交叉编译工具链已配置 `-mfpu=neon-vfpv4`，OpenCV 编译时可能已启用 NEON 优化路径。如果 OpenCV 内部的 cvtColor 已经是 NEON 优化的，我们的手写版本很难超越它。

**可能原因 2：Cortex-A7 NEON 单元较弱**

Cortex-A7 的 NEON 是单发射、顺序执行，不像 Cortex-A15/A53 那样可以乱序执行和双发射。大量 NEON 指令的延迟累积后，优势不明显。

**可能原因 3：内存带宽瓶颈**

640×480 YUYV = 614KB 输入 + 921KB 输出 = 1.5MB/帧。Cortex-A7 的 L1 cache 只有 32KB，大量 cache miss 导致内存访问成为瓶颈，NEON 计算单元大部分时间在等待数据。

### 6.4 结论

当前手写 NEON 无法超越 OpenCV cvtColor。

经确认，OpenCV ARM 版本已启用 NEON 优化（`CV_NEON 1`，见 `cv_cpu_dispatch.h`）。cvtColor 内部的 YUYV→BGR 路径已经是 NEON 优化过的，手写版本是在和 OpenCV 多年优化的 NEON 实现竞争，很难超越。

**已回退到 OpenCV cvtColor。**

P5 的方向需要调整：既然 cvtColor 已经是 NEON 优化的，采集线程的优化空间不在颜色转换，而在其他方向。详见第九章。

---

## 七、与其他优化的关系

| 优化 | 关系 |
|---|---|
| P7.1 全程 BGR | P5 输出 BGR Mat，直接 push 到队列，无需额外转换。P7.1 为 P5 铺好了数据格式基础 |
| P6 NEON CLAHE/去雾 | P5 完成后积累 NEON 开发经验，P6 复用相同的开发模式 |
| P14 自适应锐化 | P5 不影响 P14（P14 在图像编辑页，不在实时管线） |
| 13_8 FPS 总跟踪 | P5 是 13_8 第八章中优先级最高的待执行方案 |

---

## 八、后续优化方向

如果 P5 实测效果不够理想，可以进一步优化：

1. **U/V 展开改用纯 NEON 寄存器操作**：避免 vst1/vld1 的内存往返
2. **一次处理 16 像素**：使用 `vld1q_u8` ×2 加载 32 字节，减少循环开销
3. **预取指令**：`__builtin_prefetch` 提前加载下一行数据到 cache
4. **多行并行**：展开外层循环，同时处理 2-4 行，提高指令级并行度

这些优化在 P5 基础版本验证后按需实施。

---

## 九、方向调整

### 9.1 关键发现

OpenCV ARM 版本已启用 NEON（`CV_NEON 1`），cvtColor 内部的 YUYV→BGR 已经是 NEON 优化的。手写 NEON 替换 cvtColor 没有意义。

这意味着 13_8 第五章的假设“cvtColor 是未优化的浮点实现”是错误的。采集线程的 40ms 已经是 NEON 优化后的结果，不是未优化的。

### 9.2 重新评估采集线程瓶颈

采集线程每帧耗时 ~40ms 的拆分：

```
select 等待:     ~0ms（数据就绪时立即返回）
DQBUF:            ~0ms
YUYV→BGR (NEON):  17-55ms（已是 NEON 优化，无法再快）
returnBuffer:     ~0ms
push (clone):     4-25ms（900KB 内存拷贝）
```

**真正的优化空间在 push (clone)**：每帧 clone 640×480×3 = 900KB，在 Cortex-A7 的内存带宽下耗时 4-25ms。如果能避免这次 clone（比如用内存池预分配 + copyTo 复用），可以省 5-15ms/帧。

另外，采集线程的 YUYV→BGR 耗时波动大（17-55ms），这主要是 CPU 调度波动（问题 #14 已确认），不是算法本身的问题。

### 9.3 后续方向

既然采集线程的 cvtColor 已无优化空间，P5 的目标应调整为：

1. **确认 P5 YUYV→BGR 方向关闭**，回退到 OpenCV cvtColor
2. **转向 P6 NEON 加速 CLAHE/去雾**，这些算法的 OpenCV 实现不一定有针对性的 NEON 优化
3. **或者转向 P4 内存池**，减少 clone 的内存分配开销

需要在 13_8 FPS 总跟踪文档中更新这个结论。

> → FPS 问题的完整跟踪见 `16_FPS性能问题总跟踪.md`
