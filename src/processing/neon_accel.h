#ifndef NEON_ACCEL_H
#define NEON_ACCEL_H

// ============================================================
// 本文件当前未使用
// P5 NEON 加速 YUYV→BGR 尝试失败（OpenCV cvtColor 已内置 NEON 优化，手写无法超越）
// 已回退到 cv::cvtColor，详见 13_9_开发记录_NEON加速YUYV转BGR.md
// 保留文件供后续 P6 NEON 加速 CLAHE/去雾时复用
// ============================================================

#include <opencv2/core.hpp>

namespace NeonAccel {

// NEON 加速 YUYV→BGR 转换
// src: 640×480 CV_8UC2 (YUYV packed)
// dst: 640×480 CV_8UC3 (BGR)
void yuyvToBGR(const cv::Mat &src, cv::Mat &dst);

}

#endif // NEON_ACCEL_H
