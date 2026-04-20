# 嵌入式 Linux 核心系统接口总览

## 一、全局视图

嵌入式 Linux 开发中，应用程序通过**系统调用**与内核交互，内核再操作硬件。所有系统调用都遵循"一切皆文件"的原则。

```
应用程序（用户空间）
  │
  │ 系统调用（open/read/write/ioctl/mmap/select/...）
  ▼
Linux 内核
  ├── 文件系统（ext4、sysfs、procfs）
  ├── 设备驱动（V4L2、I2C、SPI、UART、GPIO）
  ├── 网络协议栈（TCP/UDP/IP）
  ├── 内存管理（mmap、DMA）
  └── 进程/线程调度（pthread、signal）
  │
  ▼
硬件（摄像头、传感器、LED、网络接口、存储）
```

### 核心接口分类

| 类别 | 接口 | 用途 | 频率 |
|---|---|---|---|
| 文件 I/O | open/read/write/close | 一切操作的基础 | 每个设备都用 |
| 设备控制 | ioctl | 配置设备参数、发送控制命令 | 非常频繁 |
| 内存映射 | mmap/munmap | 零拷贝访问设备数据 | 视频/音频采集 |
| I/O 等待 | select/poll/epoll | 等待数据就绪，不浪费 CPU | 所有异步 I/O |
| 多线程 | pthread_create/join/mutex | 并行处理 | 多线程程序 |
| 信号 | signal/sigaction | 异步事件通知 | 进程管理 |
| 文件系统接口 | sysfs/procfs | 通过文件读写硬件状态和系统信息 | GPIO、调试 |
| 网络 | socket/bind/connect/send/recv | 网络通信 | 联网设备 |

---

## 二、文件 I/O（open/read/write/close）

### 2.1 基本概念

Linux 中一切皆文件——设备、管道、socket 都通过文件描述符（fd）操作：

```cpp
int fd = open("/dev/video0", O_RDWR);   // 打开，返回文件描述符
read(fd, buf, size);                     // 读数据
write(fd, buf, size);                    // 写数据
close(fd);                               // 关闭
```

文件描述符是一个整数（0、1、2、3...），内核用它标识打开的文件/设备：

| fd | 默认指向 | 含义 |
|---|---|---|
| 0 | stdin | 标准输入（键盘） |
| 1 | stdout | 标准输出（屏幕） |
| 2 | stderr | 标准错误（屏幕） |
| 3+ | 用户打开的文件/设备 | open() 返回的 |

### 2.2 open 的标志

```cpp
int fd = open(path, flags);
```

| 标志 | 含义 | 使用场景 |
|---|---|---|
| O_RDONLY | 只读 | 读传感器数据 |
| O_WRONLY | 只写 | 控制 GPIO |
| O_RDWR | 可读可写 | 摄像头（读数据 + 写控制） |
| O_NONBLOCK | 非阻塞 | 配合 select 使用，read 无数据时立即返回而不是卡住 |
| O_CREAT | 文件不存在则创建 | 写日志文件 |
| O_TRUNC | 打开时清空文件 | 覆盖写入 |
| O_APPEND | 追加写入 | 日志文件 |
| O_NOCTTY | 不把设备作为控制终端 | 打开串口时使用 |

可以用 `|` 组合多个标志：`O_RDWR | O_NONBLOCK`

### 2.3 read/write 的返回值

```cpp
ssize_t n = read(fd, buf, count);
```

| 返回值 | 含义 |
|---|---|
| > 0 | 实际读到的字节数（可能小于 count） |
| 0 | 到达文件末尾（EOF），或对端关闭连接 |
| -1 | 出错，查看 errno |

常见 errno：

| errno | 含义 | 处理方式 |
|---|---|---|
| EAGAIN | 非阻塞模式下无数据可读 | 稍后重试或用 select 等待 |
| EINTR | 被信号中断 | 重试（xioctl 就是干这个的） |
| EIO | I/O 错误 | 硬件故障 |

### 2.4 我们项目中的使用

```cpp
// V4L2 摄像头
int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
// 之后用 ioctl 配置，用 mmap 映射，不直接 read

// sysfs GPIO（如果后续用到）
int fd = open("/sys/class/gpio/gpio5/value", O_WRONLY);
write(fd, "1", 1);   // 高电平
close(fd);

// 串口
int fd = open("/dev/ttySTM0", O_RDWR | O_NOCTTY);
write(fd, "hello", 5);
read(fd, buf, sizeof(buf));
close(fd);
```

---

## 三、mmap（内存映射）

### 3.1 基本概念

mmap 把文件或设备的内存映射到用户空间，用户可以像访问普通数组一样访问设备数据，不需要 read/write 拷贝。

```cpp
#include <sys/mman.h>

void *ptr = mmap(
    NULL,           // 让内核选择映射地址
    length,         // 映射大小（字节）
    PROT_READ | PROT_WRITE,  // 可读可写
    MAP_SHARED,     // 共享映射（修改对其他进程/内核可见）
    fd,             // 文件描述符
    offset          // 文件/设备中的偏移量
);

// 使用完毕后解除映射
munmap(ptr, length);
```

### 3.2 参数详解

prot（保护标志）：

| 标志 | 含义 |
|---|---|
| PROT_READ | 可读 |
| PROT_WRITE | 可写 |
| PROT_EXEC | 可执行（加载动态库时用） |
| PROT_NONE | 不可访问 |

flags（映射类型）：

| 标志 | 含义 | 使用场景 |
|---|---|---|
| MAP_SHARED | 共享映射，修改对内核/其他进程可见 | V4L2 buffer（DMA 写入后用户立刻能看到） |
| MAP_PRIVATE | 私有映射，修改不影响原文件 | 读取配置文件 |
| MAP_ANONYMOUS | 匿名映射，不关联文件 | 分配大块内存（替代 malloc） |

### 3.3 mmap vs read

```
read 方式：
  内核 buffer → [拷贝] → 用户 buffer
  每帧 640×480×2 = 614KB 拷贝，30fps = 18MB/s 拷贝开销

mmap 方式：
  内核 buffer ←→ 用户指针（同一块物理内存）
  零拷贝，用户直接读取 DMA 写入的数据
```

### 3.4 我们项目中的使用

```cpp
// V4L2 buffer 映射
for (int i = 0; i < bufferCount; i++) {
    buffers[i].start = mmap(NULL, buf.length,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, buf.m.offset);
}

// 使用：直接通过指针读取 YUYV 数据
cv::Mat yuyv(height, width, CV_8UC2, buffers[buf.index].start);
cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);

// 清理
for (int i = 0; i < bufferCount; i++) {
    munmap(buffers[i].start, buffers[i].length);
}
```

---

## 四、select/poll/epoll（I/O 多路复用）

### 4.1 解决什么问题

等待设备数据就绪，不浪费 CPU：

```
不用 select（阻塞 read）：
  read(fd, buf, size);   // 没数据时卡死，无法退出

不用 select（轮询）：
  while (true) {
      n = read(fd, buf, size);  // O_NONBLOCK
      if (n > 0) break;
      // 没数据，继续循环 → CPU 100% 空转
  }

用 select：
  select(fd, ..., timeout);  // 没数据时睡眠，不占 CPU
  read(fd, buf, size);       // select 返回后一定有数据
```

### 4.2 select

```cpp
#include <sys/select.h>

fd_set fds;
FD_ZERO(&fds);              // 清空集合
FD_SET(fd, &fds);           // 添加要监听的 fd

struct timeval tv;
tv.tv_sec = 2;              // 超时 2 秒
tv.tv_usec = 0;

int r = select(fd + 1, &fds, NULL, NULL, &tv);
// r > 0: fd 就绪，可以 read
// r == 0: 超时
// r < 0: 出错
```

参数说明：

| 参数 | 含义 |
|---|---|
| nfds | 最大 fd + 1 |
| readfds | 监听可读的 fd 集合 |
| writefds | 监听可写的 fd 集合（通常 NULL） |
| exceptfds | 监听异常的 fd 集合（通常 NULL） |
| timeout | 超时时间（NULL = 永远等待） |

### 4.3 select vs poll vs epoll

| | select | poll | epoll |
|---|---|---|---|
| fd 数量限制 | 1024 | 无限制 | 无限制 |
| 每次调用开销 | O(n) 扫描所有 fd | O(n) | O(1) |
| 适合场景 | 少量 fd | 中等数量 | 大量连接（网络服务器） |
| 我们项目 | ✅ 只监听 1 个摄像头 fd | 也可以用 | 没必要 |

### 4.4 我们项目中的使用

```cpp
// v4l2capture.cpp — grabFrame()
fd_set fds;
FD_ZERO(&fds);
FD_SET(m_fd, &fds);
struct timeval tv = {2, 0};

int r = select(m_fd + 1, &fds, NULL, NULL, &tv);
if (r <= 0) return false;  // 超时或出错

// select 返回后，摄像头数据就绪，可以 DQBUF
ioctl(m_fd, VIDIOC_DQBUF, &buf);
```

---

## 五、pthread（多线程）

### 5.1 基本操作

```cpp
#include <pthread.h>

// 线程函数
void *thread_func(void *arg) {
    // 在新线程中执行
    while (1) {
        // 工作...
    }
    return NULL;
}

// 创建线程
pthread_t tid;
pthread_create(&tid, NULL, thread_func, NULL);

// 等待线程结束
pthread_join(tid, NULL);
```

### 5.2 同步机制

**互斥锁（Mutex）**：保护共享数据，同一时刻只有一个线程能访问

```cpp
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_lock(&lock);
// 临界区：只有一个线程能执行到这里
shared_data = new_value;
pthread_mutex_unlock(&lock);
```

**条件变量（Condition Variable）**：线程等待某个条件成立

```cpp
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// 等待方
pthread_mutex_lock(&lock);
while (!data_ready)
    pthread_cond_wait(&cond, &lock);  // 释放锁并睡眠，被唤醒后重新加锁
// 处理数据
pthread_mutex_unlock(&lock);

// 通知方
pthread_mutex_lock(&lock);
data_ready = true;
pthread_cond_signal(&cond);           // 唤醒一个等待的线程
pthread_mutex_unlock(&lock);
```

**原子操作（Atomic）**：不需要锁的线程安全操作

```cpp
#include <atomic>
std::atomic<bool> m_running{true};

// 线程 A
m_running.store(false);    // 原子写

// 线程 B
if (m_running.load()) {    // 原子读
    // ...
}
```

### 5.3 线程绑核

```cpp
#include <sched.h>

cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset);   // 绑定到核 0
pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
```

### 5.4 我们项目中的使用

我们用 QThread 封装了 pthread，但底层是一样的：

| QThread | pthread 等价 |
|---|---|
| start() | pthread_create() |
| wait() | pthread_join() |
| QMutex | pthread_mutex |
| std::atomic | 同（C++ 标准库） |

我们项目没有用 mutex 和条件变量——用无锁队列（FrameQueue + atomic）替代了，性能更好。

---

## 六、signal（信号）

### 6.1 基本概念

信号是 Linux 的异步通知机制——内核或其他进程可以随时给你的进程发一个信号，打断当前执行：

```
用户按 Ctrl+C → 内核发送 SIGINT → 进程默认行为：退出
子进程退出    → 内核发送 SIGCHLD → 父进程可以回收资源
定时器到期    → 内核发送 SIGALRM → 执行定时任务
```

### 6.2 常用信号

| 信号 | 编号 | 触发方式 | 默认行为 | 可捕获 |
|---|---|---|---|---|
| SIGINT | 2 | Ctrl+C | 终止进程 | ✅ |
| SIGTERM | 15 | kill 命令 | 终止进程 | ✅ |
| SIGKILL | 9 | kill -9 | 终止进程 | ❌ 不可捕获 |
| SIGSEGV | 11 | 访问非法内存 | 终止 + core dump | ✅ |
| SIGPIPE | 13 | 写入已关闭的管道/socket | 终止进程 | ✅ |
| SIGCHLD | 17 | 子进程退出 | 忽略 | ✅ |
| SIGALRM | 14 | alarm() 定时器到期 | 终止进程 | ✅ |
| EINTR | - | 不是信号，是 errno | ioctl/read 被信号打断 | 重试 |

### 6.3 捕获信号

```cpp
#include <signal.h>

void handler(int sig) {
    printf("收到信号 %d\n", sig);
    // 清理资源...
    exit(0);
}

// 注册信号处理函数
signal(SIGINT, handler);    // Ctrl+C 时调用 handler 而不是直接退出
```

### 6.4 与我们项目的关系

我们没有直接处理信号，但间接涉及：

```cpp
// xioctl 处理 EINTR（ioctl 被信号打断时重试）
int xioctl(int fd, int request, void *arg) {
    int r;
    do { r = ioctl(fd, request, arg); }
    while (r == -1 && errno == EINTR);   // 被信号打断就重试
    return r;
}
```

Qt 程序中 Ctrl+C 的处理由 QApplication 自动管理，不需要手动注册信号处理函数。

---

## 七、sysfs / procfs（文件系统接口）

### 7.1 sysfs（/sys）

sysfs 把内核对象（设备、驱动、总线）暴露为文件，通过读写文件控制硬件：

```bash
# GPIO 控制
echo 5 > /sys/class/gpio/export              # 导出 GPIO5
echo out > /sys/class/gpio/gpio5/direction    # 设为输出
echo 1 > /sys/class/gpio/gpio5/value          # 高电平
echo 0 > /sys/class/gpio/gpio5/value          # 低电平
cat /sys/class/gpio/gpio5/value               # 读取当前电平

# LED 控制
echo 255 > /sys/class/leds/led0/brightness    # 最亮
echo 0 > /sys/class/leds/led0/brightness      # 灭

# 查看 CPU 频率
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq

# 查看温度
cat /sys/class/thermal/thermal_zone0/temp
```

C 代码中也是文件操作：

```cpp
// 控制 GPIO
int fd = open("/sys/class/gpio/gpio5/value", O_WRONLY);
write(fd, "1", 1);   // 高电平
write(fd, "0", 1);   // 低电平
close(fd);
```

sysfs 的优点是简单（纯文件读写，shell 脚本就能控制硬件），缺点是慢（每次要经过文件系统），适合低频操作。

### 7.2 procfs（/proc）

procfs 暴露进程和系统信息：

```bash
# 进程信息
cat /proc/self/status          # 当前进程状态
cat /proc/1234/maps            # 进程 1234 的内存映射
cat /proc/1234/fd/             # 进程打开的文件描述符

# 系统信息
cat /proc/cpuinfo              # CPU 信息
cat /proc/meminfo              # 内存信息
cat /proc/version              # 内核版本
cat /proc/interrupts           # 中断统计

# 内核参数（可读写）
cat /proc/sys/kernel/hostname  # 主机名
echo 1 > /proc/sys/net/ipv4/ip_forward  # 开启 IP 转发
```

### 7.3 sysfs vs ioctl

| | sysfs | ioctl |
|---|---|---|
| 接口 | 文件读写（open/read/write） | ioctl 系统调用 |
| 复杂度 | 简单，shell 就能用 | 需要写 C 代码，知道命令码和结构体 |
| 速度 | 慢（经过文件系统） | 快（直接系统调用） |
| 适合 | GPIO 开关、读状态、调试 | 高频设备控制（摄像头、DMA） |
| 例子 | GPIO 控制、LED 亮度 | V4L2 采集、I2C 通信 |

---

## 八、各接口在我们项目中的使用总结

| 接口 | 在哪里用 | 具体用途 |
|---|---|---|
| open/close | v4l2capture.cpp | 打开/关闭摄像头设备 |
| ioctl | v4l2capture.cpp | V4L2 全部操作（格式、帧率、buffer、采集、硬件参数） |
| mmap/munmap | v4l2capture.cpp | 映射 V4L2 buffer 到用户空间（零拷贝） |
| select | v4l2capture.cpp | 等待摄像头数据就绪 |
| pthread（QThread） | v4l2capture、processthread | 采集线程、处理线程 |
| atomic | framequeue.h、videosource.h | 无锁队列索引、线程控制标志 |
| signal（间接） | v4l2capture.cpp | xioctl 处理 EINTR |
| socket（间接） | NFS 挂载 | 开发板和虚拟机之间传输文件 |
| sysfs（未直接用） | — | 后续 M4 开发可能用于 GPIO 控制 |
| procfs（未直接用） | — | 调试时查看进程状态和系统信息 |
