# STM32 HAL 库常用函数速查手册

## 目录
1. [GPIO 函数](#1-gpio-函数)
2. [UART/USART 函数](#2-uartusart-函数)
3. [Timer 函数](#3-timer-函数)
4. [中断相关函数](#4-中断相关函数)
5. [系统函数](#5-系统函数)
6. [标准C库函数](#6-标准c库函数)
7. [典型使用流程](#7-典型使用流程)
8. [实际工作中的使用建议](#8-实际工作中的使用建议)
9. [医疗设备常用外设函数](#9-医疗设备常用外设函数)

---

## 1. GPIO 函数

### 初始化与配置

```c
void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
```
- **功能**：初始化 GPIO 引脚
- **参数**：
  - `GPIOx`：GPIO 端口（GPIOA、GPIOB...）
  - `GPIO_Init`：配置结构体指针
- **示例**：
```c
GPIO_InitTypeDef GPIO_InitStruct = {0};
GPIO_InitStruct.Pin = GPIO_PIN_0;
GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
GPIO_InitStruct.Pull = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

---

### 读写操作

```c
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
```
- **功能**：读取引脚电平
- **返回值**：`GPIO_PIN_RESET`(0) 或 `GPIO_PIN_SET`(1)
- **示例**：
```c
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
    // 引脚为低电平
}
```

---

```c
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
```
- **功能**：设置引脚电平
- **参数**：
  - `PinState`：`GPIO_PIN_RESET` 或 `GPIO_PIN_SET`
- **示例**：
```c
HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);   // 输出高电平
HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // 输出低电平
```

---

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
```
- **功能**：翻转引脚电平（高变低，低变高）
- **示例**：
```c
HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);  // LED 闪烁常用
```

---

## 2. UART/USART 函数

### 初始化

```c
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart)
```
- **功能**：初始化 UART
- **参数**：UART 句柄指针（CubeMX 自动生成）
- **返回值**：`HAL_OK` / `HAL_ERROR`

---

### 阻塞发送/接收

```c
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, 
                                     uint8_t *pData, 
                                     uint16_t Size, 
                                     uint32_t Timeout)
```
- **功能**：阻塞发送数据
- **参数**：
  - `pData`：数据缓冲区指针
  - `Size`：字节数
  - `Timeout`：超时时间（毫秒）
- **示例**：
```c
char msg[] = "Hello\r\n";
HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), 100);
```

---

```c
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, 
                                    uint8_t *pData, 
                                    uint16_t Size, 
                                    uint32_t Timeout)
```
- **功能**：阻塞接收数据
- **示例**：
```c
uint8_t rx_buf[10];
HAL_UART_Receive(&huart3, rx_buf, 10, 1000);  // 等待接收10字节，超时1秒
```

---

### 中断发送/接收

```c
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, 
                                        uint8_t *pData, 
                                        uint16_t Size)
```
- **功能**：中断方式发送数据（非阻塞）
- **注意**：发送完成后会调用 `HAL_UART_TxCpltCallback()`

---

```c
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, 
                                       uint8_t *pData, 
                                       uint16_t Size)
```
- **功能**：中断方式接收数据（非阻塞）
- **注意**：
  - 接收完成后会调用 `HAL_UART_RxCpltCallback()`
  - **每次接收完必须重新调用此函数**，否则接收停止
- **示例**：
```c
uint8_t rx_byte;
HAL_UART_Receive_IT(&huart3, &rx_byte, 1);  // 启动单字节接收

// 在回调函数中重新启动
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        // 处理 rx_byte
        HAL_UART_Receive_IT(&huart3, &rx_byte, 1);  // 继续接收下一个字节
    }
}
```

---

## 3. Timer 函数

### 基本定时器

```c
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim)
```
- **功能**：初始化定时器基本功能
- **参数**：定时器句柄（CubeMX 生成）

---

```c
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim)
```
- **功能**：启动定时器（不产生中断）

---

```c
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
```
- **功能**：启动定时器并使能中断
- **示例**：
```c
HAL_TIM_Base_Start_IT(&htim2);  // 启动 TIM2，每次溢出触发中断
```

---

```c
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
```
- **功能**：停止定时器中断

---

### PWM 相关

```c
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel)
```
- **功能**：启动 PWM 输出
- **参数**：`Channel` 如 `TIM_CHANNEL_1`

---

```c
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim, uint32_t Channel)
```
- **功能**：停止 PWM 输出

---

### 定时器回调函数

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
```
- **功能**：定时器溢出中断回调（用户实现）
- **示例**：
```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        // 每 100ms 执行一次
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    }
}
```

---

## 4. 中断相关函数

### NVIC 控制

```c
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority)
```
- **功能**：设置中断优先级
- **参数**：
  - `IRQn`：中断号（如 `TIM2_IRQn`）
  - `PreemptPriority`：抢占优先级（数字越小优先级越高）
  - `SubPriority`：子优先级
- **示例**：
```c
HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);  // 最高优先级
HAL_NVIC_SetPriority(TIM2_IRQn, 3, 0);    // 较低优先级
```

---

```c
void HAL_NVIC_EnableIRQ(IRQn_Type IRQn)
```
- **功能**：使能中断

---

```c
void HAL_NVIC_DisableIRQ(IRQn_Type IRQn)
```
- **功能**：禁用中断

---

### 中断处理函数

```c
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *htim)
```
- **功能**：定时器中断处理（在 `stm32xxx_it.c` 中调用）

---

```c
void HAL_UART_IRQHandler(UART_HandleTypeDef *huart)
```
- **功能**：UART 中断处理

---

## 5. 系统函数

### 初始化

```c
HAL_StatusTypeDef HAL_Init(void)
```
- **功能**：初始化 HAL 库（必须在 main 函数开头调用）
- **作用**：
  - 配置 SysTick 定时器
  - 初始化 NVIC 优先级分组
  - 初始化低层硬件

---

### 延时函数

```c
void HAL_Delay(uint32_t Delay)
```
- **功能**：毫秒级阻塞延时
- **参数**：延时时间（毫秒）
- **注意**：会阻塞 CPU，不能在中断中使用
- **示例**：
```c
HAL_Delay(1000);  // 延时 1 秒
```

---

### 时间获取

```c
uint32_t HAL_GetTick(void)
```
- **功能**：获取系统运行时间（毫秒）
- **返回值**：从启动到现在的毫秒数
- **示例**：
```c
uint32_t start = HAL_GetTick();
// ... 执行某些操作
uint32_t elapsed = HAL_GetTick() - start;  // 计算耗时
```

---

```c
void HAL_IncTick(void)
```
- **功能**：SysTick 中断中调用，增加系统时间计数（HAL 内部使用）

---

## 6. 标准C库函数

### 字符串操作

```c
size_t strlen(const char *str)
```
- **功能**：计算字符串长度（不含 `\0`）
- **示例**：
```c
char msg[] = "Hello";
int len = strlen(msg);  // 返回 5
```

---

```c
int strcmp(const char *str1, const char *str2)
```
- **功能**：比较两个字符串
- **返回值**：
  - `0`：相等
  - `< 0`：str1 < str2
  - `> 0`：str1 > str2
- **示例**：
```c
if (strcmp(cmd, "on") == 0) {
    // 命令是 "on"
}
```

---

```c
char *strcpy(char *dest, const char *src)
```
- **功能**：复制字符串
- **注意**：dest 必须有足够空间

---

```c
char *strcat(char *dest, const char *src)
```
- **功能**：拼接字符串

---

### 内存操作

```c
void *memset(void *ptr, int value, size_t num)
```
- **功能**：填充内存
- **示例**：
```c
uint8_t buf[100];
memset(buf, 0, sizeof(buf));  // 清零
```

---

```c
void *memcpy(void *dest, const void *src, size_t num)
```
- **功能**：复制内存块

---

### 格式化输出

```c
int sprintf(char *str, const char *format, ...)
```
- **功能**：格式化字符串到缓冲区
- **示例**：
```c
char buf[50];
int val = 123;
sprintf(buf, "Value: %d\r\n", val);
```

---

```c
int snprintf(char *str, size_t size, const char *format, ...)
```
- **功能**：带长度限制的格式化（更安全）
- **示例**：
```c
char buf[20];
snprintf(buf, sizeof(buf), "Mode: %s", "ON");  // 防止溢出
```

---

## 常用返回值类型

```c
typedef enum {
    HAL_OK       = 0x00U,
    HAL_ERROR    = 0x01U,
    HAL_BUSY     = 0x02U,
    HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef;
```

---

## 使用技巧

### 1. 检查返回值

```c
if (HAL_UART_Transmit(&huart3, data, len, 100) != HAL_OK) {
    // 处理错误
}
```

### 2. 中断回调函数命名规则

| 外设 | 回调函数 |
|------|---------|
| GPIO EXTI | `HAL_GPIO_EXTI_Callback()` |
| UART 接收完成 | `HAL_UART_RxCpltCallback()` |
| UART 发送完成 | `HAL_UART_TxCpltCallback()` |
| Timer 溢出 | `HAL_TIM_PeriodElapsedCallback()` |

### 3. 弱函数机制

HAL 库的回调函数都是 `__weak` 修饰的，用户可以直接重新定义覆盖，不需要修改库文件。

```c
// HAL 库中（弱定义）
__weak void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {}

// 用户代码中（强定义，会覆盖）
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    // 自己的实现
}
```

---

---

## 7. 典型使用流程

### 7.1 GPIO 使用流程

#### 输出模式（控制 LED）

```
步骤1：使能时钟（CubeMX 自动生成）
  ↓
步骤2：初始化 GPIO
  ↓
步骤3：读写操作
```

**完整代码示例**：

```c
// main.c
int main(void)
{
    HAL_Init();                    // 步骤0：初始化 HAL 库
    SystemClock_Config();          // 配置时钟
    
    // 步骤1：使能时钟（CubeMX 在 MX_GPIO_Init 中自动生成）
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    // 步骤2：初始化 GPIO
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // 步骤3：使用
    while (1)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);   // 点亮
        HAL_Delay(500);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // 熄灭
        HAL_Delay(500);
    }
}
```

#### 输入模式（读取按键）

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    
    // 初始化按键引脚为输入
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;      // 输入模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;          // 内部上拉
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    while (1)
    {
        // 读取按键状态（按下为低电平）
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
            // 按键按下
            HAL_Delay(20);  // 简单消抖
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
                // 确认按下，执行操作
            }
        }
    }
}
```

---

### 7.2 UART 使用流程

#### 阻塞模式（简单调试）

```
步骤1：CubeMX 配置 UART（波特率、引脚）
  ↓
步骤2：生成代码，自动调用 MX_USART3_UART_Init()
  ↓
步骤3：直接使用 HAL_UART_Transmit/Receive
```

**完整代码示例**：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_USART3_UART_Init();  // CubeMX 生成的初始化函数
    
    // 发送字符串
    char msg[] = "Hello World\r\n";
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), 100);
    
    // 接收数据
    uint8_t rx_buf[10];
    HAL_UART_Receive(&huart3, rx_buf, 10, 5000);  // 等待接收10字节，超时5秒
    
    while (1) {}
}
```

#### 中断模式（推荐，不阻塞）

```
步骤1：CubeMX 配置 UART + 使能中断
  ↓
步骤2：在 main 中启动中断接收
  ↓
步骤3：实现回调函数处理数据
  ↓
步骤4：回调函数中重新启动接收
```

**完整代码示例**：

```c
// 全局变量
uint8_t rx_byte;
volatile uint8_t data_ready = 0;

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_USART3_UART_Init();
    
    // 启动单字节中断接收
    HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
    
    while (1)
    {
        if (data_ready) {
            data_ready = 0;
            // 处理接收到的数据
            HAL_UART_Transmit(&huart3, &rx_byte, 1, 100);  // 回显
        }
    }
}

// 接收完成回调（在 stm32xxx_it.c 或 main.c 中实现）
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3) {
        data_ready = 1;
        // 重新启动接收（重要！）
        HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
    }
}
```

---

### 7.3 Timer 使用流程

#### 定时中断（周期性任务）

```
步骤1：CubeMX 配置 TIM（PSC、ARR 计算周期）
  ↓
步骤2：使能 TIM 中断
  ↓
步骤3：在 main 中启动定时器
  ↓
步骤4：实现回调函数
```

**完整代码示例**：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM2_Init();  // CubeMX 生成，配置 PSC=6399, ARR=999 → 100ms
    
    // 启动定时器中断
    HAL_TIM_Base_Start_IT(&htim2);
    
    while (1) {
        // 主循环可以做其他事情，定时任务在中断中执行
    }
}

// 定时器溢出回调
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        // 每 100ms 执行一次
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);  // LED 闪烁
    }
}
```

#### PWM 输出（控制舵机、调光）

```
步骤1：CubeMX 配置 TIM 为 PWM 模式
  ↓
步骤2：启动 PWM 输出
  ↓
步骤3：动态调整占空比
```

**完整代码示例**：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_TIM3_Init();  // CubeMX 配置 TIM3_CH1 为 PWM
    
    // 启动 PWM
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    
    // 设置占空比（0 ~ ARR）
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 500);  // 50% 占空比
    
    while (1) {
        // 动态调整亮度
        for (int i = 0; i <= 1000; i += 10) {
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, i);
            HAL_Delay(10);
        }
    }
}
```

---

### 7.4 中断优先级配置流程

```
步骤1：确定哪些中断需要嵌套（高优先级打断低优先级）
  ↓
步骤2：在 CubeMX 或代码中设置优先级
  ↓
步骤3：使能中断
```

**完整代码示例**：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    
    // 配置中断优先级（数字越小优先级越高）
    HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);  // UART 最高优先级
    HAL_NVIC_SetPriority(TIM2_IRQn, 3, 0);    // Timer 较低优先级
    
    // 使能中断
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    
    // 启动外设
    HAL_TIM_Base_Start_IT(&htim2);
    HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
    
    while (1) {}
}
```

---

### 7.5 完整项目初始化流程

```c
int main(void)
{
    // ========== 第一步：系统初始化 ==========
    HAL_Init();                    // 初始化 HAL 库
    SystemClock_Config();          // 配置系统时钟
    
    // ========== 第二步：外设初始化 ==========
    MX_GPIO_Init();                // 初始化 GPIO
    MX_TIM2_Init();                // 初始化定时器
    MX_USART3_UART_Init();         // 初始化串口
    
    // ========== 第三步：应用层初始化 ==========
    // 修正 GPIO 配置（如果 CubeMX 生成有问题）
    GPIO_FixLedOutput();
    
    // 启动定时器中断
    HAL_TIM_Base_Start_IT(&htim2);
    
    // 启动串口接收
    HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
    
    // 发送启动信息
    char msg[] = "System Ready\r\n";
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), 100);
    
    // ========== 第四步：主循环 ==========
    while (1)
    {
        // 处理串口命令
        if (cmd_ready) {
            ProcessCommand();
        }
        
        // 其他任务...
    }
}
```

---

## 8. 实际工作中的使用建议

### 8.1 不需要记住所有函数

**真实工作场景**：

| 需要记住的（高频） | 查文档即可（低频） |
|-------------------|-------------------|
| `HAL_GPIO_WritePin()` | 具体参数顺序 |
| `HAL_GPIO_ReadPin()` | 返回值枚举名 |
| `HAL_Delay()` | 不常用外设函数 |
| `HAL_UART_Transmit()` | 结构体成员名 |
| `HAL_TIM_Base_Start_IT()` | 寄存器位定义 |

---

### 8.2 实际工作流程

```
遇到需求
    ↓
知道大概有什么函数（如"UART 有发送函数"）
    ↓
在 IDE 中输入 HAL_UART_  → 自动提示列出所有函数
    ↓
选择 HAL_UART_Transmit → 看参数提示
    ↓
不确定用法 → 按 F12 跳转到定义看注释
    ↓
还不清楚 → 查 HAL 库手册或例程
```

---

### 8.3 真正需要的能力

| 能力 | 重要性 |
|------|--------|
| **知道有哪些外设和功能** | ⭐⭐⭐⭐⭐ |
| **理解底层原理**（寄存器、中断、时钟） | ⭐⭐⭐⭐⭐ |
| **会查文档和看源码** | ⭐⭐⭐⭐⭐ |
| **记住所有函数签名** | ⭐⭐ |

---

### 8.4 老工程师的经验

- **记住思路，不记细节**：知道"GPIO 可以读写翻转"，具体函数名忘了就查
- **善用 IDE**：
  - `Ctrl+Space`：自动补全
  - `F12`：跳转到定义
  - `Shift+F12`：查找所有引用
  - `Alt+←`：返回上一个位置
- **建立自己的代码库**：常用的初始化代码、通信协议封装，直接复制粘贴改改
- **看例程学习**：ST 官方例程、开源项目，看别人怎么用

---

### 8.5 给初学者的建议

**现在学习阶段**：
- **理解原理** > 记住函数名
- 多动手写代码，用多了自然记住高频函数
- 把这份文档当工具书，用到时翻一翻

**工作后**：
- 公司会有自己封装的驱动库，HAL 只是底层
- 重点是**业务逻辑**和**系统架构**，不是背 API

---

### 8.6 一句话总结

> **会用 > 会背**，IDE 和文档是你的外部大脑。

---

## 9. 医疗设备常用外设函数

> 内窥镜等医疗设备通常涉及：图像传感器、存储、通信、电机控制、ADC采集等

---

### 9.1 SPI 函数（图像传感器、Flash存储）

#### 初始化

```c
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi)
```
- **功能**：初始化 SPI
- **应用场景**：连接图像传感器、外部 Flash、SD 卡

---

#### 阻塞传输

```c
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, 
                                    uint8_t *pData, 
                                    uint16_t Size, 
                                    uint32_t Timeout)
```
- **功能**：SPI 发送数据
- **示例**：
```c
uint8_t cmd = 0x03;  // 读命令
HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);  // 片选拉低
HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);    // 片选拉高
```

---

```c
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, 
                                   uint8_t *pData, 
                                   uint16_t Size, 
                                   uint32_t Timeout)
```
- **功能**：SPI 接收数据

---

```c
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, 
                                           uint8_t *pTxData, 
                                           uint8_t *pRxData, 
                                           uint16_t Size, 
                                           uint32_t Timeout)
```
- **功能**：SPI 全双工收发
- **应用**：读取传感器数据
- **示例**：
```c
uint8_t tx_buf[3] = {0x80, 0x00, 0x00};  // 读寄存器命令
uint8_t rx_buf[3];
HAL_SPI_TransmitReceive(&hspi1, tx_buf, rx_buf, 3, 100);
// rx_buf[1], rx_buf[2] 是读到的数据
```

---

#### DMA 传输（高速图像数据）

```c
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi, 
                                        uint8_t *pData, 
                                        uint16_t Size)
```
- **功能**：DMA 方式发送（不占用 CPU）
- **应用**：传输大量图像数据到显示屏或存储器

---

```c
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *hspi, 
                                       uint8_t *pData, 
                                       uint16_t Size)
```
- **功能**：DMA 方式接收
- **应用**：从图像传感器高速读取数据

---

### 9.2 I2C 函数（传感器、EEPROM）

#### 初始化

```c
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c)
```
- **功能**：初始化 I2C
- **应用场景**：连接温度传感器、陀螺仪、EEPROM

---

#### 主机发送

```c
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, 
                                           uint16_t DevAddress, 
                                           uint8_t *pData, 
                                           uint16_t Size, 
                                           uint32_t Timeout)
```
- **功能**：I2C 主机发送数据
- **参数**：
  - `DevAddress`：从机地址（7位地址需左移1位）
- **示例**：
```c
// 向 EEPROM (地址 0x50) 写入数据
uint8_t data[] = {0x00, 0x10, 0xAA, 0xBB};  // 地址 + 数据
HAL_I2C_Master_Transmit(&hi2c1, 0x50 << 1, data, 4, 100);
```

---

#### 主机接收

```c
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, 
                                          uint16_t DevAddress, 
                                          uint8_t *pData, 
                                          uint16_t Size, 
                                          uint32_t Timeout)
```
- **功能**：I2C 主机接收数据
- **示例**：
```c
// 从温度传感器读取数据
uint8_t temp_data[2];
HAL_I2C_Master_Receive(&hi2c1, 0x48 << 1, temp_data, 2, 100);
```

---

#### 内存读写（EEPROM 常用）

```c
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, 
                                     uint16_t DevAddress, 
                                     uint16_t MemAddress, 
                                     uint16_t MemAddSize, 
                                     uint8_t *pData, 
                                     uint16_t Size, 
                                     uint32_t Timeout)
```
- **功能**：向 I2C 设备的指定地址写入数据
- **示例**：
```c
// 向 EEPROM 地址 0x0010 写入数据
uint8_t data[] = {0xAA, 0xBB};
HAL_I2C_Mem_Write(&hi2c1, 0x50 << 1, 0x0010, I2C_MEMADD_SIZE_16BIT, data, 2, 100);
```

---

```c
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, 
                                    uint16_t DevAddress, 
                                    uint16_t MemAddress, 
                                    uint16_t MemAddSize, 
                                    uint8_t *pData, 
                                    uint16_t Size, 
                                    uint32_t Timeout)
```
- **功能**：从 I2C 设备的指定地址读取数据
- **示例**：
```c
// 从 EEPROM 地址 0x0010 读取数据
uint8_t data[2];
HAL_I2C_Mem_Read(&hi2c1, 0x50 << 1, 0x0010, I2C_MEMADD_SIZE_16BIT, data, 2, 100);
```

---

### 9.3 ADC 函数（模拟信号采集）

#### 初始化

```c
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *hadc)
```
- **功能**：初始化 ADC
- **应用场景**：采集电池电压、温度、压力传感器信号

---

#### 单次转换

```c
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc)
```
- **功能**：启动 ADC 转换

---

```c
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, 
                                             uint32_t Timeout)
```
- **功能**：等待转换完成

---

```c
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc)
```
- **功能**：读取 ADC 转换结果
- **示例**：
```c
// 读取电池电压
HAL_ADC_Start(&hadc1);
HAL_ADC_PollForConversion(&hadc1, 100);
uint32_t adc_value = HAL_ADC_GetValue(&hadc1);
float voltage = (adc_value * 3.3f) / 4096.0f;  // 12位ADC
```

---

#### DMA 连续采集

```c
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, 
                                     uint32_t *pData, 
                                     uint32_t Length)
```
- **功能**：DMA 方式连续采集多个通道
- **应用**：同时采集多个传感器信号
- **示例**：
```c
uint32_t adc_buffer[3];  // 3个通道
HAL_ADC_Start_DMA(&hadc1, adc_buffer, 3);

// 在 DMA 完成回调中处理数据
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    float ch1 = (adc_buffer[0] * 3.3f) / 4096.0f;
    float ch2 = (adc_buffer[1] * 3.3f) / 4096.0f;
    float ch3 = (adc_buffer[2] * 3.3f) / 4096.0f;
}
```

---

### 9.4 DMA 函数（高速数据传输）

#### 启动 DMA

```c
HAL_StatusTypeDef HAL_DMA_Start(DMA_HandleTypeDef *hdma, 
                                 uint32_t SrcAddress, 
                                 uint32_t DstAddress, 
                                 uint32_t DataLength)
```
- **功能**：启动 DMA 传输
- **应用**：内存到内存、外设到内存的高速传输

---

```c
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *hdma, 
                                    uint32_t SrcAddress, 
                                    uint32_t DstAddress, 
                                    uint32_t DataLength)
```
- **功能**：启动 DMA 传输并使能中断
- **应用**：传输完成后触发回调处理

---

#### DMA 回调函数

```c
void HAL_DMA_XferCpltCallback(DMA_HandleTypeDef *hdma)
```
- **功能**：DMA 传输完成回调（用户实现）
- **示例**：
```c
void HAL_DMA_XferCpltCallback(DMA_HandleTypeDef *hdma)
{
    if (hdma->Instance == DMA1_Stream0) {
        // 图像数据传输完成，可以开始处理
        image_ready = 1;
    }
}
```

---

### 9.5 USB 函数（数据传输、固件升级）

#### USB Device 初始化

```c
USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *pdev, 
                              USBD_DescriptorsTypeDef *pdesc, 
                              uint8_t id)
```
- **功能**：初始化 USB 设备
- **应用**：内窥镜通过 USB 传输图像到 PC

---

#### USB CDC 发送（虚拟串口）

```c
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
```
- **功能**：通过 USB 虚拟串口发送数据
- **应用**：调试信息、命令通信
- **示例**：
```c
char msg[] = "Image captured\r\n";
CDC_Transmit_FS((uint8_t *)msg, strlen(msg));
```

---

### 9.6 SDIO/SDMMC 函数（SD卡存储）

#### 初始化

```c
HAL_StatusTypeDef HAL_SD_Init(SD_HandleTypeDef *hsd)
```
- **功能**：初始化 SD 卡
- **应用**：存储内窥镜拍摄的图像和视频

---

#### 读写块

```c
HAL_StatusTypeDef HAL_SD_ReadBlocks(SD_HandleTypeDef *hsd, 
                                     uint8_t *pData, 
                                     uint32_t BlockAdd, 
                                     uint32_t NumberOfBlocks, 
                                     uint32_t Timeout)
```
- **功能**：从 SD 卡读取数据块

---

```c
HAL_StatusTypeDef HAL_SD_WriteBlocks(SD_HandleTypeDef *hsd, 
                                      uint8_t *pData, 
                                      uint32_t BlockAdd, 
                                      uint32_t NumberOfBlocks, 
                                      uint32_t Timeout)
```
- **功能**：向 SD 卡写入数据块
- **示例**：
```c
// 写入一张图像到 SD 卡
uint8_t image_data[512];
HAL_SD_WriteBlocks(&hsd1, image_data, 0, 1, 1000);  // 写入第0块
```

---

#### DMA 方式（推荐）

```c
HAL_StatusTypeDef HAL_SD_ReadBlocks_DMA(SD_HandleTypeDef *hsd, 
                                         uint8_t *pData, 
                                         uint32_t BlockAdd, 
                                         uint32_t NumberOfBlocks)
```
- **功能**：DMA 方式读取 SD 卡
- **应用**：高速读取视频文件

---

### 9.7 RTC 函数（实时时钟）

#### 设置时间

```c
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *hrtc, 
                                   RTC_TimeTypeDef *sTime, 
                                   uint32_t Format)
```
- **功能**：设置 RTC 时间
- **应用**：为图像文件添加时间戳
- **示例**：
```c
RTC_TimeTypeDef sTime = {0};
sTime.Hours = 14;
sTime.Minutes = 30;
sTime.Seconds = 0;
HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
```

---

#### 获取时间

```c
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *hrtc, 
                                   RTC_TimeTypeDef *sTime, 
                                   uint32_t Format)
```
- **功能**：读取 RTC 时间
- **示例**：
```c
RTC_TimeTypeDef sTime;
RTC_DateTypeDef sDate;
HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);  // 必须调用，否则时间不更新

char filename[32];
sprintf(filename, "IMG_%02d%02d%02d.jpg", sTime.Hours, sTime.Minutes, sTime.Seconds);
```

---

### 9.8 CRC 函数（数据校验）

#### 计算 CRC

```c
uint32_t HAL_CRC_Calculate(CRC_HandleTypeDef *hcrc, 
                           uint32_t pBuffer[], 
                           uint32_t BufferLength)
```
- **功能**：计算数据的 CRC 校验值
- **应用**：图像数据完整性校验、固件升级校验
- **示例**：
```c
uint32_t data[] = {0x12345678, 0xABCDEF00};
uint32_t crc = HAL_CRC_Calculate(&hcrc, data, 2);
```

---

### 9.9 看门狗函数（系统可靠性）

#### 独立看门狗（IWDG）

```c
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *hiwdg)
```
- **功能**：初始化独立看门狗
- **应用**：防止系统死机

---

```c
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg)
```
- **功能**：喂狗（重置看门狗计数器）
- **示例**：
```c
while (1)
{
    // 正常工作
    ProcessImage();
    
    // 定期喂狗
    HAL_IWDG_Refresh(&hiwdg);
}
```

---

### 9.10 Flash 操作函数（参数存储、固件升级）

#### 擦除 Flash

```c
HAL_StatusTypeDef HAL_FLASH_Unlock(void)
```
- **功能**：解锁 Flash（写入前必须调用）

---

```c
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, 
                                     uint32_t *SectorError)
```
- **功能**：擦除 Flash 扇区
- **应用**：存储设备参数、用户配置

---

#### 写入 Flash

```c
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, 
                                     uint32_t Address, 
                                     uint64_t Data)
```
- **功能**：向 Flash 写入数据
- **示例**：
```c
// 保存设备参数到 Flash
HAL_FLASH_Unlock();

FLASH_EraseInitTypeDef EraseInit;
EraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
EraseInit.Sector = FLASH_SECTOR_11;  // 最后一个扇区
EraseInit.NbSectors = 1;
uint32_t SectorError;
HAL_FLASHEx_Erase(&EraseInit, &SectorError);

// 写入数据
uint32_t address = 0x080E0000;  // Sector 11 起始地址
HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, 0x12345678);

HAL_FLASH_Lock();
```

---

### 9.11 内窥镜项目典型架构

```
┌─────────────────────────────────────┐
│         应用层                       │
│  图像采集 / 存储 / 传输 / UI控制     │
├─────────────────────────────────────┤
│         驱动层                       │
│  传感器驱动 / 显示驱动 / 存储驱动    │
├─────────────────────────────────────┤
│         HAL 层                       │
│  SPI / I2C / SDIO / USB / ADC       │
└─────────────────────────────────────┘
```

**常用外设组合**：

| 功能模块 | 使用外设 |
|---------|----------|
| 图像传感器 | SPI / I2C（配置）+ DMA（数据传输） |
| 图像存储 | SDIO（SD卡）/ SPI（Flash） |
| 数据传输 | USB / UART |
| 显示屏 | SPI / LTDC（LCD控制器） |
| 按键/旋钮 | GPIO / ADC |
| 电机控制 | TIM（PWM） |
| 温度监测 | ADC / I2C（数字温度传感器） |
| 参数存储 | Flash / I2C（EEPROM） |
| 时间戳 | RTC |
| 系统监控 | IWDG（看门狗） |

---

### 9.12 医疗设备开发注意事项

1. **可靠性**：
   - 必须使用看门狗
   - 关键数据要 CRC 校验
   - 电源监控（欠压保护）

2. **实时性**：
   - 图像采集用 DMA，不能阻塞
   - 中断优先级合理分配
   - 避免在中断中执行耗时操作

3. **数据安全**：
   - 图像数据完整性校验
   - 参数存储要有备份机制
   - 固件升级要有回滚机制

4. **调试接口**：
   - 保留 UART 调试口
   - 关键状态通过 LED 指示
   - 记录错误日志到 Flash

---

## 参考资料

- STM32 HAL 库用户手册：`UM1905`（ST 官网下载）
- 芯片参考手册：`RM0436`（STM32MP1 系列）
- 数据手册：`DS12766`（STM32MP157）
- USB 协议规范：USB 2.0 Specification
- SD 卡协议：SD Card Physical Layer Specification
