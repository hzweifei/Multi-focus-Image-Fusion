# C++ SDK

目录结构：

```text
include/mif/   融合、配准、组合流程及参数和进度的公开头文件
lib/           mif_core.lib（Windows DLL 的导入库；Debug 为 mif_cored.lib）
bin/           mif_core.dll 和依赖的运行库（Debug 为 mif_cored.dll）
lib/cmake/Mif/  find_package(Mif) 所需的配置
licenses/      第三方声明
```

公开头文件按调用职责划分：

| 头文件 | 内容 |
|---|---|
| `fusion.hpp` | `fuse()`、`FusionResult`；包含融合参数和进度约定 |
| `fusion_options.hpp` | `FusionOptions`、`FusionMethod`；包含五种方法的参数头 |
| `fusion/focus_options.hpp` | `FocusOptions`、`FocusMeasure` |
| `fusion/guided_filter_options.hpp` | `GuidedFilterOptions` |
| `fusion/laplacian_pyramid_options.hpp` | `LaplacianPyramidOptions` |
| `fusion/dct_options.hpp` | `DctOptions` |
| `fusion/dtcwt_options.hpp` | `DtcwtOptions` |
| `fusion/gfgfgf_options.hpp` | `GfgfgfOptions` |
| `registration.hpp` | `registerImages()`、`RegistrationResult`；包含配准参数和进度约定 |
| `registration_options.hpp` | `RegistrationOptions`、`RegistrationMethod`、`MotionModel` |
| `pipeline.hpp` | `registerAndFuse()`、`PipelineResult`；包含两阶段公开接口 |
| `progress.hpp` | `ProgressCallback`、`Cancelled` |
| `export.hpp` | CMake 生成的库符号导出声明 |

默认构建动态库。可用 `MIF_BUILD_SHARED=OFF` 构建静态库，此时不会生成 mif_core DLL。
请让调用方与 SDK 的操作系统、架构、编译器 ABI、C++ 运行库和 Debug/Release 配置一致。
Windows 当前版本使用 MSVC x64。

公开接口使用 `cv::Mat`，因此开发者还需提供与 SDK 构建版本相同的 OpenCV **开发包**。
开发包必须包含 `opencv_contrib` 的 `ximgproc` 模块，用于官方引导滤波。
`find_package(Mif)` 会按 SDK 构建时的版本查找 OpenCV，包括 `ximgproc`。
SDK 中包含运行所需 DLL，不复制 OpenCV 的头文件或第三方导入库。

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyFusionApp LANGUAGES CXX)
find_package(Mif CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE mif::core)
```

配置时用 `-DCMAKE_PREFIX_PATH=<SDK目录>`，并按自己的依赖管理方式设置 OpenCV。
例如 vcpkg 用户同时指定自己的 toolchain。无需手写源项目路径。

## 三种调用方式

图像已对齐时直接调用纯融合入口：

```cpp
#include <mif/fusion.hpp>

cv::Mat combineAligned(const std::vector<cv::Mat>& images) {
    return mif::fuse(images).image;
}
```

需要查看、保存或重复使用配准图像时，分别调用两步：

```cpp
#include <mif/fusion.hpp>
#include <mif/registration.hpp>

mif::RegistrationOptions registration;
registration.method = mif::RegistrationMethod::Ecc;
registration.motion_model = mif::MotionModel::Affine;
auto registered = mif::registerImages(images, registration);
// registered.images 为原位深的独立图像；crop 和 transforms 保存配准元数据。

mif::FusionOptions fusion;
fusion.keep_weight_maps = true;
fusion.guided_filter.focus.window = 7;
fusion.guided_filter.base_radius = 15;
auto result = mif::fuse(registered.images, fusion);
cv::Mat fused = result.image;
```

一次完成配准和融合时使用组合入口：

```cpp
#include <mif/pipeline.hpp>

mif::RegistrationOptions registration;
registration.method = mif::RegistrationMethod::Sift;
mif::FusionOptions fusion;
auto result = mif::registerAndFuse(images, registration, fusion);
cv::Mat fused = result.fusion.image;
cv::Rect crop = result.crop;
const auto& transforms = result.transforms;
```

三个入口均要求至少两张同尺寸、同类型的灰度或 BGR 图像，支持 8 位、16 位无符号整数
和 `[0, 1]` 内的有限 float32；输入不被修改。配准默认 `method = RegistrationMethod::None`，
此时返回独立副本和完整图像范围。结果类型与矩阵坐标约定见公开头文件
`mif/registration.hpp` 和 `mif/fusion.hpp` 中的说明。

`method` 只选择算法；`motion_model` 只指定 ECC 的平移、仿射或单应性模型，
默认 `MotionModel::Translation`。SIFT 固定求解单应性，忽略保留的合法模型值；
关闭配准时也不使用该字段。两个枚举的非法值均会被拒绝。

融合参数按方法保存，仅选中方法参与计算和校验：

| `FusionMethod` | 配置成员 | 参数 |
|---|---|---|
| `GuidedFilter` | `guided_filter` | `focus`、基础/细节滤波半径与正则项 |
| `LaplacianPyramid` | `laplacian_pyramid` | `focus`、细节滤波半径与正则项、`levels` |
| `Dct` | `dct` | `block_size`、`consistency_window` |
| `Dtcwt` | `dtcwt` | `levels`、`activity_window` |
| `Gfgfgf` | `gfgfgf` | `difference_window`、`selection_ratio`、`difference_threshold`、`guided_radius`、`guided_epsilon`、`guided_subsample` |

GFG-FGF 默认 `selection_ratio=0` 保留全部输入；`difference_threshold=0.005` 表示
类高斯四邻域梯度阈值，弱梯度位置使用局部均值残差。`guided_subsample` 范围 `[1,16]`、
默认 4；1 使用完整分辨率，其余值在缩小的图上估计引导滤波系数。
窗口 7、半径 5、正则项 0.3 和下采样倍数 4 为项目默认值，论文没有完整给出这些设置。

GFF 的 `base_epsilon`、`detail_epsilon`，金字塔的 `detail_epsilon`，以及 GFG-FGF 的
`guided_epsilon` 均须为 `[1e-6, FLT_MAX]` 范围内的有限数；`FLT_MAX` 约为 `3.4e38`。
该范围适应官方引导滤波的浮点精度，防止平坦区域除零或转换为 float32 时溢出；
ECC 的 `RegistrationOptions.epsilon` 是独立的收敛阈值。Qt 融合界面采用 `[1e-6, 1]`
作为常用调参范围。

```cpp
mif::FusionOptions options;
options.guided_filter.focus.measure = mif::FocusMeasure::Tenengrad;
options.guided_filter.focus.window = 7;
options.guided_filter.detail_radius = 2;
options.laplacian_pyramid.focus.window = 11;
options.laplacian_pyramid.levels = 4;
options.dct.block_size = 8;
options.dct.consistency_window = 7;
options.method = mif::FusionMethod::Dct;
auto result = mif::fuse(images, options); // 仅使用 dct 配置，其他方法的值保留。
```

`focus_indices` 和 `weights` 是方法提供的可选诊断，使用前应检查是否为空：

| 方法 | `focus_indices` | `keep_weight_maps=true` 时的 `weights` |
|---|---|---|
| GFF、拉普拉斯金字塔 | 最大细节权重的来源索引 | 归一化细节权重 |
| DCT 块方差 | 经过一致性处理的块来源索引 | 选块权重 |
| GFG-FGF | 最大最终权重的来源索引 | 最终融合权重，排除帧为全零 |
| DTCWT | 空 `cv::Mat` | 空 `std::vector` |

索引从零开始并对应原始输入顺序；权重存在时也按该顺序排列。关闭权重保留时，
`weights` 一律为空。DTCWT 在多个尺度和方向选择系数，开启诊断也不会生成单一来源图。

每个入口的最后一个参数均可传入 `ProgressCallback`。回调在调用线程执行，
返回 `false` 抛出 `mif::Cancelled`；回调自身异常原样传播。

## 运行依赖

Windows 运行时将 `bin/` 中的 DLL 复制到调用程序旁边，或将 SDK 的 `bin/` 加入
该程序的 DLL 搜索路径。`mif_core.lib` 只用于链接，不能替代运行时 DLL。
通过 `mif::core` 链接时会自动传递头文件路径和依赖信息。
MSVC 下还会传递 `/utf-8`，以正确读取公开头文件中的中文注释；调用方源码也应使用 UTF-8。

## 接口迁移

### 新增方法与官方引导滤波

`FusionOptions` 新增 `dct`、`dtcwt`、`gfgfgf` 配置。`FusionMethod::GuidedFilter=0`
和 `LaplacianPyramid=1` 保持原值，新方法在其后追加。参数结构的大小已改变，
**SDK 消费方必须使用配套的新头文件和新库重新编译，不承诺二进制兼容**。

引导滤波已改用 OpenCV `ximgproc`，边界处理遵循该实现；与旧版本不承诺逐像素相同。
部署时应更新 OpenCV 开发包及运行库，不能只替换 `mif_core.dll`。
读取诊断时应允许空值，尤其是 DTCWT 的空索引和空权重。

### 融合方法独立参数

`FusionOptions` 不再直接包含清晰度和滤波数值字段，改为持有各方法的独立配置。
原来的扁平属性已移除，不提供转发别名。根据当时使用的方法迁移：

| 旧字段 | 双尺度引导滤波 | 拉普拉斯金字塔 |
|---|---|---|
| `focus_measure` | `guided_filter.focus.measure` | `laplacian_pyramid.focus.measure` |
| `focus_window` | `guided_filter.focus.window` | `laplacian_pyramid.focus.window` |
| `detail_radius` | `guided_filter.detail_radius` | `laplacian_pyramid.detail_radius` |
| `detail_epsilon` | `guided_filter.detail_epsilon` | `laplacian_pyramid.detail_epsilon` |
| `base_radius` | `guided_filter.base_radius` | 不使用 |
| `base_epsilon` | `guided_filter.base_epsilon` | 不使用 |
| `pyramid_levels` | 不使用 | `laplacian_pyramid.levels` |

`method`、`keep_weight_maps` 和处理入口保持原名。Python 使用相同的成员路径，
也可单独创建 `mif.FocusOptions()`、`mif.GuidedFilterOptions()`、`mif.LaplacianPyramidOptions()`。
两组配置独立存值；原来依赖一组数值控制两种方法的代码，现在需要分别赋值。
计算只校验当前方法，未选中配置中的非法值不会影响当前处理，切换后才会被拒绝。
这张迁移表适用于原有两种方法；新增方法使用自己的参数结构和诊断语义。

### 配准与融合分阶段

配准配置与流程已从融合接口拆出，旧调用方按下表更新：

| 旧接口 | 新接口 |
|---|---|
| `#include <mif/options.hpp>` | 按需使用 `fusion_options.hpp`、`registration_options.hpp`；各入口头已包含对应参数 |
| `FusionOptions.alignment` | `RegistrationOptions.method` 与 `motion_model`，对应关系见下表 |
| `FusionOptions.alignment_iterations`、`alignment_epsilon`、`alignment_max_size` | `RegistrationOptions.iterations`、`epsilon`、`max_size` |
| `FusionOptions.alignment_max_features`、`alignment_match_ratio` | `RegistrationOptions.max_features`、`match_ratio` |
| `FusionOptions.alignment_ransac_threshold`、`alignment_min_inlier_ratio` | `RegistrationOptions.ransac_threshold`、`min_inlier_ratio` |
| 用 `fuse()` 同时配准和融合 | `registerAndFuse(images, registration, fusion)`，或显式调用两个阶段 |
| 从 `FusionResult` 读取 `crop`、`transforms` | 从 `RegistrationResult` 或 `PipelineResult` 读取；纯 `FusionResult` 仅含 `image`、`focus_indices`、`weights` |
| 组合结果的 `result.image`、`result.focus_indices`、`result.weights` | C++ 使用 `result.fusion.image`、`result.fusion.focus_indices`、`result.fusion.weights`；`crop`、`transforms` 仍在外层 |

### 配准算法与模型

旧 `Alignment` 枚举已移除；不再把 ECC 的三种模型放进算法枚举。
无论旧枚举来自 `FusionOptions.alignment` 还是 `RegistrationOptions.method`，均按下表迁移：

| 旧 `Alignment` 成员 | 新 `RegistrationMethod` | 新 `MotionModel` |
|---|---|---|
| `None` | `None` | 不使用，保留默认值即可 |
| `Translation` | `Ecc` | `Translation` |
| `Affine` | `Ecc` | `Affine` |
| `EccHomography` | `Ecc` | `Homography` |
| `FeatureHomography` | `Sift` | 不使用，SIFT 固定单应性 |

例如，旧 `options.method = mif::Alignment::EccHomography` 改为：

```cpp
options.method = mif::RegistrationMethod::Ecc;
options.motion_model = mif::MotionModel::Homography;
```

Python 对应 `options.method = mif.RegistrationMethod.ECC` 和
`options.motion_model = mif.MotionModel.HOMOGRAPHY`。旧整数枚举值不能直接转换为新值。
算法内部仍沿用原来的求解、重采样和裁剪流程。

Python 同样使用独立 `RegistrationOptions` 和 `FusionOptions`。旧的配准加融合调用
改为 `register_and_fuse(images, registration_options, fusion_options)`，返回的字典保持平坦，
包含 `image`、`focus_indices`、`weights`、`crop`、`transforms`。`fuse_detailed()` 只返回前三项，
`register_images()` 返回 `images`、`crop`、`transforms`。

公开参数和结果结构已变化，**SDK 调用方必须使用配套的新头文件与新库重新编译**。
更新 Python 包时也应同时更新包装文件、扩展模块和核心库，避免混用新旧接口。
组合流程与显式两步采用相同数据路径：整数图像配准后先恢复原位深，再交给融合。
这会引入整数舍入，与旧版配准和融合之间直接传递浮点工作图相比，可能产生少量像素差异。
