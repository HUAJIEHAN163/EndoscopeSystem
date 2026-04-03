#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <opencv2/opencv.hpp>

// 图像处理算法集合
// 所有算法为静态方法，可独立调用，也可通过 process() 按管线顺序执行
class ImageProcessor {
public:
    // 算法开关
    struct Config {
        // 内窥镜专用算法
        bool whiteBalance = false;
        bool clahe = false;
        double claheClipLimit = 3.0;
        bool undistort = false;
        bool dehaze = false;
        double dehazeOmega = 0.95;
        int dehazeRadius = 7;

        // 通用增强算法
        bool sharpen = false;
        double sharpenAmount = 1.5;
        bool denoise = false;
        int denoiseD = 5;

        // 保留的经典算法
        bool edgeDetect = false;
        bool threshold = false;
        int thresholdValue = 128;
    };

    // 按固定顺序执行所有启用的算法
    // 顺序: 白平衡 → CLAHE → 去畸变 → 去雾 → 锐化 → 降噪 → 边缘检测 → 阈值分割
    static void process(const cv::Mat &src, cv::Mat &dst, const Config &cfg,
                        const cv::Mat &map1 = cv::Mat(),
                        const cv::Mat &map2 = cv::Mat());

    // === 内窥镜专用算法 ===

    // CLAHE 自适应直方图均衡化
    // 在 Lab 空间只处理 L 通道，保留色彩
    static void applyCLAHE(const cv::Mat &src, cv::Mat &dst,
                           double clipLimit = 3.0,
                           cv::Size tileSize = cv::Size(8, 8));

    // 畸变校正 — 预计算映射表 (只需调用一次)
    static void initUndistortMap(const cv::Mat &cameraMatrix,
                                 const cv::Mat &distCoeffs,
                                 cv::Size imageSize,
                                 cv::Mat &map1, cv::Mat &map2);

    // 去雾 (暗通道先验简化版)
    static void applyDehaze(const cv::Mat &src, cv::Mat &dst,
                            double omega = 0.95, int radius = 7);

    // 白平衡 (灰度世界法)
    static void applyWhiteBalance(const cv::Mat &src, cv::Mat &dst);

    // === 通用增强算法 ===

    // USM 锐化
    static void applySharpen(const cv::Mat &src, cv::Mat &dst,
                             double sigma = 3.0, double amount = 1.5);

    // 双边滤波降噪
    static void applyDenoise(const cv::Mat &src, cv::Mat &dst,
                             int d = 5, double sigmaColor = 50,
                             double sigmaSpace = 50);

    // === 保留的经典算法 (来自 VideoMonitor) ===

    // Canny 边缘检测
    static void applyEdgeDetect(const cv::Mat &src, cv::Mat &dst);

    // 阈值分割
    static void applyThreshold(const cv::Mat &src, cv::Mat &dst,
                               int threshValue = 128);

    // 清晰度评估（拉普拉斯方差，值越大越清晰）
    static double calcSharpness(const cv::Mat &src);
};

#endif // IMAGEPROCESSOR_H
