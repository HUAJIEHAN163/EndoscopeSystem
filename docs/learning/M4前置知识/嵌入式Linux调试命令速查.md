# 嵌入式 Linux 调试命令速查

本文档汇总了 STM32MP157 内窥镜项目开发中实际使用过的 Linux 调试命令。

---

## 一、GPIO 与引脚复用

### 1.1 查看所有引脚占用状态

```bash
cat /sys/kernel/debug/pinctrl/soc:pin-controller@50002000/pinmux-pins
```

输出示例：
```
pin 9 (PA9): device 58007000.sdmmc function analog group PA9    # 被 SDMMC 占用
pin 22 (PB6): UNCLAIMED                                          # 空闲可用
pin 94 (PF14): device 4c002000.i2c function af5 group PF14      # 被 I2C1 占用
```

**用途**：确认某个 GPIO 是否被 A7 侧设备树占用，M4 能否使用。

### 1.2 查找特定引脚

```bash
# 查找特定 GPIO
cat /sys/kernel/debug/pinctrl/soc:pin-controller@50002000/pinmux-pins | grep "PB10\|PB6\|PA9"

# 查找特定外设占用的引脚
cat /sys/kernel/debug/pinctrl/soc:pin-controller@50002000/pinmux-pins | grep "i2c\|usart\|tim"
```

### 1.3 GPIO sysfs 操作（测试用）

```bash
# 导出 GPIO（PB6 = 编号 22）
echo 22 > /sys/class/gpio/export

# 设置方向
echo out > /sys/class/gpio/gpio22/direction

# 拉高/拉低
echo 1 > /sys/class/gpio/gpio22/value
echo 0 > /sys/class/gpio/gpio22/value

# 读取输入
echo in > /sys/class/gpio/gpio22/direction
cat /sys/class/gpio/gpio22/value

# 释放
echo 22 > /sys/class/gpio/unexport
```

**GPIO 编号计算**：`编号 = 字母序号 × 16 + 引脚号`
- PA9 = 0×16 + 9 = 9
- PB6 = 1×16 + 6 = 22
- PB10 = 1×16 + 10 = 26
- PB13 = 1×16 + 13 = 29
- PH7 = 7×16 + 7 = 119

### 1.4 查看 GPIO 控制器信息

```bash
cat /sys/kernel/debug/gpio
```

输出各 GPIO bank 的使用情况。

---

## 二、设备树

### 2.1 查看设备树节点

```bash
# 查找特定节点
find /sys/firmware/devicetree -name "*key*" -o -name "*button*" 2>/dev/null

# 读取节点属性（注意：值可能包含 \0，需要 tr 转换）
cat /sys/firmware/devicetree/base/gpio-keys/status 2>/dev/null | tr '\0' '\n'

# 查看节点标签
cat /sys/firmware/devicetree/base/gpio-keys/Key0/label 2>/dev/null | tr '\0' '\n'
```

### 2.2 设备树 Overlay（dtbo）

```bash
# 查看当前加载的 overlay
cat /boot/uEnv.txt | grep dtoverlay

# 禁用某个 overlay（注释掉）
sed -i 's|^dtoverlay=.*stm-fire-key.dtbo|#&|' /boot/uEnv.txt

# 修改后需要重启生效
reboot
```

**本项目禁用的 overlay**：
- `stm-fire-key.dtbo`：释放 KEY1/KEY2 的 GPIO 给 M4
- `stm-fire-can.dtbo`：释放 PA11/PA12 给软件 I2C

### 2.3 查看 reserved memory（OpenAMP 共享内存）

```bash
ls /sys/firmware/devicetree/base/reserved-memory/
cat /sys/firmware/devicetree/base/reserved-memory/*/size | xxd
```

---

## 三、中断

### 3.1 查看中断注册情况

```bash
cat /proc/interrupts

# 查找特定中断
cat /proc/interrupts | grep -i "Key\|gpio\|rpmsg\|dcmi"
```

输出示例：
```
89:         18          0  stm32gpio   7 Edge      Key 1
90:          8          0  stm32gpio  13 Edge      Key 0
```

**用途**：确认 A7 是否拦截了 M4 需要的 GPIO 中断。

---

## 四、M4 核心管理（remoteproc）

### 4.1 基本操作

```bash
# 指定固件
echo EndoscopeM4_v1.2_CM4.elf > /sys/class/remoteproc/remoteproc0/firmware

# 启动
echo start > /sys/class/remoteproc/remoteproc0/state

# 查看状态
cat /sys/class/remoteproc/remoteproc0/state    # running / offline

# 停止（注意：stop 后 RPMsg 通道损坏，必须重启开发板）
echo stop > /sys/class/remoteproc/remoteproc0/state
```

### 4.2 查看 M4 启动日志

```bash
dmesg | grep -E "remoteproc|rpmsg|virtio|m4"
```

正常输出：
```
remoteproc0: Booting fw image EndoscopeM4_v1.2_CM4.elf, size 2814928
m4@0#vdev0buffer: assigned reserved memory node vdev0buffer@10044000
virtio_rpmsg_bus virtio0: rpmsg host is online
virtio_rpmsg_bus virtio0: creating channel rpmsg-tty-channel addr 0x400
rpmsg_tty virtio0.rpmsg-tty-channel.-1.1024: new channel: 0x400 -> 0x400 : ttyRPMSG0
```

异常输出：
```
remoteproc0: no resource table found for this firmware    # ELF segment alignment 问题
remoteproc0: header-less resource table                    # 同上
```

---

## 五、RPMsg 通信

### 5.1 查看 RPMsg 设备

```bash
ls /dev/ttyRPMSG*
ls /sys/bus/rpmsg/devices/
```

### 5.2 手动收发数据

```bash
# 发送数据给 M4
echo "L50" > /dev/ttyRPMSG0

# 接收 M4 数据（Ctrl+C 退出）
cat /dev/ttyRPMSG0

# 后台接收
cat /dev/ttyRPMSG0 &
```

**注意**：M4 停止时不要 `cat /dev/ttyRPMSG0`，可能触发内核 oops。

---

## 六、摄像头与 DCMI

### 6.1 查看摄像头信息

```bash
# 列出视频设备
ls /dev/video*

# 查看设备详细信息
v4l2-ctl -d /dev/video0 --all

# 查看支持的格式
v4l2-ctl -d /dev/video0 --list-formats-ext
```

### 6.2 检查 DCMI overrun

```bash
dmesg | grep dcmi
# 输出示例：
# stm32-dcmi 4c006000.dcmi: Some errors found while streaming: errors=487 (overrun=490), buffers=8655
```

**overrun 含义**：摄像头输出帧数据时所有 DMA buffer 都被占用，数据丢失。

---

## 七、内核与系统信息

### 7.1 内核配置

```bash
# 查看内核编译配置
zcat /proc/config.gz | grep -i RPMSG
zcat /proc/config.gz | grep -i V4L2
```

### 7.2 内核日志

```bash
# 查看完整内核日志
dmesg

# 实时监控
dmesg -w

# 过滤关键字
dmesg | grep -E "overrun|error|fault|oops|panic"
```

### 7.3 进程管理

```bash
# 查找进程
pidof App
pidof endoscope

# 杀掉进程
kill $(pidof App) 2>/dev/null

# 查看进程 CPU 占用
top -d 1
```

### 7.4 LCD 背光控制

```bash
# 读取当前亮度
cat /sys/class/backlight/lcd-backlight/brightness

# 设置亮度（0-100）
echo 95 > /sys/class/backlight/lcd-backlight/brightness
```

---

## 八、网络与 NFS

### 8.1 NFS 挂载

```bash
mkdir -p /mnt/nfs
mount -t nfs 192.168.0.130:/home/chris/nfs_share /mnt/nfs -o nolock

# 卸载
umount /mnt/nfs
```

### 8.2 网络检查

```bash
ifconfig
ping 192.168.0.130
```

---

## 九、ELF 文件分析

### 9.1 查看 ELF 信息

```bash
# 查看 section 信息（在虚拟机上执行）
arm-none-eabi-objdump -h firmware.elf

# 查看 segment 信息
arm-none-eabi-readelf -l firmware.elf

# 查看符号表
arm-none-eabi-nm firmware.elf | grep -i "rpmsg\|virt_uart"

# 查看 resource table
arm-none-eabi-objdump -s -j .resource_table firmware.elf
```

**用途**：对比可工作的 ELF 和有问题的 ELF，排查 RPMsg 通道建立失败等问题。

---

## 十、调试技巧总结

### 10.1 引脚确认方法

确认某个 J1/J2 引脚对应哪个 GPIO 的标准流程：

1. **查数据手册**：找到该功能（如 USART1_TX）的所有可能 GPIO
2. **查 pinmux-pins**：看哪些 GPIO 被占用、哪些 UNCLAIMED
3. **GPIO sysfs 验证**：导出 UNCLAIMED 的 GPIO，拉高后用万用表或 LED 确认物理位置

### 10.2 外设冲突排查流程

1. 在 M4 代码中逐个注释外设初始化
2. 每次只启用一个外设，观察 A7 侧是否异常
3. 定位到冲突外设后，查设备树确认 A7 侧占用情况

### 10.3 overrun vs 内存错误的区分

| 现象 | 日志特征 | 原因 |
|---|---|---|
| overrun 退出 | dmesg: `stm32-dcmi ... overrun=X`，DQBUF错误 > 0 | 硬件资源不够 |
| double free | `free(): double free detected in tcache 2` | 软件内存 bug |
| alignment trap | dmesg: `Alignment trap ... Unhandled fault` | 软件内存 bug |
| segfault | `Segmentation fault` | 软件内存 bug |

**关键判断**：看程序日志中 `DQBUF错误` 是否为 0。如果为 0，说明 overrun 是程序崩溃的副作用，不是原因。

### 10.4 M4 功能逐步排查法

当 M4 运行导致 A7 异常时，逐步禁用 M4 功能定位根因：

```
全功能 → 禁用 Task_Temperature → 禁用 FreeRTOS → 禁用 TIM3 → 只保留 IPCC
```

每步测试 2~3 分钟（关键测试跑更长时间），找到第一个不出问题的版本，上一步就是根因。
