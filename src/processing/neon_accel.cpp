#include "processing/neon_accel.h"
#include <opencv2/imgproc.hpp>

// ============================================================
// 本文件当前未使用
// P5 NEON 加速 YUYV→BGR 尝试失败（OpenCV cvtColor 已内置 NEON 优化，手写无法超越）
// 已回退到 cv::cvtColor，详见 13_9_开发记录_NEON加速YUYV转BGR.md
// 保留文件供后续 P6 NEON 加速 CLAHE/去雾时复用
// ============================================================

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace NeonAccel {

void yuyvToBGR(const cv::Mat &src, cv::Mat &dst) {
    int width = src.cols;
    int height = src.rows;
    dst.create(height, width, CV_8UC3);

#ifdef __ARM_NEON
    // BT.601 定点系数（×128）
    // R = Y + (179 * (V-128)) >> 7
    // G = Y - (44 * (U-128) + 91 * (V-128)) >> 7
    // B = Y + (227 * (U-128)) >> 7
    const int16x8_t v128 = vdupq_n_s16(128);

    for (int y = 0; y < height; y++) {
        const uint8_t *yuyv = src.ptr<uint8_t>(y);
        uint8_t *bgr = dst.ptr<uint8_t>(y);

        int x = 0;
        // 每次处理 16 像素 = 32 字节 YUYV，48 字节 BGR
        // 用 vld4 一次加载 32 字节，自动解交织为 Y0/U/Y1/V 四个通道
        for (; x <= width - 16; x += 16) {
            // vld4: 加载 32 字节，按 stride=4 解交织
            // YUYV 排列: Y0 U Y1 V Y0 U Y1 V ...
            // val[0] = 所有 Y0 (8个)
            // val[1] = 所有 U  (8个)
            // val[2] = 所有 Y1 (8个)
            // val[3] = 所有 V  (8个)
            uint8x8x4_t yuyv4 = vld4_u8(yuyv);

            uint8x8_t y0 = yuyv4.val[0];  // Y0, Y2, Y4, ...（偶数像素）
            uint8x8_t u  = yuyv4.val[1];  // U0, U1, U2, ...（8个U）
            uint8x8_t y1 = yuyv4.val[2];  // Y1, Y3, Y5, ...（奇数像素）
            uint8x8_t v  = yuyv4.val[3];  // V0, V1, V2, ...（8个V）

            // U/V 已经是每像素对一个值，vld4 自动对齐
            // y0[i] 和 y1[i] 共享 u[i] 和 v[i]

            // 扩展到 int16
            int16x8_t u16 = vreinterpretq_s16_u16(vmovl_u8(u));
            int16x8_t v16 = vreinterpretq_s16_u16(vmovl_u8(v));
            int16x8_t u_off = vsubq_s16(u16, v128);
            int16x8_t v_off = vsubq_s16(v16, v128);

            // 预计算 U/V 相关项（偶数和奇数像素共享）
            int16x8_t r_uv = vshrq_n_s16(vmulq_n_s16(v_off, 179), 7);
            int16x8_t g_uv = vshrq_n_s16(vaddq_s16(
                vmulq_n_s16(u_off, 44), vmulq_n_s16(v_off, 91)), 7);
            int16x8_t b_uv = vshrq_n_s16(vmulq_n_s16(u_off, 227), 7);

            // --- 偶数像素（y0）---
            int16x8_t yy0 = vreinterpretq_s16_u16(vmovl_u8(y0));
            uint8x8_t r0 = vqmovun_s16(vaddq_s16(yy0, r_uv));
            uint8x8_t g0 = vqmovun_s16(vsubq_s16(yy0, g_uv));
            uint8x8_t b0 = vqmovun_s16(vaddq_s16(yy0, b_uv));

            // --- 奇数像素（y1）---
            int16x8_t yy1 = vreinterpretq_s16_u16(vmovl_u8(y1));
            uint8x8_t r1 = vqmovun_s16(vaddq_s16(yy1, r_uv));
            uint8x8_t g1 = vqmovun_s16(vsubq_s16(yy1, g_uv));
            uint8x8_t b1 = vqmovun_s16(vaddq_s16(yy1, b_uv));

            // 交织偶数和奇数像素：b0[0] b1[0] b0[1] b1[1] ...
            uint8x8x2_t b_zip = vzip_u8(b0, b1);
            uint8x8x2_t g_zip = vzip_u8(g0, g1);
            uint8x8x2_t r_zip = vzip_u8(r0, r1);

            // 合并为 16 像素的 B/G/R
            uint8x16_t b16 = vcombine_u8(b_zip.val[0], b_zip.val[1]);
            uint8x16_t g16 = vcombine_u8(g_zip.val[0], g_zip.val[1]);
            uint8x16_t r16 = vcombine_u8(r_zip.val[0], r_zip.val[1]);

            // 交织存储为 BGR（16 像素 = 48 字节）
            uint8x16x3_t bgr_out;
            bgr_out.val[0] = b16;
            bgr_out.val[1] = g16;
            bgr_out.val[2] = r16;
            vst3q_u8(bgr, bgr_out);

            yuyv += 32;
            bgr += 48;
        }

        // 尾部标量处理
        for (; x < width; x += 2) {
            int yy0 = yuyv[0];
            int uu  = yuyv[1];
            int yy1 = yuyv[2];
            int vv  = yuyv[3];
            int d = uu - 128;
            int e = vv - 128;

            auto clamp = [](int val) -> uint8_t {
                return val < 0 ? 0 : (val > 255 ? 255 : (uint8_t)val);
            };

            bgr[0] = clamp(yy0 + ((227 * d) >> 7));
            bgr[1] = clamp(yy0 - ((44 * d + 91 * e) >> 7));
            bgr[2] = clamp(yy0 + ((179 * e) >> 7));
            bgr[3] = clamp(yy1 + ((227 * d) >> 7));
            bgr[4] = clamp(yy1 - ((44 * d + 91 * e) >> 7));
            bgr[5] = clamp(yy1 + ((179 * e) >> 7));

            yuyv += 4;
            bgr += 6;
        }
    }

#else
    cv::cvtColor(src, dst, cv::COLOR_YUV2BGR_YUYV);
#endif
}

} // namespace NeonAccel
