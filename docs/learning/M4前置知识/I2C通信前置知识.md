# I2C 通信前置知识

## 一、I2C 基础

### 1.1 什么是 I2C

I2C（Inter-Integrated Circuit）= 两线式串行总线，用于 MCU 与低速外设（传感器、EEPROM、OLED 等）通信。

```
        SCL（时钟线）
MCU ──────────────────── 传感器1
  │     SDA（数据线）        │
  ├──────────────────────────┤
  │                          │
  └──── 传感器2 ─────────────┘
        （可挂多个设备）
```

**特点**：
- 只需 2 根线（SCL + SDA）+ 上拉电阻
- 支持多主多从（通常单主多从）
- 每个从设备有唯一的 7 位地址
- 速率：标准模式 100kHz，快速模式 400kHz

### 1.2 通信时序

```
起始条件    地址(7bit)+R/W        ACK   数据(8bit) ACK 停止条件
  │         │                     │     │        │     │
  S ─ A6 A5 A4 A3 A2 A1 A0 R/W ─ ACK ─ D7...D0 ─ ACK ─ P
  │                         │     │              │     │
  SDA 拉低   从机地址        0=写  从机应答       数据   SDA 拉高
  SCL 为高                   1=读  (拉低SDA)            SCL 为高
```

### 1.3 7 位地址 vs 8 位地址

I2C 从机地址是 7 位，但 HAL 库使用 **8 位格式**（左移 1 位）：

```
SHT30 的 7 位地址 = 0x44
HAL 库使用的地址  = 0x44 << 1 = 0x88

#define SHT30_ADDR  (0x44 << 1)  // 0x88
```

最低位由 HAL 库自动设置（0=写，1=读）。

---

## 二、HAL 库 I2C 操作

### 2.1 主机发送

```c
uint8_t data[] = {0x24, 0x00};  // SHT30 测量命令
HAL_I2C_Master_Transmit(&hi2c1,
                         SHT30_ADDR,    // 从机地址（8位格式）
                         data,          // 发送数据
                         2,             // 数据长度
                         100);          // 超时（ms）
```

### 2.2 主机接收

```c
uint8_t buf[6];
HAL_I2C_Master_Receive(&hi2c1,
                        SHT30_ADDR,    // 从机地址
                        buf,           // 接收缓冲区
                        6,             // 接收长度
                        100);          // 超时
```

### 2.3 寄存器读写（Mem 操作）

很多传感器有内部寄存器，用 `HAL_I2C_Mem_Read/Write`：

```c
// 写寄存器：先发寄存器地址，再发数据
HAL_I2C_Mem_Write(&hi2c1, DEVICE_ADDR, REG_ADDR, I2C_MEMADD_SIZE_8BIT,
                  &value, 1, 100);

// 读寄存器：先发寄存器地址，再读数据
HAL_I2C_Mem_Read(&hi2c1, DEVICE_ADDR, REG_ADDR, I2C_MEMADD_SIZE_8BIT,
                 buf, len, 100);
```

SHT30 不使用寄存器模式，而是命令模式（发送 2 字节命令，等待，读取 6 字节结果）。

---

## 三、SHT30 温湿度传感器

### 3.1 通信流程

```
MCU                          SHT30
 │                            │
 │── 发送测量命令 [0x24,0x00] ──→│  高重复性单次测量
 │                            │
 │      等待 20ms（测量中）      │
 │                            │
 │←── 读取 6 字节数据 ─────────│
 │  [TempH, TempL, CRC,       │
 │   HumiH, HumiL, CRC]       │
```

### 3.2 数据解析

```c
// 原始值（16位无符号）
uint16_t raw_temp = (data[0] << 8) | data[1];
uint16_t raw_humi = (data[3] << 8) | data[4];

// 温度：T = -45 + 175 × raw / 65535（单位：°C）
// 乘以 10 用整数传输（避免浮点）
int16_t temp = -450 + (1750 * (int32_t)raw_temp) / 65535;
// temp = 256 表示 25.6°C

// 湿度：RH = 100 × raw / 65535（单位：%）
int16_t humi = (1000 * (int32_t)raw_humi) / 65535;
// humi = 654 表示 65.4%
```

### 3.3 CRC 校验（可选）

SHT30 每 2 字节数据后跟 1 字节 CRC-8 校验。本项目暂未实现 CRC 校验，生产环境应该加上。

---

## 四、I2C 引脚冲突问题

### 4.1 CubeMX 默认引脚可能被占用

CubeMX 自动分配的 I2C1 引脚可能与其他外设冲突：

```bash
# 查看引脚占用情况
cat /sys/kernel/debug/pinctrl/soc:pin-controller@50002000/pinmux-pins | grep "PH11\|PH12\|PF14\|PF15"

# 结果：
# PH11 → dcmi（摄像头占用）
# PH12 → display-controller（显示器占用）
# PF14 → i2c（正确）
# PF15 → i2c（正确）
```

本项目手动修正为 PF14(SCL) + PF15(SDA)。

---

## 五、常见面试问题

### Q1：I2C 的上拉电阻为什么是必须的？

I2C 的 SDA 和 SCL 是开漏输出，只能主动拉低，不能主动拉高。上拉电阻将总线默认拉到高电平。设备通过拉低总线来发送 0，释放总线（上拉电阻拉高）来发送 1。

### Q2：I2C 的 ACK 和 NACK 是什么？

- ACK（Acknowledge）：接收方在第 9 个时钟周期拉低 SDA，表示"收到了"
- NACK（Not Acknowledge）：SDA 保持高电平，表示"没收到"或"不再接收"

主机读取最后一个字节时发送 NACK，告诉从机"不用再发了"。

### Q3：I2C 总线死锁怎么办？

从机在传输中途被打断（如 MCU 复位），可能一直拉低 SDA。解决方法：
1. MCU 启动时在 SCL 上发送 9 个时钟脉冲，让从机释放 SDA
2. 硬件上加 SDA/SCL 的复位电路

### Q4：I2C 和 SPI 的区别？

| | I2C | SPI |
|---|---|---|
| 线数 | 2（SCL+SDA） | 4（SCLK+MOSI+MISO+CS） |
| 速率 | 100k-3.4MHz | 可达 50MHz+ |
| 寻址 | 地址寻址（多设备共享总线） | CS 片选（每设备一根线） |
| 适用 | 低速传感器、EEPROM | 高速 Flash、显示屏、ADC |
