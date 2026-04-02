# 02 — 文件传输与开发板验证详解

> 目的：把虚拟机上交叉编译好的 ARM 程序传到开发板上运行，验证整个工具链的正确性

---

## 整体流程

```
虚拟机 (x86_64)                          开发板 (ARM)
┌─────────────────┐                    ┌─────────────────┐
│ 交叉编译源码     │                    │                 │
│       ↓         │     scp/NFS        │                 │
│ ARM 可执行文件   │ ──────────────→    │ 运行 ARM 程序    │
│ ARM .so 库文件   │                    │                 │
└─────────────────┘                    └─────────────────┘
  192.168.0.130                          192.168.0.43
```

---

## 第一步：确认网络连通

### ping 测试

```bash
# 在虚拟机上执行
ping -c 2 192.168.0.43
```

实际输出：
```
64 bytes from 192.168.0.43: icmp_seq=1 ttl=64 time=40.9 ms
64 bytes from 192.168.0.43: icmp_seq=2 ttl=64 time=467 ms
```

**解读：**
- `ttl=64`：数据包生存时间，说明是直连（没有经过路由器转发）
- `time=40.9 ms` / `467 ms`：延迟波动很大，因为开发板用的是 WiFi
- `0% packet loss`：没有丢包，网络是通的

> 如果 ping 不通，检查：
> 1. 虚拟机是否设为桥接模式
> 2. 虚拟机和开发板是否在同一网段（都是 192.168.0.x）
> 3. 开发板是否已连接 WiFi

---

## 第二步：SSH 连接开发板

### 基本连接

```bash
ssh debian@192.168.0.43
# 密码：temppwd
```

**首次连接会看到：**
```
The authenticity of host '192.168.0.43 (192.168.0.43)' can't be established.
ED25519 key fingerprint is SHA256:xxxxx
Are you sure you want to continue connecting (yes/no/[fingerprint])? yes
```

输入 `yes` 确认，SSH 会把开发板的指纹保存到 `~/.ssh/known_hosts`，下次不再询问。

### SSH 命令解析

```
ssh debian@192.168.0.43
│    │      │
│    │      └── 开发板 IP 地址
│    └── 用户名（开发板上的用户）
└── SSH 客户端命令
```

### 切换到 root 用户

```bash
# SSH 登录后，在开发板上执行
su -
# 密码：root
```

> ⚠️ 为什么不能直接 `ssh root@192.168.0.43`？
> 开发板的 SSH 服务（sshd）默认禁止 root 远程登录，这是安全策略。
> 配置文件在开发板的 `/etc/ssh/sshd_config` 中：
> `PermitRootLogin no`
>
> 但通过串口（MobaXterm）登录不受此限制，因为串口不走 SSH。

### 退出 SSH

```bash
exit        # 如果在 root，先退回 debian
exit        # 退出 SSH，回到虚拟机
```

---

## 第三步：确认开发板环境

在传文件之前，先确认开发板上的情况，避免覆盖已有文件或遗漏依赖。

```bash
# 在开发板上执行（通过 SSH）

# 检查是否已有 OpenCV 库
find /usr/lib -name 'libopencv_core*' 2>/dev/null

# 检查 /tmp 下是否有同名文件
ls /tmp/test_level* 2>/dev/null
```

我们的实际结果：
```
=== opencv ===
（空，没有 OpenCV）
=== /tmp test files ===
（空，没有同名文件）
done
```

**为什么要做这个检查？**
- 如果开发板已有 OpenCV，可以直接用，不需要传库文件
- 如果有同名文件，scp 会直接覆盖，可能丢失开发板上的原有文件

---

## 第四步：scp 传输文件

### scp 基本语法

```
scp [选项] 源文件 用户名@目标IP:目标路径
```

### 传输测试程序

```bash
cd /home/chris/env_setup/compat_test

# 传输三个测试程序到开发板的 /tmp 目录
scp test_level1 test_level2 test_level3 debian@192.168.0.43:/tmp/
# 密码：temppwd
```

**命令解析：**

| 部分 | 含义 |
|------|------|
| `scp` | Secure Copy，基于 SSH 的安全文件传输 |
| `test_level1 test_level2 test_level3` | 要传输的本地文件（可以一次传多个） |
| `debian@192.168.0.43` | 目标机器的用户名和 IP |
| `:/tmp/` | 目标路径（开发板上的 /tmp 目录） |

> 选择 /tmp 目录是因为：
> - 所有用户都有写权限
> - 重启后会清空，不会污染系统
> - 适合临时测试

### 传输 OpenCV 库文件

因为开发板上没有 OpenCV，Level 3 测试需要把库文件也传过去：

```bash
scp /opt/opencv_arm/lib/libopencv_core.so.3.4.16 debian@192.168.0.43:/tmp/
```

只需要传实际的 .so 文件（带完整版本号的那个），符号链接在开发板上创建。

---

## 第五步：在开发板上运行测试

### Level 1：纯 C

```bash
# 在开发板上执行
cd /tmp
./test_level1
```

输出：
```
Level 1: Hello from ARM! (pure C)
```

### Level 2：C++ STL

```bash
./test_level2
```

输出：
```
Level 2: Hello from ARM! (C++ STL)
```

### Level 3：OpenCV

Level 3 需要先创建符号链接，然后指定库搜索路径：

```bash
# 创建符号链接（模拟库的版本链）
cd /tmp
ln -sf libopencv_core.so.3.4.16 libopencv_core.so.3.4
ln -sf libopencv_core.so.3.4 libopencv_core.so

# 运行（指定库搜索路径）
LD_LIBRARY_PATH=/tmp ./test_level3
```

输出：
```
Level 3: OpenCV 3.4.16 - identity matrix sum = 3
```

### 符号链接详解

Linux 共享库使用版本号命名机制：

```
libopencv_core.so.3.4.16    ← 实际文件（包含完整版本号）
libopencv_core.so.3.4       ← 符号链接（soname，主版本号）
libopencv_core.so           ← 符号链接（开发用，链接时使用）
```

**为什么需要这三层？**
- `.so.3.4.16`：实际的库文件，包含所有代码
- `.so.3.4`：程序运行时查找的名字（soname），写在 ELF 头中
- `.so`：编译时 `-lopencv_core` 查找的名字

`ln -sf` 命令解析：
- `ln`：创建链接
- `-s`：符号链接（软链接），类似 Windows 的快捷方式
- `-f`：如果目标已存在，强制覆盖

### LD_LIBRARY_PATH 详解

```bash
LD_LIBRARY_PATH=/tmp ./test_level3
```

这行命令做了两件事：
1. 临时设置环境变量 `LD_LIBRARY_PATH=/tmp`（告诉动态链接器去 /tmp 找库）
2. 运行 `./test_level3`

这个环境变量只对这一条命令生效，不会影响其他程序。

> 如果不设置 LD_LIBRARY_PATH，会报错：
> `./test_level3: error while loading shared libraries: libopencv_core.so.3.4: cannot open shared object file: No such file or directory`
> 因为动态链接器默认只在 /lib、/usr/lib 等标准路径下找库。

---

## 第六步：一条命令完成所有测试（实际操作）

我们实际执行时，把所有步骤合并成了一条 SSH 命令：

```bash
ssh debian@192.168.0.43 "cd /tmp && \
    ln -sf libopencv_core.so.3.4.16 libopencv_core.so.3.4 && \
    ln -sf libopencv_core.so.3.4 libopencv_core.so && \
    ./test_level1 && \
    ./test_level2 && \
    LD_LIBRARY_PATH=/tmp ./test_level3"
```

**技巧：** `ssh 用户@IP "命令"` 可以不进入交互式 shell，直接在远程执行命令并返回结果。
`&&` 表示前一条命令成功才执行下一条，任何一步失败就停止。

实际输出：
```
Level 1: Hello from ARM! (pure C)
Level 2: Hello from ARM! (C++ STL)
Level 3: OpenCV 3.4.16 - identity matrix sum = 3
```

---

## 验证结果汇总

| 级别 | 内容 | 编译 | 开发板运行 | GLIBC 需求 | 开发板 GLIBC |
|------|------|------|-----------|------------|-------------|
| Level 1 | 纯 C | ✅ | ✅ | 2.4 | 2.28 ✅ |
| Level 2 | C++ STL | ✅ | ✅ | 2.4 | 2.28 ✅ |
| Level 3 | OpenCV | ✅ | ✅ | 2.4 | 2.28 ✅ |

**结论：Linaro gcc 7.5.0 工具链 + 交叉编译的 OpenCV 3.4.16 完全可用。**

---

## 常见问题

### Q: scp 传输很慢怎么办？

开发板用 WiFi 连接，延迟大、带宽低。解决方案：
1. 改用有线网络连接开发板
2. 使用 NFS 共享目录（见 03_NFS 文档），避免反复传输
3. 压缩后再传：`tar czf files.tar.gz test_level* && scp files.tar.gz ...`

### Q: 开发板上运行报 "Permission denied"？

文件没有执行权限：
```bash
chmod +x /tmp/test_level1
```

scp 传输时通常会保留权限，但如果出问题可以手动加。

### Q: 开发板上运行报 "No such file or directory" 但文件明明存在？

可能是动态链接器找不到。用 `file` 命令检查：
```bash
file /tmp/test_level1
```
确认是 ARM 格式。如果显示的 interpreter 路径在开发板上不存在，说明工具链配置有问题。
