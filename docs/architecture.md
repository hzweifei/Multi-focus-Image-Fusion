# 架构与扩展

本文集中说明模块边界、[算法目录](#算法目录)、阅读顺序与新增方法的步骤。
项目目录见 [首页](../README.md#目录)，公式及参数见 [算法说明](algorithm.md)，调用约定见 [C++ SDK](sdk.md) 和
[Python 接口](../bindings/python/README.md)。

## 模块职责

| 模块 | 职责 |
|---|---|
| `algorithms/` | 不依赖 Qt 或 Python 的 C++ 核心库，`include/mif/` 为公开 API |
| `apps/desktop/` | Qt 窗口、参数表单、图片读写、双预览与后台任务 |
| `bindings/python/` | nanobind 注册、NumPy 转换与 Python 包装接口 |
| `cmake/` | SDK 配置、安装与运行库收集；依赖查找在顶层 `CMakeLists.txt` |
| `tests/` | 核心、桌面交互和外部 SDK 回归测试；Python 测试位于绑定目录 |

链接依赖为 `mif_desktop → mif_desktop_ui → mif_core → OpenCV`，
以及 `Python mif → _mif → mif_core`。OpenCV 需包含官方引导滤波所用的 `ximgproc`。
`mif_core` 默认构建动态库，也支持静态库；交付布局见 [构建与交付](build.md#交付目录)。

## 算法目录

`include/mif/` 是对外接口，`src/` 是不随 SDK 安装的内部实现。五种融合方法各用一个
`.cpp`，只有复杂的私有工具继续放在方法子目录中。

```text
algorithms/
├── CMakeLists.txt                 # 算法库构建、源文件清单与 SDK 安装
├── include/mif/
│   ├── mif.hpp                    # 统一入口，汇总配准、融合及组合接口
│   ├── fusion.hpp                 # fuse() 与 FusionResult
│   ├── fusion_options.hpp         # 内置融合参数头的汇总
│   ├── fusion/
│   │   ├── options_base.hpp       # FusionOptionsBase
│   │   ├── focus_measure_options.hpp
│   │   ├── guided_filter_fusion_options.hpp
│   │   ├── laplacian_pyramid_fusion_options.hpp
│   │   ├── block_variance_fusion_options.hpp
│   │   ├── dtcwt_fusion_options.hpp
│   │   └── gfg_fgf_fusion_options.hpp
│   ├── registration.hpp           # registerImages() 与 RegistrationResult
│   ├── registration_options.hpp   # 内置配准参数头的汇总
│   ├── registration/
│   │   ├── options_base.hpp       # RegistrationOptionsBase
│   │   ├── no_registration_options.hpp
│   │   ├── ecc_registration_options.hpp # ECC 参数与 MotionModel 枚举
│   │   └── sift_registration_options.hpp
│   ├── pipeline.hpp               # registerAndFuse() 与 PipelineResult
│   └── progress.hpp               # 进度回调与取消异常
└── src/
    ├── pipeline.cpp               # 两阶段组合与总进度换算
    ├── common/
    │   ├── image_stack.hpp/.cpp   # 图像栈校验、归一化与位深恢复
    │   ├── grayscale.hpp          # 灰度转换
    │   └── progress.hpp           # 进度报告与取消检查
    ├── fusion/
    │   ├── fusion.cpp             # 公开入口、注册查找与输入输出整理
    │   ├── registry.hpp/.cpp      # MethodResult、注册表与内置方法登记
    │   ├── guided_filter.cpp      # 双尺度引导滤波融合
    │   ├── laplacian_pyramid.cpp  # 拉普拉斯金字塔融合
    │   ├── block_variance.cpp     # 块方差融合
    │   ├── dtcwt.cpp              # 双树复小波融合
    │   ├── gfg_fgf.cpp            # GFG-FGF 融合
    │   ├── dtcwt/
    │   │   ├── transform.hpp/.cpp # 双树复小波正逆变换
    │   │   └── FILTERS.md         # 滤波器来源与实现约定
    │   ├── gfg_fgf/
    │   │   └── focus_information.hpp/.cpp # 四邻域聚焦度量与 Sobel 平局判断
    │   └── common/
    │       ├── focus_measure.hpp/.cpp    # 清晰度评分与初始决策
    │       ├── guided_filter.hpp         # 普通与快速引导滤波的声明
    │       ├── guided_filter.cpp         # 官方引导滤波的权重适配
    │       ├── fast_guided_filter.cpp    # 快速引导滤波
    │       ├── parallel_frames.hpp      # 有限帧并行与调用线程的进度、异常处理
    │       └── weight_map.hpp/.cpp       # 权重归一化与来源索引
    └── registration/
        ├── registration.cpp      # 公开入口、坐标换算、重采样与共同裁剪
        ├── registry.hpp/.cpp      # Estimator、注册表、内置登记与跳过配准
        ├── ecc.cpp               # ECC 平移、仿射或单应性估计
        └── sift_homography.cpp    # SIFT 匹配与 RANSAC 单应性估计
```

`mif/export.hpp` 由 CMake 在构建目录生成并随 SDK 安装，不在源码树中维护。
清晰度指标和引导滤波算子只承担一个处理步骤；完整融合方法还负责选图、分解或重建。
例如 `fusion/common/guided_filter.cpp` 提供滤波算子，`fusion/guided_filter.cpp` 实现双尺度融合。
普通与快速引导滤波共用 `fusion/common/guided_filter.hpp` 声明，但保留各自的 `.cpp` 实现。
普通算子使用 OpenCV `ximgproc` 并将结果裁到 `[0,1]`；快速算子采用双精度统计，
保留有符号响应，不在算子内裁剪或归一化权重。合并头文件不改变这两套数值约定。

### 阅读顺序

1. 从 [mif.hpp](../algorithms/include/mif/mif.hpp) 找到公开入口与具体参数头。
2. 阅读 [pipeline.cpp](../algorithms/src/pipeline.cpp)、[融合入口](../algorithms/src/fusion/fusion.cpp)
   和 [配准入口](../algorithms/src/registration/registration.cpp)，了解共同流程。
3. 通过 [融合注册表](../algorithms/src/fusion/registry.cpp) 或
   [配准注册表](../algorithms/src/registration/registry.cpp) 的内置登记找到方法实现。
4. 按需查看方法工具、`fusion/common/` 算子及跨阶段 `src/common/`。

## 核心流程

| 入口 | 负责的阶段 | 返回结果 |
|---|---|---|
| `registerImages()` | 校验配准配置、估计变换、重采样与裁剪共同区域 | `RegistrationResult`：原位深图像副本、`crop_region`、`transforms` |
| `fuse()` | 校验融合配置、归一化、查找方法、恢复位深与整理诊断 | `FusionResult`：`image`、可选的 `source_index_map` 与 `weight_maps` |
| `registerAndFuse()` | 顺序执行配准与融合，复用共同校验并换算总进度 | `PipelineResult`：`fusion`、`crop_region`、`transforms` |

内部依赖方向为 **`pipeline → fusion / registration → common`**，两个算法模块不互相引用。
`src/common/` 仅放跨阶段的输入校验、归一化、灰度转换和进度工具。
融合专用的评分、引导滤波与权重算子留在 `src/fusion/common/`，具体方法按需调用。

每个入口只接收本阶段的配置基类引用，**具体配置类型决定算法**。例如
`GuidedFilterFusionOptions` 直接保存双尺度融合参数，`EccRegistrationOptions` 直接保存 ECC 参数，
无需另传方法枚举。`motion_model` 只属于 ECC；SIFT 配置没有此字段，固定求解单应性。
`MotionModel` 与 `EccRegistrationOptions` 一起定义在 `registration/ecc_registration_options.hpp`。
默认调用使用 `GuidedFilterFusionOptions` 与 `NoRegistrationOptions`。

`FusionOptionsBase` 提供诊断开关 `include_weight_maps`，`RegistrationOptionsBase` 提供
共用工作尺寸上限 `max_working_dimension`。两个基类各自声明 `clone()`，由具体类型复制完整参数，
供异步任务和语言绑定持有独立快照；同步 C++ 入口借用参数，调用期间不能并发修改。
调用方可包含统一入口 `mif/mif.hpp`，也可按需包含具体参数头。

两个模块各有私有注册表，将参数的实际类型绑定到校验器与执行函数或估计器工厂。
各自的 `registry.cpp` 显式登记内置方法，首次查找时只初始化一次；查找后释放注册表锁再计算。
未知配置类型和重复注册均报错。注册接口属于核心源码，SDK 只安装公开头，
不提供对外的动态插件注册接口。

输入要求同尺寸、同类型、至少两帧，各边至少 2 像素；支持灰度/BGR、8/16 位无符号整数和有限的 `[0,1]` float32。
核心自身不修改输入，调用者也不能在计算时并发修改它。配准返回独立缓冲，即使关闭配准亦然。
组合入口执行几何配准时，使用配准阶段恢复原位深的图像，保留与显式两步相同的整数舍入。
关闭配准时，共同流程只在本次同步调用内借用输入，融合阶段仍创建独立的归一化工作图，
省去整批原图的中间副本。公开 `registerImages()` 仍返回独立副本；组合入口的结果与显式两步一致。
结果所有权、裁剪坐标和矩阵方向详见 [SDK 约定](sdk.md#三种调用方式)。

进度回调在调用线程执行。独立入口报告 0～100，组合入口将配准映射到 0～40、融合映射到 40～100，
全部完成后才报告 `done`。回调返回 `false` 抛出 `mif::Cancelled`，回调异常原样传播。
取消检查位于图像、并行小批次和处理阶段之间，单次 OpenCV 算子和 ECC 求解不能中途打断。
失败通过异常返回，不静默跳过图片或改用其他算法。

## 融合扩展方式

每种方法拥有独立的公开参数头和一个实现 `.cpp`。校验与执行函数留在该文件的匿名命名空间，
仅通过注册函数交给公共流程。执行函数接收已对齐的 `[0,1]` CV_32F 图像和本方法配置，
返回 `registry.hpp` 中的 `MethodResult`：浮点融合图及方法自行生成的诊断。
公共流程不强制所有方法先生成清晰度图或权重，也不要求其支持单一空间来源图。

1. 在 `include/mif/fusion/` 新增 `<方法>_fusion_options.hpp`，继承 `FusionOptionsBase`，
   定义参数、范围和默认值，并实现保留具体类型的 `clone()`。需要现有评分时可持有 `FocusMeasureOptions`。
2. 在 `src/fusion/` 新增 `<方法>.cpp`，实现私有校验、权重或系数选择、融合与重建。
   返回图像须为有限的、同尺寸同通道的 CV_32F；方法负责报告进度与检查取消。
   复杂工具可放在方法子目录，例如 `dtcwt/transform.hpp/.cpp`。
3. 在方法文件中提供注册函数，调用 `registerFusionMethod(validate, run)` 绑定同一配置类型的
   校验与计算函数；在 `src/fusion/registry.cpp` 声明并调用该注册函数。无需新增方法声明头或修改 `fuse()`。
4. 将方法文件列入 `algorithms/CMakeLists.txt`，在 `include/mif/fusion_options.hpp` 汇总新参数头；
   `mif/mif.hpp` 会通过汇总头提供该类型。
5. 在 Qt `FusionSettings` 中增加表单及界面内部选项，导出新配置类型；在 Python 中注册派生配置与字段。
   保持切换存值和仅重置当前方法的语义。
6. 补充注册、克隆、质量、位深、诊断、进度和取消测试；命令见 [验证指南](verification.md)。

来源索引和权重按原始输入顺序返回，筛帧不能改变下标；排除的帧以零权占位。
`source_index_map` 不支持时返回空 `cv::Mat`，Python 映射为 `None`；`weight_maps` 不支持时返回空列表。
这些诊断不能当作物理深度。公开入口统一处理输出值域、位深和是否保留权重。
方法之间不互相调用，共用算子保持在 `fusion/common/`。注册仅连接配置与方法，不持有一次任务的可变状态。

### 融合过程中的缓冲区

- GFF 和拉普拉斯金字塔在每帧权重生成后，立即释放该帧的初始决策图和引导图引用。
- GFG-FGF 在单帧内复用 `FastGuidedFilter` 的引导图均值、方差和下采样结果，分别处理 G、R 响应；
  缓存随该帧处理结束释放，仍使用双精度统计和原有边界规则。返回权重直接使用已经生成的矩阵，
  只有被筛除的原始帧需要补零权重。
- 块方差融合直接把块权重用于像素累加，来源索引在块网格上确定后展开。
  仅在 `include_weight_maps=true` 时生成逐帧的完整权重图；并列块仍均分权重，按原始帧顺序累加。

上述处理不改变方法参数或判断规则。其他方法计算时仍需要像素权重；关闭诊断仅表示不保留返回权重，
不能统一省去所有权重计算。实测方法及范围见 [性能验证](verification.md#实图性能对比)。

### 计算与并行

GFF、金字塔和 GFG-FGF 的帧间评分、权重滤波使用私有 `parallelFrames()`。
单张图像达到 512×512 的像素量且 OpenCV 允许多线程时，每批最多并行 4 帧；
达到 2048×2048 的像素量后，每批上限为 2 帧。小图、单线程及只剩一帧的批次直接顺序计算。
结果槽预先分配，每个任务只写自己的帧；跨帧决策、权重归一化及融合累加保持原输入顺序。
并行会同时保留更多滤波临时图，换取更短的耗时。

进度回调由调用线程在每批计算前，按原帧顺序报告，工作线程不调用用户回调。
回调的事件顺序保持不变，但相邻回调的间隔不能再当作单帧耗时。
取消或回调异常可在批次启动前停止；工作异常先保存，待本批全部结束后按帧序重抛。
函数返回或抛出时没有遗留计算任务。核心不修改 OpenCV 的全局线程设置。

GFG-FGF 的决策阶段按行合并 G/R 资格、最大值和主候选扫描。
唯一主候选直接获得权重 1；只有真正参与近似平局的帧及分支才计算 Sobel。
这些导数仍来自完整的同路响应图，保持原边界行为、`1e-8` 容差和最终等权归一化。

DTCWT 的滤波与方向编解码按独立输出行或分析组并行；逆变换按互不重叠的列条带划分。
每个输出像素仍按原抽头顺序使用双精度累加，跨帧系数合并和层间回调位置保持不变。
少于 256×256 输出位置的内核直接顺序执行，避免粗尺度上的调度开销。

## 配准扩展方式

每种估计方法使用一个 `.cpp`，实现 `registry.hpp` 声明的内部 `Estimator` 接口，
缓存参考图像或特征，再依次估计各源图。
估计器接收工作分辨率的归一化灰度图，返回参考到源图的 3×3 CV_64F 变换。
估计器通过 `isProjective()` 报告本次是否使用投影模型；共同流程据此选择仿射或透视重采样，
并输出 2×3 或 3×3 矩阵，不检查具体算法类型。坐标换算和有效区域裁剪也留在共同流程中。

1. 在 `include/mif/registration/` 新增 `<方法>_registration_options.hpp`，继承
   `RegistrationOptionsBase`，定义方法参数并实现 `clone()`。
2. 在 `src/registration/` 新增方法 `.cpp`，实现参数校验器、每次创建新 `Estimator` 的工厂，
   以及 `estimate()` 和 `isProjective()`。方法只估计变换，不自行裁剪或重采样原图。
3. 在方法文件提供注册函数，通过 `registerRegistrationMethod<Options>(validate, create)` 绑定配置、
   校验器与工厂；在 `src/registration/registry.cpp` 声明并调用该注册函数。`registerImages()` 入口无需修改。
4. 更新 `algorithms/CMakeLists.txt` 与公开 `registration_options.hpp` 汇总头，再接入 Qt、Python 和测试。
   验证注册、克隆、矩阵格式与失败处理；求解失败应抛异常。

`registry.cpp` 同时用 `registerRegistrationCopyMethod` 注册 `NoRegistrationOptions`：跳过求解和重采样，
公开入口由共同流程返回输入的独立副本，组合入口允许临时借用原像素。
这一处理依据注册项的 `copy_only` 标记，不检查具体参数类型；共用的 `max_working_dimension` 范围仍会被校验。

扩展 ECC 模型时只更新 `MotionModel`、模型校验与 `ecc.cpp` 的映射，不新增算法项或实现文件。
当前支持 ECC 与 SIFT，尚未实现 SIFT 初始化后再用 ECC 优化的组合估计方法。
上述扩展复用现有全局变换约定；非刚性位移场等无法用 3×3 矩阵表达的模型需另行扩展公共流程。

## 桌面程序

`main_window.cpp` 通过 C++ 构建窗口、布局和信号连接，不保留单独的 Designer 外壳文件。
`RegistrationSettings` 与 `FusionSettings` 各自管理表单、显隐和默认值。方法枚举只在界面内部选择表单；
各方法的控件保存自己的状态，`options()` 仅导出当前方法的 `unique_ptr<Base>` 配置快照。
主窗口不读取具体字段。`widgets/parameter_form.hpp` 仅复用表单布局和滚轮保护，配置职责仍各自独立。
参数页使用滚动区，运行与取消按钮固定在页外。

两个设置控件与 `FusionWorker` 的头文件只依赖所需的配置基类，不引入全部方法参数。
设置控件在各自的 `.cpp` 中包含具体参数头并创建快照；后台线程通过基类接口克隆和传递配置。

`MainWindow` 将两份配置交给 `FusionWorker`；构造函数分别调用 `clone()`，持有两份只读配置。
后台线程读取图像并调用组合入口，不访问界面控件，也不依赖原配置或参数面板的生命周期。
排队信号在主线程更新进度和结果。运行期间锁定参数与导入，关闭窗口时先取消，线程结束后再关闭。
当前单张输入预览的解码在界面线程进行，大图片可能造成短暂停顿。

文件按自然顺序排列并去重，第一张作为配准参考。单独添加文件继续追加；含文件夹的导入统一替换批次，
清除旧预览与结果，且不递归。空目录清空批次，取消选择不改变内容，多个目录一次性作为同批导入。

`ImageView` 用相对适应窗口的倍率和归一化中心同步两侧视野；接收端应用状态时不再广播，避免回环。
切换同批图片保留位置，新结果沿用输入视野，新批次恢复整图。裁剪后的结果按自身图像范围联动，
该交互不代替几何配准。预览转为 8 位显示，导出使用原始融合结果。
`QFile` / `QSaveFile` 配合 OpenCV 内存编解码，支持 Windows 中文路径和原子保存。

## Python 与交付

`bindings.cpp` 注册配置基类、具体参数类型、清晰度/运动模型枚举及处理入口；
数组转换工具放在同一文件的匿名命名空间内，集中处理 NumPy 与 `cv::Mat` 的内存所有权。
包装层支持只读与非连续数组。释放 GIL 前复制输入，并分别克隆本次使用的配置，计算期间释放 GIL。
返回数组通过 capsule 持有 `cv::Mat`，删除中间字典不会使仍被引用的数组失效。
`focus` 子参数的引用由 nanobind 保持父对象生命周期，整体赋值采用值复制。

`mif_desktop_ui` 静态库让应用与测试复用窗口实现。`Deliverables.cmake` 定义安装规则，
`RuntimeDependencies.cmake` 扫描 Windows 实际 DLL 依赖，统一整理 Qt、Python、SDK 和示例的运行库。
第三方源码位于 `ext/` 并固定版本，生成文件与第三方代码不作为自有源码修改。
