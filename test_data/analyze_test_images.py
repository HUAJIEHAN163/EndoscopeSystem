#!/usr/bin/env python3
"""
分析测试图片并解释测试重点
"""
import cv2
import numpy as np
import os

def analyze_image_info():
    """分析测试图片信息"""
    
    test_images = [
        ('test_edge_noise.jpg', '边缘+噪声测试'),
        ('test_gradient.jpg', '渐变测试'),
        ('test_checkerboard.jpg', '棋盘格测试'),
        ('test_text.jpg', '文字细节测试'),
        ('test_stomach_simulated.jpg', '模拟胃镜图像'),
        ('test_colon_simulated.jpg', '模拟肠镜图像')
    ]
    
    print("=" * 80)
    print("自适应锐化算法测试图片分析")
    print("=" * 80)
    
    for filename, description in test_images:
        if not os.path.exists(filename):
            print(f"\n⚠️  文件不存在: {filename}")
            continue
            
        img = cv2.imread(filename)
        if img is None:
            print(f"\n❌ 无法读取: {filename}")
            continue
            
        height, width, channels = img.shape
        print(f"\n📊 {description} ({filename}):")
        print(f"   尺寸: {width}×{height} 像素, 通道数: {channels}")
        
        # 计算统计信息
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        mean_val = np.mean(gray)
        std_val = np.std(gray)
        min_val = np.min(gray)
        max_val = np.max(gray)
        
        print(f"   灰度统计: 均值={mean_val:.1f}, 标准差={std_val:.1f}, 范围=[{min_val}, {max_val}]")
        
        # 边缘检测（测试边缘增强效果）
        edges = cv2.Canny(gray, 50, 150)
        edge_pixels = np.sum(edges > 0)
        edge_ratio = edge_pixels / (width * height) * 100
        
        print(f"   边缘检测: {edge_pixels} 个边缘像素 ({edge_ratio:.2f}%)")
        
        # 特定测试关注点
        if 'edge_noise' in filename:
            print(f"   🔍 测试重点: 1) 清晰边缘增强 2) 噪声抑制 3) 边缘-噪声区分能力")
            print(f"   📈 期望结果: 边缘更锐利，背景噪声不放大")
            
        elif 'gradient' in filename:
            print(f"   🔍 测试重点: 1) 平坦区域处理 2) 渐变平滑性 3) 伪影抑制")
            print(f"   📈 期望结果: 渐变保持平滑，不产生条纹或伪影")
            
        elif 'checkerboard' in filename:
            print(f"   🔍 测试重点: 1) 周期性图案处理 2) 摩尔纹抑制 3) 棋盘角点清晰度")
            print(f"   📈 期望结果: 棋盘格清晰，不产生混叠或振铃效应")
            
        elif 'text' in filename:
            print(f"   🔍 测试重点: 1) 文字细节增强 2) 笔画清晰度 3) 背景均匀性")
            print(f"   📈 期望结果: 文字更清晰易读，背景不产生噪声")
            
        elif 'stomach' in filename:
            print(f"   🔍 测试重点: 1) 组织纹理增强 2) 血管边缘清晰度 3) 反光区域处理")
            print(f"   📈 期望结果: 组织纹理更清晰，血管边缘明显，反光区域不过曝")
            
        elif 'colon' in filename:
            print(f"   🔍 测试重点: 1) 褶皱结构增强 2) 阴影细节保留 3) 颜色一致性")
            print(f"   📈 期望结果: 褶皱结构清晰，阴影区域细节可见，颜色自然")
    
    print("\n" + "=" * 80)
    print("测试策略总结:")
    print("=" * 80)
    print("""
1. 主观测试（人眼观察）:
   - 边缘是否更清晰？
   - 噪声是否被抑制？
   - 是否有伪影或振铃效应？
   - 细节是否自然增强？

2. 客观测试（量化指标）:
   - 边缘强度变化（Canny边缘检测）
   - 对比度变化（局部对比度测量）
   - 噪声水平变化（平滑区域标准差）
   - 信息熵变化（图像信息量）

3. 性能测试:
   - 处理时间（与USM锐化对比）
   - 内存使用情况
   - 实时性评估（能否满足30fps）

4. 极端情况测试:
   - 高噪声图像
   - 低对比度图像  
   - 过曝/欠曝图像
   - 运动模糊图像
""")

if __name__ == "__main__":
    # 先确保图片在当前目录
    if os.path.exists('test_data'):
        print("检测到test_data子目录，正在检查图片...")
        for file in os.listdir('test_data'):
            if file.endswith('.jpg') and not os.path.exists(file):
                os.rename(f'test_data/{file}', file)
        if os.listdir('test_data') == []:
            os.rmdir('test_data')
    
    analyze_image_info()