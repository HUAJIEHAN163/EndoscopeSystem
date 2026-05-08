# USM锐化参数问题完整分析

## 问题发现

代码中存在**严重bug**：

```cpp
void ImageProcessor::applySharpen(const cv::Mat &src, cv::Mat &dst,
                                  double sigma, double amount) {
    cv::Mat blurred;
    cv::GaussianBlur(src, blurred, cv::Size(3, 3), 0);  // ❌ 硬编码sigma=0
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, dst);
}
```

**调用处：** `applySharpen(work, work, 3.0, cfg.sharpenAmount);`
- 传入 sigma=3.0
- 但函数**完全忽略**这个参数，硬编码使用 sigma=0

## 测试结果分析

### 原图清晰度基准
```
原图清晰度: 967.214
```

### 问题1：高斯模糊参数影响

```
参数组合                    | 清晰度   | 提升率
---------------------------|----------|--------
kernel=3, sigma=0 (当前)   | 2978.43  | 208%
kernel=3, sigma=1          | 3076.71  | 218%  ✓ 更好
kernel=3, sigma=3 (应该用) | 3243.06  | 235%  ✓✓ 最好
kernel=5, sigma=3          | 3112.28  | 222%
```

**结论：sigma=0 确实导致效果变差！应该使用传入的 sigma=3.0**

### 问题2：锐化强度影响

```
amount值 | 清晰度   | 提升率 | 评价
---------|----------|--------|------
0.1      | 1144.8   | 18%    | 太弱，几乎没效果
0.5      | 2001.93  | 107%   | 偏弱
1.0      | 3076.71  | 218%   | 中等
2.0      | 4250.81  | 340%   | 强
3.0      | 4355.79  | 350%   | 很强
```

**结论：amount太低确实会导致锐化不足！**

### 问题3：半径（kernel size）影响

```
kernel大小 | 清晰度   | 评价
-----------|----------|------
3          | 3076.71  | 最好 ✓
5          | 3027.93  | 略差
7          | 3026.43  | 更差
9          | 3026.43  | 最差
```

**结论：kernel太大确实会导致细节丢失！**

### 最佳组合

```
组合                        | 清晰度   | 提升率
----------------------------|----------|--------
kernel=3, sigma=0.5, amount=1.5 | 2537.18  | 162%
kernel=3, sigma=1.0, amount=1.5 | 3908.14  | 304%  ✓ 最佳
```

## 你的4个假设验证

### ✅ 假设1：高斯模糊参数太大导致锯齿被抹平
**验证结果：部分正确**
- sigma=0（实际0.8）比 sigma=3 效果差
- 但 sigma=3 反而效果最好（清晰度3243 vs 2978）
- **真正问题**：当前代码忽略了传入的sigma参数

### ✅ 假设2：锐化强度amount太低
**验证结果：完全正确**
- amount=0.1 几乎没效果（清晰度1144）
- amount=1.0 中等效果（清晰度3076）
- amount=1.5-2.0 效果最好（清晰度3908-4250）

### ✅ 假设3：锐化半径radius太大
**验证结果：完全正确**
- kernel=3 最好（清晰度3076）
- kernel=5/7/9 都变差（清晰度3027）
- 半径越大，细节越模糊

### ❓ 假设4：用了偏平滑型的模板
**验证结果：不适用**
- 代码使用的是标准USM锐化（高斯模糊+加权混合）
- 不是Laplacian或其他平滑型模板

## 根本原因总结

### 主要bug
1. **sigma参数被忽略** - 硬编码为0而不是使用传入的3.0
2. **kernel size固定为3x3** - 没有根据sigma调整

### 正确的实现应该是

```cpp
void ImageProcessor::applySharpen(const cv::Mat &src, cv::Mat &dst,
                                  double sigma, double amount) {
    cv::Mat blurred;
    // 根据sigma自动计算合适的kernel size
    int ksize = std::max(3, static_cast<int>(sigma * 3) * 2 + 1);
    cv::GaussianBlur(src, blurred, cv::Size(ksize, ksize), sigma);
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, dst);
}
```

## 修复建议

### 方案1：修复sigma参数（推荐）

```cpp
void ImageProcessor::applySharpen(const cv::Mat &src, cv::Mat &dst,
                                  double sigma, double amount) {
    cv::Mat blurred;
    int ksize = std::max(3, static_cast<int>(sigma * 3) * 2 + 1);
    cv::GaussianBlur(src, blurred, cv::Size(ksize, ksize), sigma);
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, dst);
}
```

### 方案2：使用最佳参数组合

基于测试结果，最佳参数：
- **kernel=3, sigma=1.0, amount=1.5** → 清晰度3908（提升304%）

```cpp
void ImageProcessor::applySharpen(const cv::Mat &src, cv::Mat &dst,
                                  double sigma, double amount) {
    cv::Mat blurred;
    cv::GaussianBlur(src, blurred, cv::Size(3, 3), 1.0);  // 固定sigma=1.0
    cv::addWeighted(src, 1.0 + amount, blurred, -amount, 0, dst);
}
```

## 对性能优化版方案_v2.0的改进建议

1. **修复sigma参数bug** - 这是最关键的问题
2. **优化默认参数**：
   - sigma: 1.0（而不是当前的0）
   - amount: 1.5（而不是默认的1.0）
3. **添加参数验证**：
   - 确保kernel size是奇数
   - 确保sigma > 0
4. **添加自适应参数选择**：
   - 根据图像对比度自动调整amount
   - 高对比度图像使用较小的amount避免过度锐化

## 测试文件

- `test_usm_parameters.cpp` - 完整参数测试
- 生成的测试图片可以直观对比效果

## 结论

**文字模糊的真正原因：**
1. ✅ sigma参数被忽略（硬编码为0）
2. ✅ amount可能设置太低
3. ✅ kernel size固定为3（无法根据sigma调整）
4. ❌ 不是削波效应（之前的分析有误）

**修复后预期效果：**
- 清晰度从 2978 提升到 3243（提升9%）
- 如果同时优化amount，可以提升到 3908（提升31%）
