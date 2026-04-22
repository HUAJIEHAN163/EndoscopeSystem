# FreeRTOS 前置知识

## 一、RTOS 核心概念

### 1.1 为什么需要 RTOS

裸机（bare-metal）程序在 `while(1)` 中顺序执行所有功能，问题：
- 一个功能阻塞（如 I2C 等待 20ms），其他功能全部停滞
- 无法区分紧急任务和非紧急任务
- 代码耦合度高，添加新功能需要修改主循环

RTOS 将程序拆分为多个**任务（Task）**，每个任务独立运行，由调度器根据优先级分配 CPU 时间。

### 1.2 任务（Task）

任务 = 一个永不返回的函数 + 独立的栈空间。

```c
void Task_Temperature(void *argument)
{
    for (;;)  // 永不返回
    {
        read_sensor();
        vTaskDelay(pdMS_TO_TICKS(1000));  // 睡眠 1 秒，让出 CPU
    }
}

// 创建任务
xTaskCreate(Task_Temperature,  // 任务函数
            "Temp",            // 名称（调试用）
            384,               // 栈大小（单位：word，即 384×4=1536 字节）
            NULL,              // 传给任务的参数
            1,                 // 优先级（数字越大越高）
            NULL);             // 任务句柄（不需要可传 NULL）
```

### 1.3 任务状态

```
                    ┌─────────┐
         创建后 ──→ │  Ready  │ ←── 事件/超时到达
                    └────┬────┘
                         │ 被调度器选中
                         ▼
                    ┌─────────┐
                    │ Running │ ←── 同一时刻只有一个任务在 Running
                    └────┬────┘
                    ┌────┴────┐
          vTaskDelay│         │等待信号量/队列
                    ▼         ▼
              ┌──────────┐ ┌──────────┐
              │ Blocked  │ │ Blocked  │
              │(超时等待) │ │(事件等待) │
              └──────────┘ └──────────┘
```

- **Ready**：准备好运行，等待调度器分配 CPU
- **Running**：正在执行（单核 MCU 同一时刻只有一个）
- **Blocked**：等待某个事件（延时到期、信号量、队列消息等）

### 1.4 调度器（Scheduler）

调度器在 SysTick 中断中运行（每 1ms 触发一次），决定哪个任务获得 CPU：

**规则**：
1. 最高优先级的 Ready 任务获得 CPU
2. 同优先级的任务轮流执行（时间片轮转）
3. 高优先级任务变为 Ready 时，立即抢占低优先级任务

```
时间 →
Task_Main (优先级2):    ██  ██  ██  ██  ██  ██
Task_Temp (优先级1):      ██      ████      ██
Idle      (优先级0):        ██          ████
```

### 1.5 vTaskDelay vs HAL_Delay

| | HAL_Delay | vTaskDelay |
|---|---|---|
| 实现 | 忙等（轮询 SysTick 计数器） | 将任务设为 Blocked，让出 CPU |
| 阻塞范围 | 整个 CPU（所有功能停滞） | 仅当前任务（其他任务继续运行） |
| CPU 利用率 | 100%（浪费在等待上） | 接近 0%（其他任务或 idle 运行） |
| 适用场景 | 裸机、中断中 | FreeRTOS 任务中 |

```c
// 裸机：HAL_Delay(1000) 期间什么都不能做
HAL_Delay(1000);

// FreeRTOS：vTaskDelay(1000) 期间其他任务正常运行
vTaskDelay(pdMS_TO_TICKS(1000));
```

---

## 二、同步机制

### 2.1 信号量（Semaphore）

用于任务间通知："某个事件发生了"。

```c
SemaphoreHandle_t xSem = xSemaphoreCreateBinary();

// 任务 A：等待事件
xSemaphoreTake(xSem, portMAX_DELAY);  // 阻塞直到信号量可用
process_data();

// 任务 B 或中断：通知事件发生
xSemaphoreGive(xSem);  // 释放信号量，唤醒任务 A
```

典型场景：DMA 传输完成中断中释放信号量，任务中等待。

### 2.2 互斥锁（Mutex）

用于保护共享资源，防止多个任务同时访问。

```c
SemaphoreHandle_t xMutex = xSemaphoreCreateMutex();

// 任务 A
xSemaphoreTake(xMutex, portMAX_DELAY);
shared_resource++;  // 安全访问
xSemaphoreGive(xMutex);

// 任务 B
xSemaphoreTake(xMutex, portMAX_DELAY);
shared_resource--;  // 安全访问
xSemaphoreGive(xMutex);
```

Mutex vs Binary Semaphore：Mutex 有优先级继承机制，防止优先级反转。

### 2.3 消息队列（Queue）

用于任务间传递数据。

```c
QueueHandle_t xQueue = xQueueCreate(10, sizeof(int));

// 生产者任务
int data = 42;
xQueueSend(xQueue, &data, portMAX_DELAY);

// 消费者任务
int received;
xQueueReceive(xQueue, &received, portMAX_DELAY);  // 阻塞直到有数据
```

### 2.4 选择指南

| 需求 | 使用 |
|---|---|
| "事件发生了，快处理" | 二值信号量 |
| "保护共享变量" | 互斥锁 |
| "传递数据给另一个任务" | 消息队列 |
| "计数资源（如 buffer 池）" | 计数信号量 |

---

## 三、中断与 FreeRTOS

### 3.1 中断中可以调用的 API

FreeRTOS 的大部分 API **不能在中断中调用**。中断中必须使用 `FromISR` 后缀的版本：

| 任务中 | 中断中 |
|---|---|
| `xSemaphoreGive()` | `xSemaphoreGiveFromISR()` |
| `xQueueSend()` | `xQueueSendFromISR()` |
| `vTaskDelay()` | ❌ 不能在中断中调用 |
| `xSemaphoreTake()` | ❌ 不能在中断中调用 |

```c
// 中断处理函数
void IPCC_RX1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);  // 如果唤醒了高优先级任务，立即切换
}
```

### 3.2 中断优先级要求

FreeRTOS 管理的中断优先级必须 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`（数值上 ≥，即优先级更低）。

```
优先级数值:  0  1  2  3  4  [5]  6  7  8  ...  15
            ←── 更高优先级    │    更低优先级 ──→
                              │
                    configMAX_SYSCALL = 5
                              │
            不受 FreeRTOS 管理 │ 受 FreeRTOS 管理
            不能调用 FreeRTOS  │ 可以调用 FromISR API
```

本项目中 IPCC 中断优先级 = 5，正好在边界上。

---

## 四、内存管理

### 4.1 堆（Heap）

FreeRTOS 使用自己的堆管理器（`heap_4.c`），不使用标准 `malloc`。

```c
// FreeRTOS 堆大小配置
#define configTOTAL_HEAP_SIZE  ((size_t)15360)  // 15KB
```

堆用于：
- `xTaskCreate` 分配任务 TCB + 栈
- `xSemaphoreCreateBinary` 分配信号量
- `xQueueCreate` 分配队列

### 4.2 栈大小估算

每个任务有独立的栈，用于局部变量、函数调用、中断嵌套。

```
栈大小 = 局部变量 + 函数调用深度 × 每层开销 + 中断嵌套预留

经验值（Cortex-M4）：
  简单任务（无 printf）：128-256 words
  中等任务（有 printf/snprintf）：384-512 words
  复杂任务（深层调用链）：512-1024 words
```

本项目：
- Task_Main：512 words（有 printf、parse_command）
- Task_Temperature：384 words（有 snprintf、I2C 操作）

### 4.3 堆空间计算

```
Task_Main:        512 × 4 = 2048 bytes + TCB ~100 bytes = ~2148 bytes
Task_Temperature: 384 × 4 = 1536 bytes + TCB ~100 bytes = ~1636 bytes
Idle Task:        128 × 4 = 512 bytes（静态分配，不占堆）
信号量/队列:      ~100 bytes
────────────────────────────────────────────────────────
总计:             ~3884 bytes + FreeRTOS 内部开销 ≈ 5KB
配置堆大小:       15KB（留有余量）
```

---

## 五、FreeRTOS API 速查

### 5.1 任务管理

| API | 功能 |
|---|---|
| `xTaskCreate(func, name, stack, param, priority, handle)` | 创建任务 |
| `vTaskDelete(handle)` | 删除任务（传 NULL 删除自己） |
| `vTaskDelay(ticks)` | 延时（相对时间） |
| `vTaskDelayUntil(&lastWake, ticks)` | 延时（绝对时间，更精确的周期执行） |
| `vTaskSuspend(handle)` / `vTaskResume(handle)` | 挂起/恢复任务 |
| `uxTaskPriorityGet(handle)` / `vTaskPrioritySet(handle, prio)` | 获取/设置优先级 |
| `vTaskStartScheduler()` | 启动调度器（不会返回） |

### 5.2 信号量

| API | 功能 |
|---|---|
| `xSemaphoreCreateBinary()` | 创建二值信号量（初始为空） |
| `xSemaphoreCreateCounting(max, initial)` | 创建计数信号量 |
| `xSemaphoreCreateMutex()` | 创建互斥锁 |
| `xSemaphoreTake(sem, timeout)` | 获取（阻塞等待） |
| `xSemaphoreGive(sem)` | 释放 |
| `xSemaphoreGiveFromISR(sem, pxWoken)` | 中断中释放 |

### 5.3 队列

| API | 功能 |
|---|---|
| `xQueueCreate(length, itemSize)` | 创建队列 |
| `xQueueSend(queue, &item, timeout)` | 发送到队尾 |
| `xQueueReceive(queue, &item, timeout)` | 从队头接收 |
| `xQueueSendFromISR(queue, &item, pxWoken)` | 中断中发送 |

### 5.4 时间转换

```c
pdMS_TO_TICKS(1000)   // 1000ms → tick 数（configTICK_RATE_HZ=1000 时等于 1000）
portMAX_DELAY          // 永久等待
```

---

## 六、FreeRTOS vs uCOS 对照（面试用）

| 概念 | FreeRTOS API | uCOS API | 功能 |
|---|---|---|---|
| 创建任务 | `xTaskCreate()` | `OSTaskCreate()` | 创建一个线程 |
| 延时 | `vTaskDelay()` | `OSTimeDly()` | 任务睡眠指定时间 |
| 信号量释放 | `xSemaphoreGive()` | `OSSemPost()` | 通知等待的任务 |
| 信号量等待 | `xSemaphoreTake()` | `OSSemPend()` | 阻塞等待通知 |
| 消息队列发送 | `xQueueSend()` | `OSQPost()` | 发送数据到队列 |
| 消息队列接收 | `xQueueReceive()` | `OSQPend()` | 从队列接收数据 |
| 互斥锁 | `xSemaphoreCreateMutex()` | `OSMutexCreate()` | 保护共享资源 |
| 启动调度器 | `vTaskStartScheduler()` | `OSStart()` | 开始多任务调度 |

核心概念完全一样，只是 API 名字不同。会 FreeRTOS 就会 uCOS。

---

## 七、常见面试问题

### Q1：FreeRTOS 的任务调度算法是什么？

**抢占式优先级调度 + 同优先级时间片轮转**。最高优先级的 Ready 任务获得 CPU，同优先级任务按时间片轮流执行。

### Q2：什么是优先级反转？如何解决？

低优先级任务持有互斥锁，高优先级任务等待该锁，中优先级任务抢占低优先级任务 → 高优先级任务被中优先级任务间接阻塞。

解决：**优先级继承**（FreeRTOS 的 Mutex 自动支持）— 低优先级任务持有锁时临时提升到等待者的优先级。

### Q3：中断中为什么不能调用 `xSemaphoreTake`？

`xSemaphoreTake` 可能导致任务阻塞（Blocked 状态），但中断不是任务，不能被阻塞。中断必须快速返回，不能等待。

### Q4：`vTaskDelay(100)` 和 `HAL_Delay(100)` 的区别？

`HAL_Delay` 忙等 100ms，CPU 100% 占用，其他任务无法运行。`vTaskDelay` 将当前任务设为 Blocked，CPU 去运行其他任务，100ms 后自动唤醒。

### Q5：FreeRTOS 的堆和 C 标准库的堆有什么区别？

FreeRTOS 使用自己的堆管理器（`heap_4.c`），从一个静态数组 `ucHeap[configTOTAL_HEAP_SIZE]` 中分配。不使用 `malloc/free`，避免了标准库堆管理的碎片化和不确定性问题。

### Q6：如何确定任务栈大小？

1. 估算：局部变量 + 调用深度 × 每层开销 + 中断预留
2. 实测：用 `uxTaskGetStackHighWaterMark()` 获取栈最小剩余量
3. 经验：先给大值（512-1024），稳定后根据 high water mark 缩小

### Q7：本项目中为什么 RPMsg 轮询放在高优先级任务？

A7 发送的 LED 亮度指令需要快速响应（用户体验），不能被温度读取（含 20ms I2C 等待）阻塞。温度每秒读一次，延迟几十毫秒无影响。

---

## 八、本项目中的实际应用

### 8.1 任务间数据共享

温度数据通过 `volatile` 全局变量共享（`sht30_temp`、`sht30_humi`），不需要互斥锁，因为：
- 写入方只有 Task_Temperature（单写者）
- 读取方只有 Task_Main（通过 RPMsg 上报时读取）
- `int16_t` 在 Cortex-M4 上是原子写入

### 8.2 中断→任务通信

按键事件通过 `volatile uint8_t key1_event` 标志从 TIM3 中断传递到 Task_Main：
- TIM3 中断中置位 `key1_event = 1`
- Task_Main 中检测并清除 `key1_event = 0`
- 不需要信号量，因为 Task_Main 每 10ms 轮询一次，响应足够快

### 8.3 OpenAMP 与 FreeRTOS 的交互

`OPENAMP_check_for_message()` 必须在任务中频繁调用（不能在中断中调用），因为它内部会处理 virtio 回调链，可能涉及内存分配和 mutex 操作。

IPCC 中断只设置标志（`msg_received_ch1/ch2`），实际消息处理在 `OPENAMP_check_for_message()` → `MAILBOX_Poll()` 中完成。
