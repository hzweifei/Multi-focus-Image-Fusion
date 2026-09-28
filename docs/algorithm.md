# 传统多聚焦融合

本实现借鉴 OpenFocus 的“配准 → 清晰度估计 → 权重优化 → 融合”工作流。
当前提供两种 C++ 实现：双尺度引导滤波融合和拉普拉斯金字塔融合。
不加载神经网络或预训练模型，也不依赖 Torch、ONNX Runtime 或 CUDA。
源码位置与阅读顺序见 [算法模块导航](../algorithms/README.md)。

## 1. 输入归一化

8 位、16 位无符号整数分别除以 255、65535，在 float32 中处理。
float32 输入已经处于 `[0, 1]`。输出裁剪到 `[0, 1]` 后恢复原始类型。
灰度图直接计算清晰度，彩色图用 OpenCV BGR 到灰度转换；同一套权重作用于
全部颜色通道，避免每个通道独立选图带来的颜色不一致。

## 2. 可选配准

两种方法分别位于 `algorithms/src/registration/ecc.cpp` 和 `sift_homography.cpp`。
第一张图片为参考，其余图片直接向它配准；估计图像最长边不超过 `alignment_max_size`。

| 选项 | 方法与模型 | 返回矩阵 |
|---|---|---|
| `None` | 不配准 | 2×3 单位变换 |
| `Translation` | ECC 平移 | 2×3 |
| `Affine` | ECC 仿射 | 2×3 |
| `FeatureHomography` | SIFT + RANSAC 单应性 | 3×3 |
| `EccHomography` | ECC 单应性 | 3×3 |

### ECC

归一化灰度图经过 5×5、sigma=1.2 高斯平滑后，使用 `findTransformECC` 估计变换。
函数内部高斯窗口显式设为 1，避免重复平滑。各图从单位矩阵开始，仅在一个工作
分辨率上优化；适合已有较好初始对齐的小幅运动。参考图和源图近乎无纹理、求解异常、
相关系数非有限或非正时明确失败。正相关分数不是对齐精度保证，达到迭代上限也不代表
满足了相关系数的收敛阈值。

### 特征点单应性

工作灰度图临时转换为 8 位用于 SIFT，原始高位深图像仍用于最终输出。
参考特征只提取一次；每张源图使用 L2 两近邻匹配、Lowe 比值筛选和坐标去重，
随后由 RANSAC 求单应性矩阵。至少需要 6 个独立匹配和 6 个内点，且内点比例
达到设定阈值；拒绝近共线或过度聚集的内点。该方法可以处理有足够共同特征的
较大几何变化，严重失焦或重复纹理仍可能造成匹配失败。

### 共用流程

估计矩阵统一为“参考坐标 → 源图坐标”，按实际横纵缩放及像素中心偏移换回原始坐标。
拒绝非有限、不可逆、翻转方向或在图像内穿过投影无穷远的变换，再对非参考原图执行
一次双线性重采样。同样变换用于全 1 掩码，计算共同有效区域及最大轴对齐内接矩形，
避免边界填充值进入融合；公共矩形的短边至少为 8 像素。

两种方法都不处理局部形变或明显视差，失败会报告图片序号，不静默假设“没有运动”。
当前尚无 Homography + ECC 组合模式或配准金字塔。配准会重采样像素，保留位深
不表示像素值不发生变化。方法选择示例：

```cpp
mif::FusionOptions options;
options.alignment = mif::Alignment::FeatureHomography; // 或 EccHomography
auto result = mif::fuse(images, options);
```

```python
options = mif.FusionOptions()
options.alignment = mif.Alignment.FEATURE_HOMOGRAPHY  # 或 ECC_HOMOGRAPHY
result = mif.fuse_detailed(images, options)
```

## 3. 清晰度计算

实现位于 `algorithms/src/fusion/focus_measure.cpp`。清晰度指标只给图像局部评分，
完整融合方法还需要权重优化和图像重建。

- **改进拉普拉斯**：分别计算水平、垂直二阶差分，取绝对值之和，再做窗口均值。
- **Tenengrad**：Sobel 梯度平方和，再做窗口均值。

在每个像素选择清晰度最大的输入。相同清晰度的输入平分初始权重，平坦区域
不会固定偏向第一张图片。像素来源用 int32 存储，支持超过 256 张的图像栈。

## 4. 引导滤波权重

引导滤波算子位于 `algorithms/src/fusion/weight_map.cpp`，供两种融合方法复用。

对每张图像的决策权重 `p`，使用该图像灰度 `I` 作为引导：

```text
a = (mean(I*p) - mean(I)*mean(p)) / (var(I) + epsilon)
b = mean(p) - a*mean(I)
w = mean(a)*I + mean(b)
```

均值用方框滤波计算。权重裁剪到 `[0, 1]` 后按图像栈归一化；如果某处权重和
接近零，则均分权重。实现只需要 OpenCV 基础模块，不需要 opencv-contrib。

## 5. 两种融合方法

融合方法位于 `algorithms/src/fusion/`，每个方法文件包含权重生成和重建流程。
该目录的 `fusion.cpp` 只负责方法选择，`focus_measure.cpp` 提供共用的清晰度与初始决策图计算。
完整处理流程由 `algorithms/src/pipeline.cpp` 组织。

### 双尺度引导滤波（默认）

实现文件：`algorithms/src/fusion/guided_filter.cpp`。

输入图像经均值滤波得到基础层，输入减去基础层得到细节层。基础层使用较大半径、
较强正则化的引导权重，细节层使用较小半径、较弱正则化的权重。
分别加权求和后相加重建。该方法借鉴 OpenFocus 的 GFF 分层思路，但使用可选
清晰度指标、对称平局处理和明确的位深规范，并非逐项复现上游输出。

### 拉普拉斯金字塔

实现文件：`algorithms/src/fusion/laplacian_pyramid.cpp`，为本项目新增的融合方法。

输入图像构建拉普拉斯金字塔，细节权重构建高斯金字塔。每层权重重新归一化，
融合对应频带后逐层上采样重建。显式传递每层尺寸，支持奇数宽高；层数根据
图像大小自动限制。逐张构建金字塔并累加，避免同时持有全部输入金字塔。

## 默认参数

| 参数 | 默认值 | 含义 |
|---|---:|---|
| focus_window | 9 | 清晰度统计窗口，奇数 |
| base_radius | 15 | 基础层滤波和基础权重半径 |
| detail_radius | 3 | 细节权重半径 |
| base_epsilon | 0.01 | 基础权重正则化 |
| detail_epsilon | 0.0001 | 细节权重正则化 |
| pyramid_levels | 5 | 金字塔层数上限 |
| alignment_iterations | 150 | ECC 最大迭代次数 |
| alignment_epsilon | 1e-5 | ECC 收敛阈值 |
| alignment_max_size | 1200 | 所有配准方法的工作图像最长边 |
| alignment_max_features | 4000 | SIFT 最多保留的特征数 |
| alignment_match_ratio | 0.75 | SIFT 最近邻/次近邻距离比值阈值 |
| alignment_ransac_threshold | 3.0 | RANSAC 误差阈值，工作分辨率像素 |
| alignment_min_inlier_ratio | 0.25 | 有效匹配中 RANSAC 内点的最低比例 |

## 实际限制

整个图像栈及多组浮点中间数据驻留内存，目前没有分块或磁盘缓存。噪声、反光、
曝光差异及配准误差可能影响清晰度判断，边缘可能出现光晕。
合成测试只能验证基本性质；用于显微镜或工业检测前，应使用实际图像验证质量。
目前未实现 DCT、DTCWT、GFG-FGF 和 AI 算法。

