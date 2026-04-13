# 05 ARM NEON SIMD

## 一、什么是 SIMD

SIMD = Single Instruction, Multiple Data（单指令多数据）

普通运算一次处理 1 个数据，SIMD 一次处理多个数据：

```
标量运算（普通 CPU 指令）：
  A[0] + B[0] = C[0]     ← 第 1 条指令
  A[1] + B[1] = C[1]     ← 第 2 条指令
  A[2] + B[2] = C[2]     ← 第 3 条指令
  A[3] + B[3] = C[3]     ← 第 4 条指令
  共 4 条指令

SIMD 运算（NEON 指令）：
  [A[0], A[1], A[2], A[3]]
+ [B[0], B[1], B[2], B[3]]
= [C[0], C[1], C[2], C[3]]   ← 1 条指令完成 4 个加法
  共 1 条指令，理论加速 4 倍
```

---

## 二、NEON 寄存器

ARM Cortex-A7 有 16 个 128 位 NEON 寄存器（Q0-Q15），每个可以装：

```
128 位寄存器的不同解读方式：

16 × uint8   [u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8]
8  × uint16  [u16,  u16,  u16,  u16,  u16,  u16,  u16,  u16]
4  × uint32  [  u32,      u32,      u32,      u32  ]
4  × float32 [  f32,      f32,      f32,      f32  ]
2  × uint64  [      u64,              u64      ]
```

图像处理中最常用 `16 × uint8`——一次处理 16 个像素（灰度图）或 16 个通道值（彩色图的某个通道）。

---

## 三、NEON Intrinsic 函数

不需要写汇编，用 C 函数调用 NEON 指令：

```cpp
#include <arm_neon.h>
```

### 3.1 数据类型

```
uint8x16_t   — 16 个 uint8（最常用，处理像素）
uint8x8_t    — 8 个 uint8
int16x8_t    — 8 个 int16（中间计算用，防溢出）
uint16x8_t   — 8 个 uint16
float32x4_t  — 4 个 float32
```

命名规则：`{类型}{位数}x{个数}_t`

### 3.2 加载和存储

```cpp
uint8_t pixels[16];  // 16 个像素

// 加载：从内存读入 NEON 寄存器
uint8x16_t v = vld1q_u8(pixels);     // 加载 16 个 uint8

// 存储：从 NEON 寄存器写回内存
vst1q_u8(pixels, v);                  // 存储 16 个 uint8

// 交错加载（YUYV 格式很有用）
uint8x16x2_t yuyv = vld2q_u8(data);  // 加载 32 字节，按奇偶分成两组
// yuyv.val[0] = [Y0, Y1, Y2, ..., Y15]  所有 Y 值
// yuyv.val[1] = [U0, V0, U1, V1, ...]   所有 U/V 值

uint8x16x4_t rgba = vld4q_u8(data);  // 加载 64 字节，按 4 通道分组
// rgba.val[0] = 所有 R 值
// rgba.val[1] = 所有 G 值
// rgba.val[2] = 所有 B 值
// rgba.val[3] = 所有 A 值
```

### 3.3 算术运算

```cpp
uint8x16_t a, b, c;

c = vaddq_u8(a, b);      // 加法：c = a + b（饱和到 255）
c = vsubq_u8(a, b);      // 减法：c = a - b（饱和到 0）
c = vmulq_u8(a, b);      // 乘法

// 扩展乘法（uint8 × uint8 → uint16，防溢出）
uint16x8_t wide = vmull_u8(vget_low_u8(a), vget_low_u8(b));

// 饱和加法（结果超过 255 就截断为 255）
c = vqaddq_u8(a, b);

// 饱和减法（结果小于 0 就截断为 0）
c = vqsubq_u8(a, b);
```

### 3.4 比较和选择

```cpp
// 比较
uint8x16_t mask = vcgtq_u8(a, b);  // a > b ? 0xFF : 0x00

// 按掩码选择
c = vbslq_u8(mask, a, b);  // mask=0xFF 选 a，mask=0x00 选 b
```

### 3.5 类型转换

```cpp
// uint8 → uint16（扩展，低 8 个元素）
uint16x8_t wide = vmovl_u8(vget_low_u8(narrow));

// uint16 → uint8（收窄，饱和）
uint8x8_t narrow = vqmovn_u16(wide);

// int16 → uint8（收窄，饱和，处理负值）
uint8x8_t result = vqmovun_s16(signed_val);
```

---

## 四、YUYV → RGB 的 NEON 实现思路

### 4.1 标量版本（当前）

```cpp
// 每次处理 2 个像素
for (int i = 0; i < size; i += 4) {
    int y0 = yuyv[i+0], u = yuyv[i+1];
    int y1 = yuyv[i+2], v = yuyv[i+3];

    R0 = Y0 + 1.402 * (V - 128);
    G0 = Y0 - 0.344 * (U - 128) - 0.714 * (V - 128);
    B0 = Y0 + 1.772 * (U - 128);
    // ... 像素 1 同理
}
```

### 4.2 NEON 版本思路

```cpp
// 每次处理 16 个像素（32 字节 YUYV 数据）

// 1. 交错加载，分离 Y 和 UV
uint8x16x2_t yuyv = vld2q_u8(src);
uint8x16_t Y = yuyv.val[0];   // 16 个 Y 值
uint8x16_t UV = yuyv.val[1];  // 8 个 U + 8 个 V 交替

// 2. 分离 U 和 V（从 UV 中按奇偶提取）
uint8x8x2_t uv_pair = vuzp_u8(vget_low_u8(UV), vget_high_u8(UV));
uint8x8_t U = uv_pair.val[0];  // 8 个 U 值
uint8x8_t V = uv_pair.val[1];  // 8 个 V 值

// 3. U 和 V 需要扩展为 16 个（每个 U/V 对应 2 个像素）
// 用 vzip 复制：[U0,U1,U2,...] → [U0,U0,U1,U1,U2,U2,...]

// 4. 用定点数代替浮点数（1.402 ≈ 359/256）
//    R = Y + 359 * (V-128) / 256
//    用 vmull + vshrn 实现乘法和右移

// 5. 饱和到 [0, 255]
//    用 vqmovun 处理负值和溢出

// 6. 交错存储 RGB
//    vst3q_u8(dst, rgb);  // 按 R,G,B,R,G,B,... 存储
```

### 4.3 定点数技巧

浮点运算在 Cortex-A7 上很慢，用定点数替代：

```
浮点：R = Y + 1.402 × (V - 128)
定点：R = Y + (359 × (V - 128)) >> 8

1.402 × 256 = 358.9 ≈ 359
>> 8 等价于 / 256

同理：
0.344 × 256 = 88.1 ≈ 88
0.714 × 256 = 182.8 ≈ 183
1.772 × 256 = 453.6 ≈ 454
```

---

## 五、性能对比预期

| 实现方式 | 每帧耗时（640×480） | 加速比 |
|---|---|---|
| OpenCV cvtColor | ~15ms | 1x（基准） |
| 手写标量 C | ~12ms | 1.3x |
| NEON intrinsic | ~3-5ms | 3-5x |

---

## 六、编译配置

```cmake
# CMakeLists.txt 或 arm_toolchain.cmake 中已有
target_compile_options(endoscope PRIVATE -mfpu=neon-vfpv4 -mfloat-abi=hard)
```

`-mfpu=neon-vfpv4`：启用 NEON 指令集
`-mfloat-abi=hard`：硬件浮点

---

## 七、命名规则速查

```
v    — NEON 向量操作前缀
ld   — load（加载）
st   — store（存储）
add  — 加法
sub  — 减法
mul  — 乘法
mull — 扩展乘法（结果位宽翻倍）
q    — 128位寄存器（16个uint8）
_u8  — uint8 类型
_s16 — int16 类型

示例：
vld1q_u8   = v + ld1 + q + _u8  = 加载 16 个 uint8
vaddq_u8   = v + add + q + _u8  = 16 个 uint8 加法
vmull_u8   = v + mul + l + _u8  = 8 个 uint8 扩展乘法（结果 uint16）
vqmovn_u16 = v + q + mov + n + _u16 = 饱和收窄 uint16 → uint8
```

---

## 八、学习建议

1. 先理解 SIMD 的思想（一次处理多个数据）
2. 用简单例子练手：16 个 uint8 的加法
3. 再实现 YUYV→RGB（P5 的目标）
4. 最后尝试加速图像混合（自适应锐化的 blend 步骤）

不需要记住所有 intrinsic 函数，用到时查 ARM 官方文档：
https://developer.arm.com/architectures/instruction-sets/intrinsics/
