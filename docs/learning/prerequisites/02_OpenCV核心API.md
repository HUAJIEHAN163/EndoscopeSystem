# 02 OpenCV 核心 API

## 一、Mat — 图像容器

### 1.1 创建 Mat

```cpp
// 创建空 Mat
cv::Mat img;

// 创建指定大小和类型的 Mat（全黑）
cv::Mat img(480, 640, CV_8UC3);           // 480行×640列，BGR 彩色
cv::Mat img = cv::Mat::zeros(480, 640, CV_8UC3);  // 全零（黑色）

// 用外部数据创建（不拷贝，只是包装指针）
cv::Mat yuyv(480, 640, CV_8UC2, data);    // data 是 mmap buffer 的指针

// 深拷贝
cv::Mat copy = img.clone();               // 完整拷贝，独立内存
```

### 1.2 Mat 的属性

```cpp
img.rows      // 行数（高度）
img.cols      // 列数（宽度）
img.channels() // 通道数（灰度=1，BGR=3）
img.type()    // 类型（CV_8UC3 等）
img.step      // 每行字节数（含对齐）
img.data      // 底层数据指针
img.empty()   // 是否为空
```

### 1.3 类型系统

```
CV_{位数}{符号}{类型}C{通道数}

CV_8UC1  = 8位无符号，1通道（灰度图）
CV_8UC2  = 8位无符号，2通道（YUYV）
CV_8UC3  = 8位无符号，3通道（BGR 彩色图）
CV_32FC1 = 32位浮点，1通道（算法中间结果）
CV_32FC3 = 32位浮点，3通道（去雾算法中的浮点图像）
CV_64FC3 = 64位浮点，3通道（精度高但慢，嵌入式避免使用）
```

### 1.4 类型转换

```cpp
cv::Mat floatImg;
img.convertTo(floatImg, CV_32FC3, 1.0/255.0);  // uint8 → float，归一化到 [0,1]
floatImg.convertTo(img, CV_8UC3, 255.0);        // float → uint8，反归一化
```

---

## 二、色彩空间转换 — cvtColor

```cpp
cv::Mat bgr, rgb, gray, lab, hsv;

cv::cvtColor(src, bgr, cv::COLOR_YUV2BGR_YUYV);  // YUYV → BGR
cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);         // BGR → RGB
cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);       // BGR → 灰度
cv::cvtColor(bgr, lab, cv::COLOR_BGR2Lab);          // BGR → Lab
cv::cvtColor(lab, bgr, cv::COLOR_Lab2BGR);          // Lab → BGR
cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);          // BGR → HSV
```

---

## 三、通道操作 — split / merge

```cpp
std::vector<cv::Mat> channels;

// 分离：一张 BGR 图 → 三个单通道图
cv::split(bgr, channels);
// channels[0] = B 通道
// channels[1] = G 通道
// channels[2] = R 通道

// 单独处理某个通道
channels[0] *= 1.2;  // 蓝色通道增强

// 合并：三个单通道图 → 一张 BGR 图
cv::merge(channels, bgr);
```

项目中的使用：
- 白平衡：split → 计算各通道均值 → 乘以增益 → merge
- CLAHE：BGR→Lab → split → 对 L 通道均衡化 → merge → Lab→BGR
- 去雾：split → 取三通道最小值（暗通道）

---

## 四、滤波

### 4.1 高斯模糊

```cpp
cv::Mat blurred;
cv::GaussianBlur(src, blurred, cv::Size(3, 3), 0);
//                              核大小(3×3)    sigma(0=自动计算)
```

核越大越模糊，但越慢。项目中锐化用 3×3 核（最小，最快）。

### 4.2 双边滤波

```cpp
cv::Mat denoised;
cv::bilateralFilter(src, denoised, 5, 50, 50);
//                                 d  sigmaColor sigmaSpace
// d=邻域直径，sigmaColor=颜色差异权重，sigmaSpace=空间距离权重
```

特点：保边去噪（边缘不模糊，平坦区域去噪）。但非常慢，项目中降到半分辨率处理。

注意：bilateralFilter 不支持原地操作（src 和 dst 不能是同一个 Mat）。

### 4.3 形态学 — 腐蚀

```cpp
cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7));
cv::erode(src, dst, kernel);
```

腐蚀：取邻域最小值。项目中去雾算法用腐蚀计算暗通道。

---

## 五、算术运算

```cpp
// 加权混合
cv::addWeighted(src1, alpha, src2, beta, gamma, dst);
// dst = src1 * alpha + src2 * beta + gamma

// 项目中 USM 锐化：
cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, dst);
// dst = src * (1+amount) - blurred * amount
// = src + amount * (src - blurred)
// = 原图 + 锐化强度 × 高频细节

// 逐元素乘法
cv::Mat result = mat1.mul(mat2);

// 逐元素除法
cv::divide(mat1, scalar, dst);

// 取最小值
cv::min(mat1, mat2, dst);
```

---

## 六、边缘检测

### 6.1 Laplacian

```cpp
cv::Mat lap;
cv::Laplacian(gray, lap, CV_32F, 1);
// 输出类型 CV_32F（有负值），ksize=1
```

Laplacian 计算二阶导数，边缘处值大（正或负），平坦处值接近 0。
项目中自适应锐化用它生成边缘掩码。

### 6.2 Sobel

```cpp
cv::Mat gradX, gradY;
cv::Sobel(gray, gradX, CV_32F, 1, 0);  // X 方向梯度
cv::Sobel(gray, gradY, CV_32F, 0, 1);  // Y 方向梯度
cv::Mat magnitude;
cv::magnitude(gradX, gradY, magnitude);  // 梯度幅值
```

### 6.3 Canny

```cpp
cv::Mat edges;
cv::Canny(gray, edges, 50, 150);  // 低阈值, 高阈值
// 输出是二值图：边缘=255，非边缘=0
```

项目中 applyEdgeDetect 使用 Canny。

---

## 七、直方图 — CLAHE

```cpp
// 创建 CLAHE 对象（只创建一次，复用）
static cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
clahe->setClipLimit(3.0);              // 对比度限制
clahe->setTilesGridSize(cv::Size(8,8)); // 分块大小

// 在 Lab 空间的 L 通道上应用
cv::Mat lab;
cv::cvtColor(src, lab, cv::COLOR_BGR2Lab);
std::vector<cv::Mat> channels;
cv::split(lab, channels);
clahe->apply(channels[0], channels[0]);  // 只处理 L 通道
cv::merge(channels, lab);
cv::cvtColor(lab, dst, cv::COLOR_Lab2BGR);
```

---

## 八、缩放

```cpp
cv::Mat small, big;

// 缩小到 50%
cv::resize(src, small, cv::Size(), 0.5, 0.5, cv::INTER_LINEAR);

// 放大到指定尺寸
cv::resize(src, big, cv::Size(640, 480), 0, 0, cv::INTER_LINEAR);
```

项目中降分辨率处理：640×480 → 320×240，算法耗时降 4-5 倍。

---

## 九、阈值处理

```cpp
cv::Mat binary;
cv::threshold(gray, binary, 128, 255, cv::THRESH_BINARY);
// 像素 > 128 → 255（白），否则 → 0（黑）
```

---

## 十、与项目代码的对应

| API | 项目中的函数 | 用途 |
|---|---|---|
| cvtColor | v4l2capture.cpp run() | YUYV→BGR→RGB |
| split/merge | applyCLAHE, applyWhiteBalance, applyDehaze | 通道分离处理 |
| GaussianBlur + addWeighted | applySharpen | USM 锐化 |
| bilateralFilter | applyDenoise | 双边滤波降噪 |
| erode + min + divide | applyDehaze | 暗通道先验去雾 |
| CLAHE | applyCLAHE | 自适应直方图均衡化 |
| Canny | applyEdgeDetect | 边缘检测 |
| threshold | applyThreshold | 阈值分割 |
| Laplacian | calcSharpness, 自适应锐化 | 清晰度评估、边缘掩码 |
| resize | process() | 降分辨率处理 |
| convertTo | applyDehaze | uint8↔float 转换 |
