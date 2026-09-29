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
| `fusion_options.hpp` | `FusionOptions`、`FusionMethod`、`FocusMeasure` |
| `registration.hpp` | `registerImages()`、`RegistrationResult`；包含配准参数和进度约定 |
| `registration_options.hpp` | `RegistrationOptions`、`Alignment` |
| `pipeline.hpp` | `registerAndFuse()`、`PipelineResult`；包含两阶段公开接口 |
| `progress.hpp` | `ProgressCallback`、`Cancelled` |
| `export.hpp` | CMake 生成的库符号导出声明 |

默认构建动态库。可用 `MIF_BUILD_SHARED=OFF` 构建静态库，此时不会生成 mif_core DLL。
请让调用方与 SDK 的操作系统、架构、编译器 ABI、C++ 运行库和 Debug/Release 配置一致。
Windows 当前版本使用 MSVC x64。

公开接口使用 `cv::Mat`，因此开发者还需提供与 SDK 构建版本相同的 OpenCV **开发包**。
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
registration.method = mif::Alignment::FeatureHomography;
auto registered = mif::registerImages(images, registration);
// registered.images 为原位深的独立图像；crop 和 transforms 保存配准元数据。

mif::FusionOptions fusion;
fusion.keep_weight_maps = true;
auto result = mif::fuse(registered.images, fusion);
cv::Mat fused = result.image;
```

一次完成配准和融合时使用组合入口：

```cpp
#include <mif/pipeline.hpp>

mif::RegistrationOptions registration;
registration.method = mif::Alignment::FeatureHomography;
mif::FusionOptions fusion;
auto result = mif::registerAndFuse(images, registration, fusion);
cv::Mat fused = result.fusion.image;
cv::Rect crop = result.crop;
const auto& transforms = result.transforms;
```

三个入口均要求至少两张同尺寸、同类型的灰度或 BGR 图像，支持 8 位、16 位无符号整数
和 `[0, 1]` 内的有限 float32；输入不被修改。配准默认 `method = Alignment::None`，
此时返回独立副本和完整图像范围。结果类型与矩阵坐标约定见公开头文件
`mif/registration.hpp` 和 `mif/fusion.hpp` 中的说明。

每个入口的最后一个参数均可传入 `ProgressCallback`。回调在调用线程执行，
返回 `false` 抛出 `mif::Cancelled`；回调自身异常原样传播。

## 运行依赖

Windows 运行时将 `bin/` 中的 DLL 复制到调用程序旁边，或将 SDK 的 `bin/` 加入
该程序的 DLL 搜索路径。`mif_core.lib` 只用于链接，不能替代运行时 DLL。
通过 `mif::core` 链接时会自动传递头文件路径和依赖信息。
MSVC 下还会传递 `/utf-8`，以正确读取公开头文件中的中文注释；调用方源码也应使用 UTF-8。

## 接口迁移

配准配置与流程已从融合接口拆出，旧调用方按下表更新：

| 旧接口 | 新接口 |
|---|---|
| `#include <mif/options.hpp>` | 按需使用 `fusion_options.hpp`、`registration_options.hpp`；各入口头已包含对应参数 |
| `FusionOptions.alignment` | `RegistrationOptions.method` |
| `FusionOptions.alignment_iterations`、`alignment_epsilon`、`alignment_max_size` | `RegistrationOptions.iterations`、`epsilon`、`max_size` |
| `FusionOptions.alignment_max_features`、`alignment_match_ratio` | `RegistrationOptions.max_features`、`match_ratio` |
| `FusionOptions.alignment_ransac_threshold`、`alignment_min_inlier_ratio` | `RegistrationOptions.ransac_threshold`、`min_inlier_ratio` |
| 用 `fuse()` 同时配准和融合 | `registerAndFuse(images, registration, fusion)`，或显式调用两个阶段 |
| 从 `FusionResult` 读取 `crop`、`transforms` | 从 `RegistrationResult` 或 `PipelineResult` 读取；纯 `FusionResult` 仅含 `image`、`focus_indices`、`weights` |
| 组合结果的 `result.image`、`result.focus_indices`、`result.weights` | C++ 使用 `result.fusion.image`、`result.fusion.focus_indices`、`result.fusion.weights`；`crop`、`transforms` 仍在外层 |

Python 同样使用独立 `RegistrationOptions` 和 `FusionOptions`。旧的配准加融合调用
改为 `register_and_fuse(images, registration_options, fusion_options)`，返回的字典保持平坦，
包含 `image`、`focus_indices`、`weights`、`crop`、`transforms`。`fuse_detailed()` 只返回前三项，
`register_images()` 返回 `images`、`crop`、`transforms`。

公开参数和结果结构已变化，**SDK 调用方必须使用配套的新头文件与新库重新编译**。
组合流程与显式两步采用相同数据路径：整数图像配准后先恢复原位深，再交给融合。
这会引入整数舍入，与旧版配准和融合之间直接传递浮点工作图相比，可能产生少量像素差异。
