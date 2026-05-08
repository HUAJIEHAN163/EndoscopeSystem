#!/usr/bin/env python3
"""
生成自适应锐化算法测试图片
"""
import cv2
import numpy as np
import os

def create_test_patterns():
    """创建标准测试图案"""
    
    # 1. 边缘测试图（清晰边缘 + 噪声背景）
    img1 = np.ones((480, 640, 3), dtype=np.uint8) * 128
    # 添加清晰边缘
    cv2.rectangle(img1, (100, 100), (300, 300), (200, 100, 50), 10)
    cv2.circle(img1, (400, 200), 50, (50, 150, 200), 8)
    # 添加噪声背景
    noise = np.random.normal(0, 10, img1.shape).astype(np.uint8)
    img1 = cv2.add(img1, noise)
    cv2.imwrite('test_edge_noise.jpg', img1)
    
    # 2. 渐变测试图（测试平坦区域处理）
    img2 = np.zeros((480, 640, 3), dtype=np.uint8)
    for i in range(640):
        intensity = int(255 * i / 640)
        img2[:, i, :] = intensity
    cv2.imwrite('test_gradient.jpg', img2)
    
    # 3. 棋盘格测试图（测试周期性图案）
    img3 = np.zeros((480, 640, 3), dtype=np.uint8)
    for y in range(0, 480, 40):
        for x in range(0, 640, 40):
            if (x//40 + y//40) % 2 == 0:
                img3[y:y+40, x:x+40] = 200
            else:
                img3[y:y+40, x:x+40] = 50
    cv2.imwrite('test_checkerboard.jpg', img3)
    
    # 4. 文字测试图（测试细节增强）
    img4 = np.ones((480, 640, 3), dtype=np.uint8) * 180
    cv2.putText(img4, 'Endoscope Test', (50, 200), 
                cv2.FONT_HERSHEY_SIMPLEX, 2, (50, 50, 50), 3)
    cv2.putText(img4, 'Adaptive Sharpen', (50, 300),
                cv2.FONT_HERSHEY_SIMPLEX, 1.5, (30, 30, 30), 2)
    cv2.imwrite('test_text.jpg', img4)
    
    print("测试图片生成完成！")
    print("1. test_edge_noise.jpg - 边缘+噪声测试")
    print("2. test_gradient.jpg - 渐变测试")  
    print("3. test_checkerboard.jpg - 棋盘格测试")
    print("4. test_text.jpg - 文字细节测试")

def create_realistic_endoscope():
    """模拟内窥镜图像"""
    # 5. 模拟胃镜图像（红色黏膜 + 白色反光）
    img5 = np.ones((480, 640, 3), dtype=np.uint8) * np.array([100, 50, 50], dtype=np.uint8)  # 红色背景
    
    # 添加组织纹理
    for _ in range(1000):
        x = np.random.randint(0, 640)
        y = np.random.randint(0, 480)
        r = np.random.randint(2, 10)
        color = np.random.randint(80, 150)
        cv2.circle(img5, (x, y), r, (color, color//2, color//3), -1)
    
    # 添加反光区域（模拟镜头反光）
    cv2.ellipse(img5, (320, 240), (100, 80), 0, 0, 360, (200, 200, 200), -1)
    
    # 添加血管纹理（模拟边缘）
    for i in range(5):
        pts = np.array([[100+i*20, 150], [200+i*20, 200], [300+i*20, 180], [400+i*20, 220]], np.int32)
        cv2.polylines(img5, [pts], False, (150, 80, 80), 3)
    
    cv2.imwrite('test_stomach_simulated.jpg', img5)
    print("5. test_stomach_simulated.jpg - 模拟胃镜图像")
    
    # 6. 模拟肠镜图像（更多褶皱）
    img6 = np.ones((480, 640, 3), dtype=np.uint8) * np.array([120, 70, 60], dtype=np.uint8)
    
    # 添加褶皱纹理
    for i in range(20):
        y = i * 24
        cv2.line(img6, (0, y), (640, y), (140, 90, 80), 2)
        # 添加褶皱阴影
        if i % 2 == 0:
            cv2.rectangle(img6, (0, y), (640, y+12), (90, 50, 40), -1)
    
    cv2.imwrite('test_colon_simulated.jpg', img6)
    print("6. test_colon_simulated.jpg - 模拟肠镜图像")

if __name__ == "__main__":
    os.makedirs('test_data', exist_ok=True)
    os.chdir('test_data')
    
    create_test_patterns()
    create_realistic_endoscope()
    
    print("\n所有测试图片已生成到 test_data/ 目录")
    print("建议使用这些图片进行算法测试和对比")