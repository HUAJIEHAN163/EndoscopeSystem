# 医疗内窥镜嵌入式 MCU 开发学习路线
## 基于 STM32MP157 Cortex-M4 的实时控制技能体系

---

## 📌 背景：内窥镜中 MCU 负责什么

医疗内窥镜是一个典型的**实时控制 + 复杂计算**混合系统。以 STM32MP157 为例：

```
┌─────────────────────────────────────────────────────────┐
│                   医疗内窥镜系统                          │
│                                                          │
│  ┌──────────────────┐      ┌──────────────────────────┐ │
│  │  Cortex-M4（MCU）│      │  Cortex-A7（Linux）      │ │
│  │                  │      │                          │ │
│  │ • 补光 LED 驱动  │      │ • 图像采集（MIPI/USB）   │ │
│  │ • 电机控制       │◄────►│ • 图像处理算法           │ │
│  │ • 传感器采集     │      │ • 显示输出               │ │
│  │ • 安全监控       │      │ • 数据存储               │ │
│  │ • 实时响应       │      │ • 网络传输               │ │
│  └──────────────────┘      └──────────────────────────┘ │
│         ↑ 你现在学的方向                                  │
└─────────────────────────────────────────────────────────┘
```

**MCU 的核心价值：实时性**
- 补光 LED 亮度调节：需要在 1ms 内响应，A7 的 Linux 做不到
- 电机控制：需要精确的 PWM 时序，不能被操作系统调度打断
- 过热/过流保护：需要硬件级别的快速响应，保障患者安全

---

## 🎯 学习内容与优先级

### 优先级 P0（核心基础，你已部分掌握）

#### 1. GPIO + 定时器 PWM（补光 LED 控制）

**内窥镜场景：** 镜头前端的 LED 补光灯需要根据图像亮度动态调节，避免过曝或欠曝。

**你现在掌握的：** GPIO 输出控制 LED 亮灭（开关控制）

**需要进阶的：** PWM 调光（亮度连续可调）

```c
// 用 TIM2 的 PWM 模式控制 LED 亮度（0~100%）
void LED_SetBrightness(uint8_t percent) {
    // ARR = 999，对应 100% 占空比
    uint32_t pulse = (uint32_t)(percent * 10);  // 0~1000
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse);
}

// 使用示例：设置补光灯亮度为 60%
LED_SetBrightness(60);
```

**关键知识点：**
- PWM 原理：占空比决定平均电压，从而控制亮度
- TIM 的 PWM 模式配置（CubeMX 中选择 PWM Generation）
- 互补 PWM（用于驱动 H 桥电机）

---

#### 2. ADC（模拟信号采集）

**内窥镜场景：**
- 采集温度传感器数据，监控镜头前端温度（医疗法规要求不超过 41°C）
- 采集电流传感器，检测 LED 驱动电路是否过流
- 采集电池电压，判断设备电量

```c
// 读取温度传感器（NTC 热敏电阻）
float Read_Temperature(void) {
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t adc_val = HAL_ADC_GetValue(&hadc1);

    // 将 ADC 值转换为电压（3.3V 参考，12位ADC）
    float voltage = adc_val * 3.3f / 4096.0f;

    // 根据 NTC 特性曲线转换为温度（简化公式）
    float resistance = 10000.0f * voltage / (3.3f - voltage);
    float temp = 1.0f / (logf(resistance / 10000.0f) / 3950.0f
                 + 1.0f / 298.15f) - 273.15f;
    return temp;
}
```

**关键知识点：**
- ADC 分辨率与精度（12位 = 4096级）
- 采样时间与信号带宽的关系
- DMA 连续采样（避免 CPU 轮询）
- 硬件滤波（RC 低通滤波）+ 软件滤波（滑动平均）

---

#### 3. UART / 串口通信（你已掌握）

**内窥镜场景：**
- M4 与 A7 的调试通信（你现在做的）
- 与外部模块通信（蓝牙模块、WiFi 模块）
- 工厂测试接口

**需要进阶的：**
- DMA 方式收发（避免中断频繁打断）
- 环形缓冲区（处理高速数据流）
- 协议设计（帧头、帧尾、校验）

```c
// 带校验的简单通信协议
typedef struct {
    uint8_t  header;    // 0xAA
    uint8_t  cmd;       // 命令字
    uint16_t data;      // 数据
    uint8_t  checksum;  // 异或校验
    uint8_t  tail;      // 0x55
} __attribute__((packed)) Frame_t;

uint8_t calc_checksum(Frame_t *f) {
    return f->header ^ f->cmd
           ^ (f->data >> 8) ^ (f->data & 0xFF);
}
```

---

### 优先级 P1（内窥镜核心技能，入职后 1 个月）

#### 4. I2C 通信（传感器接口）

**内窥镜场景：**
- 读取陀螺仪/加速度计（检测镜体姿态）
- 读取温湿度传感器
- 控制 OLED 显示屏（手柄上的状态显示）
- 读取 EEPROM（存储设备序列号、校准参数）

```c
// 读取 MPU6050 陀螺仪（I2C 地址 0x68）
HAL_StatusTypeDef MPU6050_ReadGyro(int16_t *gx, int16_t *gy, int16_t *gz) {
    uint8_t buf[6];
    uint8_t reg = 0x43;  // GYRO_XOUT_H 寄存器地址

    // 先写寄存器地址，再读数据
    HAL_I2C_Master_Transmit(&hi2c1, 0x68 << 1, &reg, 1, 10);
    HAL_I2C_Master_Receive(&hi2c1, 0x68 << 1, buf, 6, 10);

    *gx = (int16_t)(buf[0] << 8 | buf[1]);
    *gy = (int16_t)(buf[2] << 8 | buf[3]);
    *gz = (int16_t)(buf[4] << 8 | buf[5]);
    return HAL_OK;
}
```

**关键知识点：**
- I2C 时序：START、地址、ACK、数据、STOP
- 7位地址 vs 10位地址
- I2C 总线拉伸（Clock Stretching）
- 多设备共享同一 I2C 总线

---

#### 5. SPI 通信（高速外设接口）

**内窥镜场景：**
- 驱动 Flash 存储芯片（W25Q128，存储固件和配置）
- 驱动高速 DAC（控制 LED 驱动电流，精度要求高）
- 与 FPGA 通信（部分高端内窥镜用 FPGA 做图像预处理）

```c
// 读写 W25Q128 Flash（SPI 接口）
void Flash_ReadData(uint32_t addr, uint8_t *buf, uint16_t len) {
    uint8_t cmd[4] = {
        0x03,               // Read Data 命令
        (addr >> 16) & 0xFF,
        (addr >> 8)  & 0xFF,
        addr & 0xFF
    };

    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);  // CS 拉低
    HAL_SPI_Transmit(&hspi1, cmd, 4, 10);
    HAL_SPI_Receive(&hspi1, buf, len, 100);
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);    // CS 拉高
}
```

**关键知识点：**
- SPI 四种模式（CPOL/CPHA 组合）
- 全双工 vs 半双工
- DMA 方式传输大块数据
- NSS（片选）的软件控制

---

#### 6. 定时器高级应用

**内窥镜场景：**
- 精确控制 LED 闪烁频率（与图像帧率同步，避免频闪）
- 电机步进控制（精确的脉冲时序）
- 超声波测距（测量脉冲宽度）

**LED 与图像帧率同步（关键技术）：**

```c
// 内窥镜补光灯必须与摄像头帧率同步
// 摄像头 30fps → 帧周期 33.3ms
// LED 在每帧曝光期间点亮，其余时间熄灭

// 摄像头 VSYNC 信号触发 TIM 输入捕获
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        // 收到帧同步信号，启动 LED 点亮定时
        __HAL_TIM_SET_COUNTER(&htim3, 0);
        HAL_TIM_Base_Start_IT(&htim3);  // 启动曝光定时器
    }
}
```

**关键知识点：**
- 输入捕获（测量外部信号频率/脉宽）
- 输出比较（精确定时输出）
- 定时器级联（扩展计数范围）
- 死区时间（互补 PWM 保护）

---

#### 7. DMA（直接内存访问）

**内窥镜场景：**
- ADC 连续采样温度/电流，不占用 CPU
- UART DMA 收发大量数据
- SPI DMA 读写 Flash

**为什么内窥镜必须用 DMA：**

```
不用 DMA（CPU 轮询）：
CPU → 等待 ADC → 读数据 → 等待 ADC → 读数据 ...
CPU 利用率：ADC 采样期间 100% 占用，无法做其他事

用 DMA：
CPU → 配置 DMA → 继续执行其他代码
DMA → 自动搬运 ADC 数据到内存 → 完成后通知 CPU
CPU 利用率：ADC 采样期间接近 0% 占用
```

```c
// ADC + DMA 连续采样（不占用 CPU）
uint16_t adc_buf[256];  // DMA 目标缓冲区

void ADC_DMA_Start(void) {
    // 启动 DMA 循环模式，自动填充 adc_buf
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, 256);
}

// DMA 传输完成回调（每采集 256 个点触发一次）
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    // 对 adc_buf 做滑动平均，得到稳定的温度值
    Process_ADC_Data(adc_buf, 256);
}
```

---

#### 8. 低功耗设计

**内窥镜场景：**
- 无线内窥镜（胶囊内窥镜）靠电池供电，续航是关键
- 手持式内窥镜需要控制发热

**STM32 低功耗模式：**

| 模式 | 功耗 | 唤醒时间 | 适用场景 |
|------|------|---------|---------|
| Sleep | 低 | 极快 | 等待中断时 |
| Stop | 极低 | 较快 | 长时间待机 |
| Standby | 最低 | 慢 | 深度休眠 |

```c
// 等待中断时进入 Sleep 模式（最常用）
void Main_Loop(void) {
    while (1) {
        if (cmd_ready) {
            Process_Command();
        }
        // 没有任务时进入 Sleep，等待中断唤醒
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON,
                               PWR_SLEEPENTRY_WFI);
    }
}
```

---

### 优先级 P2（进阶技能，入职后 2-3 个月）

#### 9. RTOS（实时操作系统）

**内窥镜场景：**
- 同时处理多个任务：温度监控、LED 控制、电机控制、通信
- 任务之间需要优先级管理（安全监控优先级最高）

**FreeRTOS 基础：**

```c
// 创建两个任务：安全监控（高优先级）和 LED 控制（低优先级）
void Safety_Task(void *pvParameters) {
    for (;;) {
        float temp = Read_Temperature();
        if (temp > 41.0f) {
            LED_SetBrightness(0);   // 立即关闭补光灯
            Send_Alert("OVERHEAT");
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 每 100ms 检查一次
    }
}

void LED_Control_Task(void *pvParameters) {
    for (;;) {
        // 处理亮度调节命令
        uint8_t brightness;
        if (xQueueReceive(led_queue, &brightness, portMAX_DELAY)) {
            LED_SetBrightness(brightness);
        }
    }
}

int main(void) {
    // ...初始化...
    xTaskCreate(Safety_Task, "Safety", 256, NULL, 3, NULL);  // 优先级 3
    xTaskCreate(LED_Control_Task, "LED", 128, NULL, 1, NULL); // 优先级 1
    vTaskStartScheduler();
}
```

**关键知识点：**
- 任务创建与优先级
- 队列（任务间传递数据）
- 信号量（任务同步）
- 互斥锁（保护共享资源）

---

#### 10. 电机控制（内窥镜弯曲部分）

**内窥镜场景：**
- 软性内窥镜的弯曲部分由微型电机驱动
- 需要精确控制角度和力矩
- 响应时间要求 < 10ms

**步进电机控制：**

```c
// 步进电机精确定位（用于内窥镜弯曲控制）
void Motor_MoveTo(int32_t target_steps) {
    int32_t current = Motor_GetPosition();
    int32_t delta = target_steps - current;

    if (delta > 0) {
        HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(DIR_GPIO_Port, DIR_Pin, GPIO_PIN_RESET);
        delta = -delta;
    }

    // 发送脉冲（每个脉冲移动一步）
    for (int32_t i = 0; i < delta; i++) {
        HAL_GPIO_WritePin(STEP_GPIO_Port, STEP_Pin, GPIO_PIN_SET);
        HAL_Delay(1);  // 实际应用中用定时器替代 Delay
        HAL_GPIO_WritePin(STEP_GPIO_Port, STEP_Pin, GPIO_PIN_RESET);
        HAL_Delay(1);
    }
}
```

---

#### 11. 安全与可靠性（医疗设备必须掌握）

**为什么医疗设备特别重要：**
- 内窥镜直接接触患者体内，软件故障可能导致人身伤害
- 需要符合 IEC 62304（医疗器械软件生命周期）标准

**必须掌握的技术：**

```c
// 1. 看门狗（防止程序跑飞）
void WDG_Init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload = 2000;  // 超时时间约 4 秒
    HAL_IWDG_Init(&hiwdg);
}

// 在主循环中定期喂狗
void Main_Loop(void) {
    while (1) {
        HAL_IWDG_Refresh(&hiwdg);  // 必须在超时前调用
        // ... 其他任务 ...
    }
}

// 2. 关键数据双备份（防止内存翻转）
typedef struct {
    uint8_t brightness;
    uint8_t brightness_backup;  // 备份值
    uint8_t checksum;
} LED_Config_t;

uint8_t Verify_Config(LED_Config_t *cfg) {
    return (cfg->brightness == cfg->brightness_backup) ? 1 : 0;
}

// 3. 温度超限保护（硬件联锁）
void Safety_Check(void) {
    if (Read_Temperature() > 41.0f) {
        LED_SetBrightness(0);  // 立即关闭
        Log_Error("Temperature exceeded 41C");
        // 通知 A7 侧显示警告
        RPMSG_Send("SAFETY_ALERT:OVERHEAT");
    }
}
```

**医疗设备软件规范要点：**
- 所有函数必须有返回值检查
- 关键变量使用 `volatile` 修饰
- 禁止使用动态内存分配（`malloc`/`free`）
- 所有中断服务函数必须有超时保护

---

#### 12. Bootloader 与固件升级（OTA）

**内窥镜场景：**
- 产品出厂后需要远程升级 M4 固件（修复 bug、添加功能）
- 升级过程中断电不能导致设备变砖

**基本原理：**

```
Flash 布局：
┌─────────────────────────────────────┐
│  Bootloader（0x08000000，32KB）      │  永远不升级
├─────────────────────────────────────┤
│  App 区 A（0x08008000，128KB）       │  当前运行的固件
├─────────────────────────────────────┤
│  App 区 B（0x08028000，128KB）       │  新固件下载到这里
├─────────────────────────────────────┤
│  参数区（0x08048000，4KB）           │  存储启动标志
└─────────────────────────────────────┘

升级流程：
1. A7 通过 RPMsg 将新固件传给 M4
2. M4 将新固件写入 App 区 B
3. 验证 CRC 校验
4. 设置启动标志，重启
5. Bootloader 检测到标志，从 App 区 B 启动
```

---

## 📊 技能体系总览

```
内窥镜 MCU 开发技能树
│
├── P0 基础（你已部分掌握）
│   ├── GPIO 输入输出 ✅
│   ├── 定时器中断 ✅
│   ├── UART 通信 ✅
│   ├── PWM 调光 ← 需要进阶
│   └── ADC 采集 ← 需要学习
│
├── P1 核心技能（入职后 1 个月）
│   ├── I2C 传感器接口
│   ├── SPI Flash/DAC
│   ├── DMA 数据传输
│   ├── 定时器高级应用
│   └── 低功耗设计
│
└── P2 进阶技能（入职后 2-3 个月）
    ├── FreeRTOS
    ├── 电机控制
    ├── 安全与可靠性
    └── Bootloader/OTA
```

---

## ⏱️ 学习时间规划

| 阶段 | 时间 | 学习内容 | 验收标准 |
|------|------|---------|---------|
| **入职前** | 1周 | PWM 调光 + ADC 采集 | 能用 PWM 控制 LED 亮度，能读取温度传感器 |
| **第 1 个月** | 4周 | I2C + SPI + DMA | 能读取 IMU 传感器，能读写 Flash |
| **第 2 个月** | 4周 | FreeRTOS + 安全设计 | 能用 RTOS 管理多任务，实现看门狗保护 |
| **第 3 个月** | 4周 | 电机控制 + OTA | 能控制步进电机，实现固件升级 |

---

## ✅ 入职前检查清单

- [ ] 理解 PWM 原理，能用 TIM 输出 PWM 控制 LED 亮度
- [ ] 能用 ADC 读取模拟传感器数据并转换为实际物理量
- [ ] 理解 I2C 通信时序，能读取一个 I2C 传感器
- [ ] 理解 DMA 的作用，知道什么时候应该用 DMA
- [ ] 了解医疗设备软件的基本安全要求（看门狗、数据校验）
- [ ] 能说出内窥镜中 M4 核心负责哪些实时控制任务
