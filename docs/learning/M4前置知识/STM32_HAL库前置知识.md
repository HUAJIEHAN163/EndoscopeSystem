# STM32 HAL 库前置知识

## 一、HAL 库架构

### 1.1 什么是 HAL

HAL（Hardware Abstraction Layer）= 硬件抽象层。ST 官方提供的外设驱动库，封装了寄存器操作，提供统一的 API。

```
用户代码（main.c）
    │
    ▼
HAL 库 API（HAL_GPIO_ReadPin、HAL_I2C_Master_Transmit 等）
    │
    ▼
HAL 底层驱动（寄存器操作）
    │
    ▼
硬件外设（GPIO、I2C、TIM、IPCC 等）
```

### 1.2 HAL vs LL vs 寄存器

| 层级 | 抽象程度 | 代码量 | 性能 | 可移植性 |
|---|---|---|---|---|
| HAL | 高 | 少 | 一般 | 好（跨 STM32 系列） |
| LL（Low Layer） | 中 | 中 | 好 | 中 |
| 寄存器直接操作 | 低 | 多 | 最好 | 差 |

本项目使用 HAL 库（CubeMX 默认生成）。

### 1.3 文件结构

```
Drivers/
├── CMSIS/                          # ARM 核心头文件
│   ├── Device/ST/STM32MP1xx/       # 芯片寄存器定义
│   └── Include/                    # Cortex-M 核心定义
└── STM32MP1xx_HAL_Driver/
    ├── Inc/                        # HAL 头文件
    │   ├── stm32mp1xx_hal.h        # HAL 总头文件
    │   ├── stm32mp1xx_hal_gpio.h   # GPIO HAL
    │   ├── stm32mp1xx_hal_i2c.h    # I2C HAL
    │   └── ...
    └── Src/                        # HAL 源文件
        ├── stm32mp1xx_hal.c
        ├── stm32mp1xx_hal_gpio.c
        └── ...
```

---

## 二、CubeMX 生成的代码结构

### 2.1 文件分工

| 文件 | 内容 | 可以修改？ |
|---|---|---|
| `main.c` | 主程序、外设初始化调用 | ✅ 在 USER CODE 区域内 |
| `main.h` | 引脚宏定义（KEY1_Pin 等） | ❌ CubeMX 管理 |
| `gpio.c` | GPIO 初始化 | ❌ CubeMX 管理 |
| `tim.c` | 定时器初始化 | ❌ CubeMX 管理 |
| `i2c.c` | I2C 初始化 | ❌ CubeMX 管理 |
| `ipcc.c` | IPCC 初始化 | ❌ CubeMX 管理 |
| `stm32mp1xx_it.c` | 中断处理函数 | ✅ 在 USER CODE 区域内 |
| `stm32mp1xx_hal_msp.c` | 外设底层初始化（时钟、引脚） | ❌ CubeMX 管理 |
| `syscalls.c` | 系统调用（printf 底层） | ✅ |
| `sysmem.c` | 堆管理（_sbrk） | ❌ |

### 2.2 USER CODE 区域

CubeMX 重新生成代码时，**只保留 `USER CODE BEGIN/END` 之间的内容**，其他全部覆盖。

```c
/* USER CODE BEGIN 2 */
// 这里的代码不会被覆盖 ✅
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
/* USER CODE END 2 */

// 这里的代码会被覆盖 ❌
MX_GPIO_Init();
```

### 2.3 MSP（MCU Support Package）

每个外设有两层初始化：
- **MX_xxx_Init()**：配置外设参数（波特率、频率等）
- **HAL_xxx_MspInit()**：配置底层硬件（时钟使能、引脚复用、NVIC）

```c
// CubeMX 生成的 I2C 初始化流程：
MX_I2C1_Init()
    └── HAL_I2C_Init(&hi2c1)
            └── HAL_I2C_MspInit(&hi2c1)   // 自动调用
                    ├── __HAL_RCC_I2C1_CLK_ENABLE()  // 使能时钟
                    ├── GPIO 配置（SCL/SDA 引脚复用）
                    └── NVIC 配置（如果用中断模式）
```

---

## 三、HAL 库回调机制

### 3.1 中断回调模式

HAL 库使用**弱函数（weak function）**实现回调：

```c
// HAL 库内部定义（弱函数，默认空实现）
__weak void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    UNUSED(htim);
}

// 用户重写（覆盖弱函数）
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        // TIM3 溢出处理
    }
}
```

### 3.2 中断处理链

```
硬件中断
    │
    ▼
IRQHandler()                    ← stm32mp1xx_it.c（CubeMX 生成）
    │
    ▼
HAL_xxx_IRQHandler()            ← HAL 库内部（清除标志、判断中断源）
    │
    ▼
HAL_xxx_XxxCallback()           ← 用户实现（业务逻辑）
```

### 3.3 常用回调函数

| 回调 | 触发条件 |
|---|---|
| `HAL_TIM_PeriodElapsedCallback` | 定时器溢出 |
| `HAL_GPIO_EXTI_Callback` | GPIO 外部中断 |
| `HAL_I2C_MasterTxCpltCallback` | I2C 发送完成（中断/DMA 模式） |
| `HAL_I2C_MasterRxCpltCallback` | I2C 接收完成（中断/DMA 模式） |
| `HAL_UART_RxCpltCallback` | UART 接收完成 |
| `HAL_UARTEx_RxEventCallback` | UART 空闲中断（DMA 模式） |

---

## 四、时钟系统

### 4.1 STM32MP157 M4 时钟

```
HSI (64MHz) ──→ MCU 时钟 (64MHz，Engineering Boot Mode)
                或
A7 配置的时钟 ──→ MCU 时钟 (209MHz，Production Boot Mode)
```

- **Engineering Boot Mode**：M4 独立运行，使用 HSI 64MHz
- **Production Boot Mode**：A7 的 Linux 通过 remoteproc 加载 M4，时钟由 A7 配置（通常 209MHz）

### 4.2 对 PWM 频率的影响

```
Engineering Boot:  64MHz / 64 / 1000 = 1kHz PWM ✅
Production Boot:  209MHz / 64 / 1000 = 3.27kHz PWM（频率变了！）
```

如果需要精确频率，应该用 `HAL_RCC_GetPCLK1Freq()` 动态计算 PSC。本项目中 LED 调光对频率不敏感，1-3kHz 都可以。

### 4.3 HAL_Delay 的时基

`HAL_Delay` 依赖 SysTick 中断中的 `HAL_IncTick()` 递增 `uwTick` 变量。如果 SysTick 被 FreeRTOS 接管，需要确保 `HAL_IncTick()` 仍然被调用。

```c
// stm32mp1xx_it.c 中的 SysTick_Handler
void SysTick_Handler(void)
{
    HAL_IncTick();           // HAL 时基（始终调用）
    xPortSysTickHandler();   // FreeRTOS tick（调度器启动后调用）
}
```

---

## 五、错误处理

### 5.1 Error_Handler

CubeMX 生成的默认错误处理：关中断 + 死循环。

```c
void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}
```

生产环境应该改为：记录错误信息 + 看门狗复位。

### 5.2 HAL 返回值

所有 HAL 函数返回 `HAL_StatusTypeDef`：

```c
typedef enum {
    HAL_OK      = 0x00,  // 成功
    HAL_ERROR   = 0x01,  // 错误
    HAL_BUSY    = 0x02,  // 忙（外设正在使用）
    HAL_TIMEOUT = 0x03   // 超时
} HAL_StatusTypeDef;

// 正确用法：检查返回值
if (HAL_I2C_Master_Transmit(&hi2c1, addr, data, len, 100) != HAL_OK) {
    // 处理错误
}
```

---

## 六、常见面试问题

### Q1：HAL 库的优缺点？

**优点**：开发快、可移植、CubeMX 图形化配置
**缺点**：代码臃肿、执行效率低于直接操作寄存器、封装层多导致调试困难

### Q2：`__weak` 关键字的作用？

GCC 扩展，声明弱符号。如果用户定义了同名函数（强符号），链接器使用用户的版本；如果没有，使用弱函数的默认实现。HAL 库用此机制实现回调。

### Q3：为什么 CubeMX 生成的代码有 USER CODE 区域？

CubeMX 每次重新生成代码时会覆盖所有文件，但保留 `USER CODE BEGIN/END` 之间的内容。这样用户代码不会丢失，同时 CubeMX 可以更新外设配置。

### Q4：HAL_Init() 做了什么？

1. 配置 Flash 预取、指令缓存、数据缓存
2. 配置 SysTick 为 1ms 中断（HAL 时基）
3. 配置 NVIC 优先级分组
4. 调用 `HAL_MspInit()`（底层初始化）

### Q5：阻塞模式、中断模式、DMA 模式的区别？

以 I2C 为例：
- **阻塞**：`HAL_I2C_Master_Transmit` — CPU 等待传输完成，期间不能做其他事
- **中断**：`HAL_I2C_Master_Transmit_IT` — 启动传输后立即返回，完成时触发回调
- **DMA**：`HAL_I2C_Master_Transmit_DMA` — 数据搬运由 DMA 硬件完成，CPU 完全不参与

本项目 I2C 使用阻塞模式（简单，SHT30 通信量小）。
