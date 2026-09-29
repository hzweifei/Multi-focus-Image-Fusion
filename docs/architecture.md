# 项目结构

```text
algorithms/       C++ 算法库，include/mif 为公开接口，src 为内部实现
apps/desktop/     Qt Widgets 桌面程序、图片读写、预览和后台任务
bindings/python/  nanobind 扩展、Python 包和接口测试
ext/              固定版本的第三方 Git 子模块
cmake/            第三方依赖查找、SDK 配置和交付文件整理
tests/            C++ 算法与桌面程序回归测试
examples/         C++ 与 Python 调用示例
data/samples/     测试图片说明
docs/             架构、算法、构建和参考来源
build/            本机构建中间文件，不纳入版本控制
outputs/          按配置整理的 Qt 程序、Python 包、C++ SDK 和示例
```

依赖方向：`mif_desktop → mif_desktop_ui → mif_core → OpenCV`；
`Python mif → _mif → mif_core`。核心库不依赖 Qt、Python 或模型推理框架。

## 核心接口

```cpp
#include <mif/pipeline.hpp>

// 配准和融合分别配置，也可分别调用。
mif::RegistrationOptions registration;
registration.method = mif::Alignment::FeatureHomography;
mif::FusionOptions fusion;
fusion.method = mif::FusionMethod::GuidedFilter;

auto registered = mif::registerImages(images, registration);
auto result = mif::fuse(registered.images, fusion);

// 一次完成两步时使用组合入口，融合结果位于 combined.fusion。
auto combined = mif::registerAndFuse(images, registration, fusion);
```

输入是至少两张同尺寸、同类型的 `cv::Mat`，灰度或 BGR 三通道。
支持 `CV_8U`、`CV_16U`、`CV_32F`。浮点输入必须位于 `[0, 1]` 且不含
NaN/Inf。不自动缩放不同尺寸的输入，也不自动丢弃透明通道。
调用期间调用者不能并发修改输入；核心函数自身不修改输入。

三个入口各自对应一个结果类型：

- `registerImages()` 返回 **`RegistrationResult`**：`images` 是保留输入位深与通道数的
  配准图像，`crop` 是共同有效区域，`transforms` 是逐图变换矩阵。
  图像使用独立缓冲，即使 `method = Alignment::None` 也返回输入的独立副本。
- `fuse()` 返回 **`FusionResult`**：`image` 保留传入图像的尺寸、位深与通道数；
  `focus_indices` 是主导细节权重来源的零起始 `int32` 索引；`weights` 仅在
  `keep_weight_maps` 开启时保留。这些诊断信息不能当作经过标定的物理深度图。
- `registerAndFuse()` 返回 **`PipelineResult`**：`fusion` 保存完整 `FusionResult`，
  `crop` 与 `transforms` 保存配准元数据；中间配准图像在函数返回时释放。

`crop` 使用第一张原图的坐标。CV_32F 变换矩阵把参考原图坐标映射到各源图坐标，
None/Translation/Affine 为 2×3，FeatureHomography/EccHomography 为 3×3。
将裁剪后的输出坐标映射到源图时，先加上 `crop` 偏移，再应用矩阵；单应性需除以齐次分母。

`FusionOptions` 和 `RegistrationOptions` 各由对应入口独立校验，组合层不增加参数对象。
组合入口直接调用这两个公开函数，与显式两步一致；整数配准图像先恢复原位深，
再进入融合。旧调用方式和像素舍入变化见 [接口迁移](sdk.md#接口迁移)。

进度回调在调用线程执行，返回 `false` 会抛出 `mif::Cancelled`。
两个独立入口均报告 0～100；组合入口将配准映射到 0～40、融合映射到 40～100，
仅全部完成时报告 `done`。回调自身抛出的异常原样传播。
取消检查发生在图像和处理阶段之间；单次 OpenCV 算子及 ECC 求解不能中途打断。
错误通过异常返回，不静默跳过失败图片或配准失败。

## 算法模块

完整目录、文件职责与阅读顺序统一放在 [算法模块导航](../algorithms/README.md)。
`algorithms/src/pipeline.cpp` 只组合两个公开入口；`fusion/` 负责纯融合的完整流程，
`registration/` 负责独立配准的完整流程。二者共用 `common/` 的输入校验、归一化、
灰度转换和进度工具。
依赖方向为 `pipeline → fusion / registration → common`，两个算法模块不互相引用。

## 融合扩展方式

方法接收已对齐的 `[0, 1]`、CV_32F 灰度或 BGR 图像，返回 `MethodResult`：
浮点融合图像和逐像素归一化的细节权重。公开入口统一处理输出值域、位深、
来源索引与可选权重保留；各方法复用 `common/progress.hpp`，沿用进度和取消约定。

以后增加方法时：

1. 在 `algorithms/src/fusion/` 新增一个方法 `.cpp`，使用 `mif::detail::fusion` 命名空间。
2. 在本目录的 `fusion.hpp` 声明方法，在 `fusion.cpp` 的 `run()` 中添加分支。
3. 在 `fusion_options.hpp` 追加 `FusionMethod` 枚举，更新 `fusion/fusion.cpp` 的入口校验和 `algorithms/CMakeLists.txt`。
4. 接入 Qt 方法选项、Python 枚举与所需参数，补充融合质量、位深、权重和取消测试。

现有 GFF 实现借鉴 OpenFocus 的分层思路，拉普拉斯金字塔为本项目新增。
OpenFocus 另有 DCT、DTCWT、GFG-FGF，当前项目尚未实现；接入时应明确各方法
的诊断权重含义，不能直接假定它们都使用相同的清晰度计算或引导滤波步骤。

## 配准扩展方式

一种估计方法一个实现文件，方法类留在各自文件内部。每次启用配准时创建一个 `Estimator`，
缓存参考图或参考特征，依次估计其他图像到同一参考坐标系的关系。所有估计器接收
工作分辨率的归一化灰度图，返回参考到源图的 3×3 CV_64F 矩阵；公共流程转换为
原图坐标，仅对原始分辨率图像执行一次重采样。公开结果按配准模式返回 2×3 或 3×3 矩阵。

以后增加方法时：

1. 在 `algorithms/src/registration/` 新增方法 `.cpp`，使用 `mif::detail::registration` 命名空间，实现 `Estimator::estimate()` 和创建函数。
2. 在本目录的 `registration.hpp` 声明工厂，在 `registration.cpp` 的 `makeEstimator()` 中分派。
3. 在 `registration_options.hpp` 追加 `Alignment` 枚举，更新 `registration/registration.cpp` 的入口校验，并在 `algorithms/CMakeLists.txt` 加入源文件。
4. 接入 Qt 的枚举数据、Python 绑定和相应测试；明确新方法的输出矩阵格式。

新方法应只负责估计变换；复用共同区域裁剪和坐标处理，避免各实现的矩阵方向不一致。
失败必须抛异常，不能静默返回单位矩阵。当前支持 ECC 与 SIFT 两类估计方法，
先用 SIFT 初始化再用 ECC 优化的配准模式尚未实现。

## 桌面程序

Qt Designer 的 `main_window.ui` 定义主窗口外框；控件、信号和布局由
`main_window.cpp` 组织。图片可通过文件对话框、文件夹或拖放导入，按自然顺序
排列并去重，列表第一张作为配准参考。单独添加文件继续追加；包含文件夹的导入
作为一个新批次，统一替换旧列表、旧预览和旧融合结果。目录导入不递归，空目录
也会清空上一批次；取消选择不改变当前内容。同时拖入多个目录时仅替换一次。

“处理设置”分为可滚动的“配准”和“融合”页，“开始融合”按钮固定在参数页外。
配准页先选方法：关闭时隐藏参数；ECC 显示迭代上限和收敛阈值；SIFT 显示特征点上限、
匹配距离比、RANSAC 阈值和最低内点比。启用配准后均显示共用的“工作最长边”。
切换方法保留已填写的值；“恢复默认参数”重置全部配准参数并保留当前方法，
融合页通过“恢复融合默认值”恢复设置。参数控件忽略滚轮改值，由父滚动区处理页面滚动。

`widgets/registration_settings.hpp/.cpp` 中的 `RegistrationSettings` 管理配准控件、
按方法显隐和恢复默认；`options()` 返回包含全部字段的 `RegistrationOptions` 快照。
`MainWindow` 启动任务时读取完整配准快照和独立融合配置，一并传入 `FusionWorker`；
后台线程不读取界面控件。`FusionWorker` 读取整组图片并调用组合入口，通过排队信号
更新主线程中的进度和结果。关闭窗口时先请求取消，线程结束后关闭。选中单张图片的预览解码
目前在界面线程进行，特别大的图片可能造成短暂停顿。

`ImageView` 的滚轮缩放、拖动、滚动条平移、双击和适应窗口按钮在两个预览之间
双向联动。同步状态使用相对适应窗口的倍率及图像中的相对中心位置，各面板根据
自身大小显示；接收端应用状态时不再发送导航信号，避免回环。
切换同批次的输入图片保留对比位置，新结果沿用当前输入预览的视野；导入新批次
恢复整图预览。配准后的裁剪结果按自身图像范围联动，这一交互不代替几何配准。
预览转换成 8 位显示，导出使用
原始融合结果；16 位 PNG/TIFF 与浮点 TIFF 的保存不会使用预览数据。
`QFile`/`QSaveFile` 配合 OpenCV 内存编解码，支持 Windows 中文路径和原子保存。

## Python

`bindings.cpp` 注册两组参数及配准、融合和组合入口；`array_utils.cpp` 负责数组转换。
耗时计算前复制输入与对应配置，配准和融合计算期间释放 GIL。返回 NumPy 数组通过 capsule 持有
`cv::Mat`，其生命周期与返回数组一致。包装层支持只读和非连续数组，校验 dtype，
颜色顺序为 BGR。Python 包构建入口放在仓库根目录，方便同时打包算法与子模块。

| Python 入口 | 返回值 |
|---|---|
| `fuse(images, options=None)` | 融合图像数组 |
| `fuse_detailed(images, options=None)` | `image`、`focus_indices`、`weights` 字典 |
| `register_images(images, options=None)` | `images`、`crop`、`transforms` 字典 |
| `register_and_fuse(images, registration_options=None, fusion_options=None)` | `image`、`focus_indices`、`weights`、`crop`、`transforms` 平坦字典 |

Python 的组合结果不嵌套 `fusion` 字段；图像数组与配准矩阵的坐标约定和 C++ 一致。

## 第三方依赖

`ext/nanobind` 固定版本，只有开启 Python 绑定时才加入构建。Qt 和 OpenCV 由
已有开发环境提供。`mif_core` 默认构建动态库，也可通过 `MIF_BUILD_SHARED=OFF`
构建静态库。父项目可通过 `add_subdirectory` 使用 `mif::core`，或使用交付 SDK
中的 `find_package(Mif CONFIG REQUIRED)`。构建后自动整理到 `outputs/<配置>/`，
详情见 [交付目录](outputs.md)。

## 中文注释

代码阅读顺序见 [算法模块导航](../algorithms/README.md#阅读顺序)。

自有代码采用以下注释方式：

- 公开接口说明输入、输出、参数范围和重要前置条件。
- 算法实现说明公式、坐标变换、边界和数值处理的原因。
- Qt 代码说明信号槽在哪个线程执行，以及窗口、工作线程、图像数据的生命周期。
- Python 绑定说明 GIL、数组布局、颜色顺序、复制与共享内存的边界。
- 测试说明它验证的性质，构建脚本说明依赖关系和构建/安装阶段的区别。

JSON 预设文件通过 `description` 字段解释用途，避免使用 JSON 不支持的注释语法。
生成文件和第三方子模块保留工具或上游的内容。

## 结构边界

配准、融合分别拥有公开入口、参数和结果类型，`pipeline.cpp` 仅负责顺序组合与进度换算。
清晰度和权重工具归入 `fusion/`，输入处理与进度工具归入 `common/`，不增加通用流程框架。

Python 绑定入口集中在 `bindings.cpp`，数组转换单独维护；`mif_desktop_ui` 静态库
让应用与测试复用窗口实现。`Deliverables.cmake` 定义安装规则，
`RuntimeDependencies.cmake` 在安装阶段扫描实际 DLL；`mif_install_runtime` 统一处理
核心库和第三方运行依赖，从 CMake 目标类型推导扫描方式。

