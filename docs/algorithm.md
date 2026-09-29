# 传统多聚焦融合

本实现借鉴 OpenFocus 的“配准 → 清晰度估计 → 权重优化 → 融合”工作流。
当前提供五种独立 C++ 实现：GFF 引导滤波、DCT 块方差、DTCWT 双树复小波、
GFG-FGF，以及额外的拉普拉斯金字塔融合。
不加载神经网络或预训练模型，也不依赖 Torch、ONNX Runtime 或 CUDA。
源码位置与阅读顺序见 [算法模块导航](../algorithms/README.md)。

## 1. 输入归一化

8 位、16 位无符号整数分别除以 255、65535，在 float32 中处理。
float32 输入已经处于 `[0, 1]`。融合输出裁剪到 `[0, 1]` 后恢复原始类型。
`registerImages()` 与 `fuse()` 各自处理输入并恢复输出位深；`fuse()` 要求输入已经对齐。
`registerAndFuse()` 将配准返回的图像交给融合，与显式两步调用一致。整数配准图像
在阶段之间恢复原位深，经过一次舍入；与旧版内部始终传递浮点工作图的流程相比，
融合结果可能有少量像素差异。

GFF、金字塔与 DCT 用灰度评分，并把同一组权重应用于全部 BGR 通道。
GFG-FGF 沿用参考实现，以首图总亮度最大的通道评分；DTCWT 则独立处理各通道的
小波系数。两者的通道策略不同，遇到明显色偏或曝光差异时需检查融合结果。

## 2. 可选配准

两种方法分别位于 `algorithms/src/registration/ecc.cpp` 和 `sift_homography.cpp`。
第一张图片为参考，其余图片直接向它配准；估计图像最长边不超过 `RegistrationOptions.max_size`。
配准由 `registerImages()` 独立执行，返回图像列表、共同裁剪区域和变换矩阵。

| `method`（算法） | `motion_model`（ECC 变换模型） | 返回矩阵 |
|---|---|---|
| `None` | 不使用 | 2×3 单位变换，返回输入的独立副本 |
| `Ecc` | `Translation`：水平、垂直位移 | 2×3 |
| `Ecc` | `Affine`：平移、旋转、缩放、剪切 | 2×3 |
| `Ecc` | `Homography`：包括透视变化 | 3×3 |
| `Sift` | 不使用，SIFT + RANSAC 固定求解单应性 | 3×3 |

ECC 的三个模型使用同一求解实现，仅允许的变换形式不同。`method` 默认 `None`，
`motion_model` 默认 `Translation`；SIFT 不使用该字段，但接口仍校验枚举值是否合法。

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
当前尚无先用 SIFT 初始化、再用 ECC 优化的配准模式，也未实现配准金字塔。配准会重采样像素，保留位深
不表示像素值不发生变化。方法选择示例：

```cpp
#include <mif/registration.hpp>

mif::RegistrationOptions options;
options.method = mif::RegistrationMethod::Ecc;
options.motion_model = mif::MotionModel::Homography;
auto registered = mif::registerImages(images, options);
// registered.images 保留原位深，可保存、检查或交给 mif::fuse()。
```

```python
import mif

options = mif.RegistrationOptions()
options.method = mif.RegistrationMethod.ECC
options.motion_model = mif.MotionModel.HOMOGRAPHY
registered = mif.register_images(images, options)
result = mif.fuse(registered["images"])
```

## 3. 清晰度计算

实现位于 `algorithms/src/fusion/common/focus_measure.cpp`，供 GFF 和金字塔使用。清晰度指标只给图像局部评分，
完整融合方法还需要权重优化和图像重建。

- **改进拉普拉斯**：分别计算水平、垂直二阶差分，取绝对值之和，再做窗口均值。
- **Tenengrad**：Sobel 梯度平方和，再做窗口均值。

在每个像素选择清晰度最大的输入。相同清晰度的输入平分初始权重，平坦区域
不会固定偏向第一张图片。像素来源用 int32 存储，支持超过 256 张的图像栈。

## 4. 引导滤波权重

引导滤波权重适配位于 `algorithms/src/fusion/common/guided_filter.cpp`，调用
`cv::ximgproc::guidedFilter`，供 GFF、金字塔和 GFG-FGF 的权重细化复用。

对每张图像的决策权重 `p`，使用该图像灰度 `I` 作为引导：

```text
a = (mean(I*p) - mean(I)*mean(p)) / (var(I) + epsilon)
b = mean(p) - a*mean(I)
w = mean(a)*I + mean(b)
```

公式由 OpenCV 官方实现计算，按完整分辨率运行，不使用降采样近似。
权重裁剪到 `[0, 1]` 后按候选图像归一化；如果某处权重和接近零，则均分权重。
依赖 opencv_contrib 的 `ximgproc`，CMake 与对外 SDK 均显式声明该模块。
官方实现使用 `BORDER_REFLECT`，旧手写实现使用 `BORDER_REFLECT_101`；因此边界附近
会有输出差异，不能按逐位一致替换来理解。GFG-FGF 第一阶段滤波的是有符号响应，
直接调用官方接口保留负值，第二阶段才按权重裁剪。

所有引导滤波正则项须位于 `[1e-6, FLT_MAX]`，其中 `FLT_MAX` 是 float32 最大有限值。
更小的数可能在官方 float32 协方差计算中被舍去，使平坦区域出现除零和 NaN；超过上限
会在转换成 float 时溢出。入口显式拒绝这些值，默认参数不变。Qt 提供 `[1e-6, 1]`
的常用调节范围。这一限制只针对融合引导滤波，不改变 ECC 的收敛阈值。

## 5. 五种融合方法

融合方法位于 `algorithms/src/fusion/` 的独立子目录，每种方法拥有自己的配置类型、
校验函数、权重生成、重建及诊断过程。该目录的 `fusion.cpp` 实现公开 `fuse()`：
分派所选方法的校验和执行、归一化输入、恢复位深并搬运诊断结果。
`common/` 提供按需复用的清晰度、引导滤波及权重工具，新方法无需强制经过这些步骤。
`algorithms/src/pipeline.cpp` 仅组合独立的配准与融合入口，并换算总进度。

### 双尺度引导滤波（默认）

实现文件：`algorithms/src/fusion/guided_filter/guided_filter.cpp`。
公开参数：`GuidedFilterOptions`，通过 `FusionOptions.guided_filter` 设置。

输入图像经均值滤波得到基础层，输入减去基础层得到细节层。基础层使用较大半径、
较强正则化的引导权重，细节层使用较小半径、较弱正则化的权重。
分别加权求和后相加重建。该方法借鉴 OpenFocus 的 GFF 分层思路，但使用可选
清晰度指标、对称平局处理和明确的位深规范，并非逐项复现上游输出。

### 拉普拉斯金字塔

实现文件：`algorithms/src/fusion/laplacian_pyramid/laplacian_pyramid.cpp`，为本项目新增的融合方法。
公开参数：`LaplacianPyramidOptions`，通过 `FusionOptions.laplacian_pyramid` 设置。

输入图像构建拉普拉斯金字塔，细节权重构建高斯金字塔。每层权重重新归一化，
融合对应频带后逐层上采样重建。显式传递每层尺寸，支持奇数宽高；层数根据
图像大小自动限制。逐张构建金字塔并累加，避免同时持有全部输入金字塔。
当前从原分辨率的决策权重生成各尺度权重，不在每层重新计算清晰度。

### DCT／块方差

实现文件：`algorithms/src/fusion/dct/dct.cpp`，配置为 `FusionOptions.dct`。

按非重叠方块计算灰度方差，选择方差最大的源图，对块来源索引做两次中值滤波，
再将选块权重应用到源图重建。这里沿用 OpenFocus 的 `dct` 标识；空间域方差等价于
正交 DCT 的交流系数能量除以块像素数，但实现没有显式变换或融合 DCT 系数。

右侧、下侧不足整块的像素仍参与计算，输出保持原始尺寸。索引始终为 int32，
不会在 256 帧处截断；平坦或近似并列块平分权重。有唯一首选的块才采用中值结果。
中值滤波作用于按输入次序编号的标签，因此多帧重排可能改变结果；它也可能抹去
小于一致性窗口的清晰区域，可减小 `consistency_window`，设为 1 时关闭平滑。

### DTCWT／双树复小波

实现目录：`algorithms/src/fusion/dtcwt/`，配置为 `FusionOptions.dtcwt`。

使用首层近对称双正交滤波器和后续 Q-shift 双树滤波器，将四个实树的高频子带组合成
每层六个复方向。低频系数按输入平均；每个高频方向先比较局部幅值最大值，再做
邻域多数一致性检查，选择复系数并逆变换。三张及以上输入按给定次序逐对融合高频，
因此保留参考流程的顺序相关性。彩色图的各通道独立变换与重建。

边界延拓和有效层数由引擎处理，支持奇数尺寸与小图，输出恢复到原始大小。
该方法没有一张能描述所有尺度、方向和通道贡献的空间权重图：C++ 返回空
`focus_indices` 和 `weights`；Python 对应 `None` 和 `[]`。
运行时只使用 C++ 和 OpenCV，不调用 Python 的 dtcwt 包。

### GFG-FGF

实现文件：`algorithms/src/fusion/gfgfgf/gfgfgf.cpp`，配置为 `FusionOptions.gfgfgf`。

1. 以 Scharr 梯度平方和的全图均值评分，保留分数达到最大值指定比例的帧。
2. 计算原图与局部均值图的绝对差，小于等于阈值的响应置零。
3. 第一遍官方引导滤波平滑响应，候选帧之间比较得到决策图。
4. 第二遍官方引导滤波细化决策权重，非负归一后加权融合原图。

彩色输入用首图总亮度最大的 B/G/R 通道作评分和引导。无纹理时保留所有帧并等权融合，
避免空候选或全黑结果。被筛除的帧始终保留原始诊断位置、权重为零；来源索引不会重新编号。
此实现对应上游优先使用官方引导滤波的路径，未采用其缺少 ximgproc 时的降采样后备实现。

## 参数

### `FusionOptions`

| 参数 | 默认值 | 范围与含义 |
|---|---|---|
| `method` | `GuidedFilter` | `GuidedFilter`、`LaplacianPyramid`、`Dct`、`Dtcwt`、`Gfgfgf` |
| `guided_filter` | `GuidedFilterOptions{}` | 双尺度方法的独立配置 |
| `laplacian_pyramid` | `LaplacianPyramidOptions{}` | 金字塔方法的独立配置 |
| `dct` | `DctOptions{}` | 块方差方法的独立配置 |
| `dtcwt` | `DtcwtOptions{}` | 双树复小波方法的独立配置 |
| `gfgfgf` | `GfgfgfOptions{}` | 梯度筛帧与两阶段引导滤波的独立配置 |
| `keep_weight_maps` | `false` | 保留所选方法提供的权重诊断；DTCWT 始终为空 |

五组参数各自保存，仅校验和使用当前选中的方法。`FocusOptions` 供 GFF 和金字塔复用，
这两个方法的 `focus` 对象各自独立。切换方法不会复制其他组参数。

#### `guided_filter`：双尺度引导滤波

| 参数 | 默认值 | 范围与含义 |
|---|---|---|
| `focus.measure` | `ModifiedLaplacian` | `ModifiedLaplacian` 或 `Tenengrad` |
| `focus.window` | 9 | `[1, 255]` 内的奇数，清晰度统计窗口边长 |
| `base_radius` | 15 | `[1, 255]`，同时用于基础层分解和基础权重滤波 |
| `detail_radius` | 3 | `[1, 255]`，细节权重半径 |
| `base_epsilon` | 0.01 | `[1e-6, FLT_MAX]`，基础权重正则化 |
| `detail_epsilon` | 0.0001 | `[1e-6, FLT_MAX]`，细节权重正则化 |

#### `laplacian_pyramid`：拉普拉斯金字塔

| 参数 | 默认值 | 范围与含义 |
|---|---|---|
| `focus.measure` | `ModifiedLaplacian` | `ModifiedLaplacian` 或 `Tenengrad` |
| `focus.window` | 9 | `[1, 255]` 内的奇数，清晰度统计窗口边长 |
| `detail_radius` | 3 | `[1, 255]`，细节权重半径 |
| `detail_epsilon` | 0.0001 | `[1e-6, FLT_MAX]`，细节权重正则化 |
| `levels` | 5 | `[1, 16]`，金字塔层数上限，包含最粗层；设为 1 时直接单尺度加权 |

例如 `options.guided_filter.focus.window = 7` 只调整双尺度方法；金字塔仍使用自己的窗口。
要让两者采用相同设置，应分别赋值。现有两方法在输入及清晰度、细节参数相同时，
返回的细节权重与来源索引一致，融合图仍由各自重建流程决定。

#### `dct`：块方差

| 参数 | 默认值 | 范围与含义 |
|---|---|---|
| `block_size` | 8 | `[2, 128]`，方块边长，单位为像素 |
| `consistency_window` | 7 | `[1, 31]` 内的奇数，块索引中值窗口，单位为块；1 关闭平滑 |

#### `dtcwt`：双树复小波

| 参数 | 默认值 | 范围与含义 |
|---|---|---|
| `levels` | 4 | `[1, 16]`，最大分解层数，小图自动限制 |
| `activity_window` | 3 | `[1, 31]` 内的奇数，同时用于幅值最大值和多数一致性检查 |

#### `gfgfgf`：梯度筛帧与两阶段引导滤波

| 参数 | 默认值 | 范围与含义 |
|---|---|---|
| `difference_window` | 7 | `[1, 255]` 内的奇数，局部均值窗口 |
| `selection_ratio` | 0.15 | `[0, 1]`，筛帧分数相对最大值的比例；0 保留所有输入 |
| `difference_threshold` | 0.005 | `[0, 1]`，归一化局部差异阈值 |
| `guided_radius` | 5 | `[1, 255]`，两个阶段共用的官方引导滤波半径 |
| `guided_epsilon` | 0.3 | `[1e-6, FLT_MAX]`，两个阶段共用的正则项 |

### `RegistrationOptions`

| 参数 | 默认值 | 范围与含义 |
|---|---|---|
| `method` | `None` | `RegistrationMethod::None`、`Ecc` 或 `Sift` |
| `motion_model` | `Translation` | `MotionModel::Translation`、`Affine` 或 `Homography`；仅 ECC 使用 |
| `iterations` | 150 | `[1, 10000]`，ECC 最大迭代次数 |
| `epsilon` | 1e-5 | 有限正数，ECC 收敛阈值 |
| `max_size` | 1200 | `[16, 8192]`，工作图像最长边上限，不放大小图；开启配准时工作图短边至少为 16 |
| `max_features` | 4000 | `[64, 100000]`，SIFT 最多保留的特征数 |
| `match_ratio` | 0.75 | 有限且在 `(0, 1)`，SIFT 最近邻/次近邻距离比值阈值 |
| `ransac_threshold` | 3.0 | 有限正数，RANSAC 误差阈值，单位为工作分辨率像素 |
| `min_inlier_ratio` | 0.25 | 有限且在 `(0, 1]`，RANSAC 内点最低比例；同时至少需要 6 个内点 |

融合入口只检查选中方法的完整配置；未选中方法即使含无效值，也不影响当前计算，切换后会被校验。
配准入口仍检查 `RegistrationOptions` 的全部字段，包括当前方法未使用的字段。
纯融合不会检查或执行配准。上述枚举使用 C++ 写法，Python 对应成员名为全大写，
例如 `FusionMethod.GUIDED_FILTER`、`RegistrationMethod.ECC`、`MotionModel.HOMOGRAPHY`。
旧参数名的替换见 [接口迁移](sdk.md#接口迁移)。

## 实际限制

整个图像栈及多组浮点中间数据驻留内存，目前没有分块或磁盘缓存。噪声、反光、
曝光差异及配准误差可能影响清晰度判断，边缘可能出现光晕。
合成测试只能验证基本性质；用于显微镜或工业检测前，应使用实际图像验证质量。
当前没有 AI 算法。DCT 的块边界、DTCWT 的顺序与逐通道选择、GFG-FGF 的全局筛帧
都可能影响特定场景：局部清晰面积很小的帧可能被 GFG-FGF 排除，可将筛选比例设为 0。

