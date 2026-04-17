# ioctl 与 V4L2 摄像头操作详解

## 一、ioctl 是什么

ioctl（input/output control）是 Linux 的系统调用，用于**对设备发送控制命令**。

在 Linux 中一切皆文件，摄像头也是一个文件（`/dev/video0`）。普通文件用 read/write 读写数据，但设备需要更多操作（设置分辨率、设置帧率、启动采集等），这些操作通过 ioctl 完成。

```cpp
#include <sys/ioctl.h>

int ioctl(int fd, unsigned long request, void *arg);
//        ↑ 设备文件描述符  ↑ 命令码        ↑ 参数（结构体指针）
```

| 参数 | 含义 | 例子 |
|---|---|---|
| fd | open() 返回的文件描述符 | 摄像头 fd、串口 fd、I2C fd |
| request | 要执行什么操作（命令码） | VIDIOC_QUERYCAP、VIDIOC_S_FMT 等 |
| arg | 操作的参数（传入或传出） | 结构体指针，不同命令用不同结构体 |
| 返回值 | 0 = 成功，-1 = 失败 | 失败时 errno 说明原因 |

### 类比理解

```
ioctl 就像对设备说话：

ioctl(摄像头, "你支持什么功能？", &回答)     → VIDIOC_QUERYCAP
ioctl(摄像头, "设置分辨率 640x480", &参数)   → VIDIOC_S_FMT
ioctl(摄像头, "开始采集", &类型)             → VIDIOC_STREAMON
ioctl(摄像头, "给我一帧数据", &buffer信息)   → VIDIOC_DQBUF
ioctl(摄像头, "还你 buffer", &buffer信息)    → VIDIOC_QBUF
ioctl(摄像头, "把白平衡打开", &控制参数)     → VIDIOC_S_CTRL
```

---

## 二、xioctl 封装

我们项目中不直接调用 ioctl，而是用 xioctl 封装：

```cpp
int V4l2Capture::xioctl(int fd, int request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); }
    while (r == -1 && errno == EINTR);
    return r;
}
```

**为什么需要封装**：ioctl 可能被系统信号中断（比如定时器信号），此时返回 -1 且 errno == EINTR。这不是真正的错误，重试就行。xioctl 自动重试，调用方不需要处理这个情况。

---

## 三、V4L2 完整操作流程

### 3.1 流程总览

```
open("/dev/video0")          打开设备
  ↓
VIDIOC_QUERYCAP              查询设备能力
  ↓
VIDIOC_ENUM_FMT              枚举支持的格式（可选，仅查看）
  ↓
VIDIOC_TRY_FMT               试探格式（驱动调整到最接近的值）
  ↓
VIDIOC_S_FMT                 正式设置格式和分辨率
  ↓
VIDIOC_S_PARM                设置帧率
  ↓
VIDIOC_REQBUFS               申请内核缓冲区
  ↓
VIDIOC_QUERYBUF × N          查询每个缓冲区的地址和大小
  ↓
mmap() × N                   映射到用户空间
  ↓
VIDIOC_QBUF × N              所有缓冲区入队
  ↓
VIDIOC_STREAMON               开始采集
  ↓
循环：
  select()                    等待数据就绪
  VIDIOC_DQBUF               取出已填充的缓冲区
  处理数据（cvtColor 等）
  VIDIOC_QBUF                归还缓冲区
  ↓
VIDIOC_STREAMOFF              停止采集
  ↓
munmap() × N                  解除映射
  ↓
close(fd)                     关闭设备
```

### 3.2 每一步详解

#### 第 1 步：打开设备

```cpp
int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
```

| 标志 | 含义 |
|---|---|
| O_RDWR | 可读可写（采集需要读，控制参数需要写） |
| O_NONBLOCK | 非阻塞模式（配合 select 使用） |

返回文件描述符 fd，后续所有操作都通过它。

#### 第 2 步：查询设备能力（VIDIOC_QUERYCAP）

```cpp
struct v4l2_capability cap;
memset(&cap, 0, sizeof(cap));    // V4L2 要求结构体先清零

xioctl(fd, VIDIOC_QUERYCAP, &cap);

// 检查关键能力
cap.capabilities & V4L2_CAP_VIDEO_CAPTURE  // 支持视频采集？
cap.capabilities & V4L2_CAP_STREAMING      // 支持 streaming I/O（mmap）？
```

| 字段 | 含义 | 我们设备的值 |
|---|---|---|
| cap.driver | 驱动名 | "stm32-dcmi" |
| cap.card | 设备名 | "STM32 Camera Memory Interface" |
| cap.bus_info | 总线信息 | "platform:dcmi" |
| cap.capabilities | 能力位掩码 | 包含 VIDEO_CAPTURE 和 STREAMING |

#### 第 3 步：枚举支持的格式（VIDIOC_ENUM_FMT）

```cpp
struct v4l2_fmtdesc fmt;
memset(&fmt, 0, sizeof(fmt));
fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
fmt.index = 0;    // 从第 0 个格式开始

while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
    printf("[%d] %s\n", fmt.index, fmt.description);
    fmt.index++;   // 下一个格式
}
// 返回 -1 时表示没有更多格式了
```

我们设备支持的格式：

```
[0] JFIF JPEG
[1] UYVY 4:2:2
[2] YUYV 4:2:2    ← 我们使用这个
[3] 16-bit RGB 5-6-5
```

这一步只是查看，不影响后续流程。

#### 第 4 步：设置格式和分辨率（VIDIOC_TRY_FMT + VIDIOC_S_FMT）

```cpp
struct v4l2_format fmt;
memset(&fmt, 0, sizeof(fmt));
fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
fmt.fmt.pix.width = 640;                    // 期望宽度
fmt.fmt.pix.height = 480;                   // 期望高度
fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;  // YUYV 格式
fmt.fmt.pix.field = V4L2_FIELD_NONE;        // 逐行扫描

// 试探：驱动会把不支持的值调整到最接近的
xioctl(fd, VIDIOC_TRY_FMT, &fmt);
// 此时 fmt.fmt.pix.width/height 可能被驱动修改

// 正式设置
xioctl(fd, VIDIOC_S_FMT, &fmt);
```

TRY_FMT 和 S_FMT 的区别：

| | VIDIOC_TRY_FMT | VIDIOC_S_FMT |
|---|---|---|
| 作用 | 试探，不生效 | 正式设置，立即生效 |
| 驱动行为 | 调整参数到最接近的支持值并返回 | 同上，且应用到硬件 |
| 用途 | 先看驱动能支持什么 | 确认后正式设置 |

#### 第 5 步：设置帧率（VIDIOC_S_PARM）

```cpp
struct v4l2_streamparm parm;
memset(&parm, 0, sizeof(parm));
parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
parm.parm.capture.timeperframe.numerator = 1;
parm.parm.capture.timeperframe.denominator = 30;  // 30fps

xioctl(fd, VIDIOC_S_PARM, &parm);
```

帧率 = denominator / numerator = 30/1 = 30fps。

#### 第 6 步：申请缓冲区（VIDIOC_REQBUFS）

```cpp
struct v4l2_requestbuffers req;
memset(&req, 0, sizeof(req));
req.count = 4;                            // 请求 4 个缓冲区
req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
req.memory = V4L2_MEMORY_MMAP;            // 使用 mmap 方式

xioctl(fd, VIDIOC_REQBUFS, &req);
// req.count 可能被驱动修改为实际分配的数量
```

这一步让内核在 DMA 可访问的内存区域分配缓冲区。摄像头硬件（DCMI）通过 DMA 直接把数据写入这些缓冲区，不经过 CPU。

#### 第 7 步：查询并映射缓冲区（VIDIOC_QUERYBUF + mmap）

```cpp
for (int i = 0; i < req.count; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    // 查询缓冲区的内核地址偏移和大小
    xioctl(fd, VIDIOC_QUERYBUF, &buf);

    // 映射到用户空间
    buffers[i].start = mmap(NULL, buf.length,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, buf.m.offset);
    buffers[i].length = buf.length;
}
```

mmap 后 `buffers[i].start` 直接指向内核缓冲区的数据。摄像头写入数据后，用户空间立刻能读到，不需要 copy。这就是"零拷贝"。

```
mmap 前：
  内核空间：[缓冲区数据]     用户空间：[无法访问]

mmap 后：
  内核空间：[缓冲区数据] ←→ 用户空间：buffers[i].start 指向同一块物理内存
```

**为什么 mmap 后还需要 DQBUF/QBUF**：

mmap 解决的是地址空间问题（用户能访问内核内存），但摄像头的 DMA 硬件也在往这块内存写数据。如果不做同步，用户读数据时 DMA 可能正在写，导致读到写了一半的数据（花屏）。

DQBUF/QBUF 解决的是时序问题：

```
没有 DQBUF/QBUF 的情况：
  T=0ms:   DMA 开始往 buf0 写第 1 帧（从第 1 行开始）
  T=10ms:  用户开始读 buf0（前 1/3 是新数据）
  T=25ms:  用户读中间 1/3（DMA 正在写的区域）→ 读到写了一半的数据！
  T=33ms:  DMA 写完第 1 帧，开始写第 2 帧（覆盖 buf0）
  T=35ms:  用户读最后 1/3 → 读到的是第 2 帧的数据！
  结果：一帧图像混合了两帧数据 → 花屏

有 DQBUF/QBUF 的情况：
  buf0 在驱动队列中 → DMA 可以写
  DMA 写完 → buf0 标记为 DONE
  用户 DQBUF → buf0 从队列取出 → DMA 不会再碰 buf0
  用户安全地读 buf0（DMA 在写 buf1/buf2/buf3）
  用户 QBUF 归还 buf0 → DMA 可以再次使用
```

| 操作 | 含义 | 类比 |
|---|---|---|
| mmap | 用户能访问这块内存 | 拿到仓库的钥匙 |
| DQBUF | “这个 buffer 我要用了，你别动” | 从传送带上取下一个包裹 |
| QBUF | “我用完了，还给你” | 把空包裹放回传送带 |

这也是问题 #7（花屏）的根因——当时 QBUF 归还太早，cvtColor 还在读 buf0 的数据，驱动就开始往 buf0 写新帧了。

#### 第 8 步：缓冲区入队（VIDIOC_QBUF）

```cpp
for (int i = 0; i < req.count; i++) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    xioctl(fd, VIDIOC_QBUF, &buf);  // 放入采集队列
}
```

QBUF = Queue Buffer，把缓冲区放入驱动的采集队列。驱动会从队列中取出空缓冲区，让 DMA 往里填数据。

#### 第 9 步：开始采集（VIDIOC_STREAMON）

```cpp
enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
xioctl(fd, VIDIOC_STREAMON, &type);
```

从这一刻起，摄像头开始工作，DMA 开始往缓冲区填数据。

#### 第 10 步：采集循环（select + DQBUF + QBUF）

```cpp
while (running) {
    // 等待数据就绪
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {2, 0};  // 超时 2 秒
    select(fd + 1, &fds, NULL, NULL, &tv);

    // 取出已填充数据的缓冲区
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    xioctl(fd, VIDIOC_DQBUF, &buf);

    // 此时 buffers[buf.index].start 指向 YUYV 数据
    // 处理数据...
    void *data = buffers[buf.index].start;
    int size = buf.bytesused;

    // 归还缓冲区（必须在数据处理完成后）
    xioctl(fd, VIDIOC_QBUF, &buf);
}
```

DQBUF = Dequeue Buffer，从驱动队列中取出一个已填充数据的缓冲区。取出后驱动不会再往里写数据，用户可以安全读取。

QBUF = Queue Buffer，归还缓冲区。归还后驱动可以再次使用它。

**缓冲区轮转**（4 个 buffer）：

```
初始：  驱动队列 [buf0, buf1, buf2, buf3]

第 1 帧：
  驱动填充 buf0 → DQBUF 取出 buf0 → 用户处理 → QBUF 归还 buf0
  同时驱动在填充 buf1

第 2 帧：
  DQBUF 取出 buf1 → 用户处理 → QBUF 归还 buf1
  同时驱动在填充 buf2

...循环往复
```

#### 第 11 步：停止采集（VIDIOC_STREAMOFF）

```cpp
enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
xioctl(fd, VIDIOC_STREAMOFF, &type);
```

停止采集，所有缓冲区被取消排队。注意：STREAMOFF 后如果要重新采集，需要重新 QBUF 所有缓冲区再 STREAMON。这就是为什么我们用 pause/resume 而不是 stop/start（见问题 #16）。

#### 第 12 步：清理（munmap + close）

```cpp
for (int i = 0; i < bufferCount; i++) {
    munmap(buffers[i].start, buffers[i].length);
}
close(fd);
```

---

## 四、硬件参数控制（VIDIOC_S_CTRL / VIDIOC_G_CTRL）

除了采集流程，ioctl 还用于控制摄像头硬件参数：

```cpp
// 设置参数
struct v4l2_control ctrl;
ctrl.id = V4L2_CID_SATURATION;   // 参数 ID
ctrl.value = 128;                  // 参数值
xioctl(fd, VIDIOC_S_CTRL, &ctrl);

// 读取参数
ctrl.id = V4L2_CID_SATURATION;
xioctl(fd, VIDIOC_G_CTRL, &ctrl);
int current = ctrl.value;          // 当前值
```

### 调用链

```
用户拖动饱和度滑块
  → Qt 信号 valueChanged(128)
    → lambda: v4l2->setSaturation(128)
      → setControl(V4L2_CID_SATURATION, 128)
        → xioctl(fd, VIDIOC_S_CTRL, &ctrl)
          → Linux 内核 V4L2 子系统
            → OV5640 驱动
              → I2C 总线写入 Sensor 寄存器
                → 下一帧图像生效
```

### 常用参数 ID

| 参数 ID | 含义 | 范围 | 我们项目中的方法 |
|---|---|---|---|
| V4L2_CID_AUTO_WHITE_BALANCE | 自动白平衡 | 0/1 | setAutoWhiteBalance() |
| V4L2_CID_EXPOSURE_AUTO | 自动曝光 | 0(手动)/1(自动) | setAutoExposure() |
| V4L2_CID_EXPOSURE | 手动曝光值 | 0-65535 | setExposure() |
| V4L2_CID_GAIN | 增益 | 0-1023 | setGain() |
| V4L2_CID_CONTRAST | 对比度 | 0-255 | setContrast() |
| V4L2_CID_SATURATION | 饱和度 | 0-255 | setSaturation() |
| V4L2_CID_HUE | 色调 | 0-359 | setHue() |
| V4L2_CID_HFLIP | 水平翻转 | 0/1 | setHFlip() |
| V4L2_CID_VFLIP | 垂直翻转 | 0/1 | setVFlip() |

---

## 五、V4L2 命令码速查

| 命令码 | 方向 | 结构体 | 含义 |
|---|---|---|---|
| VIDIOC_QUERYCAP | 读 | v4l2_capability | 查询设备能力 |
| VIDIOC_ENUM_FMT | 读 | v4l2_fmtdesc | 枚举支持的格式 |
| VIDIOC_TRY_FMT | 读写 | v4l2_format | 试探格式（不生效） |
| VIDIOC_S_FMT | 读写 | v4l2_format | 设置格式（生效） |
| VIDIOC_G_FMT | 读 | v4l2_format | 获取当前格式 |
| VIDIOC_S_PARM | 读写 | v4l2_streamparm | 设置帧率 |
| VIDIOC_REQBUFS | 读写 | v4l2_requestbuffers | 申请缓冲区 |
| VIDIOC_QUERYBUF | 读 | v4l2_buffer | 查询缓冲区信息 |
| VIDIOC_QBUF | 写 | v4l2_buffer | 缓冲区入队 |
| VIDIOC_DQBUF | 读 | v4l2_buffer | 缓冲区出队 |
| VIDIOC_STREAMON | 写 | v4l2_buf_type | 开始采集 |
| VIDIOC_STREAMOFF | 写 | v4l2_buf_type | 停止采集 |
| VIDIOC_S_CTRL | 写 | v4l2_control | 设置控制参数 |
| VIDIOC_G_CTRL | 读 | v4l2_control | 获取控制参数 |

"方向"指数据流向：读 = 内核→用户（查询），写 = 用户→内核（设置），读写 = 用户传入参数，内核可能修改后返回。

---

## 六、关键结构体字段说明

### v4l2_capability（VIDIOC_QUERYCAP）

```cpp
struct v4l2_capability {
    __u8  driver[16];      // 驱动名，如 "stm32-dcmi"
    __u8  card[32];        // 设备名，如 "STM32 Camera Memory Interface"
    __u8  bus_info[32];    // 总线信息，如 "platform:dcmi"
    __u32 capabilities;    // 能力位掩码，用 & 检查是否支持某功能
};
```

capabilities 常用标志：

| 标志 | 含义 | 说明 |
|---|---|---|
| V4L2_CAP_VIDEO_CAPTURE | 支持视频采集 | 摄像头必须有 |
| V4L2_CAP_VIDEO_OUTPUT | 支持视频输出 | 显示设备用，摄像头不需要 |
| V4L2_CAP_STREAMING | 支持 streaming I/O | mmap/userptr 方式必须有 |
| V4L2_CAP_READWRITE | 支持 read/write I/O | 最简单但最慢的方式，需要拷贝数据 |

检查方式：`if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)` —— 用位与操作检查某一位是否为 1。

### v4l2_format（VIDIOC_TRY_FMT / VIDIOC_S_FMT / VIDIOC_G_FMT）

```cpp
struct v4l2_format {
    __u32 type;            // 缓冲区类型
    union {
        struct v4l2_pix_format pix;
    } fmt;
};

struct v4l2_pix_format {
    __u32 width;           // 宽度，如 640
    __u32 height;          // 高度，如 480
    __u32 pixelformat;     // 像素格式
    __u32 field;           // 扫描方式
    __u32 bytesperline;    // 每行字节数（驱动填充）
    __u32 sizeimage;       // 整幅图像字节数（驱动填充）
};
```

type 常用值：

| 值 | 含义 |
|---|---|
| V4L2_BUF_TYPE_VIDEO_CAPTURE | 视频采集（摄像头输入） |
| V4L2_BUF_TYPE_VIDEO_OUTPUT | 视频输出（显示设备） |
| V4L2_BUF_TYPE_VIDEO_OVERLAY | 视频叠加 |

pixelformat 常用值：

| 值 | 格式 | 每像素字节 | 说明 |
|---|---|---|---|
| V4L2_PIX_FMT_YUYV | YUYV 4:2:2 | 2 | 我们使用的格式，每 2 像素共享 UV |
| V4L2_PIX_FMT_UYVY | UYVY 4:2:2 | 2 | 和 YUYV 类似，字节顺序不同 |
| V4L2_PIX_FMT_NV12 | YUV 4:2:0 | 1.5 | Y 平面 + UV 交织平面，手机摄像头常用 |
| V4L2_PIX_FMT_NV21 | YVU 4:2:0 | 1.5 | 同 NV12，UV 顺序相反，Android 默认 |
| V4L2_PIX_FMT_MJPEG | MJPEG 压缩 | 变长 | 硬件压缩，数据量小，需要解码 |
| V4L2_PIX_FMT_RGB565 | RGB 5-6-5 | 2 | 16位彩色，嵌入式屏幕常用 |
| V4L2_PIX_FMT_BGR24 | BGR 8-8-8 | 3 | 24位彩色，OpenCV 默认格式 |

field 常用值：

| 值 | 含义 | 说明 |
|---|---|---|
| V4L2_FIELD_NONE | 逐行扫描 | 我们使用的，现代摄像头都是逐行 |
| V4L2_FIELD_INTERLACED | 隔行扫描 | 老式模拟摄像头，奇偶行交替 |
| V4L2_FIELD_ANY | 驱动自动选择 | |

### v4l2_requestbuffers（VIDIOC_REQBUFS）

```cpp
struct v4l2_requestbuffers {
    __u32 count;           // 缓冲区数量（传入请求数，驱动可能修改为实际分配数）
    __u32 type;            // V4L2_BUF_TYPE_VIDEO_CAPTURE
    __u32 memory;          // 内存方式
};
```

memory 常用值：

| 值 | 含义 | 数据流向 | 说明 |
|---|---|---|---|
| V4L2_MEMORY_MMAP | 内核分配，用户 mmap 映射 | 内核→mmap→用户直接读 | 我们使用的，零拷贝，性能最好 |
| V4L2_MEMORY_USERPTR | 用户分配，告诉内核地址 | 内核 DMA 直接写入用户内存 | 用户控制内存分配，灵活但复杂 |
| V4L2_MEMORY_DMABUF | DMA buffer 共享 | 多设备共享同一块 DMA 内存 | 用于 GPU/ISP/显示控制器之间共享帧 |

三种方式对比：

```
MMAP（我们用的）：
  内核分配 buffer → mmap 映射到用户空间 → 用户直接读
  优点：零拷贝，简单
  缺点：内存由内核管理，用户不能控制分配位置

USERPTR：
  用户分配 buffer → 告诉内核地址 → 内核 DMA 直接写入
  优点：用户控制内存（可以用内存池）
  缺点：需要对齐内存，不是所有驱动都支持

DMABUF：
  多个设备共享同一块 DMA buffer
  优点：设备间零拷贝（如摄像头→GPU→显示）
  缺点：最复杂，需要硬件支持
```

### v4l2_buffer（VIDIOC_QUERYBUF / VIDIOC_QBUF / VIDIOC_DQBUF）

```cpp
struct v4l2_buffer {
    __u32 index;           // 缓冲区索引（0, 1, 2, 3）
    __u32 type;            // 缓冲区类型
    __u32 memory;          // 内存方式（同 v4l2_requestbuffers）
    __u32 bytesused;       // 实际数据大小（DQBUF 时驱动填充）
    __u32 length;          // 缓冲区总大小（QUERYBUF 时驱动填充）
    __u32 flags;           // 状态标志
    struct timeval timestamp; // 采集时间戳（驱动填充）
    union {
        __u32 offset;      // MMAP 模式：mmap 偏移量（QUERYBUF 时用）
        unsigned long userptr; // USERPTR 模式：用户空间地址
        __s32 fd;          // DMABUF 模式：DMA buffer 文件描述符
    } m;
};
```

flags 常用标志：

| 标志 | 含义 |
|---|---|
| V4L2_BUF_FLAG_MAPPED | 缓冲区已被 mmap 映射 |
| V4L2_BUF_FLAG_QUEUED | 缓冲区在驱动队列中（等待填充） |
| V4L2_BUF_FLAG_DONE | 缓冲区已填充完成（可以 DQBUF） |
| V4L2_BUF_FLAG_ERROR | 采集出错（如 overrun） |

三个命令中各字段的使用：

| 字段 | QUERYBUF | QBUF | DQBUF |
|---|---|---|---|
| index | 用户填（查询第几个） | 用户填（归还第几个） | 驱动填（告诉你取出的是第几个） |
| length | 驱动填 | - | - |
| m.offset | 驱动填 | - | - |
| bytesused | - | - | 驱动填（实际数据大小） |
| timestamp | - | - | 驱动填（采集时间） |
| flags | - | - | 驱动填（状态） |

### v4l2_streamparm（VIDIOC_S_PARM）

```cpp
struct v4l2_streamparm {
    __u32 type;            // V4L2_BUF_TYPE_VIDEO_CAPTURE
    union {
        struct v4l2_captureparm capture;
    } parm;
};

struct v4l2_captureparm {
    struct v4l2_fract timeperframe;  // 帧间隔
    // timeperframe.numerator = 1
    // timeperframe.denominator = 30  → 30fps
};
```

### v4l2_control（VIDIOC_S_CTRL / VIDIOC_G_CTRL）

```cpp
struct v4l2_control {
    __u32 id;              // 参数 ID，如 V4L2_CID_SATURATION
    __s32 value;           // 参数值，如 128
};
```

### v4l2_fmtdesc（VIDIOC_ENUM_FMT）

```cpp
struct v4l2_fmtdesc {
    __u32 index;           // 格式索引，从 0 开始递增
    __u32 type;            // V4L2_BUF_TYPE_VIDEO_CAPTURE
    __u8  description[32]; // 格式描述，如 "YUYV 4:2:2"
    __u32 pixelformat;     // 格式编码，如 V4L2_PIX_FMT_YUYV
};
```

---

## 七、ioctl 不只用于摄像头

ioctl 是 Linux 设备控制的通用接口，不同设备用不同的命令码：

| 设备 | 命令码前缀 | 例子 |
|---|---|---|
| V4L2 摄像头 | VIDIOC_ | VIDIOC_QUERYCAP、VIDIOC_S_FMT |
| I2C 设备 | I2C_ | I2C_SLAVE（设置从机地址） |
| SPI 设备 | SPI_IOC_ | SPI_IOC_MESSAGE（传输数据） |
| 串口 | TIOC | TIOCGSERIAL（获取串口信息） |
| 网络接口 | SIOC | SIOCGIFADDR（获取 IP 地址） |

后续做 M4 开发时，A7 侧通过 I2C/SPI 和传感器通信也会用到 ioctl，模式完全一样：open → ioctl 设置参数 → read/write 数据 → close。
