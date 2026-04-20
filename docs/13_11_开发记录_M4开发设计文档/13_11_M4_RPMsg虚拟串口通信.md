# 13_11 开发记录：RPMsg 虚拟串口通信

## 一、背景

### 设计文档步骤 6 目标

原设计为「UART 通信 + DMA 接收 + printf 重定向」，即通过物理 USART1 实现 A7-M4 通信。

实际开发中改用 **OpenAMP RPMsg 虚拟串口**方案，优势：
- 不占用物理 UART 引脚（省出给其他用途）
- 不需要配置 DMA、空闲中断
- A7 侧用法几乎一样（`/dev/ttyRPMSG0` 代替 `/dev/ttySTMx`）
- 通信协议完全不变（`T256\n`, `L75\n`, `K1\n`）

### 前置条件

| 项目 | 状态 |
|---|---|
| CubeMX 启用 OpenAMP (Remote 模式) | ✅ |
| IPCC 已配置 + NVIC 中断使能 | ✅ |
| 链接脚本 SRAM3_ipc_shm 区域 | ✅ |
| OpenAMP 框架代码已生成 | ✅ |
| 基础编译通过 | ✅ |

---

## 二、最终架构

```
A7 (Linux)                              M4 (裸机)
┌──────────────────┐                   ┌──────────────────┐
│ /dev/ttyRPMSG0   │                   │ VIRT_UART        │
│   echo "L50" >   │ ──── RPMsg ────→ │   RxCallback     │
│   cat <           │ ←── RPMsg ──── │   Transmit       │
└──────────────────┘                   └──────────────────┘
        │                                       │
        │ rpmsg_tty 驱动                        │ IPCC + 共享内存
        │ (CONFIG_RPMSG_TTY=y)                  │ (SRAM3 0x10040000)
        ▼                                       ▼
   Linux remoteproc                      OpenAMP 框架
   加载 M4 ELF                           resource_table
```

### 通信协议（与设计文档一致）

| 方向 | 格式 | 示例 | 含义 |
|---|---|---|---|
| A7→M4 | `L<0-100>\n` | `L75\n` | LED 亮度 75% |
| M4→A7 | `T<temp>\n` | `T255\n` | 温度 25.5°C |
| M4→A7 | `H<humi>\n` | `H580\n` | 湿度 58.0% |
| M4→A7 | `K<1\|2>\n` | `K1\n` | 按键事件（冻结） |
| M4→A7 | `A<1\|2>\n` | `A2\n` | 温度报警（严重） |

---

## 三、实施步骤

- [x] Step 1：CubeMX 启用 OpenAMP + IPCC
- [x] Step 2：启用 VIRT_UART 模块
- [x] Step 3：实现 M4 侧通信逻辑（接收回调、发送、printf 重定向）
- [x] Step 4：解决 resource table 识别问题（GCC 13 alignment）
- [x] Step 5：解决 RPMsg service name 不匹配问题
- [x] Step 6：开发板验证全部功能

---

## 四、代码修改详细记录

### 4.1 openamp_conf.h — 启用 VIRT_UART

```c
// 改动前
//#define VIRTUAL_UART_MODULE_ENABLED

// 改动后
#define VIRTUAL_UART_MODULE_ENABLED
```

### 4.2 virt_uart.c — 修正 service name

```c
// 改动前（CubeMX 生成的默认值）
#define RPMSG_SERVICE_NAME  "rpmsg-tty"

// 改动后（匹配野火开发板 Linux 内核的 rpmsg_tty 驱动）
#define RPMSG_SERVICE_NAME  "rpmsg-tty-channel"
```

### 4.3 .cproject — 添加链接器参数

在 MCU GCC Linker 的 Other flags 中添加：

```
-Wl,-z,max-page-size=0x10000
```

解决 GCC 13 默认 segment alignment 为 0x1000 导致 Linux remoteproc 无法识别 resource table 的问题。

### 4.4 main.c — 完整通信逻辑

**新增头文件：**

```c
#include "openamp.h"
#include "virt_uart.h"
#include <string.h>
#include <stdio.h>
```

**新增宏定义：**

```c
#define RPMSG_SERVICE_NAME  "rpmsg-tty"
#define RPMSG_RX_BUF_SIZE   64
#define TEMP_WARNING   410  // 41.0°C
#define TEMP_CRITICAL  430  // 43.0°C
#define PWM_SAFE_DUTY  200  // 安全模式 20%
```

**新增变量：**

```c
static VIRT_UART_HandleTypeDef huart0;
static volatile uint8_t rpmsg_rx_buf[RPMSG_RX_BUF_SIZE];
static volatile uint16_t rpmsg_rx_len = 0;
static volatile uint8_t  rpmsg_rx_flag = 0;
static volatile uint8_t  rpmsg_ready = 0;
static volatile uint8_t key1_event = 0;
static volatile uint8_t key2_event = 0;
```

**VIRT_UART 接收回调：**

```c
static void VIRT_UART0_RxCpltCallback(VIRT_UART_HandleTypeDef *huart)
{
  uint16_t copy_len = (huart->RxXferSize < RPMSG_RX_BUF_SIZE - 1)
                      ? huart->RxXferSize : RPMSG_RX_BUF_SIZE - 1;
  memcpy((void *)rpmsg_rx_buf, huart->pRxBuffPtr, copy_len);
  rpmsg_rx_buf[copy_len] = '\0';
  rpmsg_rx_len = copy_len;
  rpmsg_rx_flag = 1;
}
```

**发送封装：**

```c
static void rpmsg_send_str(const char *str)
{
  if (rpmsg_ready)
    VIRT_UART_Transmit(&huart0, (void *)str, strlen(str));
}
```

**printf 重定向（按行缓冲发送）：**

```c
int __io_putchar(int ch)
{
  static char tx_buf[128];
  static uint8_t tx_idx = 0;

  tx_buf[tx_idx++] = (char)ch;
  if (ch == '\n' || tx_idx >= sizeof(tx_buf) - 1)
  {
    tx_buf[tx_idx] = '\0';
    rpmsg_send_str(tx_buf);
    tx_idx = 0;
  }
  return ch;
}
```

**指令解析（A7→M4）：**

```c
static void parse_command(const uint8_t *data, uint16_t len)
{
  if (len < 2) return;
  char type = data[0];
  int value = 0;

  for (uint16_t i = 1; i < len; i++)
  {
    if (data[i] >= '0' && data[i] <= '9')
      value = value * 10 + (data[i] - '0');
    else if (data[i] == '\n' || data[i] == '\r')
      break;
  }

  if (type == 'L' || type == 'l')
  {
    if (value > 100) value = 100;
    led_duty = (uint32_t)value * PWM_MAX / 100;
    led_on = (value > 0) ? 1 : 0;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, led_on ? led_duty : 0);
    printf("[M4] LED=%d%% (PWM=%lu)\r\n", value, (unsigned long)led_duty);
  }
}
```

**温度超限保护：**

```c
static void check_temperature(int16_t temp)
{
  if (temp > TEMP_CRITICAL)
  {
    led_duty = 0; led_on = 0;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0);
    rpmsg_send_str("A2\n");
  }
  else if (temp > TEMP_WARNING)
  {
    led_duty = PWM_SAFE_DUTY;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, led_on ? PWM_SAFE_DUTY : 0);
    rpmsg_send_str("A1\n");
  }
}
```

**初始化（USER CODE BEGIN 2）：**

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, led_duty);
HAL_TIM_Base_Start_IT(&htim3);

if (!IS_ENGINEERING_BOOT_MODE())
{
  if (VIRT_UART_Init(&huart0) == VIRT_UART_OK)
  {
    VIRT_UART_RegisterCallback(&huart0, VIRT_UART_RXCPLT_CB_ID,
                               VIRT_UART0_RxCpltCallback);
    rpmsg_ready = 1;
  }
}
```

**主循环（非阻塞，~100Hz）：**

```c
while (1)
{
  // 1. 轮询 RPMsg 消息
  OPENAMP_check_for_message();

  // 2. 处理 A7 指令
  if (rpmsg_rx_flag) {
    rpmsg_rx_flag = 0;
    parse_command((const uint8_t *)rpmsg_rx_buf, rpmsg_rx_len);
  }

  // 3. 处理按键事件
  if (key1_event) { key1_event = 0; rpmsg_send_str("K1\n"); }
  if (key2_event) { key2_event = 0; rpmsg_send_str("K2\n"); }

  // 4. 每秒读取 SHT30 + 上报
  static uint32_t last_sht30_tick = 0;
  if (HAL_GetTick() - last_sht30_tick >= 1000)
  {
    last_sht30_tick = HAL_GetTick();
    int16_t t, h;
    if (SHT30_Read(&t, &h) == HAL_OK) {
      char buf[32];
      snprintf(buf, sizeof(buf), "T%d\n", t); rpmsg_send_str(buf);
      snprintf(buf, sizeof(buf), "H%d\n", h); rpmsg_send_str(buf);
      check_temperature(t);
    }
  }

  HAL_Delay(10);
}
```

**TIM3 按键回调（改为设置事件标志）：**

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM3) return;
  // ... 读取 GPIO、消抖 ...
  if (key1_now && !key1_prev && (now - last_key1_tick > DEBOUNCE_MS)) {
    last_key1_tick = now;
    key1_event = 1;  // 主循环中通过 RPMsg 发送
  }
  if (key2_now && !key2_prev && (now - last_key2_tick > DEBOUNCE_MS)) {
    last_key2_tick = now;
    key2_event = 1;
  }
}
```

---

## 五、遇到的问题与解决

### 问题 1：Linux 报 "no resource table found"（问题 #20）

**现象**：M4 启动成功但 `/dev/ttyRPMSG0` 不出现

**根因**：GCC 13 链接器默认 `max-page-size=0x1000`，而野火开发板的 Linux 内核（较旧版本）在解析 ELF 时依赖 `0x10000` 对齐来定位 resource table section。

**排查过程**：
1. 确认内核支持 rpmsg（`CONFIG_RPMSG_TTY=y`）
2. 用野火示例 ELF 验证硬件链路正常
3. 用 `readelf -l` 对比 segment alignment 差异
4. 确认 GCC 版本差异（9.3 vs 13.3）

**修复**：链接器参数添加 `-Wl,-z,max-page-size=0x10000`

### 问题 2：channel 创建但 rpmsg_tty 驱动不匹配

**现象**：dmesg 显示 `creating channel rpmsg-tty addr 0x400` 但无 ttyRPMSG0

**根因**：Linux 内核的 `rpmsg_tty` 驱动匹配的 channel name 是 `"rpmsg-tty-channel"`，而 CubeMX 生成的 `virt_uart.c` 中定义的是 `"rpmsg-tty"`。

**修复**：修改 `virt_uart.c` 中的 `RPMSG_SERVICE_NAME` 为 `"rpmsg-tty-channel"`

### 架构变化（相比旧代码）

| 项目 | 旧代码（LED 闪烁显示温度） | 新代码（RPMsg 通信） |
|---|---|---|
| 主循环 | 阻塞式 HAL_Delay(3000) | 非阻塞 10ms 轮询 |
| 温度显示 | LED 闪烁编码 | RPMsg 上报 `T255\n` |
| 按键功能 | 直接控制 LED 开关/亮度 | 发送事件给 A7 (`K1\n`/`K2\n`) |
| LED 控制 | 本地按键 | A7 远程指令 `L50\n` |
| 温度保护 | 无 | M4 本地判断，不依赖 A7 |

---

## 六、开发板验证结果

### 6.1 测试环境

| 项目 | 内容 |
|---|---|
| 开发板 | 野火 STM32MP157 (lubancat) |
| M4 固件 | EndoscopeM4_CM4.elf (47KB text + OpenAMP) |
| Linux 内核 | CONFIG_RPMSG=y, CONFIG_RPMSG_TTY=y |
| 通信设备 | /dev/ttyRPMSG0 |

### 6.2 功能验证

| 测试项 | 命令 | 结果 |
|---|---|---|
| RPMsg 通道建立 | `echo start > .../state` | ✅ `ttyRPMSG0` 出现 |
| A7→M4 亮度指令 | `echo "L50" > /dev/ttyRPMSG0` | ✅ LED 变化，回复 `LED=50% (PWM=499)` |
| A7→M4 关灯 | `echo "L0" > /dev/ttyRPMSG0` | ✅ LED 熄灭 |
| A7→M4 全亮 | `echo "L100" > /dev/ttyRPMSG0` | ✅ `LED=100% (PWM=999)` |
| M4→A7 温度 | `cat /dev/ttyRPMSG0` | ✅ 每秒 `T255`（25.5°C） |
| M4→A7 湿度 | 同上 | ✅ 每秒 `H580`（58.0%） |
| M4→A7 按键 | 按 KEY1 | ✅ `K1` + `[M4] KEY1 → freeze` |
| printf 重定向 | 发送指令后 | ✅ 调试信息通过 RPMsg 可见 |

### 6.3 固件大小

| 段 | 无 OpenAMP | 含 RPMsg 通信 | 说明 |
|---|---|---|---|
| text | ~22KB | 47KB | OpenAMP 框架 + VIRT_UART |
| data | 12B | 296B | |
| bss | 1.7KB | 2.6KB | 接收缓冲区 |
| 总计 | 24KB | 50KB | SRAM1 128KB，占用 39% |

---

## 七、经验总结

### 7.1 GCC 版本升级的隐患

新版 GCC 链接器可能改变 ELF segment alignment 默认值，导致旧版 Linux 内核无法正确解析固件。排查方法：找一个已知能工作的 ELF，用 `readelf -l` 逐项对比。

### 7.2 RPMsg service name 必须精确匹配

不同开发板/内核版本的 `rpmsg_tty` 驱动可能匹配不同的 channel name。确认方法：加载已知能工作的 ELF，用 `dmesg` 查看实际创建的 channel name。

### 7.3 VIRT_UART vs 底层 RPMsg API

| | VIRT_UART | 底层 OPENAMP_create_endpoint |
|---|---|---|
| 复杂度 | 低（3 个 API） | 高（需要手动管理 endpoint） |
| 兼容性 | 好（STM32 官方封装） | 需要自己匹配 service name |
| 功能 | 虚拟串口（收发字节流） | 灵活（可自定义协议） |
| 推荐 | ✅ 一般场景 | 需要多通道或自定义协议时 |

### 7.4 主循环设计原则

从阻塞式改为非阻塞式轮询：
- `OPENAMP_check_for_message()` 必须在主循环中频繁调用
- 按键事件通过标志位从中断传递到主循环（中断中不能调用 RPMsg 发送）
- 温度读取用 `HAL_GetTick()` 计时，不用 `HAL_Delay()` 阻塞

---

## 八、相关文件清单

| 文件 | 修改内容 |
|---|---|
| `CM4/OPENAMP/openamp_conf.h` | 启用 `VIRTUAL_UART_MODULE_ENABLED` |
| `CM4/Core/Src/main.c` | 完整通信逻辑（接收、发送、解析、保护） |
| `CM4/.cproject` | 添加 `-Wl,-z,max-page-size=0x10000` |
| `Middlewares/.../virt_uart.c` | service name 改为 `"rpmsg-tty-channel"` |

---

## 九、下一步

| 步骤 | 内容 | 状态 |
|---|---|---|
| 步骤 7 | FreeRTOS 集成：拆分为独立任务 + 信号量同步 | ⬜ 待开始 |
| 步骤 8 | 整合：温度保护 + 按键→A7 + A7→PWM | ✅ 已在本步骤完成 |
| 步骤 9 | A7 侧 SerialListener + Qt UI 集成 | ⬜ 待开始 |

---

## 十、CubeMX 重新生成代码后的注意事项

> ❗ 每次 CubeMX 重新生成代码后，必须手动检查并恢复以下两处修改：
>
> 1. `CM4/OPENAMP/openamp_conf.h` — 确认 `VIRTUAL_UART_MODULE_ENABLED` 已启用（未被注释）
> 2. `Middlewares/.../virt_uart.c` — 将 `RPMSG_SERVICE_NAME` 改为 `"rpmsg-tty-channel"`
>
> 详见 `13_11_M4工程CubeMX配置记录.md` 第六章。
