# 架构与扩展

项目目录见 [首页](../README.md#目录)，算法完整文件树和阅读顺序见
[算法模块导航](../algorithms/README.md)。本文说明模块边界、运行时关系与新增方法的步骤；
公式及参数见 [算法说明](algorithm.md)，调用约定见 [C++ SDK](sdk.md) 和
[Python 接口](../bindings/python/README.md)。

## 模块职责

| 模块 | 职责 |
|---|---|
| `algorithms/` | 不依赖 Qt 或 Python 的 C++ 核心库，`include/mif/` 为公开 API |
| `apps/desktop/` | Qt 窗口、参数表单、图片读写、双预览与后台任务 |
| `bindings/python/` | nanobind 注册、NumPy 转换与 Python 包装接口 |
| `cmake/` | 依赖查找、SDK 配置、安装与运行库收集 |
| `tests/` | 核心、桌面交互和外部 SDK 回归测试；Python 测试位于绑定目录 |

链接依赖为 `mif_desktop → mif_desktop_ui → mif_core → OpenCV`，
以及 `Python mif → _mif → mif_core`。OpenCV 需包含官方引导滤波所用的 `ximgproc`。
`mif_core` 默认构建动态库，也支持静态库；交付布局见 [构建与交付](build.md#交付目录)。

## 核心流程

| 入口 | 负责的阶段 | 返回结果 |
|---|---|---|
| `registerImages()` | 校验配准配置、估计变换、重采样与裁剪共同区域 | `RegistrationResult`：原位深图像副本、`crop`、`transforms` |
| `fuse()` | 校验所选融合配置、归一化、分派方法、恢复位深与整理诊断 | `FusionResult`：`image`、可选的 `focus_indices` 与 `weights` |
| `registerAndFuse()` | 按顺序调用上述两个入口，换算总进度 | `PipelineResult`：`fusion`、`crop`、`transforms` |

内部依赖方向为 **`pipeline → fusion / registration → common`**，两个算法模块不互相引用。
`src/common/` 仅放跨阶段的输入校验、归一化、灰度转换和进度工具。
融合专用的评分、引导滤波与权重算子留在 `src/fusion/common/`，具体方法按需调用。

每个入口只校验本阶段的参数。`FusionOptions` 保存五组独立配置，只校验和执行当前选中方法；
`RegistrationOptions.method` 选择 None/Ecc/Sift，`motion_model` 仅控制 ECC。
SIFT 固定求解单应性，忽略保留的合法模型；两个枚举始终检查合法性。

输入要求同尺寸、同类型、至少两帧，支持灰度/BGR、8/16 位无符号整数和 `[0,1]` float32。
核心自身不修改输入，调用者也不能在计算时并发修改它。配准返回独立缓冲，即使关闭配准亦然。
组合入口直接使用配准阶段恢复原位深的图像，与显式两步完全相同；中间图像在返回时释放。
结果所有权、裁剪坐标和矩阵方向详见 [SDK 约定](sdk.md#三种调用方式)。

进度回调在调用线程执行。独立入口报告 0～100，组合入口将配准映射到 0～40、融合映射到 40～100，
全部完成后才报告 `done`。回调返回 `false` 抛出 `mif::Cancelled`，回调异常原样传播。
取消检查位于图像和处理阶段之间，单次 OpenCV 算子和 ECC 求解不能中途打断。
失败通过异常返回，不静默跳过图片或改用其他算法。

## 融合扩展方式

每种方法拥有独立的公开参数头、内部目录、校验函数及执行入口。执行入口接收已对齐的
`[0,1]` CV_32F 图像和本方法配置，返回 `MethodResult`：浮点融合图及方法自行生成的诊断。
公共流程不强制所有方法先生成清晰度图或权重，也不要求其支持单一空间来源图。

1. 在 `include/mif/fusion/` 新增 `<方法>_options.hpp`，定义配置、范围和默认值。
   需要现有清晰度评分时可持有 `FocusOptions`，其他方法使用自己的参数。
2. 在 `src/fusion/<方法>/` 新增同名 `.hpp/.cpp`，实现本方法的校验、权重或系数选择、融合与重建。
   较大的变换可拆成目录内私有工具，例如 DTCWT 的 `transform.hpp/.cpp`。
3. 在 `fusion_options.hpp` 添加枚举与配置成员，在 `src/fusion/fusion.cpp` 接入校验和执行分派，
   并将源文件加入 `algorithms/CMakeLists.txt`。
4. 在 Qt `FusionSettings` 中增加独立表单，在 Python 中注册枚举、配置与成员。
   保持切换存值和仅重置当前方法的语义。
5. 补充参数隔离、质量、位深、诊断、进度和取消测试；可用命令见 [验证指南](verification.md)。

来源索引和权重按原始输入顺序返回，筛帧不能改变下标；排除的帧以零权占位。
不支持来源图的方法返回空 `cv::Mat`，Python 映射为 `None`；不支持权重时返回空列表。
这些诊断不能当作物理深度。公开入口统一处理输出值域、位深和是否保留权重。
方法之间不互相调用，共用算子保持在 `fusion/common/`，不引入插件注册器或继承框架。

## 配准扩展方式

每种估计方法使用一个 `.cpp`，实现私有 `Estimator`，缓存参考图像或特征，再依次估计各源图。
估计器接收工作分辨率的归一化灰度图，返回参考到源图的 3×3 CV_64F 变换。
共同流程负责坐标换算、原始分辨率重采样、有效区域裁剪及公开矩阵格式。

1. 在 `src/registration/` 新增方法 `.cpp`，使用 `mif::detail::registration` 命名空间，
   实现 `Estimator::estimate()` 和创建函数。
2. 在本目录的 `registration.hpp` 声明工厂，在 `registration.cpp` 的 `makeEstimator()` 接入分派。
3. 在公开 `registration_options.hpp` 增加算法枚举，更新入口校验与 `algorithms/CMakeLists.txt`。
4. 接入 Qt、Python 和测试，明确新方法的公开矩阵格式。失败应抛异常，不能静默返回单位矩阵。

扩展 ECC 模型时只更新 `MotionModel`、模型校验与 `ecc.cpp` 的映射，不新增算法项或实现文件。
当前支持 ECC 与 SIFT，尚未实现 SIFT 初始化后再用 ECC 优化的组合估计方法。

## 桌面程序

`main_window.cpp` 通过 C++ 构建窗口、布局和信号连接，不保留单独的 Designer 外壳文件。
`RegistrationSettings` 与 `FusionSettings` 各自管理表单、显隐和默认值，通过 `options()` 返回完整快照；
主窗口不读取具体字段。`widgets/parameter_form.hpp` 仅复用表单布局和滚轮保护，配置职责仍各自独立。
参数页使用滚动区，运行与取消按钮固定在页外。

`MainWindow` 将两个快照交给 `FusionWorker`；后台线程读取图像并调用组合入口，不访问界面控件。
排队信号在主线程更新进度和结果。运行期间锁定参数与导入，关闭窗口时先取消，线程结束后再关闭。
当前单张输入预览的解码在界面线程进行，大图片可能造成短暂停顿。

文件按自然顺序排列并去重，第一张作为配准参考。单独添加文件继续追加；含文件夹的导入统一替换批次，
清除旧预览与结果，且不递归。空目录清空批次，取消选择不改变内容，多个目录一次性作为同批导入。

`ImageView` 用相对适应窗口的倍率和归一化中心同步两侧视野；接收端应用状态时不再广播，避免回环。
切换同批图片保留位置，新结果沿用输入视野，新批次恢复整图。裁剪后的结果按自身图像范围联动，
该交互不代替几何配准。预览转为 8 位显示，导出使用原始融合结果。
`QFile` / `QSaveFile` 配合 OpenCV 内存编解码，支持 Windows 中文路径和原子保存。

## Python 与交付

`bindings.cpp` 注册枚举、配置和处理入口，`array_utils.cpp` 负责数组与 `cv::Mat` 的转换。
包装层支持只读与非连续数组。耗时计算前复制输入和完整配置，计算期间释放 GIL。
返回数组通过 capsule 持有 `cv::Mat`，删除中间字典不会使仍被引用的数组失效。
嵌套参数引用由 nanobind 保持父对象生命周期，整体赋值采用值复制。

`mif_desktop_ui` 静态库让应用与测试复用窗口实现。`Deliverables.cmake` 定义安装规则，
`RuntimeDependencies.cmake` 扫描 Windows 实际 DLL 依赖，统一整理 Qt、Python、SDK 和示例的运行库。
第三方源码位于 `ext/` 并固定版本，生成文件与第三方代码不作为自有源码修改。
