#!/bin/bash
# Overrun 错误验证脚本
# 测试不同处理负载下的 overrun 频率

echo "=========================================="
echo "Overrun 错误验证脚本"
echo "=========================================="
echo ""

# 测试时长（秒）
TEST_DURATION=30

# 清除之前的 dmesg 日志
dmesg -c > /dev/null

echo "测试配置："
echo "- 测试时长：${TEST_DURATION} 秒"
echo "- 摄像头：/dev/video0"
echo "- 分辨率：640x480 @ 30fps"
echo "- Buffer 数量：4 个"
echo ""

# 测试场景列表
declare -a SCENARIOS=(
    "不开任何处理"
    "只开白平衡"
    "白平衡+CLAHE"
    "白平衡+CLAHE+锐化"
    "白平衡+CLAHE+锐化+降噪"
)

# 结果数组
declare -a RESULTS=()

for i in "${!SCENARIOS[@]}"; do
    SCENARIO="${SCENARIOS[$i]}"
    
    echo "=========================================="
    echo "场景 $((i+1))/${#SCENARIOS[@]}: ${SCENARIO}"
    echo "=========================================="
    
    # 清除 dmesg
    dmesg -c > /dev/null
    
    echo "启动程序..."
    # 注意：这里需要根据实际情况修改程序启动参数
    # 或者在程序中添加命令行参数来控制开启哪些算法
    /tmp/endoscope -platform linuxfb > /tmp/test_${i}.log 2>&1 &
    PID=$!
    
    echo "程序 PID: $PID"
    echo "运行 ${TEST_DURATION} 秒..."
    sleep ${TEST_DURATION}
    
    echo "停止程序..."
    kill $PID 2>/dev/null
    wait $PID 2>/dev/null
    sleep 1
    
    # 统计 overrun 错误
    OVERRUN_COUNT=$(dmesg | grep -c "overrun")
    OVERRUN_ERRORS=$(dmesg | grep "overrun" | tail -1 | grep -oP 'errors=\K[0-9]+' || echo "0")
    TOTAL_BUFFERS=$(dmesg | grep "overrun" | tail -1 | grep -oP 'buffers=\K[0-9]+' || echo "0")
    
    # 计算 overrun 率
    if [ "$TOTAL_BUFFERS" -gt 0 ]; then
        OVERRUN_RATE=$(awk "BEGIN {printf \"%.2f\", ($OVERRUN_ERRORS / $TOTAL_BUFFERS) * 100}")
    else
        OVERRUN_RATE="0.00"
    fi
    
    # 从日志中提取 FPS
    AVG_FPS=$(grep "FPS" /tmp/test_${i}.log | tail -20 | awk '{sum+=$NF; count++} END {if(count>0) printf "%.1f", sum/count; else print "N/A"}')
    
    echo "结果："
    echo "  - Overrun 错误次数: ${OVERRUN_ERRORS}"
    echo "  - 总帧数: ${TOTAL_BUFFERS}"
    echo "  - Overrun 率: ${OVERRUN_RATE}%"
    echo "  - 平均 FPS: ${AVG_FPS}"
    echo ""
    
    # 保存结果
    RESULTS[$i]="${SCENARIO}|${OVERRUN_ERRORS}|${TOTAL_BUFFERS}|${OVERRUN_RATE}|${AVG_FPS}"
done

echo "=========================================="
echo "测试完成 - 汇总结果"
echo "=========================================="
echo ""
printf "%-30s | %-10s | %-10s | %-12s | %-8s\n" "场景" "Overrun" "总帧数" "Overrun率" "平均FPS"
echo "--------------------------------------------------------------------------------"

for result in "${RESULTS[@]}"; do
    IFS='|' read -r scenario overrun total rate fps <<< "$result"
    printf "%-30s | %-10s | %-10s | %-11s%% | %-8s\n" "$scenario" "$overrun" "$total" "$rate" "$fps"
done

echo ""
echo "=========================================="
echo "分析建议"
echo "=========================================="
echo ""
echo "1. 如果 Overrun 率 < 1%，说明性能可接受"
echo "2. 如果 Overrun 率 > 5%，说明处理速度严重不足"
echo "3. 对比不同场景，找出导致 overrun 的关键算法"
echo "4. FPS 下降和 Overrun 率应该呈正相关"
echo ""
echo "详细日志保存在："
for i in "${!SCENARIOS[@]}"; do
    echo "  - /tmp/test_${i}.log (${SCENARIOS[$i]})"
done
