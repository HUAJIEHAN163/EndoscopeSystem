# STM32MP157 Linux开发学习路线
## 面向异构芯片（Linux + MCU）快速上手指南

---

## 📌 为什么必须学习Linux开发

### 1. STM32MP157的真实工作模式

你现在的开发方式是**Engineering Boot Mode**（工程调试模式），这只是开发阶段的特殊模式。

**实际产品的启动流程：**

```
上电
 ↓
Cortex-A7 启动 Linux 系统（主控）
 ↓
Linux 加载 M4 固件到共享内存
 ↓
Linux 通过 remoteproc 框架启动 M4 核
 ↓
A7 和 M4 通过 RPMsg/共享内存通信
```

**关键点：**
- M4 核心**不是独立运行**的，而是由 Linux 侧管理和启动
- M4 的时钟、电源、复位都由 Linux 控制
- M4 固件的部署、调试、日志查看都在 Linux 环境下进行

### 2. 公司实际项目中的分工

以医疗内窥镜为例：

| 核心 | 操作系统 | 负责功能 | 你需要掌握的技能 |
|------|---------|---------|----------------|
| **Cortex-A7** | Linux | • 图像处理算法<br>• UI 显示<br>• 网络传输<br>• 文件存储<br>• USB 主机 | **Linux 应用开发**<br>**设备树配置**<br>**驱动调试** |
| **Cortex-M4** | 裸机 | • 实时控制（电机、补光灯）<br>• 传感器采集<br>• 快速响应（<1ms） | **你现在掌握的技能**<br>（HAL 库开发） |

**你需要做的工作：**
1. 在 Linux 侧编写应用程序，通过 RPMsg 向 M4 发送控制命令
2. 配置设备树，分配硬件资源（哪些 GPIO 给 M4 用）
3. 编译 Linux 内核和根文件系统
4. 调试 A7 和 M4 的通信问题

### 3. Windows vs Linux 开发的本质区别

| 开发内容 | Windows | Linux |
|---------|---------|-------|
| **M4 裸机开发**<br>（你现在做的） | ✅ STM32CubeIDE | ✅ STM32CubeIDE |
| **Linux 内核编译** | ❌ 不支持 | ✅ 原生支持 |
| **设备树修改** | ❌ 不支持 | ✅ 原生支持 |
| **交叉编译工具链** | ❌ 配置复杂 | ✅ 原生支持 |
| **A7 应用开发** | ❌ 无法测试 | ✅ 可直接部署到板子 |
| **remoteproc/rpmsg 调试** | ❌ 不支持 | ✅ 原生支持 |

**结论：M4 开发在 Windows 和 Linux 下没区别，但 A7 开发必须在 Linux 下进行。**

---

## 🎯 学习内容与优先级

### 优先级 P0（入职前必须掌握，1-2周）

#### 1. Linux 基础命令（3天）

**为什么学：** 你需要在开发板的 Linux 终端上操作文件、查看日志、启动 M4。

**必须掌握的命令：**

```bash
# 文件操作
ls -lh              # 查看文件详细信息
cd /lib/firmware    # 切换目录
cp m4_fw.elf /lib/firmware/  # 复制 M4 固件
cat /proc/cpuinfo   # 查看 CPU 信息
vi /etc/config      # 编辑配置文件（至少会基本操作）

# 进程管理
ps aux | grep app   # 查看进程
kill -9 1234        # 强制结束进程
top                 # 实时查看系统资源

# 权限管理
chmod +x app        # 给程序添加执行权限
chown root:root app # 修改文件所有者

# 日志查看
dmesg | tail -20    # 查看内核日志（M4 启动失败时必看）
cat /sys/kernel/debug/remoteproc/remoteproc0/trace  # M4 的 printf 输出
```

**学习方式：**
- 在虚拟机 Ubuntu 中练习（不要只看，必须手敲）
- 重点练习 `vi` 编辑器（至少会 `i` 插入、`Esc` 退出编辑、`:wq` 保存退出）

---

#### 2. 交叉编译（3天）

**为什么学：** 你在 PC 上写的 C 程序需要编译成 ARM 架构的可执行文件才能在开发板上运行。

**核心概念：**

```
┌─────────────────────────────────────────────┐
│  你的 PC（x86_64 架构）                      │
│  ↓ 使用交叉编译器                            │
│  arm-linux-gnueabihf-gcc                    │
│  ↓ 编译出 ARM 架构的程序                     │
│  app（可在 STM32MP157 的 A7 核上运行）       │
└─────────────────────────────────────────────┘
```

**实战练习：**

```bash
# 1. 安装交叉编译工具链
sudo apt install gcc-arm-linux-gnueabihf

# 2. 编写测试程序 hello.c
cat > hello.c << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello from A7 core!\n");
    return 0;
}
EOF

# 3. 交叉编译
arm-linux-gnueabihf-gcc hello.c -o hello

# 4. 查看编译结果
file hello  # 输出：ELF 32-bit LSB executable, ARM

# 5. 传到开发板（假设开发板 IP 是 192.168.1.100）
scp hello root@192.168.1.100:/root/

# 6. 在开发板上运行
ssh root@192.168.1.100
./hello  # 输出：Hello from A7 core!
```

**必须理解的概念：**
- 本地编译 vs 交叉编译
- 静态链接 vs 动态链接（`-static` 参数）
- 库文件路径问题（`-L` 和 `-l` 参数）

---

#### 3. remoteproc 框架（M4 管理）（2天）

**为什么学：** 这是 Linux 管理 M4 核心的核心机制，你每天都会用到。

**核心操作：**

```bash
# 查看 M4 状态
cat /sys/class/remoteproc/remoteproc0/state
# 输出：running 或 offline

# 停止 M4
echo stop > /sys/class/remoteproc/remoteproc0/state

# 部署新固件（你在 Windows 上编译的 .elf 文件）
cp your_m4_firmware.elf /lib/firmware/

# 启动 M4
echo start > /sys/class/remoteproc/remoteproc0/state

# 查看 M4 的 printf 输出（重要！）
cat /sys/kernel/debug/remoteproc/remoteproc0/trace

# 查看启动失败原因
dmesg | grep remoteproc
```

**实战任务：**
1. 把你现在的 M4 项目编译成 `.elf` 文件
2. 在 Linux 下通过 remoteproc 启动它
3. 通过 trace 文件查看串口输出

---

#### 4. RPMsg 通信（A7 与 M4 通信）（3天）

**为什么学：** 这是 A7 和 M4 之间传递数据的标准方式。

**通信原理：**

```
┌─────────────────────────────────────────────┐
│  A7 (Linux)          共享内存          M4   │
│     ↓                  ↓                ↓   │
│  /dev/rpmsg0  ←→  RPMsg 框架  ←→  虚拟串口 │
└─────────────────────────────────────────────┘
```

**A7 侧代码示例（你需要写的）：**

```c
// a7_control.c - 在 Linux 上运行，控制 M4 的 LED
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main() {
    int fd = open("/dev/rpmsg0", O_RDWR);
    if (fd < 0) {
        perror("打开 rpmsg 设备失败");
        return -1;
    }

    // 发送命令给 M4
    char cmd[] = "on\r\n";
    write(fd, cmd, strlen(cmd));
    printf("已发送命令: %s", cmd);

    // 接收 M4 的回复
    char buf[64];
    int len = read(fd, buf, sizeof(buf));
    if (len > 0) {
        buf[len] = '\0';
        printf("M4 回复: %s\n", buf);
    }

    close(fd);
    return 0;
}
```

**M4 侧修改（基于你现在的代码）：**

```c
// 在 app_control.c 中添加 RPMsg 支持
#include "openamp/open_amp.h"

// 替换原来的 UART 发送函数
static void RPMSG_SendString(const char *str) {
    VIRT_UART_Transmit(&huart0, (uint8_t *)str, strlen(str));
}
```

**学习重点：**
- 理解 `/dev/rpmsg0` 设备节点的作用
- 掌握 `open/read/write/close` 系统调用
- 了解 M4 侧的 OpenAMP 库（ST 提供的示例代码）

---

### 优先级 P1（入职后 1 个月内掌握）

#### 5. 设备树（Device Tree）（1周）

**为什么学：** 设备树告诉 Linux 内核硬件资源如何分配，哪些 GPIO 给 A7 用，哪些给 M4 用。

**核心概念：**

```dts
// stm32mp157c-custom.dts
&m4_rproc {
    memory-region = <&retram>, <&mcuram>;  // M4 的内存区域
    mboxes = <&ipcc 0>, <&ipcc 1>;         // A7 和 M4 的邮箱通信
    status = "okay";                        // 启用 M4
};

&usart3 {
    status = "disabled";  // USART3 分配给 M4，Linux 不使用
};

&gpioi {
    led_r {
        gpios = <&gpioi 0 GPIO_ACTIVE_LOW>;  // PI0 给 M4 控制 LED
    };
};
```

**学习任务：**
1. 理解设备树的基本语法（节点、属性、引用）
2. 学会修改引脚分配（`pinctrl` 子系统）
3. 学会编译设备树（`.dts` → `.dtb`）

```bash
# 编译设备树
dtc -I dts -O dtb -o custom.dtb stm32mp157c-custom.dts

# 部署到开发板
cp custom.dtb /boot/
reboot
```

---

#### 6. Linux 应用开发（1周）

**为什么学：** 你需要在 A7 侧写应用程序，实现 UI、网络、文件存储等功能。

**必须掌握的知识：**

```c
// 1. 文件 I/O
int fd = open("/dev/rpmsg0", O_RDWR);
write(fd, data, len);
read(fd, buf, sizeof(buf));
close(fd);

// 2. 多线程（pthread）
pthread_t tid;
pthread_create(&tid, NULL, thread_func, NULL);
pthread_join(tid, NULL);

// 3. 进程间通信（消息队列、共享内存）
int msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
msgsnd(msgid, &msg, sizeof(msg), 0);

// 4. 信号处理
signal(SIGINT, signal_handler);
```

**实战项目：**
写一个简单的 LED 控制程序，通过命令行参数控制 M4 的 LED：

```bash
./led_control on red    # 红色 LED 常亮
./led_control blink blue  # 蓝色 LED 闪烁
```

---

#### 7. Makefile 与构建系统（3天）

**为什么学：** 实际项目有几十个源文件，不能每次手动输入 `gcc` 命令。

**基础 Makefile 示例：**

```makefile
CC = arm-linux-gnueabihf-gcc
CFLAGS = -Wall -O2
TARGET = led_control

SRCS = main.c rpmsg.c utils.c
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
```

**学习重点：**
- 理解自动变量（`$@`、`$^`、`$<`）
- 掌握模式规则（`%.o: %.c`）
- 了解 CMake（更复杂的项目会用到）

---

### 优先级 P2（入职后 2-3 个月内掌握）

#### 8. Yocto/Buildroot（构建完整 Linux 系统）（2周）

**为什么学：** 公司的产品需要定制 Linux 系统，不能用通用发行版。

**核心概念：**
- Yocto：构建嵌入式 Linux 的工业标准工具
- Buildroot：更轻量的构建工具
- 作用：编译内核、根文件系统、工具链、应用程序，打包成完整镜像

**学习任务：**
1. 使用 Buildroot 构建一个最小 Linux 系统
2. 添加自己的应用程序到镜像中
3. 理解 `.config` 文件的作用

---

#### 9. Linux 驱动基础（2周）

**为什么学：** 有些硬件（如自定义传感器）需要写驱动才能在 Linux 下使用。

**最简单的字符设备驱动：**

```c
// hello_driver.c
#include <linux/module.h>
#include <linux/fs.h>

static int major;

static int hello_open(struct inode *inode, struct file *file) {
    printk("Hello driver opened\n");
    return 0;
}

static struct file_operations fops = {
    .open = hello_open,
};

static int __init hello_init(void) {
    major = register_chrdev(0, "hello", &fops);
    printk("Hello driver loaded, major=%d\n", major);
    return 0;
}

static void __exit hello_exit(void) {
    unregister_chrdev(major, "hello");
    printk("Hello driver unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
```

**学习重点：**
- 字符设备驱动框架
- 平台设备驱动（platform driver）
- 设备树与驱动的绑定

---

#### 10. 调试工具（1周）

**为什么学：** 程序出问题时需要快速定位原因。

**必须掌握的工具：**

```bash
# 1. GDB 远程调试
gdbserver :1234 ./app  # 在开发板上运行
arm-linux-gnueabihf-gdb app  # 在 PC 上连接
(gdb) target remote 192.168.1.100:1234
(gdb) break main
(gdb) continue

# 2. strace（跟踪系统调用）
strace ./app  # 查看程序调用了哪些系统函数

# 3. valgrind（内存泄漏检测）
valgrind --leak-check=full ./app

# 4. perf（性能分析）
perf record ./app
perf report
```

---

## 📚 学习资源推荐

### 书籍
1. **《鸟哥的 Linux 私房菜》** - Linux 基础命令（P0）
2. **《嵌入式 Linux 应用开发完全手册》** - 韦东山（P1）
3. **《Linux 设备驱动程序》** - LDD3（P2）

### 在线资源
1. ST 官方文档：[STM32MP157 Wiki](https://wiki.st.com/stm32mpu)
2. OpenAMP 官方示例：[GitHub - OpenAMP](https://github.com/OpenAMP/open-amp)
3. 野火 MP157 教程：[野火官网](http://doc.embedfire.com)

### 视频教程
1. 韦东山嵌入式 Linux 视频（B站免费）
2. 正点原子 STM32MP157 视频教程

---

## 🛠️ 实战项目建议

### 项目 1：LED 远程控制（1周，P0 阶段）

**目标：** 在 Linux 侧写程序，通过 RPMsg 控制 M4 的 LED。

**步骤：**
1. 在 M4 侧添加 RPMsg 支持（修改你现在的代码）
2. 在 A7 侧写 C 程序，打开 `/dev/rpmsg0` 发送命令
3. 测试 `on`、`off`、`blink` 命令是否生效

---

### 项目 2：温度监控系统（2周，P1 阶段）

**目标：** M4 读取温度传感器，通过 RPMsg 发送给 A7，A7 保存到文件并显示。

**涉及知识：**
- M4 侧：ADC 采集 + RPMsg 发送
- A7 侧：RPMsg 接收 + 文件 I/O + 多线程

---

### 项目 3：Web 控制界面（3周，P2 阶段）

**目标：** 在 A7 上运行 Web 服务器，通过浏览器控制 M4 的 LED。

**技术栈：**
- A7 侧：lighttpd + CGI 脚本
- M4 侧：保持现有代码
- 通信：Web → A7 应用 → RPMsg → M4

---

## ⏱️ 学习时间规划

| 阶段 | 时间 | 学习内容 | 验收标准 |
|------|------|---------|---------|
| **入职前** | 1-2周 | P0（Linux 命令、交叉编译、remoteproc、RPMsg） | 能在 Linux 下启动 M4 并通过 RPMsg 通信 |
| **第 1 个月** | 4周 | P1（设备树、应用开发、Makefile） | 完成项目 1 和项目 2 |
| **第 2-3 个月** | 8周 | P2（Yocto、驱动、调试工具） | 能独立构建系统镜像，写简单驱动 |

---

## ✅ 入职前检查清单

在入职前，确保你能完成以下任务：

- [ ] 在虚拟机 Ubuntu 中熟练使用 `ls`、`cd`、`vi`、`cat`、`grep` 等命令
- [ ] 能用交叉编译器编译一个 Hello World 程序并在开发板上运行
- [ ] 理解 remoteproc 的作用，能通过命令行启动/停止 M4
- [ ] 知道 RPMsg 是什么，能说出 A7 和 M4 通信的基本流程
- [ ] 能看懂简单的设备树代码（知道 `status`、`gpios` 等属性的含义）
- [ ] 能写一个简单的 Makefile 编译多个 `.c` 文件

**如果以上都能做到，你就具备了入职的基本能力！**

---

## 💡 学习建议

1. **边学边做**：不要只看教程，必须在虚拟机里实际操作
2. **保留笔记**：把每个命令的作用、遇到的错误都记录下来
3. **问对问题**：入职后遇到问题，先用 `dmesg`、`strace` 自己排查，再问同事
4. **代码复用**：把你现在的 M4 代码保留好，入职后可以直接改造成 RPMsg 版本

**记住：你现在掌握的 M4 开发技能是基础，Linux 开发是为了让 M4 在实际产品中发挥作用。两者结合才是完整的嵌入式开发能力！**
