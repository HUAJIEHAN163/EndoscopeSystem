#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <opencv2/opencv.hpp>

// =====================================================================
// ImageProcessor — 图像处理算法集合
//
// 所有算法为静态方法，无状态，可独立调用，也可通过 process() 按管线顺序执行。
// 输入输出均为 cv::Mat BGR 格式（与管线全程 BGR 架构一致，见 13_7）。
//
// 算法分三类：
//   1. 内窥镜专用：CLAHE、畸变校正、去雾、白平衡 — 解决体腔内特有的光照/光学问题
//   2. 通用增强：锐化、降噪 — 提升画质，已移至图像编辑页离线处理（见 13_4）
//   3. 经典保留：边缘检测、阈值分割 — 来自原 VideoMonitor 项目，辅助分析用
//
// 处理管线顺序（process 函数内部）：
//   白平衡 → CLAHE → 畸变校正 → 去雾 → 锐化 → 降噪 → 边缘检测 → 阈值分割
//   顺序设计理由见 06_图像处理算法.md
//
// 性能说明：
//   process() 内部先 resize 到半分辨率（640→320）再处理，输出半分辨率结果，
//   由调用方（processthread.h）resize 到显示尺寸。
//   各算法在 Cortex-A7 320×240 上的实测耗时见 问题记录.md #12。
// =====================================================================
class ImageProcessor {
public:
    // =================================================================
    // Config — 算法开关和参数
    //
    // 三线程架构中，主线程通过 displayTimer 将 UI 控件状态同步到
    // processThread->config，处理线程每帧读取 config 决定执行哪些算法。
    // =================================================================
    struct Config {
        // --- 内窥镜专用算法（实时流中使用）---

        // 软件白平衡（灰度世界法）
        // 已下放给 OV5640 硬件自动白平衡（见 13_3），此开关始终为 false
        // 保留字段是为了兼容预设文件格式和离线编辑的潜在需求
        bool whiteBalance = false;

        // CLAHE 自适应直方图均衡化
        // 解决体腔内光照不均（近处过亮、远处过暗）
        // 在 Lab 色彩空间的 L（亮度）通道做均衡化，保留原始色彩
        bool clahe = false;
        double claheClipLimit = 3.0;  // 对比度限制（1.0-8.0），越大对比度越强但噪声越明显

        // 畸变校正
        // 用预计算的映射表（initUndistortMap）做 remap，校正广角镜头的桶形畸变
        // 需要 camera_calib.yml 标定文件，没有则不可用
        bool undistort = false;

        // 去雾（暗通道先验）
        // 解决体腔内温差导致的镜头起雾
        // 计算量较大（320×240 上 ~87ms），是实时流中最慢的算法
        bool dehaze = false;
        double dehazeOmega = 0.95;  // 去雾强度（0.5-1.0），越大去雾越强
        int dehazeRadius = 7;       // 暗通道窗口半径（3-15）

        // --- 通用增强算法（已移至图像编辑页离线处理，见 13_4）---
        // 实时流中这些字段始终为 false（mainwindow.cpp displayTimer 中强制设置）

        // USM 锐化（Unsharp Masking）
        // 原理：dst = src + amount × (src - GaussianBlur(src))
        // 提取高频细节（边缘）并放大叠加回原图
        bool sharpen = false;
        double sharpenAmount = 1.5;  // 锐化强度（0.5-3.0），越大边缘越锐利但噪声越明显

        // 双边滤波降噪
        // 在空间域和像素值域同时加权，平滑噪声但保留边缘
        // 计算量大，内部先缩小 0.5x 再处理再放大
        bool denoise = false;
        int denoiseD = 5;  // 滤波直径（3-9），越大越平滑但越慢

        // --- 经典算法（来自原 VideoMonitor 项目，已移至图像编辑页）---

        // Canny 边缘检测
        // 输出灰度边缘图（内部转回 BGR 保持管线格式一致）
        bool edgeDetect = false;

        // 阈值分割
        // 灰度值 > thresholdValue 为白，否则为黑
        bool threshold = false;
        int thresholdValue = 128;  // 阈值（0-255）
    };

    // =================================================================
    // process — 管线入口
    //
    // 按固定顺序执行所有启用的算法。
    // 内部先 resize 到半分辨率处理，输出半分辨率结果。
    //
    // 参数：
    //   src  — 输入图像（BGR，640×480）
    //   dst  — 输出图像（BGR，320×240）
    //   cfg  — 算法开关和参数
    //   map1, map2 — 畸变校正映射表（由 initUndistortMap 预计算）
    // =================================================================
    static void process(const cv::Mat &src, cv::Mat &dst, const Config &cfg,
                        const cv::Mat &map1 = cv::Mat(),
                        const cv::Mat &map2 = cv::Mat());

    // =================================================================
    // 内窥镜专用算法
    // =================================================================

    // CLAHE — 自适应直方图均衡化（Contrast Limited Adaptive Histogram Equalization）
    //
    // 原理：将图像分成 tileSize×tileSize 的小块，对每块分别做直方图均衡化，
    //       用 clipLimit 限制对比度放大倍数，避免噪声被过度放大。
    //       在 Lab 空间只处理 L 通道，a/b 通道不动，保留原始色彩。
    //
    // 为什么不在 BGR 空间做：对 B/G/R 三通道分别均衡化会导致颜色严重失真。
    // 为什么不在 HSV 的 V 通道做：V = max(R,G,B)，感知不均匀，增强效果不自然。
    // Lab 的 L* 是感知均匀的亮度，增强效果最自然。
    //
    // 性能：320×240 约 28ms（Cortex-A7），内部使用 static CLAHE 对象复用，
    //       首次调用 ~3s（OpenCV 内部初始化），程序启动时预热消除。
    //
    // 参数：
    //   clipLimit — 对比度限制，默认 3.0，UI 滑块范围 1.0-8.0
    //   tileSize  — 分块大小，默认 8×8
    static void applyCLAHE(const cv::Mat &src, cv::Mat &dst,
                           double clipLimit = 3.0,
                           cv::Size tileSize = cv::Size(8, 8));

    // initUndistortMap — 预计算畸变校正映射表
    //
    // 根据相机标定得到的内参矩阵和畸变系数，预计算像素映射表。
    // 只需在程序启动时调用一次，后续每帧用 cv::remap 查表校正，速度很快。
    //
    // 需要 camera_calib.yml 标定文件（用内窥镜拍摄棋盘格照片后生成）。
    // 没有标定文件时此功能不可用，UI 上畸变校正复选框置灰。
    static void initUndistortMap(const cv::Mat &cameraMatrix,
                                 const cv::Mat &distCoeffs,
                                 cv::Size imageSize,
                                 cv::Mat &map1, cv::Mat &map2);

    // applyDehaze — 去雾（暗通道先验简化版）
    //
    // 原理（何恺明 2009）：
    //   1. 暗通道：取每个像素 RGB 三通道最小值，再做最小值滤波（erode）
    //   2. 大气光估计：取暗通道最亮区域的均值作为大气光 A
    //   3. 透射率估计：t = 1 - omega × (归一化暗通道)
    //   4. 场景恢复：J = (I - A) / max(t, 0.1) + A
    //
    // 内窥镜场景：体腔内温差导致镜头起雾，画面变白变模糊。
    // 性能：320×240 约 87ms（Cortex-A7），是实时流中最慢的算法。
    //       使用 CV_32F 单精度浮点（原 CV_64F 双精度优化后速度翻倍）。
    //
    // 参数：
    //   omega  — 去雾强度，默认 0.95（去除 95% 的雾）
    //   radius — 暗通道窗口半径，默认 7
    static void applyDehaze(const cv::Mat &src, cv::Mat &dst,
                            double omega = 0.95, int radius = 7);

    // applyWhiteBalance — 白平衡（灰度世界法）
    //
    // 原理：假设场景中所有颜色的平均值应该是灰色，
    //       据此计算 B/G/R 三通道的增益系数进行校正。
    //
    // 当前状态：已下放给 OV5640 硬件自动白平衡（见 13_3）。
    //   - Config::whiteBalance 始终为 false，此函数不会被 process() 调用
    //   - OV5640 的 AWB 在 Sensor 内部 RAW 数据阶段处理，精度更高且零 CPU 开销
    //   - 软件灰度世界法在内窥镜场景（大面积红色黏膜）下假设不成立，效果不如硬件 AWB
    //   - 保留此函数供离线编辑或学习参考
    static void applyWhiteBalance(const cv::Mat &src, cv::Mat &dst);

    // =================================================================
    // 通用增强算法（已移至图像编辑页离线处理，见 13_4）
    // =================================================================

    // applySharpen — USM 锐化（Unsharp Masking）
    //
    // 原理：dst = src + amount × (src - GaussianBlur(src))
    //   1. 高斯模糊得到低频分量
    //   2. 原图 - 模糊 = 高频细节（边缘 + 噪声）
    //   3. 高频细节 × amount 叠加回原图，边缘被增强
    //
    // 已知问题：对所有区域一视同仁，平坦区域的噪声也会被放大。
    // P14 计划实现自适应边缘锐化（用 Laplacian 掩码只锐化边缘区域）。
    //
    // 性能：320×240 约 7ms（3×3 固定核）。
    //       原 sigma=3.0 自动核（~19×19）耗时 210ms，已优化为 3×3 固定核。
    //
    // 参数：
    //   sigma  — 高斯模糊 sigma（当前未使用，固定 3×3 核）
    //   amount — 锐化强度，默认 1.5，UI 滑块范围 0.5-3.0
    static void applySharpen(const cv::Mat &src, cv::Mat &dst,
                             double sigma = 3.0, double amount = 1.5);

    // applyDenoise — 双边滤波降噪
    //
    // 原理：在空间域和像素值域同时加权滤波。
    //   - 空间权重：距离近的邻居权重大（和高斯模糊一样）
    //   - 像素值权重：像素值接近的邻居权重大（边缘两侧像素值差异大，权重低，不会被模糊）
    //   → 平滑噪声但保留边缘
    //
    // 性能优化：内部先缩小 0.5x 再做双边滤波再放大，速度提升约 4 倍。
    //
    // 参数：
    //   d          — 滤波直径，默认 5（不要设太大，d=9 速度慢 3 倍）
    //   sigmaColor — 像素值域 sigma，越大越平滑颜色差异
    //   sigmaSpace — 空间域 sigma，越大越考虑远处邻居
    static void applyDenoise(const cv::Mat &src, cv::Mat &dst,
                             int d = 5, double sigmaColor = 50,
                             double sigmaSpace = 50);

    // =================================================================
    // 经典算法（来自原 VideoMonitor 项目，已移至图像编辑页）
    // =================================================================

    // applyEdgeDetect — Canny 边缘检测
    //
    // 流程：BGR → 灰度 → 3×3 均值模糊（去噪）→ Canny → 灰度转回 BGR
    // 输出为灰度边缘图（转回 BGR 是为了保持管线格式一致）
    static void applyEdgeDetect(const cv::Mat &src, cv::Mat &dst);

    // applyThreshold — 阈值分割
    //
    // 流程：BGR → 灰度 → 二值化（> threshValue 为 255，否则为 0）→ 灰度转回 BGR
    static void applyThreshold(const cv::Mat &src, cv::Mat &dst,
                               int threshValue = 128);

    // =================================================================
    // 工具方法
    // =================================================================

    // calcSharpness — 清晰度评估
    //
    // 原理：计算图像的拉普拉斯方差。
    //   拉普拉斯算子是二阶导数，对边缘和细节敏感：
    //   - 清晰图像：边缘锐利，拉普拉斯响应大，方差高
    //   - 模糊图像：边缘平滑，拉普拉斯响应小，方差低
    //
    // 用途：冻结画面时从缓存的 8 帧中选最清晰的一帧（见 13_2）
    //
    // 返回值：方差值，越大越清晰，无固定范围（只用于相对比较）
    static double calcSharpness(const cv::Mat &src);
};

#endif // IMAGEPROCESSOR_H
