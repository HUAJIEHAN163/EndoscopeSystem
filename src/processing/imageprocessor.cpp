#include "processing/imageprocessor.h"

// === 管线入口 ===

void ImageProcessor::process(const cv::Mat &src, cv::Mat &dst,
                             const Config &cfg,
                             const cv::Mat &map1, const cv::Mat &map2) {
    // 缩小到半分辨率处理
    cv::Mat work;
    cv::resize(src, work, cv::Size(), 0.5, 0.5, cv::INTER_LINEAR);

    if (cfg.whiteBalance)
        applyWhiteBalance(work, work);

    if (cfg.clahe)
        applyCLAHE(work, work, cfg.claheClipLimit);

    if (cfg.undistort && !map1.empty() && !map2.empty())
        cv::remap(work, work, map1, map2, cv::INTER_LINEAR);

    if (cfg.dehaze)
        applyDehaze(work, work, cfg.dehazeOmega, cfg.dehazeRadius);

    // USM 锐化和自适应锐化互斥
    if (cfg.sharpen)
        applySharpen(work, work, 3.0, cfg.sharpenAmount);
    else if (cfg.adaptiveSharpen)
        applyAdaptiveSharpen(work, work, cfg.adaptiveSharpenAmount, cfg.adaptiveSharpenThreshold);

    if (cfg.denoise)
        applyDenoise(work, work, cfg.denoiseD);

    if (cfg.edgeDetect)
        applyEdgeDetect(work, work);

    if (cfg.threshold)
        applyThreshold(work, work, cfg.thresholdValue);

    // 直接输出半分辨率结果，由调用方缩放到显示尺寸
    dst = work;
}

// === 内窥镜专用算法 ===

void ImageProcessor::applyCLAHE(const cv::Mat &src, cv::Mat &dst,
                                double clipLimit, cv::Size tileSize) {
    cv::Mat lab;
    cv::cvtColor(src, lab, cv::COLOR_BGR2Lab);

    std::vector<cv::Mat> channels;
    cv::split(lab, channels);

    static cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
    clahe->setClipLimit(clipLimit);
    clahe->setTilesGridSize(tileSize);
    clahe->apply(channels[0], channels[0]);

    cv::merge(channels, lab);
    cv::cvtColor(lab, dst, cv::COLOR_Lab2BGR);
}

void ImageProcessor::initUndistortMap(const cv::Mat &cameraMatrix,
                                      const cv::Mat &distCoeffs,
                                      cv::Size imageSize,
                                      cv::Mat &map1, cv::Mat &map2) {
    cv::initUndistortRectifyMap(cameraMatrix, distCoeffs,
                                cv::Mat(), cameraMatrix,
                                imageSize, CV_16SC2,
                                map1, map2);
}

// 去雾（暗通道先验简化版）
// 优化：CV_32F 代替 CV_64F，减少一次 split/merge
void ImageProcessor::applyDehaze(const cv::Mat &src, cv::Mat &dst,
                                 double omega, int radius) {
    cv::Mat srcFloat;
    src.convertTo(srcFloat, CV_32FC3, 1.0f / 255.0f);

    // 暗通道：取三通道最小值再腐蚀
    std::vector<cv::Mat> channels;
    cv::split(srcFloat, channels);
    cv::Mat darkChannel;
    cv::min(channels[0], channels[1], darkChannel);
    cv::min(darkChannel, channels[2], darkChannel);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT,
                                               cv::Size(radius, radius));
    cv::erode(darkChannel, darkChannel, kernel);

    // 大气光估计：取暗通道最亮区域的均值
    double maxVal;
    cv::minMaxLoc(darkChannel, nullptr, &maxVal);
    cv::Scalar A = cv::mean(srcFloat, darkChannel > maxVal * 0.9f);

    // 透射率估计
    cv::Mat normMin;
    cv::Mat c0, c1, c2;
    cv::divide(channels[0], A[0] + 1e-6, c0);
    cv::divide(channels[1], A[1] + 1e-6, c1);
    cv::divide(channels[2], A[2] + 1e-6, c2);
    cv::min(c0, c1, normMin);
    cv::min(normMin, c2, normMin);
    cv::erode(normMin, normMin, kernel);

    cv::Mat transmission = 1.0f - static_cast<float>(omega) * normMin;
    cv::max(transmission, 0.1f, transmission);

    // 恢复场景
    for (int i = 0; i < 3; i++)
        channels[i] = (channels[i] - static_cast<float>(A[i])) / transmission
                       + static_cast<float>(A[i]);
    cv::merge(channels, dst);

    dst.convertTo(dst, CV_8UC3, 255.0);
}

// 白平衡（灰度世界法）
void ImageProcessor::applyWhiteBalance(const cv::Mat &src, cv::Mat &dst) {
    std::vector<cv::Mat> channels;
    cv::split(src, channels);

    double avgB = cv::mean(channels[0])[0];
    double avgG = cv::mean(channels[1])[0];
    double avgR = cv::mean(channels[2])[0];
    double avg = (avgB + avgG + avgR) / 3.0;

    channels[0] *= (avg / (avgB + 1e-6));
    channels[1] *= (avg / (avgG + 1e-6));
    channels[2] *= (avg / (avgR + 1e-6));

    cv::merge(channels, dst);
}

// === 通用增强算法 ===

// USM 锐化
void ImageProcessor::applySharpen(const cv::Mat &src, cv::Mat &dst,
                                  double sigma, double amount) {
    cv::Mat blurred;
    // 限制sigma最大为1.0，避免过大光晕（测试显示sigma=3.0会产生ksize=19的巨大光晕）
    double limitedSigma = std::min(sigma, 1.0);
    int ksize = std::max(3, static_cast<int>(limitedSigma * 3) * 2 + 1);
    cv::GaussianBlur(src, blurred, cv::Size(ksize, ksize), limitedSigma);
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, dst);
}

// 自适应锐化（Adaptive Sharpen）
// 使用 Laplacian 边缘掩码，只对边缘区域进行锐化
void ImageProcessor::applyAdaptiveSharpen(const cv::Mat &src, cv::Mat &dst,
                                          double amount, int threshold) {
    // 1. 转换为灰度图计算边缘强度
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    
    // 2. 计算 Laplacian 边缘强度（使用绝对值）
    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_16S, 3);  // 使用 3×3 核
    cv::convertScaleAbs(laplacian, laplacian);  // 转换为 CV_8U 并取绝对值
    
    // 3. 归一化边缘强度到 0-255
    double minVal, maxVal;
    cv::minMaxLoc(laplacian, &minVal, &maxVal);
    cv::Mat edgeMask;
    if (maxVal > 0) {
        laplacian.convertTo(edgeMask, CV_32F, 255.0 / maxVal);
    } else {
        edgeMask = cv::Mat::zeros(laplacian.size(), CV_32F);
    }
    
    // 4. 应用阈值：只保留强度 > threshold 的边缘
    // threshold 范围 0-50，映射到归一化后的 0-255 范围
    float thresholdNorm = threshold * 255.0f / 50.0f;
    edgeMask = cv::max(edgeMask - thresholdNorm, 0.0f);
    
    // 重新归一化到 0-1 范围作为混合权重
    cv::minMaxLoc(edgeMask, &minVal, &maxVal);
    if (maxVal > 0) {
        edgeMask /= maxVal;
    }
    
    // 5. 计算 USM 锐化结果
    // USM 公式：sharpened = src + amount × (src - blurred)
    cv::Mat blurred, unsharpMask, sharpened;
    // 使用小sigma（0.8）减少光晕，自适应掩码会进一步抑制高对比度区域
    cv::GaussianBlur(src, blurred, cv::Size(3, 3), 0.8);
    cv::subtract(src, blurred, unsharpMask);  // 提取高频细节
    cv::addWeighted(src, 1.0, unsharpMask, amount, 0, sharpened);  // 叠加回原图
    
    // 6. 根据边缘掩码混合原图和锐化图
    // result = src + (sharpened - src) × edgeMask
    src.convertTo(dst, CV_32FC3);
    sharpened.convertTo(sharpened, CV_32FC3);
    
    // 将单通道掩码扩展为三通道
    std::vector<cv::Mat> maskChannels(3);
    maskChannels[0] = edgeMask;
    maskChannels[1] = edgeMask;
    maskChannels[2] = edgeMask;
    cv::Mat mask3ch;
    cv::merge(maskChannels, mask3ch);
    
    // 混合：dst = dst + (sharpened - dst) × mask3ch
    cv::Mat diff = sharpened - dst;
    diff = diff.mul(mask3ch);
    dst = dst + diff;
    
    // 转换回 CV_8UC3
    dst.convertTo(dst, CV_8UC3);
}

// 降噪：缩小后做双边滤波再放大，速度提升约 4 倍
void ImageProcessor::applyDenoise(const cv::Mat &src, cv::Mat &dst,
                                  int d, double sigmaColor,
                                  double sigmaSpace) {
    cv::Mat small, tmp;
    cv::resize(src, small, cv::Size(), 0.5, 0.5, cv::INTER_LINEAR);
    cv::bilateralFilter(small, tmp, d, sigmaColor, sigmaSpace);
    cv::resize(tmp, dst, src.size(), 0, 0, cv::INTER_LINEAR);
}

// === 保留的经典算法 ===

// Canny 边缘检测
void ImageProcessor::applyEdgeDetect(const cv::Mat &src, cv::Mat &dst) {
    cv::Mat gray, edge;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::blur(gray, edge, cv::Size(3, 3));
    cv::Canny(edge, edge, 3, 9, 3);
    cv::cvtColor(edge, dst, cv::COLOR_GRAY2BGR);
}

// 阈值分割
void ImageProcessor::applyThreshold(const cv::Mat &src, cv::Mat &dst,
                                    int threshValue) {
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, gray, threshValue, 255, cv::THRESH_BINARY);
    cv::cvtColor(gray, dst, cv::COLOR_GRAY2BGR);
}

// 清晰度评估：用拉普拉斯算子计算图像方差，值越大越清晰
double ImageProcessor::calcSharpness(const cv::Mat &src) {
    cv::Mat gray, lap;
    if (src.channels() == 3)
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else
        gray = src;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma;
    cv::meanStdDev(lap, mu, sigma);
    return sigma.val[0] * sigma.val[0];  // 方差 = 标准差的平方
}
