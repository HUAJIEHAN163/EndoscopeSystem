# 阶段 2：Linux 系统 + A7-M4 通信
## 从零搭建 Linux 环境，实现 A7 控制 M4

---

## Step 2.1：Linux 开发环境搭建

### 安装 Ubuntu 虚拟机

```bash
# 1. 下载 VMware Workstation Player（免费）或 VirtualBox
# 2. 下载 Ubuntu 22.04 LTS ISO
# 3. 创建虚拟机：
#    - 内存：4GB 以上（推荐 8GB）
#    - 硬盘：60GB 以上（编译内核需要空间）
#    - CPU：4 核以上
```

### 安装必要工具

```bash
# 更新系统
sudo apt update && sudo apt upgrade -y

# 安装基础开发工具
sudo apt install -y build-essential git wget curl vim

# 安装交叉编译工具链（编译 A7 程序）
sudo apt install -y gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf

# 安装网络工具（与开发板通信）
sudo apt install -y openssh-client sshpass nfs-kernel-server

# 安装 STM32MP1 相关工具
sudo apt install -y libusb-1.0-0-dev stlink-tools

# 验证交叉编译器
arm-linux-gnueabihf-gcc --version
# 输出：arm-linux-gnueabihf-gcc (Ubuntu ...) 11.x.x
```

---

## Step 2.2：开发板 Linux 系统烧录

### 获取系统镜像

```bash
# 野火 MP157 提供预编译的 Linux 镜像
# 下载地址：野火官网 → 资料下载 → STM32MP157 → 系统镜像

# 镜像文件通常包含：
# ├── u-boot.stm32        ← 引导程序
# ├── zImage               ← Linux 内核
# ├── stm32mp157c-xxx.dtb  ← 设备树
# └── rootfs.ext4          ← 根文件系统
```

### 烧录到 SD 卡

```bash
# 1. 将 SD 卡插入 PC（通过读卡器）
# 2. 确认 SD 卡设备名
lsblk
# 假设 SD 卡是 /dev/sdb

# 3. 烧录镜像（使用野火提供的烧录工具或 dd 命令）
# 注意：dd 会清除 SD 卡所有数据！
sudo dd if=sdcard.img of=/dev/sdb bs=4M status=progress
sync
```

### 启动开发板

```bash
# 1. 将 SD 卡插入开发板
# 2. 拨码开关设置为 SD 卡启动模式（参考开发板手册）
# 3. 连接串口线（USB 转 TTL → 开发板调试串口）
# 4. 连接网线（开发板 → 路由器/PC）
# 5. 上电

# 在串口终端看到 Linux 启动日志：
# U-Boot 2020.xx ...
# Starting kernel ...
# [    0.000000] Booting Linux on physical CPU 0x0
# ...
# Welcome to Buildroot
# login: root
# Password: root（或空密码）
```

### 配置网络

```bash
# 在开发板上执行
ifconfig eth0
# 记录 IP 地址，例如 192.168.1.100

# 在 Ubuntu 虚拟机上测试连通性
ping 192.168.1.100

# SSH 登录开发板
ssh root@192.168.1.100
```

---

## Step 2.3：交叉编译 Hello World

### 在 Ubuntu 上编写程序

```bash
mkdir -p ~/mp157/hello
cd ~/mp157/hello
```

```c
// hello.c
#include <stdio.h>

int main() {
    printf("Hello from Cortex-A7!\n");
    printf("This runs on Linux.\n");
    return 0;
}
```

### 交叉编译

```bash
# 编译
arm-linux-gnueabihf-gcc hello.c -o hello

# 确认是 ARM 程序
file hello
# 输出：hello: ELF 32-bit LSB executable, ARM, ...
```

### 部署到开发板

```bash
# 方法 1：SCP 传输
scp hello root@192.168.1.100:/root/

# 方法 2：NFS 共享（开发阶段更方便）
# Ubuntu 上配置 NFS：
echo "/home/$USER/mp157 *(rw,sync,no_root_squash)" | sudo tee -a /etc/exports
sudo exportfs -ra
sudo systemctl restart nfs-kernel-server

# 开发板上挂载：
mkdir -p /mnt/nfs
mount -t nfs 192.168.1.200:/home/user/mp157 /mnt/nfs
# 192.168.1.200 是 Ubuntu 虚拟机的 IP
```

### 在开发板上运行

```bash
# SSH 登录开发板后
chmod +x /root/hello
./hello
# 输出：Hello from Cortex-A7!
# 输出：This runs on Linux.
```

**如果看到输出，说明交叉编译环境搭建成功！**

---

## Step 2.4：M4 固件添加 OpenAMP 支持

### 概念说明

```
当前 M4 固件（Engineering Boot Mode）：
  M4 独立运行，通过 USART3 与 PC 通信

目标 M4 固件（Production Mode）：
  M4 由 Linux 启动，通过 RPMsg 与 A7 通信
  USART3 仍然保留用于调试
```

### CubeMX 配置

```
1. 打开 STM32CubeIDE 中的 .ioc 文件
2. Middleware → OPENAMP → 勾选启用
3. 配置 IPCC（Inter-Processor Communication Controller）：
   IPCC → 启用
   NVIC → IPCC RX1 Interrupt → 启用
   NVIC → IPCC TX1 Interrupt → 启用
4. 生成代码
```

### M4 侧 RPMsg 代码

**rpmsg_handler.h：**

```c
#ifndef RPMSG_HANDLER_H
#define RPMSG_HANDLER_H

#include "openamp.h"

void RPMsg_Init(void);
void RPMsg_Process(void);  // 在主循环中调用
void RPMsg_Send(const char *msg);

#endif
```

**rpmsg_handler.c：**

```c
#include "rpmsg_handler.h"
#include "openamp_interface.h"
#include "app_control.h"
#include <string.h>

static VIRT_UART_HandleTypeDef huart0;  // 虚拟串口
static volatile uint8_t rpmsg_recv_flag = 0;
static char rpmsg_recv_buf[64];
static uint16_t rpmsg_recv_len = 0;

// RPMsg 接收回调（中断上下文）
static void RPMsg_RxCallback(VIRT_UART_HandleTypeDef *huart) {
    rpmsg_recv_len = huart->RxXferSize;
    if (rpmsg_recv_len < sizeof(rpmsg_recv_buf)) {
        memcpy(rpmsg_recv_buf, huart->pRxBuffPtr, rpmsg_recv_len);
        rpmsg_recv_buf[rpmsg_recv_len] = '\0';
        rpmsg_recv_flag = 1;
    }
}

void RPMsg_Init(void) {
    // 初始化 OpenAMP 框架
    MX_OPENAMP_Init(RPMSG_REMOTE, NULL);

    // 创建虚拟串口端点
    if (VIRT_UART_Init(&huart0) != VIRT_UART_OK) {
        // 初始化失败处理
        return;
    }

    // 注册接收回调
    VIRT_UART_RegisterCallback(&huart0, VIRT_UART_RXCPLT_CB_ID,
                               RPMsg_RxCallback);
}

void RPMsg_Process(void) {
    // 必须在主循环中调用，处理 OpenAMP 消息
    OPENAMP_check_for_message();

    if (rpmsg_recv_flag) {
        rpmsg_recv_flag = 0;

        // 复用现有的命令解析逻辑
        // rpmsg_recv_buf 中的内容格式与串口命令相同
        if (strcmp(rpmsg_recv_buf, "on") == 0) {
            // 调用已有的 LED 控制函数
            RPMsg_Send("LED ON\r\n");
        } else if (strcmp(rpmsg_recv_buf, "off") == 0) {
            RPMsg_Send("LED OFF\r\n");
        } else if (strncmp(rpmsg_recv_buf, "bright ", 7) == 0) {
            int val = atoi(rpmsg_recv_buf + 7);
            LED_SetBrightness((uint8_t)val);
            char reply[32];
            snprintf(reply, sizeof(reply), "Brightness: %d%%\r\n", val);
            RPMsg_Send(reply);
        } else if (strcmp(rpmsg_recv_buf, "temp") == 0) {
            float temp = App_ReadTemperature();
            char reply[32];
            snprintf(reply, sizeof(reply), "Temp: %.1f C\r\n", temp);
            RPMsg_Send(reply);
        }
    }
}

void RPMsg_Send(const char *msg) {
    VIRT_UART_Transmit(&huart0, (uint8_t *)msg, strlen(msg));
}
```

**main.c 修改：**

```c
int main(void) {
    HAL_Init();
    if (IS_ENGINEERING_BOOT_MODE()) {
        SystemClock_Config();
    }

    MX_GPIO_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_ADC1_Init();
    MX_USART3_UART_Init();
    MX_IPCC_Init();          // 新增：IPCC 初始化

    App_Init();
    RPMsg_Init();             // 新增：RPMsg 初始化

    while (1) {
        App_ProcessUartCmd();   // 串口命令（调试用）
        RPMsg_Process();        // RPMsg 命令（A7 通信）
    }
}
```

### 编译并导出固件

```
1. STM32CubeIDE → Build Project
2. 生成的 .elf 文件路径：
   CM4/Debug/stm32_device_control_CM4.elf
3. 将 .elf 文件复制到 Ubuntu 虚拟机
```

---

## Step 2.5：Linux 通过 remoteproc 启动 M4

### 部署 M4 固件

```bash
# 在开发板上（通过 SSH）

# 1. 将 M4 固件复制到指定目录
cp stm32_device_control_CM4.elf /lib/firmware/

# 2. 查看当前 M4 状态
cat /sys/class/remoteproc/remoteproc0/state
# 输出：offline

# 3. 设置要加载的固件名
echo stm32_device_control_CM4.elf > /sys/class/remoteproc/remoteproc0/firmware

# 4. 启动 M4
echo start > /sys/class/remoteproc/remoteproc0/state

# 5. 确认 M4 已启动
cat /sys/class/remoteproc/remoteproc0/state
# 输出：running

# 6. 查看 M4 的调试输出
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

### 常用操作脚本

```bash
#!/bin/bash
# start_m4.sh - 一键启动 M4

FW_NAME="stm32_device_control_CM4.elf"
RPROC="/sys/class/remoteproc/remoteproc0"

# 如果 M4 正在运行，先停止
STATE=$(cat $RPROC/state)
if [ "$STATE" = "running" ]; then
    echo "Stopping M4..."
    echo stop > $RPROC/state
    sleep 1
fi

# 设置固件并启动
echo $FW_NAME > $RPROC/firmware
echo start > $RPROC/state
echo "M4 started: $(cat $RPROC/state)"
```

---

## Step 2.6：RPMsg 双向通信

### 确认 RPMsg 设备节点

```bash
# M4 启动后，Linux 会自动创建 RPMsg 设备
ls /dev/ttyRPMSG*
# 输出：/dev/ttyRPMSG0

# 如果没有出现，检查：
dmesg | grep rpmsg
# 查看是否有错误信息
```

### 命令行测试通信

```bash
# 发送命令给 M4
echo "on" > /dev/ttyRPMSG0

# 接收 M4 的回复
cat /dev/ttyRPMSG0 &
# 后台运行，实时显示 M4 发来的消息

# 测试各种命令
echo "off" > /dev/ttyRPMSG0
echo "bright 50" > /dev/ttyRPMSG0
echo "temp" > /dev/ttyRPMSG0
echo "status" > /dev/ttyRPMSG0
```

---

## Step 2.7：A7 程序通过 RPMsg 控制 M4

### 编写 A7 控制程序

```bash
mkdir -p ~/mp157/led_control
cd ~/mp157/led_control
```

**rpmsg_comm.h：**

```c
#ifndef RPMSG_COMM_H
#define RPMSG_COMM_H

int rpmsg_open(const char *dev);
int rpmsg_send(int fd, const char *cmd);
int rpmsg_recv(int fd, char *buf, int buf_size, int timeout_ms);
void rpmsg_close(int fd);

#endif
```

**rpmsg_comm.c：**

```c
#include "rpmsg_comm.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>

int rpmsg_open(const char *dev) {
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("Failed to open RPMsg device");
    }
    return fd;
}

int rpmsg_send(int fd, const char *cmd) {
    int len = strlen(cmd);
    int ret = write(fd, cmd, len);
    if (ret != len) {
        perror("RPMsg send failed");
        return -1;
    }
    return 0;
}

int rpmsg_recv(int fd, char *buf, int buf_size, int timeout_ms) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        int n = read(fd, buf, buf_size - 1);
        if (n > 0) {
            buf[n] = '\0';
            return n;
        }
    }
    return 0;  // 超时或无数据
}

void rpmsg_close(int fd) {
    close(fd);
}
```

**main.c（A7 侧控制程序）：**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rpmsg_comm.h"

#define RPMSG_DEV "/dev/ttyRPMSG0"

void print_usage(void) {
    printf("Usage:\n");
    printf("  led_control on          - Turn LED on\n");
    printf("  led_control off         - Turn LED off\n");
    printf("  led_control bright <N>  - Set brightness (0-100)\n");
    printf("  led_control temp        - Read temperature\n");
    printf("  led_control status      - Get status\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int fd = rpmsg_open(RPMSG_DEV);
    if (fd < 0) return 1;

    // 构造命令
    char cmd[64] = {0};
    if (strcmp(argv[1], "bright") == 0 && argc >= 3) {
        snprintf(cmd, sizeof(cmd), "bright %s", argv[2]);
    } else {
        strncpy(cmd, argv[1], sizeof(cmd) - 1);
    }

    // 发送命令
    printf("Sending to M4: %s\n", cmd);
    rpmsg_send(fd, cmd);

    // 等待回复（500ms 超时）
    char reply[128];
    int n = rpmsg_recv(fd, reply, sizeof(reply), 500);
    if (n > 0) {
        printf("M4 reply: %s", reply);
    } else {
        printf("No reply from M4 (timeout)\n");
    }

    rpmsg_close(fd);
    return 0;
}
```

### Makefile

```makefile
CC = arm-linux-gnueabihf-gcc
CFLAGS = -Wall -O2
TARGET = led_control
SRCS = main.c rpmsg_comm.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

deploy: $(TARGET)
	scp $(TARGET) root@192.168.1.100:/usr/bin/

.PHONY: clean deploy
```

### 编译、部署、测试

```bash
# 在 Ubuntu 上编译
cd ~/mp157/led_control
make

# 部署到开发板
make deploy

# 在开发板上测试
ssh root@192.168.1.100

led_control on
# 输出：Sending to M4: on
# 输出：M4 reply: LED ON

led_control bright 60
# 输出：Sending to M4: bright 60
# 输出：M4 reply: Brightness: 60%

led_control temp
# 输出：Sending to M4: temp
# 输出：M4 reply: Temp: 28.5 C
```

---

## 阶段 2 验收清单

- [ ] Ubuntu 虚拟机安装完成，交叉编译器可用
- [ ] 开发板 Linux 系统启动正常，能 SSH 登录
- [ ] Hello World 交叉编译并在开发板上运行成功
- [ ] M4 固件添加 OpenAMP 支持，编译通过
- [ ] remoteproc 启动 M4 成功
- [ ] `/dev/ttyRPMSG0` 设备节点出现
- [ ] 命令行 `echo "on" > /dev/ttyRPMSG0` 能控制 LED
- [ ] A7 程序 `led_control` 能通过 RPMsg 控制 M4

**阶段 2 完成后，A7 和 M4 的通信链路已打通，可以进入阶段 3。**

---

→ 下一步：`03_阶段3_图像处理集成.md`
