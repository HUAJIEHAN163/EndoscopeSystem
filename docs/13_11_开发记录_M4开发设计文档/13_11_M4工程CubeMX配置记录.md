# M4 工程 CubeMX 配置记录

## 一、工程创建

| 项目 | 内容 |
|------|------|
| IDE | STM32CubeIDE 1.19.0（虚拟机 Linux） |
| 芯片型号 | STM32MP157AAC3 |
| 工程名称 | EndoscopeM4 |
| 工程路径 | `/mnt/hgfs/VM_Share/project/EndoscopeM4` |
| 工程类型 | 双核工程（自动生成 CM4 + CA7 子目录，仅使用 CM4） |

---

## 二、完整操作步骤（从新建工程到生成代码）

### 步骤 1：启动 CubeIDE

```bash
# 添加软链接（仅首次）
sudo ln -s /opt/st/stm32cubeide_1.19.0/stm32cubeide /usr/local/bin/stm32cubeide

# 启动
stm32cubeide &
```

### 步骤 2：新建 STM32 工程

1. **File → New → STM32 Project**
2. 芯片选择器中搜索 **STM32MP157AAC3**（不选带 T 后缀的），选中后点 **Next**
3. 填写工程信息：
   - Project Name: **EndoscopeM4**
   - 取消勾选 "Use default location"，手动指定路径：`/mnt/hgfs/VM_Share/project/EndoscopeM4`
4. 点 **Finish**，等待工程生成
5. 生成完成后，左侧 Project Explorer 中可以看到 **CM4** 和 **CA7** 两个子目录（双核工程），我们只使用 CM4

### 步骤 3：打开 CubeMX 配置界面

双击左侧 Project Explorer 中的 **EndoscopeM4.ioc** 文件，进入引脚配置图形界面。

### 步骤 4：配置 TIM2_CH3 PWM（LED 调光）

1. 在芯片引脚图上找到 **PB10**，右键选择 **TIM2_CH3**
2. 左侧面板展开 **Timers → TIM2**
3. Channel3 选择 **PWM Generation CH3**
4. 下方 Parameter Settings 中配置：
   - Prescaler (PSC): **63**
   - Counter Period (ARR): **999**
   - Pulse (CCR3): **0**
   - 其他保持默认
5. 确认 Runtime Context 为 **Cortex-M4**

### 步骤 5：配置 GPIO 按键（KEY1/KEY2）

1. 找到 **PB13**，右键选择 **GPIO_EXTI13**（注意：不是 GPIO_Input）
2. 找到 **PH7**，右键选择 **GPIO_EXTI7**
3. 左侧展开 **System Core → GPIO**，分别配置 PB13 和 PH7：
   - GPIO mode: **External Interrupt Mode with Falling edge trigger detection**
   - GPIO Pull-up/Pull-down: **Pull-up**
   - User Label: PB13 填 **KEY1**，PH7 填 **KEY2**
4. 左侧展开 **System Core → NVIC**，使能中断：
   - **EXTI7_IRQn** → Enabled（覆盖 PH7）
   - **EXTI13_IRQn** → Enabled（覆盖 PB13）
5. 确认 Runtime Context 为 **Cortex-M4**

> **踩坑记录**：如果引脚选成 GPIO_Input 而非 GPIO_EXTI，则 GPIO 配置面板中不会出现边沿触发选项，NVIC 中对应中断也无法勾选。必须选 GPIO_EXTIx 模式。

### 步骤 6：配置 I2C1（SHT30 温湿度传感器）

1. 左侧展开 **Connectivity → I2C1**
2. Mode 选择 **I2C**
3. Parameter Settings 中配置：
   - I2C Speed Mode: **Fast Mode**（400kHz）
   - 其他保持默认
4. **检查并修正引脚**：CubeMX 可能自动分配到错误引脚（如 PH11/PH12），必须手动修正：
   - 在引脚图上找到 **PF14**，右键选择 **I2C1_SCL**
   - 在引脚图上找到 **PF15**，右键选择 **I2C1_SDA**
5. 确认 I2C1 的 GPIO Settings 中显示 PF14 和 PF15
6. 确认 Runtime Context 为 **Cortex-M4**

> **踩坑记录**：CubeMX 默认分配的 PH11 被摄像头 DCMI 占用，PH12 被显示控制器占用。通过以下命令确认了正确引脚：
> ```bash
> cat /sys/kernel/debug/pinctrl/soc:pin-controller@50002000/pinmux-pins | grep "PH11\|PH12\|PF14\|PF15"
> # PF14 → i2c (正确)
> # PF15 → i2c (正确)
> # PH11 → dcmi (摄像头占用，不能用)
> # PH12 → display-controller (显示器占用，不能用)
> ```

### 步骤 7：配置 IPCC（核间通信）

1. 左侧展开 **System Core → IPCC**
2. Mode 选择 **Activated**
3. 确认 Runtime Context 为 **Cortex-M4**

### 步骤 8：生成代码

1. **Ctrl+S** 保存
2. 弹窗询问是否生成代码，点 **Yes**
3. 等待代码生成完成

### 步骤 9：验证生成结果

检查 CM4/Core/Src/ 下是否生成了以下文件：

| 文件 | 内容 |
|------|------|
| main.c | 主程序，调用各外设初始化函数 |
| tim.c | TIM2 CH3 PWM 配置，PB10 AF1 |
| i2c.c | I2C1 配置，PF14/PF15 AF5 |
| gpio.c | KEY1/KEY2 EXTI 中断配置 |
| ipcc.c | IPCC 核间通信初始化 |
| stm32mp1xx_it.c | EXTI7/EXTI13 中断处理函数 |

---

## 二、外设配置详情

### 2.1 TIM2_CH3 — PWM 输出（LED 调光）

**用途**：通过 PB10 输出 PWM 信号，直接驱动 HW-269 LED 模块实现无级调光。

**引脚配置**：

| 引脚 | 功能 | 复用 |
|------|------|------|
| PB10 | TIM2_CH3 | AF1 |

**参数配置**：

| 参数 | 值 | 说明 |
|------|-----|------|
| Channel3 Mode | PWM Generation CH3 | PWM 输出模式 |
| Prescaler (PSC) | 63 | 分频系数 = 63+1 = 64 |
| Counter Period (ARR) | 999 | 自动重装值 = 999+1 = 1000 |
| Pulse (CCR3) | 0 | 初始占空比 = 0%（LED 灭） |
| Counter Mode | Up | 向上计数 |
| OC Polarity | High | 高电平有效 |
| Auto-Reload Preload | Disable | 默认 |

**PWM 频率计算**：

```
假设 TIM2 时钟源 = 64MHz（HSI，Engineering Boot Mode）
PWM 频率 = 64MHz / (PSC+1) / (ARR+1) = 64MHz / 64 / 1000 = 1kHz
占空比 = Pulse / (ARR+1) × 100%
  - Pulse=0    → 0%（灭）
  - Pulse=500  → 50%（半亮）
  - Pulse=999  → 99.9%（最亮）
```

> **注意**：实际运行时 M4 由 A7 的 Linux 通过 remoteproc 加载，时钟由 A7 侧配置（通常 209MHz）。届时需要通过 `HAL_RCC_GetPCLK1Freq()` 获取实际 TIM2 时钟频率，重新调整 PSC 值以保持 1kHz PWM。

**为什么这样配置**：
- 1kHz PWM 频率足够高，人眼无法感知闪烁，实现平滑调光
- HW-269 LED 模块 data 引脚高电平点亮，PWM 占空比直接对应亮度
- 已通过 GPIO sysfs 验证 PB10 可直接驱动 HW-269，无需 MOSFET

---

### 2.2 GPIO 按键输入（KEY1/KEY2）+ TIM3 轮询中断

**用途**：检测板载按键按下事件，模拟内窥镜脚踏板控制（冻结画面/拍照）。

**引脚配置**：

| 引脚 | 功能 | User Label | 对应按键 | 内窥镜功能 |
|------|------|-----------|---------|-----------|
| PB13 | GPIO_EXTI13 | KEY1 | 板载 KEY1 | 冻结/恢复画面 |
| PH7 | GPIO_EXTI7 | KEY2 | 板载 KEY2 | 拍照 |

**GPIO 参数**（已修正）：

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | External Interrupt, Rising edge | 上升沿触发（高电平有效） |
| Pull-up/Pull-down | Pull-down | 下拉，松开时为低电平 |

> **踩坑记录 1 — 电平逻辑**：初始配置为上拉+下降沿（假设低电平有效），通过轮询测试发现实际按键是**高电平有效**（不按=低，按下=高），已修正为下拉+上升沿。

> **踩坑记录 2 — EXTI 中断无法触发**：STM32MP1 的 EXTI 控制器由 A7 侧 Linux 内核管理（通过 hwlock 仲裁），M4 通过 HAL 库配置的 EXTI 寄存器不能真正生效。详见问题记录 #19。

**解决方案 — TIM3 定时器中断轮询**：

由于 EXTI 中断无法使用，改用 TIM3 硬件定时器中断做 10ms 周期性轮询：

| 参数 | 值 | 说明 |
|------|-----|------|
| Clock Source | Internal Clock | 内部时钟 |
| Prescaler (PSC) | 6399 | 分频 = 6400 |
| Counter Period (ARR) | 99 | 计数 = 100 |
| TIM3 global interrupt | Enabled | NVIC 中断使能 |
| 中断频率 | 64MHz / 6400 / 100 = **100Hz (10ms)** | 按键轮询周期 |

**工作原理**：

```c
// TIM3 每 10ms 触发一次中断
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  // 读取 GPIO 电平，检测上升沿（0→1），消抖 200ms
  // KEY1: 切换 LED 开关
  // KEY2: 调节亮度
}
```

**为什么用 TIM3 而不是主循环轮询**：
- 不阻塞主循环，后续加 FreeRTOS 时兼容
- 响应时间可控（10ms 采样率足够快）
- CPU 开销可忽略（每次只做两次 GPIO 读取）
- 不需要改设备树，零风险
- 对 A7 侧性能无影响（TIM3 完全在 M4 核心上运行）

**生成的宏定义**（main.h）：

```c
#define KEY2_Pin         GPIO_PIN_7
#define KEY2_GPIO_Port   GPIOH
#define KEY2_EXTI_IRQn   EXTI7_IRQn
#define KEY1_Pin         GPIO_PIN_13
#define KEY1_GPIO_Port   GPIOB
#define KEY1_EXTI_IRQn   EXTI13_IRQn
```

---

### 2.3 I2C1 — SHT30 温湿度传感器

**用途**：通过 I2C 总线读取 SHT30-DIS 温湿度传感器数据，实现探头温度监控和超温保护。

**引脚配置**：

| 引脚 | 功能 | 复用 | J1 接口 |
|------|------|------|--------|
| PF14 | I2C1_SCL | AF5 | J1 pin 13 |
| PF15 | I2C1_SDA | AF5 | J1 pin 14 |

**参数配置**：

| 参数 | 值 | 说明 |
|------|-----|------|
| Speed Mode | Fast Mode | 400kHz |
| Addressing Mode | 7-bit | SHT30 地址 0x44 |
| Timing | 0x00602173 | CubeMX 自动计算的时序值 |
| Analog Filter | Enabled | 硬件滤波，抗干扰 |
| Digital Filter | 0 | 不使用数字滤波 |

**引脚确认过程**：

CubeMX 默认分配了 PH11/PH12，但通过开发板实际查询发现：
```bash
# PH11 被摄像头 DCMI 占用，PH12 被显示控制器占用
###
cat /sys/kernel/debug/pinctrl/soc:pin-controller@50002000/pinmux-pins | grep "PH11|PH12|PF14|PF15"
pin 94 (PF14): device 40012000.i2c function af5 group PF14
pin 95 (PF15): device 40012000.i2c function af5 group PF15
pin 123 (PH11): device 4c006000.dcmi function af13 group PH11
pin 124 (PH12): device 5a001000.display-controller function af14 group PH12
###
pin 123 (PH11): device 4c006000.dcmi function af13
pin 124 (PH12): device 5a001000.display-controller function af14

# PF14/PF15 才是 J1 扩展口上的 I2C1
pin 94 (PF14): device 40012000.i2c function af5
pin 95 (PF15): device 40012000.i2c function af5
```

野火源码中的 I2C1 overlay 设备树也确认了 PF14 (AF5) + PF15 (AF5)。

**为什么这样配置**：
- Fast Mode 400kHz 满足 SHT30 的通信速率要求（SHT30 最高支持 1MHz）
- 必须手动修正引脚到 PF14/PF15，避免与摄像头和显示器冲突

---

### 2.4 IPCC — 核间通信

**用途**：A7 和 M4 之间的硬件通信通道，是 OpenAMP/RPMsg 的底层传输机制。

**配置**：

| 参数 | 值 |
|------|-----|
| Mode | Activated |
| Runtime Context | Cortex-M4 |

**为什么需要 IPCC**：
- M4 固件通过 A7 的 Linux remoteproc 框架加载
- A7 发送 LED 亮度指令 → M4 调节 PWM
- M4 上报温度/按键事件 → A7 显示/执行
- IPCC 提供中断通知机制，RPMsg 在其上实现消息传递

---

## 三、未配置的外设（后续按需添加）

| 外设 | 说明 | 计划 |
|------|------|------|
| UART | A7-M4 通信 | 使用 RPMsg 虚拟串口，不需要物理 UART |
| FreeRTOS | 多任务调度 | 基础功能验证后再集成 |
| DMA | UART DMA 接收 | 配合 FreeRTOS 后添加 |

---

## 四、生成的工程结构

```
EndoscopeM4/
├── CM4/                          ← 我们使用的 M4 工程
│   ├── Core/
│   │   ├── Inc/
│   │   │   ├── main.h            # 引脚宏定义（KEY1/KEY2）
│   │   │   ├── gpio.h
│   │   │   ├── tim.h
│   │   │   ├── i2c.h
│   │   │   ├── ipcc.h
│   │   │   ├── stm32mp1xx_hal_conf.h
│   │   │   └── stm32mp1xx_it.h
│   │   ├── Src/
│   │   │   ├── main.c            # 主程序入口
│   │   │   ├── gpio.c            # KEY1/KEY2 EXTI 配置
│   │   │   ├── tim.c             # TIM2 CH3 PWM 配置
│   │   │   ├── i2c.c             # I2C1 配置（PF14/PF15）
│   │   │   ├── ipcc.c            # IPCC 核间通信配置
│   │   │   ├── stm32mp1xx_it.c   # 中断处理（EXTI7/EXTI13）
│   │   │   ├── stm32mp1xx_hal_msp.c
│   │   │   ├── syscalls.c
│   │   │   └── sysmem.c
│   │   └── Startup/
│   │       └── startup_stm32mp157aacx.s
│   ├── RemoteProc/               # remoteproc 加载脚本
│   └── STM32MP157AACX_RAM.ld     # 链接脚本
├── CA7/                          ← 不使用
├── Common/
├── Drivers/                      # HAL 库
└── EndoscopeM4.ioc               # CubeMX 配置文件
```

---

## 五、代码验证清单

| 检查项 | 文件 | 结果 |
|--------|------|------|
| TIM2 PWM 初始化 | tim.c | ✅ PSC=63, ARR=999, CH3, PB10 AF1 |
| TIM3 定时器中断 | tim.c | ✅ PSC=6399, ARR=99, 100Hz, NVIC 已使能 |
| I2C1 引脚正确 | i2c.c | ✅ PF14 SCL + PF15 SDA, AF5 |
| 按键 GPIO 配置 | gpio.c | ✅ 上升沿 + 下拉（高电平有效） |
| TIM3 中断处理 | stm32mp1xx_it.c | ✅ TIM3_IRQHandler 已生成 |
| IPCC 初始化 | ipcc.c | ✅ |
| 引脚宏定义 | main.h | ✅ KEY1=PB13, KEY2=PH7 |
| 外设初始化调用顺序 | main.c | ✅ GPIO → I2C1 → TIM2 → TIM3 |

---

## 六、CubeMX 重新生成代码后的必要手动修改

> ⚠️ **重要**：每次在 CubeMX 中修改配置并重新生成代码（Ctrl+S）后，以下两处修改会被覆盖，必须手动恢复。

### 6.1 启用 VIRT_UART 模块

**文件**：`CM4/OPENAMP/openamp_conf.h`

CubeMX 可能将 `VIRTUAL_UART_MODULE_ENABLED` 还原为注释状态：

```c
// CubeMX 生成的默认值（错误）：
//#define VIRTUAL_UART_MODULE_ENABLED

// 需要改为（取消注释）：
#define VIRTUAL_UART_MODULE_ENABLED
```

> 注：该文件中的 `USER CODE BEGIN INCLUDE` 区域有注释提醒此操作，不会被覆盖。

### 6.2 修改 RPMsg service name

**文件**：`Middlewares/Third_Party/OpenAMP/virtual_driver/virt_uart.c`

CubeMX 会将 service name 还原为默认值 `"rpmsg-tty"`，必须改为野火开发板 Linux 内核匹配的名称：

```c
// CubeMX 生成的默认值（错误）：
#define RPMSG_SERVICE_NAME  "rpmsg-tty"

// 需要改为：
#define RPMSG_SERVICE_NAME  "rpmsg-tty-channel"
```

**原因**：野火开发板 Linux 内核 4.19.94 中的 `rpmsg_tty` 驱动匹配的 channel name 是 `"rpmsg-tty-channel"`。如果不修改，M4 启动后 virtio channel 能创建，但 `/dev/ttyRPMSG0` 不会出现。详见问题记录 #20。

### 6.3 不需要手动修改的地方

以下修改在 CubeMX 重新生成后**不会丢失**：

| 文件 | 内容 | 原因 |
|---|---|---|
| `CM4/Core/Src/main.c` | 全部通信逻辑 | 在 USER CODE 区域内 |
| `CM4/.cproject` | `-Wl,-z,max-page-size=0x10000` | CubeMX 不修改构建设置 |
| `CM4/STM32MP157AACX_RAM.ld` | 链接脚本 | CubeMX 不修改链接脚本 |
| `CM4/OPENAMP/openamp_conf.h` USER CODE 区域 | 注释提醒 | USER CODE 区域受保护 |

### 6.4 快速检查脚本

CubeMX 重新生成后，执行以下命令确认是否需要修改：

```bash
# 检查 VIRT_UART 是否启用
grep "VIRTUAL_UART_MODULE_ENABLED" CM4/OPENAMP/openamp_conf.h
# 期望看到：#define VIRTUAL_UART_MODULE_ENABLED（无 // 注释）

# 检查 service name
grep "RPMSG_SERVICE_NAME" Middlewares/Third_Party/OpenAMP/virtual_driver/virt_uart.c
# 期望看到："rpmsg-tty-channel"（不是 "rpmsg-tty"）
```
