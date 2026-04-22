# GPIO 接口映射

## J1 扩展口（DIP 26P_2P54 双排）

| Pin | 功能标注 | GPIO | AF | A7 侧占用 | 本项目用途 | 状态 |
|---|---|---|---|---|---|---|
| 1 | GND | - | - | - | 所有模块共地 | ✅ 使用中 |
| 2 | GND | - | - | - | | |
| 3 | USART1_CTS | ? | | 未确认 | | ⬜ |
| 4 | USART1_RTS | ? | | 未确认 | | ⬜ |
| 5 | USART1_TX | ? | | 未确认 | 预留（原设计 A7-M4 UART，已改用 RPMsg） | ⬜ |
| 6 | USART1_RX | ? | | 未确认 | 预留 | ⬜ |
| 7 | USART3_TX | **PB10** | AF7 | UNCLAIMED | **LED 补光灯（软件 PWM）** | ✅ 使用中 |
| 8 | USART3_RX | **PB11** | AF7 | ethernet 占用 | 不可用 | ❌ |
| 9 | GND | - | - | - | | |
| 10 | GND | - | - | - | | |
| 11 | FDCAN1_TX | **PA12** | AF9 | ~~CAN~~ 已禁用 | **SHT30 软件 I2C SCL** | ✅ 使用中 |
| 12 | FDCAN1_RX | **PA11** | AF9 | ~~CAN~~ 已禁用 | **SHT30 软件 I2C SDA** | ✅ 使用中 |
| 13 | I2C1_SCL | **PF14** | AF5 | **触摸屏 I2C1** | ⚗️ 不可用（触摸屏占用） | ❌ 冲突 |
| 14 | I2C1_SDA | **PF15** | AF5 | **触摸屏 I2C1** | ⚗️ 不可用（触摸屏占用） | ❌ 冲突 |
| 15 | I2C1_SCL | ? | | 未确认 | 可能是第二组 I2C1 引脚，待确认 | ⬜ |
| 16 | I2C1_SDA | ? | | 未确认 | 可能是第二组 I2C1 引脚，待确认 | ⬜ |
| 17 | GND | - | - | - | | |
| 18 | GND | - | - | - | | |
| 19 | 3V3 | - | - | - | SHT30 供电 | ✅ 使用中 |
| 20 | 3V3 | - | - | - | | |
| 21 | 3V3 | - | - | - | | |
| 22 | 3V3 | - | - | - | | |
| 23 | 5V | - | - | - | HW-269 LED 供电 | ✅ 使用中 |
| 24 | 5V | - | - | - | | |
| 25 | GND | - | - | - | | |
| 26 | GND | - | - | - | | |

## J2 扩展口（DIP 26P_2P54 双排）

| Pin | 功能标注 | GPIO | A7 侧占用 | 本项目用途 | 状态 |
|---|---|---|---|---|---|
| 1 | GND | - | - | | |
| 2 | GND | - | - | | |
| 3 | ANA0 | ? | 未确认 | | ⬜ |
| 4 | ANA1 | ? | 未确认 | | ⬜ |
| 5 | GND | - | - | | |
| 6 | GND | - | - | | |
| 7 | UART4_TX | ? | 未确认 | | ⬜ |
| 8 | UART4_RX | ? | 未确认 | | ⬜ |
| 9 | GND | - | - | | |
| 10 | GND | - | - | | |
| 11 | QSPI_IO0 | ? | 未确认 | | ⬜ |
| 12 | QSPI_IO1 | ? | 未确认 | | ⬜ |
| 13 | QSPI_IO2 | ? | 未确认 | | ⬜ |
| 14 | QSPI_IO3 | ? | 未确认 | | ⬜ |
| 15 | QSPI_CLK | ? | 未确认 | | ⬜ |
| 16 | QSPI_NCS | ? | 未确认 | | ⬜ |
| 17-26 | GND/3V3/5V | - | - | | |

## KEY 接口

| 按键 | GPIO | 编号 | 电平逻辑 | 本项目用途 |
|---|---|---|---|---|
| KEY1 | **PB13** | 29 | 高电平有效（下拉+上升沿） | 冻结/恢复画面 |
| KEY2 | **PH7** | 119 | 高电平有效（下拉+上升沿） | 拍照 |
| RESET | NRST_D | - | - | 系统复位 |
| WAKE UP | PA0_WKUP | 0 | - | 唤醒（未使用） |

## 已确认的 UNCLAIMED GPIO

通过 `pinmux-pins` 查询确认 A7 侧未占用的引脚：

| GPIO | 编号 | 物理位置 | 可用复用功能 |
|---|---|---|---|
| PA0 | 0 | KEY 接口 WAKE UP | GPIO, TIM2_CH1, TIM5_CH1 |
| PB6 | 22 | J1 附近（SPI OLED 示例用作 CS） | GPIO, I2C1_SCL, TIM4_CH1, UART5_TX |
| PB10 | 26 | **J1 pin 7** | GPIO（当前 LED 软件 PWM）, I2C2_SCL, TIM2_CH3 |
| PB12 | 28 | 未确认 | GPIO, I2C2_SMBA |
| PH0 | 112 | 未确认 | GPIO |
| PH1 | 113 | 未确认 | GPIO |
| PH5 | 117 | 未确认 | GPIO, I2C2_SDA |
| PD12 | 60 | 未确认 | GPIO, I2C1_SCL, TIM4_CH1 |
| PD13 | 61 | 未确认 | GPIO, I2C1_SDA, TIM4_CH2 |
| PF12 | 92 | 未确认 | GPIO |
| PF13 | 93 | 未确认 | GPIO |

## 已确认被 A7 占用的引脚

| GPIO | 编号 | 占用外设 | 说明 |
|---|---|---|---|
| PF0 | 80 | SDMMC (SD卡) | I2C2_SDA 复用不可用 |
| PF1 | 81 | SDMMC (SD卡) | I2C2_SCL 复用不可用 |
| PF14 | 94 | **I2C1 (触摸屏)** | J1 pin 13, 与 SHT30 冲突 |
| PF15 | 95 | **I2C1 (触摸屏)** | J1 pin 14, 与 SHT30 冲突 |
| PB7 | 23 | DCMI (摄像头) | |
| PB8 | 24 | display-controller | |
| PB11 | 27 | ethernet | J1 pin 8 |
| PH11 | 123 | DCMI (摄像头) | CubeMX 默认 I2C1 引脚，不可用 |
| PH12 | 124 | display-controller | CubeMX 默认 I2C1 引脚，不可用 |

## 关键发现

### TIM2 与 LCD 背光冲突（问题 #22）

LCD 背光由 **TIM2_CH1 (PA15)** 的 PWM 控制。M4 初始化 TIM2（用于 CH3 PB10 LED PWM）时重新配置了 TIM2 全局参数（PSC/ARR），破坏了 LCD 背光 PWM。

**解决方案**：M4 不使用 TIM2，LED 改用 TIM3 中断中的软件 PWM。

### I2C1 与触摸屏冲突（问题 #24）

触摸屏（Goodix）和 J1 扩展口的 I2C1 共享 PF14/PF15。M4 初始化 I2C1 会破坏触摸屏通信。

**解决方案**：待确认 J1 pin 15/16 的 GPIO 映射（可能是另一组 I2C1 引脚），或使用软件 I2C 在空闲 GPIO 上通信。

### 待确认引脚（需联系野火）

- J1 pin 3/4 (USART1_CTS/RTS) 的 GPIO
- J1 pin 5/6 (USART1_TX/RX) 的 GPIO
- J1 pin 11/12 (FDCAN1_TX/RX) 的 GPIO
- **J1 pin 15/16 (I2C1_SCL/SDA 第二组) 的 GPIO** ← 最重要，可能解决温度传感器问题
- J2 各引脚的 GPIO
