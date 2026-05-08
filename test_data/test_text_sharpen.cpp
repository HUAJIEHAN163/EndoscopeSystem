// 测试文字锐化效果
#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // 读取测试图片
    cv::Mat src = cv::imread("test_data/test_text.jpg");
    if (src.empty()) {
        std::cerr << "无法读取图片" << std::endl;
        return -1;
    }
    
    double amount = 1.0;  // 默认锐化强度
    
    // USM锐化
    cv::Mat blurred, sharpened;
    cv::GaussianBlur(src, blurred, cv::Size(3, 3), 0);
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, sharpened);
    
    // 保存结果
    cv::imwrite("test_text_original.jpg", src);
    cv::imwrite("test_text_blurred.jpg", blurred);
    cv::imwrite("test_text_sharpened.jpg", sharpened);
    
    // 计算清晰度（Laplacian方差）
    auto calcSharpness = [](const cv::Mat& img) {
        cv::Mat gray, lap;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        cv::Laplacian(gray, lap, CV_64F);
        cv::Scalar mu, sigma;
        cv::meanStdDev(lap, mu, sigma);
        return sigma.val[0] * sigma.val[0];
    };
    
    double sharpness_orig = calcSharpness(src);
    double sharpness_sharp = calcSharpness(sharpened);
    
    std::cout << "原图清晰度: " << sharpness_orig << std::endl;
    std::cout << "锐化后清晰度: " << sharpness_sharp << std::endl;
    std::cout << "清晰度提升: " << ((sharpness_sharp - sharpness_orig) / sharpness_orig * 100) << "%" << std::endl;
    
    // 分析文字边缘
    std::cout << "\n分析文字区域（y=200附近）：" << std::endl;
    for (int x = 50; x < 60; x++) {
        int y = 200;
        cv::Vec3b orig = src.at<cv::Vec3b>(y, x);
        cv::Vec3b blur = blurred.at<cv::Vec3b>(y, x);
        cv::Vec3b sharp = sharpened.at<cv::Vec3b>(y, x);
        
        std::cout << "x=" << x << ": 原图=" << (int)orig[0] 
                  << ", 模糊=" << (int)blur[0]
                  << ", 锐化=" << (int)sharp[0] << std::endl;
    }
    
    std::cout << "\n图片已保存，请对比查看：" << std::endl;
    std::cout << "- test_text_original.jpg" << std::endl;
    std::cout << "- test_text_sharpened.jpg" << std::endl;
    
    return 0;
}
