// 测试USM锐化参数对文字的影响
#include <opencv2/opencv.hpp>
#include <iostream>

void testUSM(const cv::Mat& src, const std::string& name, 
             int kernelSize, double sigma, double amount) {
    cv::Mat blurred, sharpened;
    cv::GaussianBlur(src, blurred, cv::Size(kernelSize, kernelSize), sigma);
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, sharpened);
    
    // 计算清晰度
    cv::Mat gray, lap;
    cv::cvtColor(sharpened, gray, cv::COLOR_BGR2GRAY);
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma_val;
    cv::meanStdDev(lap, mu, sigma_val);
    double sharpness = sigma_val.val[0] * sigma_val.val[0];
    
    std::cout << name << ": kernel=" << kernelSize << ", sigma=" << sigma 
              << ", amount=" << amount << " -> 清晰度=" << sharpness << std::endl;
    
    cv::imwrite(name + ".jpg", sharpened);
}

int main() {
    cv::Mat src = cv::imread("test_data/test_text.jpg");
    if (src.empty()) {
        std::cerr << "无法读取图片" << std::endl;
        return -1;
    }
    
    // 计算原图清晰度
    cv::Mat gray, lap;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma_val;
    cv::meanStdDev(lap, mu, sigma_val);
    double orig_sharpness = sigma_val.val[0] * sigma_val.val[0];
    std::cout << "原图清晰度: " << orig_sharpness << "\n" << std::endl;
    
    std::cout << "=== 问题1：高斯模糊参数影响 ===" << std::endl;
    testUSM(src, "test_kernel3_sigma0", 3, 0, 1.0);      // 当前代码
    testUSM(src, "test_kernel3_sigma1", 3, 1.0, 1.0);    // sigma=1
    testUSM(src, "test_kernel3_sigma3", 3, 3.0, 1.0);    // sigma=3（传入值）
    testUSM(src, "test_kernel5_sigma3", 5, 3.0, 1.0);    // 更大kernel
    
    std::cout << "\n=== 问题2：锐化强度影响 ===" << std::endl;
    testUSM(src, "test_amount0.1", 3, 1.0, 0.1);   // 太低
    testUSM(src, "test_amount0.5", 3, 1.0, 0.5);   // 较低
    testUSM(src, "test_amount1.0", 3, 1.0, 1.0);   // 中等
    testUSM(src, "test_amount2.0", 3, 1.0, 2.0);   // 较高
    testUSM(src, "test_amount3.0", 3, 1.0, 3.0);   // 很高
    
    std::cout << "\n=== 问题3：半径（kernel size）影响 ===" << std::endl;
    testUSM(src, "test_radius3", 3, 1.0, 1.0);     // 小半径
    testUSM(src, "test_radius5", 5, 1.0, 1.0);     // 中半径
    testUSM(src, "test_radius7", 7, 1.0, 1.0);     // 大半径
    testUSM(src, "test_radius9", 9, 1.0, 1.0);     // 很大半径
    
    std::cout << "\n=== 最佳组合测试 ===" << std::endl;
    testUSM(src, "test_best_small", 3, 0.5, 1.5);   // 小sigma + 高amount
    testUSM(src, "test_best_medium", 3, 1.0, 1.5);  // 中sigma + 高amount
    
    return 0;
}
