# C++ SDK

## 接入项目

交付目录 `outputs/<配置>/sdk/` 包含：

```text
include/mif/   公开头文件，普通调用只需包含 mif/mif.hpp
lib/           导入库或静态库；Windows Debug 库名带 d
bin/           核心 DLL 与依赖运行库
lib/cmake/Mif/  find_package(Mif) 配置
licenses/      第三方声明
```

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyFusionApp LANGUAGES CXX)
find_package(Mif CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE mif::core)
```

用 `-DCMAKE_PREFIX_PATH=<SDK目录>` 指定 SDK；OpenCV 可通过 vcpkg toolchain 或
`OpenCV_DIR` 提供。公开接口使用 `cv::Mat`，需要与构建 SDK 时版本相同、包含
`ximgproc` 的 OpenCV 开发包。SDK 附带运行库，不复制 OpenCV 头文件和导入库。

调用方与 SDK 应使用一致的架构、编译器 ABI、C++ 运行库及 Debug/Release 配置。
当前 Windows 环境使用 MSVC x64。`mif::core` 自动传递依赖、包含路径及 MSVC 的 `/utf-8`。
默认生成动态库；`MIF_BUILD_SHARED=OFF` 可生成静态库。

当前 SDK 版本为 `0.2.0`。0.x 阶段只声明同一次版本内的兼容性；可用
`find_package(Mif 0.2 CONFIG REQUIRED)` 避免误用接口不同的 0.1.x SDK。

## 三种调用方式

统一包含一个头文件，具体参数类型决定方法，无需再设置 `method`：

```cpp
#include <mif/mif.hpp>

// 1. 已对齐图像直接融合；默认使用 GFF。
auto default_result = mif::fuse(images);

// 2. 分别配准与融合，允许保存或重复使用配准结果。
mif::EccRegistrationOptions registration_options;
registration_options.motion_model = mif::MotionModel::Affine;
registration_options.max_iterations = 200;
auto registration_result = mif::registerImages(images, registration_options);

mif::BlockVarianceFusionOptions fusion_options;
fusion_options.block_size = 8;
fusion_options.include_weight_maps = true;
auto fusion_result = mif::fuse(registration_result.images, fusion_options);

// 3. 同一套配置也可直接交给组合入口。
auto pipeline_result = mif::registerAndFuse(images, registration_options, fusion_options);
cv::Mat fused = pipeline_result.fusion.image;
```

配准默认使用 `NoRegistrationOptions`：复制输入，不估计变换、不重采样。
需要 ECC 或 SIFT 时传入相应配置。`motion_model` 只存在于 ECC 配置中，
SIFT 固定估计单应性；ECC 支持 `Translation`、`Affine`、`Homography`。

三个入口均要求至少两张同尺寸、同类型的二维灰度或 BGR 图像，支持 8 位、16 位
无符号整数及值域 `[0,1]` 的有限 float32。支持非连续 ROI，各边至少 2 像素。
开启配准时工作图各边至少 16 像素，共同有效区域各边至少 8 像素。
输入不被修改，调用期间也不能被其他线程修改。整数图像配准后恢复原位深再融合，
组合入口与显式两步具有相同的量化路径。

## 参数类型

| 类型 | 主要字段 |
|---|---|
| `EccRegistrationOptions` | `motion_model`、`max_iterations`、`convergence_tolerance` |
| `SiftRegistrationOptions` | `max_features`、`match_ratio_threshold`、`ransac_reprojection_threshold`、`min_inlier_ratio` |
| `NoRegistrationOptions` | 不执行变换；保留输入副本 |
| `GuidedFilterFusionOptions` | `focus`、`base_radius`、`detail_radius`、`base_epsilon`、`detail_epsilon` |
| `LaplacianPyramidFusionOptions` | `focus`、`detail_radius`、`detail_epsilon`、`max_levels` |
| `BlockVarianceFusionOptions` | `block_size`、`consistency_window_size` |
| `DtcwtFusionOptions` | `max_levels`、`activity_window_size` |
| `GfgFgfFusionOptions` | `local_mean_window_size`、`selection_ratio`、`gfg_threshold`、`guided_radius`、`guided_epsilon`、`guided_subsample_factor` |

配准基类 `RegistrationOptionsBase` 提供 `max_working_dimension`，默认 1200，限制估计
变换时的最长边；重采样始终在原分辨率进行。融合基类 `FusionOptionsBase` 提供
`include_weight_maps`，默认关闭，只控制返回的权重诊断。二者独立，不共用一个总配置。

`FocusMeasureOptions` 包含 `measure` 与 `window_size`；GFF 和金字塔各自持有 `focus`。
窗口名以 `window_size` 结尾时表示边长，`radius` 表示半径。块方差的一致性窗口
以块为单位，小波活动度窗口以子带采样点为单位，详见对应头文件。

GFG-FGF 默认 `selection_ratio=0` 保留全部输入；`gfg_threshold=0.005` 是 GFG 响应阈值，
弱响应回退到局部均值残差。`guided_subsample_factor=4` 控制快速滤波下采样，1 表示全分辨率。
GFF/金字塔的引导滤波使用 OpenCV `ximgproc`；GFG-FGF 使用项目的快速引导滤波实现。
各方法引导滤波正则项要求为 `[1e-6, FLT_MAX]` 内的有限值；ECC 的
`convergence_tolerance` 是独立的收敛阈值。完整默认值、单位和边界见 [算法说明](algorithm.md)。

同步 C++ 入口借用 `const Base&`。后台任务可调用 `options.clone()` 保存具体类型的独立副本，
不要按值复制基类。Qt 工作线程和 Python 绑定已采用该快照方式。

## 结果与进度

| 结果 | 字段 |
|---|---|
| `FusionResult` | `image`、`source_index_map`、`weight_maps` |
| `RegistrationResult` | `images`、`crop_region`、`transforms` |
| `PipelineResult` | `fusion`、`crop_region`、`transforms`；不长期保留中间图像 |

`source_index_map` 为可选 `CV_32SC1` 诊断图，记录原始输入的零起始索引，不是物理深度或置信度。
GFF/金字塔记录最大细节权重的来源，块方差记录选块来源，GFG-FGF 记录最大最终权重的来源。
`weight_maps` 仅在 `include_weight_maps=true` 且方法支持时返回，按原始输入顺序排列；
被筛除的帧保留零权重占位。GFF/金字塔返回细节权重，不含全部中间权重。
DTCWT 在尺度和方向上选择系数，来源图与空间权重均为空，使用诊断前应检查。

`crop_region` 位于第一张原始输入的坐标系中。`transforms[i]` 将参考图原始坐标映射到
第 i 张源图原始坐标；矩阵不含裁剪偏移，映射裁剪后像素前应加上裁剪区域的左上角。
None/ECC 平移与仿射返回 2×3 float32 矩阵，ECC 单应性/SIFT 返回 3×3 矩阵。
3×3 映射需要齐次除法。第一张输入对应单位矩阵。

结果不引用输入缓冲区，但复制结果结构会共享 `cv::Mat` 数据；需要独立可写副本时使用 `clone()`。
进度回调在调用线程同步执行，返回 `false` 抛出 `mif::Cancelled`，回调自身异常原样传播。
取消只在检查点生效，不能中断正在执行的单次 OpenCV 操作。

## 头文件与扩展

普通调用使用 `<mif/mif.hpp>`；也可按需包含 `fusion.hpp`、`registration.hpp`、`pipeline.hpp`。
`fusion_options.hpp` 和 `registration_options.hpp` 是内置参数汇总头。
具体参数头位于 `fusion/*_fusion_options.hpp`、`registration/*_registration_options.hpp`；
两条体系的基类位于各自的 `options_base.hpp`，清晰度配置为 `fusion/focus_measure_options.hpp`。
`MotionModel` 定义在 `registration/ecc_registration_options.hpp`，与 ECC 参数一起提供。
`progress.hpp` 提供回调与取消异常，`export.hpp` 由 CMake 生成。

新增源码内的方法只需定义参数、实现算法并加入显式注册清单，统一处理入口无需增加分支。
注册表是内部扩展接口，不是动态插件 ABI，也不随 SDK 安装；步骤见 [架构与扩展](architecture.md)。

## 接口迁移

这次移除了聚合式 `FusionOptions`、`RegistrationOptions` 及核心方法枚举。用对应的具体参数
类型替换旧配置，直接设置该类型的字段，例如 `options.dct.block_size` 改为
`BlockVarianceFusionOptions options; options.block_size = 8;`。不保留旧字段转发别名。

| 原名称 | 新名称 |
|---|---|
| `DctOptions` | `BlockVarianceFusionOptions`，反映实际块方差实现 |
| `FocusOptions.window` | `FocusMeasureOptions.window_size` |
| ECC `iterations` / `epsilon` | `max_iterations` / `convergence_tolerance` |
| 配准 `max_size` | `max_working_dimension` |
| SIFT `match_ratio` / `ransac_threshold` | `match_ratio_threshold` / `ransac_reprojection_threshold` |
| `levels` | `max_levels` |
| `consistency_window` / `activity_window` | `consistency_window_size` / `activity_window_size` |
| `difference_window` / `difference_threshold` / `guided_subsample` | `local_mean_window_size` / `gfg_threshold` / `guided_subsample_factor` |
| `keep_weight_maps` | `include_weight_maps` |
| `focus_indices` / `weights` / `crop` | `source_index_map` / `weight_maps` / `crop_region` |

原先单独包含 `<mif/registration/motion_model.hpp>` 的代码，应改为
`<mif/registration/ecc_registration_options.hpp>` 或统一入口 `<mif/mif.hpp>`。
旧的独立枚举头已移除；`mif::MotionModel` 的名称、枚举值和调用方式保持不变。

SDK 调用方应使用配套的新头文件与新库重新编译。Windows 将 `bin/` 中的 DLL 放到调用程序旁，
或加入该程序的 DLL 搜索路径；导入库不能替代 DLL。Python 包的包装文件、扩展和核心库也需同步更新。
Python 使用相同类型和字段名，函数采用下划线命名；返回字典见 [Python 接口](../bindings/python/README.md)。
