#include "processing/imageprocessor.h"

// === 管线入口 ===

void ImageProcessor::process(const cv::Mat &src, cv::Mat &dst,
                             const Config &cfg,
                             const cv::Mat &map1, const cv::Mat &map2) {
    dst = src.clone();

    if (cfg.whiteBalance)
        applyWhiteBalance(dst, dst);

    if (cfg.clahe)
        applyCLAHE(dst, dst, cfg.claheClipLimit);

    if (cfg.undistort && !map1.empty() && !map2.empty())
        cv::remap(dst, dst, map1, map2, cv::INTER_LINEAR);

    if (cfg.dehaze)
        applyDehaze(dst, dst, cfg.dehazeOmega, cfg.dehazeRadius);

    if (cfg.sharpen)
        applySharpen(dst, dst, 3.0, cfg.sharpenAmount);

    if (cfg.denoise)
        applyDenoise(dst, dst, cfg.denoiseD);

    if (cfg.edgeDetect)
        applyEdgeDetect(dst, dst);

    if (cfg.threshold)
        applyThreshold(dst, dst, cfg.thresholdValue);
}

// === 内窥镜专用算法 ===

void ImageProcessor::applyCLAHE(const cv::Mat &src, cv::Mat &dst,
                                double clipLimit, cv::Size tileSize) {
    cv::Mat lab;
    cv::cvtColor(src, lab, cv::COLOR_BGR2Lab);

    std::vector<cv::Mat> channels;
    cv::split(lab, channels);

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clipLimit, tileSize);
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

void ImageProcessor::applyDehaze(const cv::Mat &src, cv::Mat &dst,
                                 double omega, int radius) {
    cv::Mat srcFloat;
    src.convertTo(srcFloat, CV_64FC3, 1.0 / 255.0);

    // 暗通道
    std::vector<cv::Mat> channels;
    cv::split(srcFloat, channels);
    cv::Mat darkChannel = cv::min(cv::min(channels[0], channels[1]), channels[2]);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT,
                                               cv::Size(radius, radius));
    cv::erode(darkChannel, darkChannel, kernel);

    // 大气光估计
    double maxVal;
    cv::minMaxLoc(darkChannel, nullptr, &maxVal);
    cv::Scalar A = cv::mean(srcFloat, darkChannel > maxVal * 0.9);

    // 透射率
    std::vector<cv::Mat> normChannels;
    cv::split(srcFloat, normChannels);
    for (int i = 0; i < 3; i++)
        normChannels[i] /= (A[i] + 1e-6);

    cv::Mat normImg;
    cv::merge(normChannels, normImg);
    cv::split(normImg, normChannels);

    cv::Mat transmission = cv::min(cv::min(normChannels[0], normChannels[1]),
                                   normChannels[2]);
    cv::erode(transmission, transmission, kernel);
    transmission = 1.0 - omega * transmission;
    cv::max(transmission, 0.1, transmission);

    // 恢复
    cv::split(srcFloat, channels);
    for (int i = 0; i < 3; i++)
        channels[i] = (channels[i] - A[i]) / transmission + A[i];
    cv::merge(channels, dst);

    dst.convertTo(dst, CV_8UC3, 255.0);
}

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

void ImageProcessor::applySharpen(const cv::Mat &src, cv::Mat &dst,
                                  double sigma, double amount) {
    cv::Mat blurred;
    cv::GaussianBlur(src, blurred, cv::Size(0, 0), sigma);
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, dst);
}

void ImageProcessor::applyDenoise(const cv::Mat &src, cv::Mat &dst,
                                  int d, double sigmaColor,
                                  double sigmaSpace) {
    cv::bilateralFilter(src, dst, d, sigmaColor, sigmaSpace);
}

// === 保留的经典算法 ===

void ImageProcessor::applyEdgeDetect(const cv::Mat &src, cv::Mat &dst) {
    cv::Mat gray, edge;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::blur(gray, edge, cv::Size(3, 3));
    cv::Canny(edge, edge, 3, 9, 3);
    cv::cvtColor(edge, dst, cv::COLOR_GRAY2BGR);
}

void ImageProcessor::applyThreshold(const cv::Mat &src, cv::Mat &dst,
                                    int threshValue) {
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, gray, threshValue, 255, cv::THRESH_BINARY);
    cv::cvtColor(gray, dst, cv::COLOR_GRAY2BGR);
}
