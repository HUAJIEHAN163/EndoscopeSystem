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

### 2.2 GPIO EXTI — 按键输入（KEY1/KEY2）

**用途**：检测板载按键按下事件，模拟内窥镜脚踏板控制（冻结画面/拍照）。

**引脚配置**：

| 引脚 | 功能 | User Label | 对应按键 | 内窥镜功能 |
|------|------|-----------|---------|-----------|
| PB13 | GPIO_EXTI13 | KEY1 | 板载 KEY1 | 冻结/恢复画面 |
| PH7 | GPIO_EXTI7 | KEY2 | 板载 KEY2 | 拍照 |

**GPIO 参数**：

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | External Interrupt, Falling edge | 下降沿触发（按下时触发） |
| Pull-up/Pull-down | Pull-up | 上拉，松开时为高电平 |

**NVIC 中断配置**：

| 中断 | 优先级 | 状态 | 覆盖引脚 |
|------|--------|------|---------|
| EXTI7_IRQn | 1 | Enabled | PH7 (KEY2) |
| EXTI13_IRQn | 1 | Enabled | PB13 (KEY1) |

**为什么这样配置**：
- 板载按键低电平有效（按下接地），所以配置上拉 + 下降沿触发
- 使用 EXTI 中断而非轮询，按键响应更及时，不占用主循环 CPU 时间
- 后续在中断回调中加 20ms 软件消抖

**生成的中断处理函数**（stm32mp1xx_it.c）：

```c
void EXTI7_IRQHandler(void)   → HAL_GPIO_EXTI_IRQHandler(KEY2_Pin)
void EXTI13_IRQHandler(void)  → HAL_GPIO_EXTI_IRQHandler(KEY1_Pin)
```

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
| I2C1 引脚正确 | i2c.c | ✅ PF14 SCL + PF15 SDA, AF5 |
| 按键 EXTI 配置 | gpio.c | ✅ 下降沿中断 + 上拉 |
| 中断处理函数 | stm32mp1xx_it.c | ✅ EXTI7 + EXTI13 已生成 |
| IPCC 初始化 | ipcc.c | ✅ |
| 引脚宏定义 | main.h | ✅ KEY1=PB13, KEY2=PH7 |
| 外设初始化调用顺序 | main.c | ✅ GPIO → I2C1 → TIM2 |
