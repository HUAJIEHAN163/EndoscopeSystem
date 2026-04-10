#!/bin/bash
# Buffer 数量验证脚本
# 对比程序运行前后的 v4l2 参数

echo "=========================================="
echo "Buffer 数量验证脚本"
echo "=========================================="
echo ""

# 1. 程序运行前查询
echo "[1] 程序运行前的 v4l2 参数："
echo "-------------------------------------------"
v4l2-ctl -d /dev/video0 --all | grep -A5 "Streaming Parameters"
echo ""

# 2. 启动程序（后台运行）
echo "[2] 启动程序..."
/tmp/endoscope -platform linuxfb > /tmp/endoscope_output.log 2>&1 &
ENDOSCOPE_PID=$!
echo "程序 PID: $ENDOSCOPE_PID"
echo ""

# 3. 等待程序初始化
echo "[3] 等待程序初始化（3秒）..."
sleep 3
echo ""

# 4. 程序运行时查询
echo "[4] 程序运行时的 v4l2 参数："
echo "-------------------------------------------"
v4l2-ctl -d /dev/video0 --all | grep -A5 "Streaming Parameters"
echo ""

# 5. 查看程序日志中的 buffer 信息
echo "[5] 程序日志中的 buffer 信息："
echo "-------------------------------------------"
grep "Buffer" /tmp/endoscope_output.log
grep "V4L2 就绪" /tmp/endoscope_output.log
echo ""

# 6. 停止程序
echo "[6] 停止程序..."
kill $ENDOSCOPE_PID 2>/dev/null
wait $ENDOSCOPE_PID 2>/dev/null
echo ""

# 7. 程序停止后查询
echo "[7] 程序停止后的 v4l2 参数："
echo "-------------------------------------------"
v4l2-ctl -d /dev/video0 --all | grep -A5 "Streaming Parameters"
echo ""

echo "=========================================="
echo "验证完成"
echo "=========================================="
echo ""
echo "分析："
echo "- 如果运行时 Read buffers 从 2 变成 4，说明是程序动态分配的"
echo "- 如果运行时仍然是 2，说明 Read buffers 指的是其他东西"
echo "- 程序日志中显示的是实际 mmap 的 buffer 数量（最可靠）"
