# 13_11 开发记录：FreeRTOS 多任务集成

## 一、背景

### 裸机架构的局限

步骤 6 完成后，M4 固件在裸机 `while(1)` 主循环中运行所有逻辑：

```
while(1) {
    OPENAMP_check_for_message();   // RPMsg 轮询
    parse_command();                // 指令解析
    handle_key_events();            // 按键处理
    if (每秒一次) SHT30_Read();    // 温度读取（含 20ms HAL_Delay）
    HAL_Delay(10);                  // 主循环节拍
}
```

问题：
- 所有功能串行执行，`SHT30_Read` 中的 `HAL_Delay(20)` 会阻塞 RPMsg 轮询
- 无法实现优先级调度（A7 指令应该比温度读取更优先响应）
- 后续添加更多功能（电机控制、看门狗等）会使主循环越来越臃肿

### 目标

用 FreeRTOS 将裸机主循环拆分为独立任务，每个任务负责一个功能，通过优先级调度实现实时响应。

---

## 二、最终架构

```
Task_Main (优先级 2)              Task_Temperature (优先级 1)
├── OPENAMP_check_for_message()   ├── SHT30_Read()
├── 指令解析 (A7→M4)              ├── 上报温湿度 (M4→A7)
├── 按键事件处理                   ├── 温度超限保护
├── vTaskDelay(10ms)              └── vTaskDelay(1000ms)
└── 循环                          └── 循环
```

### 任务设计

| 任务 | 优先级 | 栈大小 | 职责 | 周期 |
|---|---|---|---|---|
| Task_Main | 2（较高） | 512 words | RPMsg 轮询、指令解析、按键事件 | 10ms |
| Task_Temperature | 1（较低） | 384 words | SHT30 读取、温湿度上报、温度保护 | 1000ms |

### 优先级设计理由

- Task_Main 优先级更高：A7 指令需要快速响应（如 LED 亮度调节），不能被温度读取阻塞
- Task_Temperature 优先级较低：每秒执行一次，不紧急，可以被 Task_Main 抢占

---

## 三、实施步骤

- [x] Step 1：CubeMX 添加 FreeRTOS（CMSIS_V1）
- [x] Step 2：修复 3 处 CubeMX 覆盖的文件
- [x] Step 3：修复 libmetal FreeRTOS 后端空实现
- [x] Step 4：用 FreeRTOS 原生 API 替代缺失的 CMSIS_V1 API
- [x] Step 5：编写任务代码
- [x] Step 6：开发板验证

---

## 四、遇到的问题

### 问题 1：CMSIS_V2 缺少头文件

CubeMX 选择 CMSIS_V2 接口时，缺少 `freertos_mpool.h` 和 `freertos_os2.h`，编译失败。

**解决**：改用 CMSIS_V1。

### 问题 2：CMSIS_V1 也缺少 `cmsis_os.h`

CubeMX 生成的代码引用了 `osThreadId`、`osKernelStart` 等 CMSIS API，但 `cmsis_os.h` 文件不存在。

**解决**：
- 用 FreeRTOS 原生 API 替代（`xTaskCreate`、`vTaskStartScheduler`、`vTaskDelay`）
- 在 USER CODE 中 `typedef void* osThreadId` 满足 CubeMX 生成的变量声明

### 问题 3：libmetal FreeRTOS 后端 `sys_irq_save_disable` 空实现

`Middlewares/.../libmetal/lib/system/freertos/template/sys.c` 中的中断保护函数是空的（模板文件，标注 "Add implementation here"），导致 OpenAMP 内部临界区保护无效。

**解决**：填写 PRIMASK 实现：
```c
void sys_irq_restore_enable(unsigned int flags)
{
    __asm volatile ("msr primask, %0" :: "r" (flags) : "memory");
}

unsigned int sys_irq_save_disable(void)
{
    unsigned int state;
    __asm volatile ("mrs %0, primask" : "=r" (state) :: "memory");
    __asm volatile ("cpsid i" ::: "memory");
    return state;
}
```

### 问题 4：开发板内核 oops 导致 RPMsg 驱动状态异常（问题 #21）

在排查过程中，`cat /dev/ttyRPMSG0` 在 M4 停止时触发内核 oops，破坏了 `rpmsg_tty` 驱动状态。后续所有测试都因为内核状态异常而失败，误导了排查方向。

**解决**：重启开发板。

**经验**：停止 M4 前必须先 kill 所有读取 `/dev/ttyRPMSG0` 的进程。

---

## 五、代码修改详细记录

### 5.1 main.c 修改

**变量去掉 `static`**（供 app_freertos.c 通过 extern 访问）：

```c
// 改动前
static VIRT_UART_HandleTypeDef huart0;
static volatile uint8_t rpmsg_rx_buf[...];
// ...

// 改动后
VIRT_UART_HandleTypeDef huart0;
volatile uint8_t rpmsg_rx_buf[...];
// ...
```

**函数去掉 `static`**：

```c
// 改动前
static void rpmsg_send_str(const char *str);
static void parse_command(...);
static HAL_StatusTypeDef SHT30_Read(...);
static void check_temperature(...);

// 改动后
void rpmsg_send_str(const char *str);
void parse_command(...);
HAL_StatusTypeDef SHT30_Read(...);
void check_temperature(...);
```

**调度器启动**：

```c
// 替换 CubeMX 生成的 CMSIS API
// osThreadDef(...); osThreadCreate(...); osKernelStart();
// 改为：
MX_FREERTOS_Init();
vTaskStartScheduler();
```

**while(1) 清空**（调度器启动后不会执行到）。

### 5.2 app_freertos.c 修改

**使用 FreeRTOS 原生 API**：

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

void MX_FREERTOS_Init(void)
{
  xTaskCreate(Task_Main, "Main", 512, NULL, 2, NULL);
  xTaskCreate(Task_Temperature, "Temp", 384, NULL, 1, NULL);
}
```

**Task_Main**：RPMsg 轮询 + 指令解析 + 按键事件

**Task_Temperature**：每秒读取 SHT30 + 上报 + 温度保护

### 5.3 FreeRTOSConfig.h

| 参数 | 值 | 说明 |
|---|---|---|
| configTOTAL_HEAP_SIZE | 15360 | 15KB，足够 2 个任务 + idle |
| configTICK_RATE_HZ | 1000 | 1ms tick |
| configUSE_MUTEXES | 1 | libmetal 需要 |
| configSUPPORT_STATIC_ALLOCATION | 1 | idle task 静态分配 |
| configSUPPORT_DYNAMIC_ALLOCATION | 1 | 任务动态创建 |

### 5.4 CubeMX 重新生成后需要手动修改的地方（4 处）

| # | 文件 | 修改 |
|---|---|---|
| 1 | `CM4/OPENAMP/openamp_conf.h` | 启用 `VIRTUAL_UART_MODULE_ENABLED` |
| 2 | `Middlewares/.../virt_uart.c` | service name 改为 `"rpmsg-tty-channel"` |
| 3 | `Middlewares/.../freertos/template/sys.c` | 填写 `sys_irq_save_disable` / `sys_irq_restore_enable` |
| 4 | `CM4/Core/Src/main.c` | 如果 CubeMX 重新生成了 CMSIS API 调用，需要替换为原生 FreeRTOS API |

---

## 六、开发板验证结果

### 测试环境

| 项目 | 内容 |
|---|---|
| 工程 | EndoscopeM4_v1.2 |
| 固件大小 | text 56KB, bss 12KB |
| FreeRTOS heap | 15KB |
| 开发板 | 野火 STM32MP157 (lubancat)，重启后干净状态 |

### 功能验证

| 测试项 | 结果 |
|---|---|
| RPMsg 通道建立 | ✅ ttyRPMSG0 出现 |
| A7→M4 亮度指令 `L50` | ✅ `[M4] LED=50% (PWM=499)` |
| M4→A7 温度上报 | ✅ 每秒 `T244`（24.4°C） |
| M4→A7 湿度上报 | ✅ 每秒 `H614`（61.4%） |
| 多任务并行 | ✅ 指令响应和温度上报互不阻塞 |

---

## 七、FreeRTOS vs 裸机对比

| 项目 | 裸机 | FreeRTOS |
|---|---|---|
| 代码结构 | 单个 while(1) | 独立任务函数 |
| 优先级调度 | 无 | Task_Main 可抢占 Task_Temperature |
| 阻塞处理 | HAL_Delay 阻塞所有功能 | vTaskDelay 只阻塞当前任务 |
| 扩展性 | 添加功能需修改主循环 | 新建任务即可 |
| 固件大小 | 46KB | 56KB (+10KB FreeRTOS 内核) |
| RAM 占用 | 2.7KB | 12KB (+9KB 任务栈+堆) |

---

## 八、相关文件

| 文件 | 内容 |
|---|---|
| `CM4/Core/Src/main.c` | 外设初始化、共享函数、调度器启动 |
| `CM4/Core/Src/app_freertos.c` | 任务创建和实现 |
| `CM4/Core/Inc/FreeRTOSConfig.h` | FreeRTOS 配置 |
| `Middlewares/.../freertos/template/sys.c` | libmetal IRQ 实现 |
