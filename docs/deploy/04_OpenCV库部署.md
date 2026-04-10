# Step 4：OpenCV 库部署到开发板 [✅ 已完成]

## 这一步在做什么

开发板上没有预装 OpenCV，但我们的程序依赖 OpenCV 库。需要把虚拟机上交叉编译好的 OpenCV ARM 版库文件部署到开发板上。

```
虚拟机 /opt/opencv_arm/lib/
  ├── libopencv_core.so.3.4.16
  ├── libopencv_imgproc.so.3.4.16
  ├── ...（共 17 个库文件）
      ↓ NFS 传输
开发板 /usr/lib/
  ├── libopencv_core.so.3.4.16
  ├── libopencv_imgproc.so.3.4.16
  ├── ...
```

---

## 4.1 部署步骤

### 虚拟机上：复制到 NFS 共享目录

```bash
cp /opt/opencv_arm/lib/libopencv_*.so* /home/chris/nfs_share/
```

### 开发板上：复制到系统库目录

```bash
mount -t nfs 192.168.0.130:/home/chris/nfs_share /mnt/nfs -o nolock
cp /mnt/nfs/libopencv_*.so* /usr/lib/
```

### 创建符号链接

每个库需要创建符号链接链：
```
libopencv_core.so → libopencv_core.so.3.4 → libopencv_core.so.3.4.16
```

**实际上不需要手动创建**。上一步的 `cp /mnt/nfs/libopencv_*.so* /usr/lib/` 中 `*.so*` 通配符会匹配三种文件：
- `libopencv_core.so.3.4.16`（真实文件）
- `libopencv_core.so.3.4`（符号链接）
- `libopencv_core.so`（符号链接）

`cp` 会把符号链接一并复制过去，无需额外操作。

如果需要手动创建（例如符号链接丢失），在开发板 `/usr/lib/` 目录下执行：

```bash
cd /usr/lib
ln -s libopencv_core.so.3.4.16 libopencv_core.so.3.4
ln -s libopencv_core.so.3.4    libopencv_core.so
```

`ln -s A B` 的含义：创建名为 B 的符号链接，指向 A。

### 刷新库缓存

```bash
ldconfig
```

---

## 4.2 验证

```bash
# 检查库文件
ls /usr/lib/libopencv_core.so*

# 运行测试程序（之前 Step 5 验证时已通过）
# 无需 LD_LIBRARY_PATH 即可直接运行
```

---

## 4.3 部署的库文件清单

项目用到的 OpenCV 模块（CMakeLists.txt 中指定）：

| 模块 | 库文件 | 用途 |
|------|--------|------|
| core | libopencv_core.so | Mat、基础数据结构 |
| imgproc | libopencv_imgproc.so | 图像处理（cvtColor、滤波、CLAHE 等） |
| imgcodecs | libopencv_imgcodecs.so | 图像编解码（读写 jpg/png） |
| videoio | libopencv_videoio.so | 视频读写（VideoCapture、VideoWriter） |
| calib3d | libopencv_calib3d.so | 相机标定、畸变校正 |

这些模块还会依赖其他 OpenCV 库（如 imgproc 依赖 core），所以实际部署了 17 个库文件。

### 验证结果

- [x] 17 个 OpenCV 3.4.16 库文件部署到 /usr/lib/ ✓
- [x] 符号链接已创建 ✓
- [x] ldconfig 已执行 ✓
- [x] 无需 LD_LIBRARY_PATH 即可运行 ✓
