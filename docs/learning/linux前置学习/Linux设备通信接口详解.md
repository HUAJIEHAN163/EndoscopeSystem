# Linux 设备通信接口详解

## 一、总览

Linux 中通过 ioctl + 设备文件操作各种硬件外设，模式统一：open → ioctl 配置 → read/write 数据 → close。

| 接口 | 设备文件 | 速率 | 线数 | 典型用途 | 本项目关联 |
|---|---|---|---|---|---|
| I2C | /dev/i2c-X | 100-400KHz | 2（SDA+SCL） | 传感器、EEPROM、OLED | OV5640 寄存器读写（由驱动封装） |
| SPI | /dev/spidevX.Y | 1-50MHz | 4（MOSI+MISO+SCK+CS） | Flash、TFT 屏、ADC | 未使用 |
| UART | /dev/ttyXXX | 9600-115200 | 2（TX+RX） | 调试串口、A7-M4 通信 | minicom 串口控制台 |
| 网络 | socket fd | - | - | TCP/UDP 通信 | NFS 文件共享 |

---

## 二、I2C 通信

### 2.1 I2C 基础

I2C（Inter-Integrated Circuit）是两线制串行总线，一条数据线（SDA）一条时钟线（SCL），支持一主多从：

```
        SCL ─────┬──────┬──────┬─────
        SDA ─────┤      │      │
                 │      │      │
              [主机]  [从机A] [从机B]
              (A7)   (OV5640) (SHT30)
                     addr=0x3C addr=0x44
```

每个从机有唯一的 7 位地址（如 OV5640 是 0x3C），主机通过地址选择和哪个从机通信。

### 2.2 通信时序

```
主机发起通信：
  START → [从机地址(7bit) + 读/写(1bit)] → ACK → [数据] → ACK → ... → STOP

写寄存器（主机→从机）：
  START → [0x3C + W] → ACK → [寄存器地址] → ACK → [数据] → ACK → STOP

读寄存器（从机→主机）：
  START → [0x3C + W] → ACK → [寄存器地址] → ACK →
  START → [0x3C + R] → ACK → [数据] → NACK → STOP
```

### 2.3 Linux 用户空间操作

```cpp
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

// 1. 打开 I2C 总线
int fd = open("/dev/i2c-1", O_RDWR);

// 2. 设置从机地址
ioctl(fd, I2C_SLAVE, 0x3C);    // OV5640 的地址

// 3. 写寄存器
uint8_t buf[3] = {0x30, 0x08, 0x42};  // 寄存器地址(2字节) + 数据(1字节)
write(fd, buf, 3);

// 4. 读寄存器
uint8_t reg[2] = {0x30, 0x08};  // 先写寄存器地址
write(fd, reg, 2);
uint8_t value;
read(fd, &value, 1);            // 再读数据

// 5. 关闭
close(fd);
```

### 2.4 I2C ioctl 命令

| 命令 | 参数 | 含义 |
|---|---|---|
| I2C_SLAVE | 从机地址（7位） | 设置要通信的从机地址 |
| I2C_SLAVE_FORCE | 从机地址 | 强制设置（即使地址已被内核驱动占用） |
| I2C_TENBIT | 0 或 1 | 切换 7 位/10 位地址模式 |
| I2C_RDWR | struct i2c_rdwr_ioctl_data* | 复合读写（一次 ioctl 完成写+读） |

### 2.5 I2C_RDWR 复合读写

读寄存器需要先写地址再读数据，用 I2C_RDWR 可以一次完成：

```cpp
#include <linux/i2c.h>

struct i2c_msg msgs[2];
uint8_t reg_addr[2] = {0x30, 0x08};
uint8_t value;

// 第一条消息：写寄存器地址
msgs[0].addr = 0x3C;
msgs[0].flags = 0;           // 写
msgs[0].len = 2;
msgs[0].buf = reg_addr;

// 第二条消息：读数据
msgs[1].addr = 0x3C;
msgs[1].flags = I2C_M_RD;   // 读
msgs[1].len = 1;
msgs[1].buf = &value;

struct i2c_rdwr_ioctl_data data;
data.msgs = msgs;
data.nmsgs = 2;

ioctl(fd, I2C_RDWR, &data);
// value 中就是寄存器的值
```

### 2.6 i2c_msg 结构体

```cpp
struct i2c_msg {
    __u16 addr;     // 从机地址
    __u16 flags;    // 标志
    __u16 len;      // 数据长度
    __u8  *buf;     // 数据缓冲区
};
```

flags 常用值：

| 标志 | 值 | 含义 |
|---|---|---|
| 0 | 0 | 写操作（主机→从机） |
| I2C_M_RD | 0x0001 | 读操作（从机→主机） |
| I2C_M_TEN | 0x0010 | 10 位地址模式 |
| I2C_M_NOSTART | 0x4000 | 不发送 START 条件（连续传输） |

### 2.7 与我们项目的关系

OV5640 摄像头通过 I2C 连接到 STM32MP157，但我们**不直接操作 I2C**——V4L2 驱动内部封装了 I2C 通信：

```
我们的代码：
  ioctl(fd, VIDIOC_S_CTRL, &ctrl)     ← V4L2 ioctl

V4L2 驱动内部：
  → OV5640 驱动 (ov5640.c)
    → i2c_smbus_write_byte_data(client, reg, value)   ← I2C 操作
      → 硬件 I2C 控制器
        → SCL/SDA 总线
          → OV5640 Sensor 寄存器
```

如果后续做 M4 开发（P2/P10），A7 侧可能需要直接操作 I2C 读写温度传感器（SHT30）等外设。

### 2.8 常用 I2C 调试工具

```bash
# 扫描总线上的所有从机
i2cdetect -y 1

# 读取从机的所有寄存器
i2cdump -y 1 0x3C

# 读单个寄存器
i2cget -y 1 0x3C 0x30

# 写单个寄存器
i2cset -y 1 0x3C 0x30 0x42
```

---

## 三、SPI 通信

### 3.1 SPI 基础

SPI（Serial Peripheral Interface）是四线制高速串行总线：

```
        SCK  ──────────────────────
        MOSI ──────────────────────   (Master Out Slave In)
        MISO ──────────────────────   (Master In Slave Out)
        CS0  ──────┐
        CS1  ──────┤──────┐
                   │      │
                [从机A] [从机B]
                (Flash) (TFT屏)
```

| 线 | 方向 | 含义 |
|---|---|---|
| SCK | 主→从 | 时钟信号 |
| MOSI | 主→从 | 主机发送数据 |
| MISO | 从→主 | 从机返回数据 |
| CS | 主→从 | 片选，低电平有效，选择哪个从机 |

与 I2C 的区别：

| | I2C | SPI |
|---|---|---|
| 线数 | 2 | 4（+每个从机一根 CS） |
| 速率 | 100-400KHz | 1-50MHz |
| 寻址 | 地址（总线上广播） | CS 片选（硬件选择） |
| 全双工 | 否（半双工） | 是（MOSI 和 MISO 同时传输） |
| 适合场景 | 低速传感器、配置寄存器 | 高速数据传输（Flash、屏幕） |

### 3.2 SPI 模式

SPI 有 4 种模式，由时钟极性（CPOL）和时钟相位（CPHA）决定：

| 模式 | CPOL | CPHA | 含义 |
|---|---|---|---|
| Mode 0 | 0 | 0 | 空闲低电平，上升沿采样（最常用） |
| Mode 1 | 0 | 1 | 空闲低电平，下降沿采样 |
| Mode 2 | 1 | 0 | 空闲高电平，下降沿采样 |
| Mode 3 | 1 | 1 | 空闲高电平，上升沿采样 |

主机和从机必须使用相同的模式，否则数据错乱。查从机数据手册确定。

### 3.3 Linux 用户空间操作

```cpp
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

// 1. 打开 SPI 设备
int fd = open("/dev/spidev0.0", O_RDWR);
//                        ↑ 总线号  ↑ 片选号

// 2. 配置 SPI 参数
uint8_t mode = SPI_MODE_0;
ioctl(fd, SPI_IOC_WR_MODE, &mode);           // 设置模式

uint8_t bits = 8;
ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);  // 每字 8 位

uint32_t speed = 1000000;  // 1MHz
ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);  // 设置速率

// 3. 传输数据（全双工：同时发送和接收）
uint8_t tx[] = {0x9F};     // 发送：读 JEDEC ID 命令
uint8_t rx[4] = {0};       // 接收缓冲区

struct spi_ioc_transfer tr;
memset(&tr, 0, sizeof(tr));
tr.tx_buf = (unsigned long)tx;
tr.rx_buf = (unsigned long)rx;
tr.len = 4;
tr.speed_hz = speed;
tr.bits_per_word = bits;

ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
// rx 中就是从机返回的数据

// 4. 关闭
close(fd);
```

### 3.4 SPI ioctl 命令

| 命令 | 参数 | 含义 |
|---|---|---|
| SPI_IOC_WR_MODE | uint8_t* | 设置 SPI 模式（0/1/2/3） |
| SPI_IOC_RD_MODE | uint8_t* | 读取当前模式 |
| SPI_IOC_WR_BITS_PER_WORD | uint8_t* | 设置每字位数（通常 8） |
| SPI_IOC_WR_MAX_SPEED_HZ | uint32_t* | 设置最大时钟频率 |
| SPI_IOC_MESSAGE(N) | struct spi_ioc_transfer* | 执行 N 次传输 |

### 3.5 spi_ioc_transfer 结构体

```cpp
struct spi_ioc_transfer {
    __u64 tx_buf;          // 发送缓冲区地址（强转为 unsigned long）
    __u64 rx_buf;          // 接收缓冲区地址
    __u32 len;             // 传输字节数
    __u32 speed_hz;        // 本次传输的时钟频率（0 = 用默认值）
    __u16 delay_usecs;     // 传输后延迟（微秒）
    __u8  bits_per_word;   // 每字位数（0 = 用默认值）
    __u8  cs_change;       // 传输后是否切换 CS（多从机时用）
};
```

SPI 是全双工的——发送和接收同时进行。即使你只想读数据，也要发送（发送的内容无所谓，从机不看）；即使你只想写数据，rx_buf 也会收到数据（可以忽略）。

---

## 四、UART 串口通信

### 4.1 UART 基础

UART（Universal Asynchronous Receiver/Transmitter）是最简单的串行通信，两根线全双工：

```
  设备 A                设备 B
  TX ──────────────→ RX
  RX ←────────────── TX
  GND ─────────────── GND
```

异步通信——没有时钟线，双方约定相同的波特率（如 115200bps）。

### 4.2 数据帧格式

```
空闲(高) → [起始位(低)] [D0] [D1] [D2] [D3] [D4] [D5] [D6] [D7] [停止位(高)] → 空闲(高)
            1 bit        ←────── 8 bit 数据 ──────→      1 bit
```

常用配置：8N1 = 8 数据位 + 无校验 + 1 停止位（最常用）。

### 4.3 Linux 用户空间操作

```cpp
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

// 1. 打开串口
int fd = open("/dev/ttySTM0", O_RDWR | O_NOCTTY);
//                                      ↑ 不把串口作为控制终端

// 2. 配置串口参数
struct termios tty;
tcgetattr(fd, &tty);              // 获取当前配置

cfsetispeed(&tty, B115200);       // 输入波特率
cfsetospeed(&tty, B115200);       // 输出波特率

tty.c_cflag &= ~PARENB;          // 无校验
tty.c_cflag &= ~CSTOPB;          // 1 停止位
tty.c_cflag &= ~CSIZE;
tty.c_cflag |= CS8;              // 8 数据位
tty.c_cflag |= CLOCAL | CREAD;   // 本地连接 + 使能接收

tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // 原始模式（非行缓冲）
tty.c_iflag &= ~(IXON | IXOFF | ICRNL);           // 关闭软件流控
tty.c_oflag &= ~OPOST;                             // 原始输出

tty.c_cc[VMIN] = 1;              // 至少读 1 字节才返回
tty.c_cc[VTIME] = 10;            // 超时 1 秒（单位 0.1 秒）

tcsetattr(fd, TCSANOW, &tty);    // 立即生效

// 3. 发送数据
write(fd, "hello", 5);

// 4. 接收数据
char buf[256];
int n = read(fd, buf, sizeof(buf));  // 阻塞等待数据

// 5. 关闭
close(fd);
```

### 4.4 串口不用 ioctl

串口配置用的是 `termios` 结构体 + `tcsetattr/tcgetattr`，不是 ioctl。这是历史原因——UART 比 V4L2/I2C/SPI 出现得早，有自己的一套 API。

但底层仍然是 ioctl：`tcsetattr` 内部调用 `ioctl(fd, TCSETS, &tty)`。

### 4.5 termios 关键字段

```cpp
struct termios {
    tcflag_t c_cflag;    // 控制标志（波特率、数据位、停止位、校验）
    tcflag_t c_iflag;    // 输入标志（流控、回车转换）
    tcflag_t c_oflag;    // 输出标志（输出处理）
    tcflag_t c_lflag;    // 本地标志（回显、行缓冲）
    cc_t     c_cc[NCCS]; // 特殊字符（VMIN、VTIME）
};
```

c_cflag 常用设置：

| 标志 | 含义 |
|---|---|
| B115200 | 波特率 115200 |
| B9600 | 波特率 9600 |
| CS8 | 8 数据位 |
| CS7 | 7 数据位 |
| CSTOPB | 2 停止位（不设则 1 停止位） |
| PARENB | 使能校验（不设则无校验） |
| PARODD | 奇校验（需配合 PARENB） |
| CLOCAL | 忽略调制解调器状态线 |
| CREAD | 使能接收 |

c_lflag 常用设置：

| 标志 | 含义 | 说明 |
|---|---|---|
| ICANON | 行缓冲模式 | 收到换行符才返回（终端用） |
| ~ICANON | 原始模式 | 收到数据立即返回（通信用） |
| ECHO | 回显 | 输入的字符显示在终端 |

### 4.6 常用波特率

| 波特率 | 宏 | 每秒字节数 | 典型用途 |
|---|---|---|---|
| 9600 | B9600 | ~960 | GPS 模块、低速传感器 |
| 38400 | B38400 | ~3840 | 蓝牙模块 |
| 115200 | B115200 | ~11520 | 调试串口（我们项目用的） |
| 460800 | B460800 | ~46080 | 高速数据传输 |
| 921600 | B921600 | ~92160 | 最高常用速率 |

### 4.7 与我们项目的关系

```
当前使用：
  PC (minicom) ←── USB转串口 ──→ /dev/ttySTM0 (开发板调试串口)
  波特率 115200，用于查看 qDebug 输出和 dmesg

后续 M4 开发（P2）：
  A7 (/dev/ttySTMx) ←── UART ──→ M4 (HAL_UART)
  A7 发送指令，M4 执行（如按键事件、LED 控制）
```

### 4.8 常用串口设备文件

| 设备文件 | 含义 |
|---|---|
| /dev/ttySTM0 | STM32MP157 的硬件串口 |
| /dev/ttyUSB0 | USB 转串口适配器 |
| /dev/ttyACM0 | USB CDC 虚拟串口（如 Arduino） |
| /dev/ttyS0 | PC 的硬件串口（COM1） |
| /dev/ttyAMA0 | 树莓派的硬件串口 |

---

## 五、网络接口（Socket）

### 5.1 Socket 基础

网络通信不用 ioctl（用 socket API），但概念类似——创建 → 配置 → 读写 → 关闭：

```cpp
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// TCP 客户端示例

// 1. 创建 socket
int fd = socket(AF_INET, SOCK_STREAM, 0);
//               ↑ IPv4    ↑ TCP

// 2. 连接服务器
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port = htons(8080);                    // 端口号
inet_pton(AF_INET, "192.168.0.130", &addr.sin_addr);  // IP 地址
connect(fd, (struct sockaddr*)&addr, sizeof(addr));

// 3. 发送数据
write(fd, "hello", 5);

// 4. 接收数据
char buf[256];
int n = read(fd, buf, sizeof(buf));

// 5. 关闭
close(fd);
```

### 5.2 TCP vs UDP

| | TCP | UDP |
|---|---|---|
| socket 类型 | SOCK_STREAM | SOCK_DGRAM |
| 连接 | 需要 connect（三次握手） | 不需要连接 |
| 可靠性 | 保证送达、保证顺序 | 不保证 |
| 速度 | 较慢（有确认机制） | 快（无确认） |
| 适合场景 | 文件传输、HTTP、NFS | 视频流、DNS |

### 5.3 网络相关的 ioctl

虽然网络通信主要用 socket API，但网络接口配置用 ioctl：

```cpp
#include <net/if.h>
#include <sys/ioctl.h>

int fd = socket(AF_INET, SOCK_DGRAM, 0);
struct ifreq ifr;
strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ);

// 获取 IP 地址
ioctl(fd, SIOCGIFADDR, &ifr);

// 获取 MAC 地址
ioctl(fd, SIOCGIFHWADDR, &ifr);

// 获取接口状态（UP/DOWN）
ioctl(fd, SIOCGIFFLAGS, &ifr);
```

| 命令 | 含义 |
|---|---|
| SIOCGIFADDR | 获取 IP 地址 |
| SIOCSIFADDR | 设置 IP 地址 |
| SIOCGIFHWADDR | 获取 MAC 地址 |
| SIOCGIFFLAGS | 获取接口标志（UP/DOWN/RUNNING） |
| SIOCSIFFLAGS | 设置接口标志 |

### 5.4 与我们项目的关系

```
当前使用：
  开发板 (192.168.0.43) ←── WiFi ──→ 虚拟机 (192.168.0.130)
  NFS 挂载：mount -t nfs 192.168.0.130:/home/chris/nfs_share /mnt/nfs
  用于传输编译好的程序和拍照/录像文件

后续可能使用（P1）：
  FTP 服务：开发板跑 vsftpd，电脑用文件管理器访问
  HTTP 服务：Qt 内嵌简易 HTTP 服务，浏览器查看图片
  RTSP 流：实时视频流推送（高级）
```

---

## 六、四种接口对比总结

| | I2C | SPI | UART | Socket |
|---|---|---|---|---|
| 打开 | open("/dev/i2c-1") | open("/dev/spidev0.0") | open("/dev/ttySTM0") | socket() |
| 配置 | ioctl(I2C_SLAVE) | ioctl(SPI_IOC_WR_*) | tcsetattr() | connect()/bind() |
| 写数据 | write() 或 ioctl(I2C_RDWR) | ioctl(SPI_IOC_MESSAGE) | write() | write()/send() |
| 读数据 | read() 或 ioctl(I2C_RDWR) | ioctl(SPI_IOC_MESSAGE) | read() | read()/recv() |
| 关闭 | close() | close() | close() | close() |
| 全双工 | 否 | 是 | 是 | 是 |
| 速率 | 低（100-400K） | 高（1-50M） | 中（9600-921600） | 取决于网络 |
| 寻址 | 地址（软件） | CS 片选（硬件） | 点对点 | IP + 端口 |

所有接口都遵循 Linux 的"一切皆文件"原则——open 得到 fd，通过 fd 操作设备，最后 close。区别只在配置方式和数据传输方式。
