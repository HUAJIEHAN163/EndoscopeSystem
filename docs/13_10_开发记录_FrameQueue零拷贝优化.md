# 13_10 开发记录：FrameQueue 零拷贝优化（P16）

## 一、背景

### 问题来源

16_FPS性能问题总跟踪 第七章 7.5 节追溯了不开算法 FPS 逐步下降的原因：

| 阶段 | 不开算法 FPS | 变化原因 |
|---|---|---|
| 原始两线程 | 24-28fps | 基准 |
| P3 三线程 | ~20fps | 多一个线程争抢 CPU |
| **P7.1 全程 BGR** | **17-20fps** | **FrameQueue 从 QImage 改为 cv::Mat，clone 每帧 2×900KB 真拷贝** |

P7.1 引入的 clone 开销是当前 FPS 下降的直接原因之一。每帧经过两个队列，各 clone 一次：

```
采集线程: cvtColor → bgr (新 Mat) → captureQueue.push(bgr) → clone ① 900KB
处理线程: pop → 算法/resize → result (新 Mat) → displayQueue.push(result) → clone ② 900KB
```

每帧 2 次 clone = 1.8MB 内存拷贝，在 Cortex-A7 的内存带宽（DDR3 ~2GB/s）下耗时可观。

### 核心观察

**clone 是不必要的。** 分析两个 push 的场景：

1. **captureQueue.push(bgr)**：`bgr` 是 `cvtColor` 的输出，每帧都是新分配的 Mat。push 后采集线程进入下一帧循环，`cvtColor` 会分配新的 `bgr`，不会覆盖旧的。所以 push 时可以直接转移所有权（move），不需要 clone。

2. **displayQueue.push(result)**：`result` 是 `cv::resize` 的输出，同理每帧新分配。push 后处理线程进入下一帧循环，不会覆盖旧的。

### 预期收益

省掉每帧 2 次 clone（共 1.8MB memcpy），预计恢复 P7.1 前的 FPS 水平（~20-22fps）。

---

## 二、设计方案

### 2.1 改动思路

FrameQueue 新增 `pushMove` 方法，接受右值引用，直接用 `std::move` 转移 Mat 的所有权到队列槽位，不做数据拷贝。

```cpp
// 改动前：每次 push 都 clone 900KB
void push(const cv::Mat &frame) {
    m_frames[w] = frame.clone();  // 分配新内存 + memcpy 900KB
}

// 改动后：转移所有权，零拷贝
void push(cv::Mat &&frame) {
    m_frames[w] = std::move(frame);  // 只交换指针，~0ms
}
```

`std::move` 后原始 Mat 变为空（数据指针转移给了队列槽位），调用方不能再使用它。这正好符合我们的场景——采集线程 push 后不再使用 bgr。

### 2.2 pop/latest 的安全性

pop 和 latest 返回的 Mat 与队列槽位共享数据。如果生产者后续 pushMove 覆盖了该槽位，消费者持有的 Mat 数据会被破坏吗？

**不会。** cv::Mat 使用引用计数。pop/latest 中 `frame = m_frames[r]` 是浅拷贝，引用计数 +1。后续 pushMove 覆盖 `m_frames[w]` 时，`std::move` 会让旧 Mat 的引用计数 -1，但消费者仍持有一份引用，数据不会被释放。

### 2.3 队列满时的覆盖

当前队列满时移动 readIdx 丢弃最旧帧。pushMove 覆盖 `m_frames[w]` 时，如果该槽位的旧 Mat 没有被 pop 过（被跳过了），`std::move` 赋值会释放旧 Mat 的数据。这是正确的行为——被跳过的帧本来就应该丢弃。

### 2.4 改动范围

| 文件 | 改动 |
|---|---|
| `src/utils/framequeue.h` | 新增 push(cv::Mat&&) 移动语义重载 |
| `src/capture/v4l2capture.cpp` | push(bgr) 改为 push(std::move(bgr)) |
| `src/processing/processthread.h` | push(result) 改为 push(std::move(result)) |

---

## 三、实施步骤

- [x] Step 1：FrameQueue 新增移动语义 push
- [x] Step 2：采集线程改用 std::move push
- [x] Step 3：处理线程改用 std::move push
- [x] Step 4：虚拟机编译验证
- [x] Step 5：ARM 交叉编译验证
- [x] Step 6：开发板验证

---

## 四、代码修改详细记录

### 4.1 framequeue.h — 新增移动语义 push

保留原有 `push(const cv::Mat&)` 拷贝版本兼容，新增 `push(cv::Mat&&)` 移动版本：

```cpp
// 新增：移动语义 push
void push(cv::Mat &&frame) {
    // ... 队列满处理同拷贝版本 ...
    m_frames[w] = std::move(frame);  // 转移所有权，零拷贝
    m_writeIdx.store(next, std::memory_order_release);
}
```

C++ 会根据参数类型自动选择重载：`push(bgr)` 调用拷贝版本，`push(std::move(bgr))` 调用移动版本。

### 4.2 v4l2capture.cpp — 采集线程改用 std::move

**改动前：**
```cpp
m_captureQueue->push(bgr);  // clone 900KB
```

**改动后：**
```cpp
m_captureQueue->push(std::move(bgr));  // 转移指针，0ms
```

安全性：push 之后到循环末尾之间只有计时日志（使用 tYuv2Bgr 和 tPush，不使用 bgr），安全。

### 4.3 processthread.h — 处理线程改用 std::move

**改动前：**
```cpp
m_displayQueue->push(result);  // clone 900KB
```

**改动后：**
```cpp
m_displayQueue->push(std::move(result));  // 转移指针，0ms
```

安全性：push 之后只有帧计数和统计日志，不使用 result，安全。

---

## 五、设计决策记录

### 5.1 为什么用移动语义而不是内存池

P4 内存池方案是预分配 Mat，用 copyTo 复用内存。copyTo 避免了 malloc/free，但 memcpy 仍然在（900KB/次）。

移动语义直接转移指针，零 memcpy。在我们的场景下（生产者 push 后不再使用原 Mat），移动语义是最优解。

| 方案 | malloc/free | memcpy | 复杂度 |
|---|---|---|---|
| clone（当前） | 每帧 2 次 | 每帧 2×900KB | 低 |
| copyTo + 内存池（P4） | 首次分配后复用 | 每帧 2×900KB | 中（需要管理内存池） |
| **std::move（P16）** | **零** | **零** | **低（改 3 行代码）** |

### 5.2 保留 clone 版本的 push 作为兼容

保留原有的 `push(const cv::Mat&)` 重载（内部 clone），供需要保留原始数据的场景使用。新增 `push(cv::Mat&&)` 重载供移动语义使用。C++ 会根据参数类型自动选择正确的重载。

### 5.3 风险评估

**风险 1：采集线程 push 后误用 bgr**

`std::move(bgr)` 后 bgr 变为空 Mat。如果后续代码仍然使用 bgr，会导致崩溃或空数据。

**缓解**：检查 push 之后到循环末尾之间是否有使用 bgr 的代码。当前代码中 push 之后只有计时日志（使用 tYuv2Bgr 和 tPush，不使用 bgr），安全。

**风险 2：pop/latest 返回的 Mat 生命周期**

消费者通过 pop/latest 获取的 Mat 与队列槽位共享数据（浅拷贝）。只要消费者持有 Mat，数据就不会被释放（引用计数保护）。但如果消费者在处理过程中，生产者 pushMove 覆盖了同一个槽位，不会影响消费者——因为 move 赋值只是替换槽位的 Mat 对象，不影响已经被 pop 出去的引用。

**结论：风险可控。**

---

## 六、开发板验证结果

### 6.1 采集线程耗时对比

| 环节 | P16 前 | P16 后 | 变化 |
|---|---|---|---|
| YUYV→BGR | 17-55ms（平均 ~40ms） | 18-75ms（平均 ~33ms） | ✅ 平均降 7ms |
| push | 4-25ms (clone) | 0-1ms (move) | ✅ **基本归零** |
| **合计** | **~40ms** | **~33ms** | ✅ 省 ~7ms |

### 6.2 FPS 对比

| 指标 | P16 前 | P16 后 | 变化 |
|---|---|---|---|
| PROC 统计 FPS | 14-16fps | **21-22fps** | ✅ **+5-6fps** |
| 屏幕显示 FPS | 17-20fps | 16-20fps（波动） | 略有提升 |

PROC 统计 FPS 提升明显，说明处理线程的吐出帧率提高了。屏幕显示 FPS 波动是因为主线程的 BGR→RGB + QImage copy + paintEvent 仍有 CPU 调度波动。

### 6.3 关键观察

- push 耗时从 4-25ms 降到 0-1ms，std::move 生效确认
- 采集线程平均耗时从 ~40ms 降到 ~33ms，接近帧间隔 33ms（30fps）
- YUYV→BGR 耗时波动仍然大（18-75ms），这是 CPU 调度波动，不是 P16 能解决的
- 队列剩余稳定在 1-3，说明采集线程产出速度快于处理线程消费速度，管线工作正常

---

## 七、与其他优化的关系

| 优化 | 关系 |
|---|---|
| P7.1 全程 BGR | P16 解决 P7.1 引入的 clone 开销副作用 |
| P3 三线程 | P16 不改变线程架构，只优化队列的数据传递方式 |
| P6 NEON CLAHE/去雾 | P16 完成后 CPU 负载降低，P6 的收益更容易体现 |

> → FPS 问题的完整跟踪见 `16_FPS性能问题总跟踪.md`
