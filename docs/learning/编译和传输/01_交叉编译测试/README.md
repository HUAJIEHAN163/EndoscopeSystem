# 01 — 交叉编译三级测试详解

> 目的：验证 Linaro gcc 7.5.0 工具链能否正确编译出开发板可运行的程序
> 测试从简单到复杂，逐级排查问题

---

## 为什么要分三个级别？

交叉编译涉及多个环节，任何一个出问题都会导致程序无法在开发板上运行。
分级测试的思路是**逐步增加复杂度**，这样出问题时能快速定位原因：

```
Level 1: 纯 C        → 验证编译器本身能否工作
Level 2: C++ STL     → 验证 C++ 标准库是否兼容
Level 3: OpenCV      → 验证第三方库链接是否正确
```

如果 Level 1 就失败了，说明工具链本身有问题；
如果 Level 1 通过但 Level 2 失败，说明 C++ 运行时库不兼容；
如果 Level 1、2 通过但 Level 3 失败，说明 OpenCV 交叉编译有问题。

---

## 准备工作

### 目录结构

```bash
mkdir -p /home/chris/env_setup/compat_test
cd /home/chris/env_setup/compat_test
```

### 工具链路径

我们使用的是 Linaro gcc 7.5.0，完整路径很长，先了解一下：

```
/opt/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc
/opt/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-g++
```

为了方便，可以用环境变量简化：

```bash
export LINARO=/opt/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf/bin
```

> ⚠️ 为什么不用 apt 安装的 arm-linux-gnueabihf-gcc？
> 因为 Ubuntu 24.04 apt 装的交叉编译器（gcc 13.3）链接的 sysroot glibc 是 2.39，
> 而开发板的 glibc 只有 2.28，编译出来的程序在开发板上会报错：
> `/lib/arm-linux-gnueabihf/libc.so.6: version 'GLIBC_2.34' not found`
>
> Linaro gcc 7.5.0 自带的 sysroot glibc 是 2.25，编译产物只需要 GLIBC_2.4~2.6，
> 开发板的 2.28 完全满足。

---

## Level 1：纯 C 程序

### 源码 test_level1.c

```c
#include <stdio.h>
int main() {
    printf("Level 1: Hello from ARM! (pure C)\n");
    return 0;
}
```

这是最简单的 C 程序，只用了 printf，验证编译器最基本的功能。

### 编译命令

```bash
$LINARO/arm-linux-gnueabihf-gcc test_level1.c -o test_level1
```

**命令解析：**

| 部分 | 含义 |
|------|------|
| `$LINARO/arm-linux-gnueabihf-gcc` | 使用 Linaro 的 ARM 交叉编译器（C 编译器） |
| `test_level1.c` | 输入的源文件 |
| `-o test_level1` | 输出的可执行文件名 |

### 验证编译产物

```bash
file test_level1
```

期望输出：
```
test_level1: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
dynamically linked, interpreter /lib/ld-linux-armhf.so.3, ...
```

**关键信息解读：**

| 字段 | 含义 |
|------|------|
| `ELF 32-bit` | 可执行文件格式，32 位（ARM 是 32 位处理器） |
| `LSB` | 小端序（Little-Endian），ARM 默认字节序 |
| `ARM, EABI5` | 目标架构是 ARM，使用 EABI5 接口规范 |
| `dynamically linked` | 动态链接（依赖运行时的共享库） |
| `interpreter /lib/ld-linux-armhf.so.3` | 动态链接器路径（armhf = ARM hard float） |

> 如果你在虚拟机上直接运行 `./test_level1`，会报错：
> `cannot execute binary file: Exec format error`
> 因为虚拟机是 x86 架构，无法运行 ARM 程序。这恰恰说明编译正确了。

### 检查 GLIBC 依赖

```bash
objdump -T test_level1 | grep -oP 'GLIBC_\d+\.\d+' | sort -V | uniq
```

期望输出：
```
GLIBC_2.4
```

含义：这个程序运行时需要目标系统至少有 GLIBC 2.4。开发板是 2.28，完全满足。

**objdump 命令解析：**

| 部分 | 含义 |
|------|------|
| `objdump -T` | 显示动态符号表（程序依赖的外部符号） |
| `grep -oP 'GLIBC_\d+\.\d+'` | 用正则提取 GLIBC 版本号 |
| `sort -V` | 按版本号排序 |
| `uniq` | 去重 |

---

## Level 2：C++ STL 程序

### 源码 test_level2.cpp

```cpp
#include <iostream>
#include <vector>
#include <string>
int main() {
    std::vector<std::string> v = {"Level 2:", "Hello", "from", "ARM!", "(C++ STL)"};
    for (const auto& s : v) std::cout << s << " ";
    std::cout << std::endl;
    return 0;
}
```

比 Level 1 复杂的地方：
- 使用了 C++ 标准库（iostream, vector, string）
- 使用了 C++11 特性（初始化列表、auto、范围 for）
- 需要链接 libstdc++（C++ 标准库的运行时）

### 编译命令

```bash
$LINARO/arm-linux-gnueabihf-g++ test_level2.cpp -o test_level2
```

注意这里用的是 `g++`（C++ 编译器），不是 `gcc`（C 编译器）。
g++ 会自动链接 C++ 标准库（libstdc++）。

### 验证

```bash
file test_level2
# 期望：ELF 32-bit LSB executable, ARM, ...

objdump -T test_level2 | grep -oP 'GLIBC_\d+\.\d+' | sort -V | uniq
# 期望：GLIBC_2.4

objdump -T test_level2 | grep -oP 'GLIBCXX_\d+\.\d+(\.\d+)?' | sort -V | uniq
# 期望：GLIBCXX_3.4, GLIBCXX_3.4.21 等
```

> Level 2 额外引入了 GLIBCXX 依赖（C++ 标准库版本）。
> 开发板上的 libstdc++ 需要提供这些版本的符号。
> Linaro 7.5 编译出来的 GLIBCXX 依赖较低，开发板一般都满足。

---

## Level 3：OpenCV 程序

### 源码 test_level3.cpp

```cpp
#include <opencv2/core.hpp>
#include <iostream>
int main() {
    cv::Mat m = cv::Mat::eye(3, 3, CV_32F);
    std::cout << "Level 3: OpenCV " << CV_VERSION
              << " - identity matrix sum = " << cv::sum(m)[0] << std::endl;
    return 0;
}
```

这个程序：
- 创建了一个 3×3 单位矩阵（对角线为 1，其余为 0）
- 计算矩阵所有元素的和（应该是 3）
- 输出 OpenCV 版本号

### 编译命令

```bash
$LINARO/arm-linux-gnueabihf-g++ test_level3.cpp -o test_level3 \
    -I/opt/opencv_arm/include/opencv2 \
    -I/opt/opencv_arm/include \
    -L/opt/opencv_arm/lib \
    -lopencv_core \
    -Wl,-rpath,/opt/opencv_arm/lib
```

**这条命令比前两个复杂很多，逐个解析：**

| 参数 | 含义 |
|------|------|
| `-I/opt/opencv_arm/include/opencv2` | 添加头文件搜索路径（opencv2 子目录） |
| `-I/opt/opencv_arm/include` | 添加头文件搜索路径（include 根目录） |
| `-L/opt/opencv_arm/lib` | 添加库文件搜索路径 |
| `-lopencv_core` | 链接 libopencv_core.so（-l 后面跟库名，省略 lib 前缀和 .so 后缀） |
| `-Wl,-rpath,/opt/opencv_arm/lib` | 运行时库搜索路径（写入可执行文件中） |

**头文件搜索过程：**
```
源码写了 #include <opencv2/core.hpp>
编译器搜索：/opt/opencv_arm/include/opencv2/core.hpp → 找到！
```

**库文件链接过程：**
```
编译器需要 -lopencv_core
搜索：/opt/opencv_arm/lib/libopencv_core.so → 找到！
```

### -I、-L、-l 的区别（重要概念）

```
-I（大写 i）：告诉编译器去哪里找 .h / .hpp 头文件（编译阶段）
-L（大写 L）：告诉链接器去哪里找 .so / .a 库文件（链接阶段）
-l（小写 L）：指定要链接哪个库（链接阶段）
-Wl,-rpath：告诉程序运行时去哪里找 .so（运行阶段）
```

### 验证

```bash
file test_level3
# 期望：ELF 32-bit LSB executable, ARM, ...

objdump -T test_level3 | grep -oP 'GLIBC_\d+\.\d+' | sort -V | uniq
# 期望：GLIBC_2.4
```

---

## 三级测试汇总

| 级别 | 测试内容 | 编译器 | 额外依赖 | GLIBC 需求 |
|------|----------|--------|----------|------------|
| Level 1 | 纯 C (printf) | gcc | 无 | 2.4 |
| Level 2 | C++ STL (vector, string) | g++ | libstdc++ | 2.4 |
| Level 3 | OpenCV (cv::Mat) | g++ | libopencv_core.so | 2.4 |

开发板 glibc = 2.28，全部兼容 ✅

---

## 常见问题

### Q: 为什么 Level 3 需要 -I -L -l 而 Level 1/2 不需要？

Level 1/2 用的是系统标准库（libc、libstdc++），编译器知道它们在哪里。
OpenCV 是第三方库，安装在自定义路径 /opt/opencv_arm/，编译器不知道，需要手动指定。

### Q: -rpath 和 LD_LIBRARY_PATH 有什么区别？

两者都是告诉程序运行时去哪里找共享库：
- `-Wl,-rpath`：编译时写死在可执行文件里，永久生效
- `LD_LIBRARY_PATH`：运行时临时设置的环境变量，只对当前 shell 生效

### Q: 为什么不能在虚拟机上直接运行这些程序？

虚拟机 CPU 是 x86_64 架构，编译出来的是 ARM 架构的程序。
就像 Windows 的 .exe 不能在 Mac 上运行一样，不同 CPU 架构的程序互不兼容。
这就是为什么需要把程序传到 ARM 开发板上运行。
