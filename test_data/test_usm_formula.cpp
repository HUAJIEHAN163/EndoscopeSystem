// 测试USM锐化公式
#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // 创建简单测试图像：中间是白色方块
    cv::Mat src = cv::Mat::zeros(100, 100, CV_8UC1);
    cv::rectangle(src, cv::Point(30, 30), cv::Point(70, 70), cv::Scalar(255), -1);
    
    double amount = 1.0;
    cv::Mat blurred;
    cv::GaussianBlur(src, blurred, cv::Size(5, 5), 1.0);
    
    // 方法1：原来的公式（可能有问题）
    cv::Mat result1;
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, result1);
    
    // 方法2：正确的USM公式
    cv::Mat unsharpMask, result2;
    cv::subtract(src, blurred, unsharpMask);
    cv::addWeighted(src, 1.0, unsharpMask, amount, 0, result2);
    
    // 比较边缘点的值（边缘才能看出锐化效果）
    int x = 30, y = 50;  // 方块左边缘
    std::cout << "测试点位置: (" << x << ", " << y << ") - 方块左边缘" << std::endl;
    std::cout << "原图值: " << (int)src.at<uchar>(y, x) << std::endl;
    std::cout << "模糊图值: " << (int)blurred.at<uchar>(y, x) << std::endl;
    std::cout << "方法1结果: " << (int)result1.at<uchar>(y, x) << std::endl;
    std::cout << "方法2结果: " << (int)result2.at<uchar>(y, x) << std::endl;
    
    // 数学验证
    int src_val = src.at<uchar>(y, x);
    int blur_val = blurred.at<uchar>(y, x);
    
    // 方法1: src * (1 + amount) + blurred * (-amount)
    double formula1 = src_val * (1.0 + amount) + blur_val * (-amount);
    std::cout << "\n方法1公式计算: " << src_val << " * " << (1.0 + amount) 
              << " + " << blur_val << " * " << (-amount) << " = " << formula1 << std::endl;
    
    // 方法2: src + amount * (src - blurred)
    double formula2 = src_val + amount * (src_val - blur_val);
    std::cout << "方法2公式计算: " << src_val << " + " << amount << " * (" 
              << src_val << " - " << blur_val << ") = " << formula2 << std::endl;
    
    // 判断哪个更锐利（值应该更大）
    std::cout << "\n结论：" << std::endl;
    if (formula1 > src_val) {
        std::cout << "方法1会增强（值从 " << src_val << " 增加到 " << formula1 << "）" << std::endl;
    } else {
        std::cout << "方法1会减弱（值从 " << src_val << " 减少到 " << formula1 << "）" << std::endl;
    }
    
    if (formula2 > src_val) {
        std::cout << "方法2会增强（值从 " << src_val << " 增加到 " << formula2 << "）" << std::endl;
    } else {
        std::cout << "方法2会减弱（值从 " << src_val << " 减少到 " << formula2 << "）" << std::endl;
    }
    
    return 0;
}
