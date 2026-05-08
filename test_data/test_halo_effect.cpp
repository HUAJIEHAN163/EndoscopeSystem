// 测试不同sigma对光晕效果的影响
#include <opencv2/opencv.hpp>
#include <iostream>

void testHalo(const cv::Mat& src, const std::string& name, 
              double sigma, double amount) {
    cv::Mat blurred, sharpened;
    int ksize = std::max(3, static_cast<int>(sigma * 3) * 2 + 1);
    cv::GaussianBlur(src, blurred, cv::Size(ksize, ksize), sigma);
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, sharpened);
    
    std::cout << name << ": sigma=" << sigma << ", ksize=" << ksize 
              << ", amount=" << amount << std::endl;
    
    // 分析文字边缘的光晕范围
    int y = 200;  // 文字行
    std::cout << "  边缘像素值变化：" << std::endl;
    for (int x = 54; x < 60; x++) {
        cv::Vec3b orig = src.at<cv::Vec3b>(y, x);
        cv::Vec3b sharp = sharpened.at<cv::Vec3b>(y, x);
        int diff = (int)sharp[0] - (int)orig[0];
        std::cout << "    x=" << x << ": " << (int)orig[0] << " -> " 
                  << (int)sharp[0] << " (diff=" << diff << ")" << std::endl;
    }
    
    cv::imwrite(name + ".jpg", sharpened);
}

int main() {
    cv::Mat src = cv::imread("test_data/test_text.jpg");
    if (src.empty()) {
        std::cerr << "无法读取图片" << std::endl;
        return -1;
    }
    
    std::cout << "=== 测试不同sigma对光晕的影响（amount=1.5） ===" << std::endl;
    testHalo(src, "halo_sigma0.5", 0.5, 1.5);
    testHalo(src, "halo_sigma0.8", 0.8, 1.5);
    testHalo(src, "halo_sigma1.0", 1.0, 1.5);
    testHalo(src, "halo_sigma1.5", 1.5, 1.5);
    testHalo(src, "halo_sigma3.0", 3.0, 1.5);
    
    std::cout << "\n=== 测试不同amount对光晕的影响（sigma=1.0） ===" << std::endl;
    testHalo(src, "halo_amount0.5", 1.0, 0.5);
    testHalo(src, "halo_amount1.0", 1.0, 1.0);
    testHalo(src, "halo_amount1.5", 1.0, 1.5);
    testHalo(src, "halo_amount2.0", 1.0, 2.0);
    
    return 0;
}
