# Buffer 数量验证测试

**目的**：验证 V4L2 驱动实际分配的 buffer 数量是否少于请求数量

**相关问题**：问题记录.md #13

---

## 修改内容

在 `src/capture/v4l2capture.cpp` 的 `requestBuffers()` 函数中添加了日志验证代码：

```cpp
// 请求 buffer
req.count = m_bufferCount;  // 请求 4 个
if (xioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) {
    emit errorOccurred("VIDIOC_REQBUFS 失败");
    return false;
}

// 【新增】验证实际分配的数量
qWarning() << QString("[Buffer 验证] 请求 %1 个 buffer，驱动实际分配 %2 个")
              .arg(m_bufferCount).arg(req.count);

if (req.count != (unsigned)m_bufferCount) {
    qWarning() << "[Buffer 警告] 驱动分配的 buffer 数量少于请求数量！"
               << "这可能导致 overrun 错误，建议优化处理速度。";
}

// 更新为实际分配的数量
m_bufferCount = req.count;
```

---

## 测试步骤

### 1. 编译项目

#### 虚拟机本地编译（测试代码是否正确）

```bash
cd /mnt/hgfs/VM_Share/project/EndoscopeSystem
bash scripts/build_local.sh
```

#### 交叉编译（部署到开发板）

**首次编译**（需要运行 cmake）：
```bash
cd /home/chris/build_arm
cmake /mnt/hgfs/VM_Share/project/EndoscopeSystem \
      -DCMAKE_TOOLCHAIN_FILE=../EndoscopeSystem/scripts/arm_toolchain.cmake \
      -DQt5_DIR=/opt/qt5.15.2_arm/lib/cmake/Qt5 \
      -DOpenCV_DIR=/opt/opencv4.5.5_arm/lib/cmake/opencv4
make -j$(nproc)
```

**后续编译**（只需要 make）：
```bash
cd /home/chris/build_arm
make -j$(nproc)
```

### 2. 部署到开发板

#### 通过 NFS 共享传输

```bash
# 虚拟机上：复制到 NFS 共享目录
cp /home/chris/build_arm/endoscope /home/chris/nfs_share/
```

### 3. 在开发板上运行并查看日志

```bash
# SSH 登录开发板
ssh root@192.168.7.2

# 重新挂载 NFS（确保读取最新文件）
umount /mnt/nfs
mount -t nfs 192.168.0.130:/home/chris/nfs_share /mnt/nfs -o nolock

# 复制到本地执行（避免 NFS I/O 问题）
cp /mnt/nfs/endoscope /tmp/endoscope
chmod +x /tmp/endoscope

# 停止可能占用摄像头的程序
kill $(pidof App) 2>/dev/null
sleep 0.5

# 运行程序并查看日志
/tmp/endoscope -platform linuxfb 2>&1 | tee /tmp/endoscope.log

# 或者只查看关键日志
/tmp/endoscope -platform linuxfb 2>&1 | grep -E "Buffer|V4L2 就绪|overrun"
```

### 4. 查看日志输出

**预期输出（如果驱动只分配 2 个）：**

```
[Buffer 验证] 请求 4 个 buffer，驱动实际分配 2 个
[Buffer 警告] 驱动分配的 buffer 数量少于请求数量！这可能导致 overrun 错误，建议优化处理速度。
V4L2 就绪: 640x480 @ 30fps, 2 buffers (实际分配)
```

**预期输出（如果驱动正常分配 4 个）：**

```
[Buffer 验证] 请求 4 个 buffer，驱动实际分配 4 个
V4L2 就绪: 640x480 @ 30fps, 4 buffers (实际分配)
```

### 5. 对比 v4l2-ctl 的输出

**重要验证**：对比程序运行前后的 v4l2 参数

#### 方法 1：使用验证脚本（推荐）

```bash
# 复制验证脚本到开发板
cp /mnt/nfs/scripts/verify_buffer_count.sh /tmp/
chmod +x /tmp/verify_buffer_count.sh

# 运行验证脚本
/tmp/verify_buffer_count.sh
```

#### 方法 2：手动验证

```bash
# 程序运行前查询
v4l2-ctl -d /dev/video0 --all | grep -A5 "Streaming Parameters"

# 启动程序（后台运行）
/tmp/endoscope -platform linuxfb &
sleep 3

# 程序运行时查询
v4l2-ctl -d /dev/video0 --all | grep -A5 "Streaming Parameters"

# 停止程序
kill %1

# 程序停止后查询
v4l2-ctl -d /dev/video0 --all | grep -A5 "Streaming Parameters"
```

**预期结果分析**：

**情况 A**：`Read buffers` 从 2 变成 4
```
程序运行前：Read buffers     : 2
程序运行时：Read buffers     : 4  ← 变化了
程序停止后：Read buffers     : 2
```
→ 说明 `Read buffers` 确实是 mmap buffer 数量，程序动态分配了 4 个

**情况 B**：`Read buffers` 始终是 2
```
程序运行前：Read buffers     : 2
程序运行时：Read buffers     : 2  ← 没变化
程序停止后：Read buffers     : 2
```
→ 说明 `Read buffers` 指的是其他东西（驱动内部参数），不是 mmap buffer 数量
→ 程序日志中的 4 个才是真实的 mmap buffer 数量

### 6. 观察 overrun 错误

运行程序一段时间后，查看内核日志：

```bash
dmesg | grep dcmi | tail -20
```

如果看到类似输出：
```
stm32-dcmi 4c006000.dcmi: Some errors found while streaming: errors=3721 (overrun=3724)
```

说明确实存在 buffer 不足导致的 overrun 问题。

---

## 验证结果记录

### 测试环境

- 开发板：野火鲁班猫 STM32MP157
- 摄像头：OV5640
- 驱动：stm32-dcmi
- 内核版本：4.19.94

### 测试结果

**日期**：____________________

**程序日志输出**：

```
（在此粘贴实际日志输出）
```

**v4l2-ctl 输出**：

```bash
v4l2-ctl -d /dev/video0 --all | grep "Read buffers"
# 输出：
```

**dmesg overrun 错误**：

```bash
dmesg | grep dcmi | tail -10
# 输出：
```

**结论**：

- [ ] 驱动实际分配的 buffer 数量：______ 个
- [ ] 是否少于请求数量（4 个）：是 / 否
- [ ] 是否出现 overrun 错误：是 / 否
- [ ] overrun 错误频率：______ 次 / ______ 帧

---

## 后续行动

### 如果驱动只分配 2 个 buffer

**短期方案**（推荐）：
1. ✅ 继续优化处理速度（已在进行）
   - 启用 OV5640 硬件白平衡
   - NEON 指令集加速
   - 多线程流水线

2. 🔲 监控 overrun 频率
   - 定期检查 `dmesg | grep overrun`
   - 记录不同处理负载下的 overrun 次数

**长期方案**（高级）：
1. 🔲 研究驱动源码
   - 查找 `drivers/media/platform/stm32/stm32-dcmi.c`
   - 确认是否有 `MAX_BUFFER_NUM` 限制
   - 评估修改风险

2. 🔲 尝试修改驱动（需要内核编译环境）
   - 增加 buffer 数量限制
   - 检查 DMA 内存是否足够
   - 重新编译内核并测试

### 如果驱动正常分配 4 个 buffer

说明问题不在 buffer 数量，需要排查其他原因：
- 检查 overrun 是否仍然发生
- 如果仍有 overrun，说明是处理速度问题
- 继续优化算法性能

---

## 参考资料

- V4L2 API 文档：https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-reqbufs.html
- 问题记录：`docs/问题记录.md` #13
- STM32 DCMI 驱动：`drivers/media/platform/stm32/stm32-dcmi.c`（需要内核源码）

---

## 注意事项

1. **日志级别**：使用 `qWarning()` 而不是 `qDebug()`，确保在 Release 版本中也能看到
2. **线程安全**：日志输出在采集线程中，Qt 的日志系统是线程安全的
3. **性能影响**：日志输出只在初始化时执行一次，不影响运行时性能
4. **后续清理**：验证完成后，可以将 `qWarning()` 改为 `qDebug()`，或者保留作为诊断信息
