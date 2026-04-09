# Step 3：NFS 共享目录配置 [✅ 已完成]

## 这一步在做什么

配置 NFS（Network File System），让虚拟机和开发板共享一个目录。编译好的程序放到共享目录，开发板直接就能访问，不需要手动用 scp 传文件。

```
虚拟机                              开发板
/home/chris/nfs_share/    ←NFS→    /mnt/nfs/
  ├── endoscope           同步      ├── endoscope
  ├── config/                       ├── config/
  └── qt5_libs/                     └── qt5_libs/
```

---

## 3.1 虚拟机侧（NFS 服务端）

### 安装 NFS 服务

```bash
sudo apt install nfs-kernel-server
```

### 创建共享目录

```bash
mkdir -p /home/chris/nfs_share
```

### 配置导出

编辑 `/etc/exports`，添加：

```
/home/chris/nfs_share *(rw,sync,no_subtree_check,no_root_squash)
```

参数说明：
- `*`：允许所有 IP 访问
- `rw`：可读可写
- `sync`：同步写入
- `no_root_squash`：开发板 root 用户有完整权限

### 生效配置

```bash
sudo exportfs -ra
sudo systemctl restart nfs-kernel-server
```

---

## 3.2 开发板侧（NFS 客户端）

### 安装 NFS 客户端

```bash
apt install nfs-common
```

### 挂载共享目录

```bash
mount -t nfs 192.168.0.130:/home/chris/nfs_share /mnt/nfs -o nolock
```

- `192.168.0.130`：虚拟机 IP
- `/home/chris/nfs_share`：虚拟机上的共享目录
- `/mnt/nfs`：开发板上的挂载点
- `-o nolock`：禁用文件锁（嵌入式环境常用，避免锁相关问题）

### 验证

```bash
# 虚拟机上创建文件
echo "hello" > /home/chris/nfs_share/test.txt

# 开发板上查看
cat /mnt/nfs/test.txt
# 输出：hello
```

---

## 3.3 注意事项

- NFS 挂载在开发板重启后会失效，需要重新执行 mount 命令
- WiFi 连接延迟较大（40~467ms），NFS 传输大文件时可能较慢
- 不要在 NFS 目录下编译（I/O 慢），编译在虚拟机本地目录进行，编译好再复制到 nfs_share
