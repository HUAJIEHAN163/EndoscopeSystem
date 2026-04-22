# OpenAMP / RPMsg 核间通信前置知识

## 一、双核架构概述

### 1.1 STM32MP157 双核

```
┌─────────────────────────────────────────────┐
│              STM32MP157                       │
│                                              │
│  ┌──────────────┐    ┌──────────────┐        │
│  │  Cortex-A7   │    │  Cortex-M4   │        │
│  │  (650MHz)    │    │  (209MHz)    │        │
│  │  Linux       │    │  裸机/RTOS   │        │
│  │  Qt + OpenCV │    │  外设控制    │        │
│  └──────┬───────┘    └──────┬───────┘        │
│         │                    │                │
│         │    IPCC + 共享内存  │                │
│         └────────────────────┘                │
│                                              │
│  SRAM1/2 (256KB) ── M4 代码/数据             │
│  SRAM3 (64KB)   ── 共享内存 (vring+buffer)   │
└─────────────────────────────────────────────┘
```

### 1.2 各核职责

| | Cortex-A7 (主核) | Cortex-M4 (协处理器) |
|---|---|---|
| 操作系统 | Linux | 裸机 / FreeRTOS |
| 主要任务 | 图像处理、UI 显示 | 外设实时控制 |
| 启动方式 | 上电自动启动 | A7 通过 remoteproc 加载 |
| 通信接口 | `/dev/ttyRPMSG0` | VIRT_UART API |

---

## 二、通信协议栈

### 2.1 分层架构

```
A7 (Linux)                          M4 (裸机/FreeRTOS)
┌──────────────┐                   ┌──────────────┐
│ 应用层        │                   │ 应用层        │
│ echo/cat     │                   │ rpmsg_send_str│
├──────────────┤                   ├──────────────┤
│ tty 驱动层    │                   │ VIRT_UART    │
│ /dev/ttyRPMSG│                   │ Transmit/Rx  │
├──────────────┤                   ├──────────────┤
│ RPMsg        │                   │ RPMsg        │
│ rpmsg_tty    │                   │ OpenAMP      │
├──────────────┤                   ├──────────────┤
│ VirtIO       │                   │ VirtIO       │
│ virtio_rpmsg │                   │ rpmsg_virtio │
├──────────────┤                   ├──────────────┤
│ 传输层        │                   │ 传输层        │
│ IPCC 驱动    │  ←── 中断通知 ──→ │ IPCC HAL     │
│              │  ←── 共享内存 ──→ │              │
└──────────────┘                   └──────────────┘
```

### 2.2 各层职责

| 层 | A7 侧 | M4 侧 | 功能 |
|---|---|---|---|
| 应用层 | `echo/cat /dev/ttyRPMSG0` | `rpmsg_send_str()` | 收发字符串 |
| VIRT_UART | rpmsg_tty 内核驱动 | `VIRT_UART_Init/Transmit` | 虚拟串口封装 |
| RPMsg | virtio_rpmsg_bus | OpenAMP rpmsg | 消息通道管理 |
| VirtIO | virtio 子系统 | rpmsg_virtio | 虚拟设备抽象 |
| IPCC | IPCC 内核驱动 | `MAILBOX_Init/Poll/Notify` | 中断通知 |
| 共享内存 | reserved-memory | SRAM3 (0x10040000) | 数据传输 |

---

## 三、关键组件

### 3.1 IPCC（Inter-Processor Communication Controller）

硬件邮箱，用于 A7 和 M4 之间的**中断通知**（不传数据，只通知"有新消息"）。

```
Channel 1: A7 → M4 方向
  A7 写入 → M4 收到 RX 中断 → msg_received_ch1 = MBOX_BUF_FREE

Channel 2: M4 → A7 方向
  A7 写入 → M4 收到 RX 中断 → msg_received_ch2 = MBOX_NEW_MSG
```

### 3.2 共享内存布局

```
SRAM3 (0x10040000, 64KB)
├── vdev0vring0 @ 0x10040000 (8KB)  ── A7→M4 消息环
├── vdev0vring1 @ 0x10042000 (8KB)  ── M4→A7 消息环
└── vdev0buffer @ 0x10044000 (16KB) ── 消息数据缓冲区
```

### 3.3 Resource Table

M4 固件中的特殊数据结构，告诉 A7 的 remoteproc 驱动：
- VirtIO 设备描述（类型、特性）
- Vring 配置（地址、对齐、buffer 数量）
- Trace buffer（调试日志，可选）

```c
// 放在 ELF 的 .resource_table section 中
struct shared_resource_table {
    uint32_t version;     // = 1
    uint32_t num;         // resource entry 数量
    struct fw_rsc_vdev vdev;    // VirtIO 设备描述
    struct fw_rsc_vdev_vring vring0, vring1;  // Vring 配置
};
```

### 3.4 VIRT_UART（虚拟串口）

STM32 官方封装的虚拟串口层，简化 RPMsg 使用：

```c
// 初始化
VIRT_UART_HandleTypeDef huart0;
VIRT_UART_Init(&huart0);
VIRT_UART_RegisterCallback(&huart0, VIRT_UART_RXCPLT_CB_ID, RxCallback);

// 发送
VIRT_UART_Transmit(&huart0, "hello", 5);

// 接收（在回调中）
void RxCallback(VIRT_UART_HandleTypeDef *huart) {
    // huart->pRxBuffPtr = 接收数据指针
    // huart->RxXferSize = 接收数据长度
}
```

---

## 四、M4 固件加载流程

### 4.1 A7 侧操作

```bash
# 1. 复制固件到指定目录
cp firmware.elf /lib/firmware/

# 2. 设置固件文件名
echo firmware.elf > /sys/class/remoteproc/remoteproc0/firmware

# 3. 启动 M4
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. 确认状态
cat /sys/class/remoteproc/remoteproc0/state  # → running
```

### 4.2 启动过程

```
A7: echo start
    │
    ▼
remoteproc 驱动:
    1. 加载 ELF 到 SRAM
    2. 解析 .resource_table section
    3. 初始化 VirtIO/Vring（写入共享内存）
    4. 设置 vdev.status = DRIVER_OK
    5. 释放 M4 复位
    │
    ▼
M4 开始执行:
    1. HAL_Init()
    2. MX_IPCC_Init()
    3. MX_OPENAMP_Init() → 等待 status == DRIVER_OK → 初始化 vring
    4. VIRT_UART_Init() → 创建 RPMsg endpoint → A7 收到 name service
    │
    ▼
A7 内核:
    5. rpmsg_tty 驱动匹配 channel name → 创建 /dev/ttyRPMSG0
```

---

## 五、消息收发流程

### 5.1 M4 → A7 发送

```
M4: VIRT_UART_Transmit("T255\n", 5)
    │
    ▼
OPENAMP_send() → 写入 vring1 的 buffer
    │
    ▼
MAILBOX_Notify() → HAL_IPCC_NotifyCPU() → 触发 A7 侧 IPCC 中断
    │
    ▼
A7: IPCC 中断 → virtio_rpmsg_bus 处理 → rpmsg_tty 驱动 → /dev/ttyRPMSG0 可读
```

### 5.2 A7 → M4 接收

```
A7: echo "L50" > /dev/ttyRPMSG0
    │
    ▼
rpmsg_tty 驱动 → virtio_rpmsg_bus → 写入 vring0 的 buffer
    │
    ▼
IPCC 通知 M4 → M4 IPCC 中断 → msg_received_ch2 = MBOX_NEW_MSG
    │
    ▼
M4: OPENAMP_check_for_message() → MAILBOX_Poll() → rproc_virtio_notified()
    → VIRT_UART 回调 → rpmsg_rx_flag = 1
    │
    ▼
M4 主循环/任务: 检测 rpmsg_rx_flag → parse_command("L50")
```

### 5.3 关键：OPENAMP_check_for_message()

这个函数**必须在主循环/任务中频繁调用**。它不是中断驱动的，而是轮询 `msg_received_ch1/ch2` 标志（由 IPCC 中断设置），然后处理 virtio 回调链。

```c
// 必须频繁调用，否则收不到 A7 的消息
while (1) {
    OPENAMP_check_for_message();  // 处理接收
    // ... 其他逻辑 ...
    vTaskDelay(10);
}
```

---

## 六、本项目踩过的坑

### 6.1 GCC 13 segment alignment（问题 #20）

新版 GCC 链接器默认 `max-page-size=0x1000`，导致 ELF 的 segment alignment 与旧版 Linux remoteproc 驱动不兼容。

**修复**：`-Wl,-z,max-page-size=0x10000`

### 6.2 RPMsg service name 不匹配（问题 #20）

CubeMX 生成的默认值 `"rpmsg-tty"` 与野火开发板内核的 `rpmsg_tty` 驱动匹配的 `"rpmsg-tty-channel"` 不一致。

### 6.3 libmetal FreeRTOS 后端空实现（问题 #21）

`sys_irq_save_disable` / `sys_irq_restore_enable` 是空的模板函数，需要手动填写中断禁用/恢复实现。

### 6.4 内核 oops 导致 RPMsg 驱动异常（问题 #21）

M4 运行时直接停止（不先 kill 读取进程），会触发内核 oops，破坏 rpmsg_tty 驱动状态。**必须重启开发板恢复**。

**正确的停止顺序**：
```bash
kill -9 $(pgrep -f "cat /dev/ttyRPMSG") 2>/dev/null  # 先杀读取进程
sleep 1
echo stop > /sys/class/remoteproc/remoteproc0/state   # 再停止 M4
```

---

## 七、常见面试问题

### Q1：RPMsg 和物理 UART 的区别？

| | RPMsg | 物理 UART |
|---|---|---|
| 硬件 | 不需要额外引脚 | 需要 TX/RX 引脚 |
| 传输介质 | 芯片内部共享内存 | 物理串口线 |
| 速率 | 内存带宽（极快） | 115200bps 等 |
| 适用 | 同芯片双核通信 | 不同芯片间通信 |

### Q2：OpenAMP 的 resource table 有什么作用？

告诉 Linux remoteproc 驱动：M4 固件需要什么资源（VirtIO 设备、vring 配置、共享内存地址）。Linux 根据 resource table 初始化通信基础设施。

### Q3：为什么 OPENAMP_check_for_message 不能放在中断中？

它内部会处理 virtio 回调链，可能涉及内存分配、mutex 操作、endpoint 回调等复杂操作，不适合在中断上下文中执行。中断中只设置标志（`msg_received_ch1/ch2`），实际处理在任务/主循环中完成。

### Q4：VirtIO 的 vring 是什么？

Vring（Virtual Ring）= 环形缓冲区，用于生产者-消费者模式的数据传递。包含描述符表（descriptor table）、可用环（available ring）、已用环（used ring）。A7 和 M4 各操作一端，通过 IPCC 中断通知对方。
