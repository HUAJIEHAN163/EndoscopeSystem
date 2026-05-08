#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <opencv2/opencv.hpp>

// 假设你的算法在这里
#include "processing/imageprocessor.h"

// 测试配置
struct TestConfig {
    std::string image_path;
    std::string test_name;
    float usm_amount = 1.5f;
    float adaptive_amount = 1.5f;
    int adaptive_threshold = 10;
};

// 测试结果
struct TestResult {
    std::string test_name;
    double sharpness_original = 0;
    double sharpness_usm = 0;
    double sharpness_adaptive = 0;
    double time_usm_ms = 0;
    double time_adaptive_ms = 0;
    double memory_peak_mb = 0;
};

// 计算清晰度（拉普拉斯方差法）
double calculate_sharpness(const cv::Mat& image) {
    if (image.empty()) return 0;
    
    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image.clone();
    }
    
    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);
    
    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    
    return stddev.val[0] * stddev.val[0];  // 方差
}

// 运行单次测试
TestResult run_single_test(const TestConfig& config) {
    TestResult result;
    result.test_name = config.test_name;
    
    // 加载测试图像
    cv::Mat original = cv::imread(config.image_path);
    if (original.empty()) {
        std::cerr << "错误: 无法加载图像 " << config.image_path << std::endl;
        return result;
    }
    
    std::cout << "测试: " << config.test_name 
              << " (" << original.cols << "x" << original.rows << ")" << std::endl;
    
    // 计算原图清晰度
    result.sharpness_original = calculate_sharpness(original);
    
    // 测试标准USM
    cv::Mat result_usm;
    auto start = std::chrono::high_resolution_clock::now();
    
    ImageProcessor::applySharpen(original, result_usm, 3.0, config.usm_amount);
    
    auto end = std::chrono::high_resolution_clock::now();
    result.time_usm_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.sharpness_usm = calculate_sharpness(result_usm);
    
    // 测试自适应锐化
    cv::Mat result_adaptive;
    start = std::chrono::high_resolution_clock::now();
    
    // 调用你的自适应锐化函数
    // ImageProcessor::applyAdaptiveSharpen(original, result_adaptive, 
    //                                     config.adaptive_amount, config.adaptive_threshold);
    
    // 临时使用USM代替（你需要替换为你的实现）
    ImageProcessor::applySharpen(original, result_adaptive, 3.0, config.adaptive_amount);
    
    end = std::chrono::high_resolution_clock::now();
    result.time_adaptive_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.sharpness_adaptive = calculate_sharpness(result_adaptive);
    
    // 保存结果图像用于对比
    std::string base_name = config.image_path.substr(0, config.image_path.find_last_of('.'));
    cv::imwrite(base_name + "_usm.jpg", result_usm);
    cv::imwrite(base_name + "_adaptive.jpg", result_adaptive);
    
    return result;
}

// 打印测试结果
void print_results(const std::vector<TestResult>& results) {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "自适应锐化算法测试报告\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    std::cout << std::left << std::setw(25) << "测试名称"
              << std::setw(15) << "原图清晰度"
              << std::setw(15) << "USM清晰度"
              << std::setw(20) << "自适应清晰度"
              << std::setw(15) << "USM耗时(ms)"
              << std::setw(20) << "自适应耗时(ms)"
              << std::setw(15) << "清晰度提升%"
              << std::setw(15) << "性能比" << "\n";
    
    std::cout << std::string(140, '-') << "\n";
    
    double total_usm_time = 0;
    double total_adaptive_time = 0;
    int count = 0;
    
    for (const auto& r : results) {
        if (r.sharpness_original == 0) continue;
        
        double usm_improvement = (r.sharpness_usm / r.sharpness_original - 1) * 100;
        double adaptive_improvement = (r.sharpness_adaptive / r.sharpness_original - 1) * 100;
        double performance_ratio = r.time_usm_ms / r.time_adaptive_ms;
        
        std::cout << std::left << std::setw(25) << r.test_name
                  << std::setw(15) << std::fixed << std::setprecision(1) << r.sharpness_original
                  << std::setw(15) << std::fixed << std::setprecision(1) << r.sharpness_usm
                  << std::setw(20) << std::fixed << std::setprecision(1) << r.sharpness_adaptive
                  << std::setw(15) << std::fixed << std::setprecision(2) << r.time_usm_ms
                  << std::setw(20) << std::fixed << std::setprecision(2) << r.time_adaptive_ms
                  << std::setw(15) << std::fixed << std::setprecision(1) << adaptive_improvement
                  << std::setw(15) << std::fixed << std::setprecision(2) << performance_ratio << "\n";
        
        total_usm_time += r.time_usm_ms;
        total_adaptive_time += r.time_adaptive_ms;
        count++;
    }
    
    std::cout << std::string(140, '-') << "\n";
    
    if (count > 0) {
        double avg_usm_time = total_usm_time / count;
        double avg_adaptive_time = total_adaptive_time / count;
        double avg_performance_ratio = avg_usm_time / avg_adaptive_time;
        
        std::cout << "平均值:"
                  << std::setw(90) << " "
                  << std::setw(15) << std::fixed << std::setprecision(2) << avg_usm_time
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_adaptive_time
                  << std::setw(30) << " "
                  << std::setw(15) << std::fixed << std::setprecision(2) << avg_performance_ratio << "\n";
    }
    
    // 总结
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "测试总结:\n";
    std::cout << "1. 清晰度: 自适应锐化应该比USM提升10-30%\n";
    std::cout << "2. 性能: 自适应锐化耗时应该 <= USM耗时（目标: 性能比 >= 1.0）\n";
    std::cout << "3. 内存: 峰值内存增加应该 < 50MB\n";
    std::cout << "4. 效果: 边缘增强明显，平坦区域噪声不放大\n";
    std::cout << std::string(80, '=') << "\n";
}

// 创建对比图
void create_comparison_image(const std::string& image_path) {
    cv::Mat original = cv::imread(image_path);
    if (original.empty()) return;
    
    cv::Mat result_usm, result_adaptive;
    ImageProcessor::applySharpen(original, result_usm, 3.0, 1.5);
    // ImageProcessor::applyAdaptiveSharpen(original, result_adaptive, 1.5, 10);
    ImageProcessor::applySharpen(original, result_adaptive, 3.0, 1.5); // 临时
    
    // 创建4格对比图
    int width = original.cols;
    int height = original.rows;
    cv::Mat comparison(height * 2, width * 2, CV_8UC3);
    
    // 左上: 原图
    original.copyTo(comparison(cv::Rect(0, 0, width, height)));
    cv::putText(comparison, "Original", cv::Point(10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
    
    // 右上: USM
    result_usm.copyTo(comparison(cv::Rect(width, 0, width, height)));
    cv::putText(comparison, "USM Sharpening", cv::Point(width + 10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
    
    // 左下: 自适应锐化
    result_adaptive.copyTo(comparison(cv::Rect(0, height, width, height)));
    cv::putText(comparison, "Adaptive Sharpening", cv::Point(10, height + 30),
                cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
    
    // 右下: 差异图（红色表示增强区域）
    cv::Mat diff = cv::abs(result_adaptive - result_usm);
    diff.copyTo(comparison(cv::Rect(width, height, width, height)));
    cv::putText(comparison, "Difference", cv::Point(width + 10, height + 30),
                cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
    
    std::string output_name = image_path.substr(0, image_path.find_last_of('.')) + "_comparison.jpg";
    cv::imwrite(output_name, comparison);
    std::cout << "对比图已保存: " << output_name << std::endl;
}

int main() {
    std::vector<TestConfig> test_configs = {
        {"test_data/test_edge_noise.jpg", "边缘+噪声测试"},
        {"test_data/test_gradient.jpg", "渐变测试"},
        {"test_data/test_checkerboard.jpg", "棋盘格测试"},
        {"test_data/test_text.jpg", "文字细节测试"},
        {"test_data/test_stomach_simulated.jpg", "模拟胃镜图像"},
        {"test_data/test_colon_simulated.jpg", "模拟肠镜图像"}
    };
    
    std::vector<TestResult> results;
    
    std::cout << "开始自适应锐化算法测试...\n";
    std::cout << "测试配置:\n";
    std::cout << "  USM强度: 1.5\n";
    std::cout << "  自适应锐化强度: 1.5\n";
    std::cout << "  边缘阈值: 10\n\n";
    
    // 运行所有测试
    for (const auto& config : test_configs) {
        TestResult result = run_single_test(config);
        results.push_back(result);
        
        // 创建对比图
        create_comparison_image(config.image_path);
    }
    
    // 打印结果
    print_results(results);
    
    // 保存详细报告
    std::ofstream report("test_report.txt");
    report << "自适应锐化算法测试报告\n";
    report << "生成时间: " << __DATE__ << " " << __TIME__ << "\n\n";
    
    for (const auto& r : results) {
        report << "测试: " << r.test_name << "\n";
        report << "  原图清晰度: " << r.sharpness_original << "\n";
        report << "  USM清晰度: " << r.sharpness_usm 
               << " (提升" << (r.sharpness_usm/r.sharpness_original-1)*100 << "%)\n";
        report << "  自适应清晰度: " << r.sharpness_adaptive
               << " (提升" << (r.sharpness_adaptive/r.sharpness_original-1)*100 << "%)\n";
        report << "  USM耗时: " << r.time_usm_ms << "ms\n";
        report << "  自适应耗时: " << r.time_adaptive_ms << "ms\n";
        report << "  性能比: " << r.time_usm_ms/r.time_adaptive_ms << "\n\n";
    }
    
    report.close();
    std::cout << "\n详细报告已保存到: test_report.txt\n";
    
    return 0;
}