# GPIO 与中断前置知识

## 一、GPIO 基础

### 1.1 什么是 GPIO

GPIO（General Purpose Input/Output）= 通用输入输出引脚。MCU 通过 GPIO 与外部硬件交互：
- **输出模式**：MCU 控制引脚电平（高/低），驱动 LED、继电器等
- **输入模式**：MCU 读取引脚电平，检测按键、传感器信号等

### 1.2 引脚复用（Alternate Function）

STM32 的每个引脚可以有多种功能：
- GPIO 模式：普通输入/输出
- AF（Alternate Function）模式：连接到内部外设（UART、I2C、TIM、SPI 等）

```
PB10 的可选功能：
  GPIO        → 普通输入/输出
  TIM2_CH3    → 定时器 2 通道 3（PWM 输出）  ← 本项目使用
  USART3_TX   → 串口 3 发送
  I2C2_SCL    → I2C 2 时钟
```

在 CubeMX 中右键引脚选择功能，CubeMX 自动配置复用寄存器。

### 1.3 上拉/下拉电阻

| 配置 | 效果 | 适用场景 |
|---|---|---|
| 无上拉无下拉（浮空） | 引脚电平不确定 | 有外部电路驱动时 |
| 上拉（Pull-up） | 默认高电平，按下拉低 | 低电平有效的按键 |
| 下拉（Pull-down） | 默认低电平，按下拉高 | 高电平有效的按键 |

本项目 KEY1/KEY2 是**高电平有效**（按下=高），所以配置为**下拉**。

### 1.4 HAL 库 GPIO 操作

```c
// 读取引脚电平
GPIO_PinState state = HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin);
// 返回 GPIO_PIN_SET（高电平）或 GPIO_PIN_RESET（低电平）

// 设置引脚电平（输出模式）
HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);    // 高电平
HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);  // 低电平

// 翻转引脚电平
HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
```

---

## 二、中断（Interrupt）

### 2.1 什么是中断

CPU 正在执行主程序时，外部事件（按键按下、定时器溢出、数据接收完成）触发中断，CPU 暂停当前工作，跳转到**中断服务函数（ISR）**处理事件，处理完后返回继续执行主程序。

```
主程序:  ████████████  ██████████████  ████████████
                    ↑                ↑
中断:               ├── ISR ──┤      ├── ISR ──┤
                    按键按下          定时器溢出
```

### 2.2 NVIC（嵌套向量中断控制器）

NVIC 管理所有中断的优先级和使能：

```c
// 设置中断优先级（数值越小优先级越高）
HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);  // 抢占优先级 5，子优先级 0

// 使能中断
HAL_NVIC_EnableIRQ(TIM3_IRQn);
```

**中断嵌套**：高优先级中断可以打断低优先级中断的 ISR。

### 2.3 EXTI（外部中断）

EXTI 可以检测 GPIO 引脚的边沿变化并触发中断：

| 触发方式 | 含义 |
|---|---|
| 上升沿（Rising edge） | 低→高 时触发 |
| 下降沿（Falling edge） | 高→低 时触发 |
| 双边沿（Both edges） | 任何变化都触发 |

```c
// CubeMX 配置 PB13 为 GPIO_EXTI13，上升沿触发
// HAL 库自动生成中断处理函数，用户实现回调：
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == KEY1_Pin) {
        // KEY1 按下
    }
}
```

### 2.4 STM32MP1 的 EXTI 限制（重要！）

**STM32MP1 双核架构下，M4 无法直接使用 EXTI GPIO 中断**。

原因：EXTI 控制器由 A7 侧 Linux 内核管理（通过 hwlock 仲裁），M4 通过 HAL 库配置的 EXTI 寄存器不能真正生效。

**解决方案**：使用 TIM 定时器中断做周期性 GPIO 轮询（本项目采用此方案）。

---

## 三、按键消抖

### 3.1 为什么需要消抖

机械按键按下/松开时，触点会弹跳产生多次通断：

```
实际电平:  ────┐┌┐┌┐┌──────────────┐┌┐┌┐┌────
               └┘└┘└┘              └┘└┘└┘
               ← 抖动 →  稳定按下   ← 抖动 → 稳定松开
               约 5-20ms            约 5-20ms
```

如果不消抖，一次按键会被检测为多次。

### 3.2 软件消抖方法

**方法 1：延时消抖**（简单但阻塞）
```c
if (按键按下) {
    HAL_Delay(20);  // 等待抖动结束
    if (仍然按下) {
        // 确认按下
    }
}
```

**方法 2：时间戳消抖**（本项目采用，非阻塞）
```c
static uint32_t last_tick = 0;
if (按键按下 && (HAL_GetTick() - last_tick > 200)) {
    last_tick = HAL_GetTick();
    // 确认按下，200ms 内不再响应
}
```

**方法 3：状态机消抖**（检测上升沿）
```c
static uint8_t prev = 0;
uint8_t now = HAL_GPIO_ReadPin(...);
if (now && !prev) {  // 0→1 上升沿
    // 按下事件
}
prev = now;
```

本项目结合了方法 2 和方法 3：在 TIM3 中断（10ms 周期）中检测上升沿 + 200ms 时间戳消抖。

---

## 四、本项目中的实际应用

### 4.1 按键检测架构

```
TIM3 中断 (10ms)          主循环/任务
├── 读取 KEY1 电平         ├── 检测 key1_event 标志
├── 检测上升沿 (0→1)       ├── 发送 "K1\n" 给 A7
├── 时间戳消抖 (200ms)     └── 清除标志
├── 置位 key1_event = 1
└── 同样处理 KEY2
```

### 4.2 为什么用 TIM 轮询而不是 EXTI

| | EXTI 中断 | TIM 轮询 |
|---|---|---|
| 响应速度 | 微秒级 | 10ms（足够快） |
| STM32MP1 支持 | ❌ M4 无法使用 | ✅ 完全可用 |
| CPU 开销 | 几乎为零 | 极低（每 10ms 读两次 GPIO） |
| 消抖 | 需要额外处理 | 天然消抖（10ms 采样间隔） |

### 4.3 GPIO 编号计算

STM32MP157 的 GPIO 编号 = 组号 × 16 + 引脚号：

| GPIO | 组号 | 引脚号 | 编号 |
|---|---|---|---|
| PB10 | B=1 | 10 | 26 |
| PB13 | B=1 | 13 | 29 |
| PH7 | H=7 | 7 | 119 |

A7 侧 Linux 通过 sysfs 操作 GPIO 时需要此编号：
```bash
echo 26 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio26/direction
echo 1 > /sys/class/gpio/gpio26/value
```

---

## 五、常见面试问题

### Q1：GPIO 的推挽输出和开漏输出有什么区别？

- **推挽**：可以输出高电平和低电平，驱动能力强
- **开漏**：只能输出低电平，高电平需要外部上拉电阻。优点是可以实现线与（多个设备共享一条线）

I2C 的 SDA/SCL 就是开漏输出 + 外部上拉。

### Q2：中断服务函数中应该做什么？

尽量短！只做：
- 设置标志位
- 清除中断挂起位
- 如果用 RTOS，释放信号量（`xSemaphoreGiveFromISR`）

不要做：
- `HAL_Delay`（依赖 SysTick，可能死锁）
- `printf`（耗时太长）
- 复杂计算

### Q3：什么是中断优先级反转？

不同于 RTOS 的优先级反转。中断优先级反转是指：低优先级中断的 ISR 执行时间过长，导致高优先级中断的响应被延迟。解决方法：保持 ISR 尽量短。
