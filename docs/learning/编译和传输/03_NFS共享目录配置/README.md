# 03 — NFS 共享目录配置详解

> 目的：让虚拟机和开发板共享一个目录，虚拟机编译完的文件开发板立刻就能访问，不需要手动 scp
> NFS = Network File System（网络文件系统）

---

## 为什么需要 NFS？

对比三种文件传输方式：

| 方式 | 操作 | 效率 | 适用场景 |
|------|------|------|----------|
| scp | 每次编译后手动传输 | 低（重复操作多） | 偶尔传几个文件 |
| NFS | 编译完直接在开发板上运行 | 高（零拷贝） | 频繁编译调试 |
| SD 卡 | 拔卡→拷贝→插卡 | 最低 | 传大文件、离线场景 |

开发过程中经常是：改代码 → 编译 → 传到开发板 → 运行 → 发现 bug → 改代码 → ...
用 NFS 可以省掉"传到开发板"这一步，大幅提高效率。

---

## NFS 工作原理

```
虚拟机（NFS 服务端）                    开发板（NFS 客户端）
┌──────────────────────┐              ┌──────────────────────┐
│ /home/chris/nfs_share│              │ /mnt/nfs             │
│   ├── test_level1    │◄────────────►│   ├── test_level1    │
│   ├── test_level2    │   网络共享    │   ├── test_level2    │
│   └── ...            │              │   └── ...            │
└──────────────────────┘              └──────────────────────┘
     192.168.0.130                         192.168.0.43
```

虚拟机上的 `/home/chris/nfs_share` 和开发板上的 `/mnt/nfs` 看到的是**同一份文件**。
在虚拟机上往 nfs_share 里放文件，开发板上立刻就能看到。

---

## 配置步骤

### 第一步：虚拟机上创建共享目录

```bash
mkdir -p ~/nfs_share
```

`mkdir -p` 的 `-p` 表示：如果目录已存在不报错，如果父目录不存在则一并创建。

### 第二步：配置 NFS 导出

```bash
echo "/home/chris/nfs_share *(rw,sync,no_root_squash)" | sudo tee -a /etc/exports
```

**这条命令做了什么？**

1. `echo "..."` — 生成一行配置文本
2. `| sudo tee -a /etc/exports` — 以 root 权限追加到 /etc/exports 文件末尾

> 为什么不用 `sudo echo "..." >> /etc/exports`？
> 因为 `>>` 重定向是 shell 执行的，shell 本身没有 root 权限，所以会报 Permission denied。
> `tee -a` 是一个程序，可以被 sudo 提权。

**配置内容解析：**

```
/home/chris/nfs_share *(rw,sync,no_root_squash)
│                     │ │    │     │
│                     │ │    │     └── no_root_squash: 客户端 root 保持 root 权限
│                     │ │    └── sync: 同步写入（数据安全，但稍慢）
│                     │ └── rw: 读写权限
│                     └── *: 允许所有 IP 访问
└── 要共享的目录路径
```

**各参数详解：**

| 参数 | 含义 | 如果不设置 |
|------|------|-----------|
| `*` | 允许任何 IP 的客户端挂载 | 可以改成具体 IP，如 `192.168.0.43` |
| `rw` | 客户端可读可写 | 默认 `ro`（只读） |
| `sync` | 写操作同步到磁盘后才返回 | `async` 更快但断电可能丢数据 |
| `no_root_squash` | 客户端 root 用户在 NFS 上也是 root | 默认 `root_squash`，root 会被映射为 nobody |

> `no_root_squash` 在开发环境中很方便（开发板 root 可以直接读写所有文件），
> 但在生产环境中有安全风险。

### 第三步：使配置生效

```bash
sudo exportfs -arv
```

**参数解析：**

| 参数 | 含义 |
|------|------|
| `-a` | 导出 /etc/exports 中的所有目录 |
| `-r` | 重新导出（刷新，同步配置变更） |
| `-v` | 显示详细信息（verbose） |

实际输出：
```
exporting *:/home/chris/nfs_share
```

表示 `/home/chris/nfs_share` 已经对所有 IP（*）开放。

> 输出中的警告 `Neither 'subtree_check' or 'no_subtree_check' specified` 可以忽略，
> 这只是提醒你没有显式指定子树检查选项，使用了默认值 `no_subtree_check`。

### 第四步：重启 NFS 服务

```bash
sudo systemctl restart nfs-kernel-server
```

**systemctl 命令解析：**

| 命令 | 含义 |
|------|------|
| `systemctl start xxx` | 启动服务 |
| `systemctl stop xxx` | 停止服务 |
| `systemctl restart xxx` | 重启服务（stop + start） |
| `systemctl status xxx` | 查看服务状态 |
| `systemctl enable xxx` | 设为开机自启 |

---

## 开发板上挂载

### 前提：安装 NFS 客户端

开发板上默认没有 NFS 客户端工具，需要先安装：

```bash
# 在开发板上以 root 执行
apt update && apt install -y nfs-common
```

`nfs-common` 包提供了 `mount.nfs` 命令，没有它 mount 会报错：
```
mount: /mnt/nfs: bad option; for several filesystems (e.g. nfs, cifs)
you might need a /sbin/mount.<type> helper program.
```

### 挂载命令

```bash
# 在开发板上以 root 执行
mkdir -p /mnt/nfs
mount -t nfs 192.168.0.130:/home/chris/nfs_share /mnt/nfs -o nolock
```

**命令解析：**

| 部分 | 含义 |
|------|------|
| `mount` | 挂载命令 |
| `-t nfs` | 文件系统类型为 NFS |
| `192.168.0.130:/home/chris/nfs_share` | NFS 服务端 IP + 共享目录路径 |
| `/mnt/nfs` | 本地挂载点（开发板上的目录） |
| `-o nolock` | 不使用文件锁（嵌入式环境常用，避免锁管理器问题） |

### 验证挂载

挂载成功后输出：
```
NFS mount OK
```

---

## 双向验证测试

### 虚拟机写 → 开发板读

```bash
# 虚拟机上
echo "hello from VM" > ~/nfs_share/test.txt

# 开发板上
cat /mnt/nfs/test.txt
# 输出：hello from VM
```

### 开发板写 → 虚拟机读

```bash
# 开发板上
echo "hello from board" > /mnt/nfs/test2.txt

# 虚拟机上
cat ~/nfs_share/test2.txt
# 输出：hello from board
```

---

## 实际开发中的使用方式

配好 NFS 后，开发流程变成：

```bash
# 1. 虚拟机上编译，输出到 nfs_share
cd /home/chris/nfs_share
$LINARO/arm-linux-gnueabihf-gcc test.c -o test

# 2. 开发板上直接运行（不需要 scp！）
cd /mnt/nfs
./test
```

---

## 注意事项

### 开发板重启后需要重新挂载

NFS 挂载是临时的，开发板重启后会丢失。每次开机后需要重新执行：

```bash
mount -t nfs 192.168.0.130:/home/chris/nfs_share /mnt/nfs -o nolock
```

如果想开机自动挂载，可以编辑开发板的 `/etc/fstab`：

```
192.168.0.130:/home/chris/nfs_share /mnt/nfs nfs nolock,auto 0 0
```

> 但不建议这样做，因为如果虚拟机没开机，开发板启动时会卡在挂载步骤很久。

### SSH 登录权限问题

我们遇到的实际问题：

```bash
ssh debian@192.168.0.43
sudo mount ...
# 报错：sudo: /usr/bin/sudo must be owned by uid 0 and have the setuid bit set
```

开发板上 debian 用户的 sudo 有问题，解决方案是先 `su -` 切换到 root 再操作。

### root SSH 登录被禁止

```bash
ssh root@192.168.0.43
# 报错：Permission denied
```

这是 SSH 安全策略，不是密码错误。解决方案：
1. 用 debian 用户 SSH 进去，再 `su -` 切换 root
2. 或者通过串口（MobaXterm）直接以 root 登录

---

## 常见问题

### Q: mount 报 "Connection refused"？

虚拟机上的 NFS 服务没启动：
```bash
# 虚拟机上检查
sudo systemctl status nfs-kernel-server
# 如果不是 active，启动它
sudo systemctl start nfs-kernel-server
```

### Q: mount 报 "access denied"？

检查 /etc/exports 配置是否正确，然后重新导出：
```bash
cat /etc/exports
sudo exportfs -arv
```

### Q: 挂载后看不到文件？

确认挂载是否成功：
```bash
df -h | grep nfs
# 应该能看到 192.168.0.130:/home/chris/nfs_share
```

### Q: NFS 性能很差？

WiFi 连接延迟大，NFS 对网络质量敏感。建议：
1. 开发板改用有线网络
2. 使用 `-o nolock,rsize=8192,wsize=8192` 参数优化
