# 算法模块导航

本目录构建 `mif_core`，供 Qt、Python 和外部 C++ 程序调用。
公开接口位于 `include/mif/`；`src/` 全部为内部实现，不随 SDK 安装。

## 目录

```text
algorithms/
├── CMakeLists.txt                 # 算法库的构建与 SDK 安装规则
├── README.md                      # 本模块导航
├── include/mif/
│   ├── fusion.hpp                 # 纯融合入口 fuse() 与 FusionResult
│   ├── fusion_options.hpp         # 五种方法选择、独立配置与诊断开关
│   ├── fusion/
│   │   ├── focus_options.hpp             # 清晰度指标和可复用的 FocusOptions 类型
│   │   ├── guided_filter_options.hpp     # 双尺度引导滤波的完整参数
│   │   ├── laplacian_pyramid_options.hpp # 拉普拉斯金字塔的完整参数
│   │   ├── dct_options.hpp               # 块尺寸与一致性窗口
│   │   ├── dtcwt_options.hpp             # 复小波层数与活动度窗口
│   │   └── gfgfgf_options.hpp            # 梯度筛帧与两阶段引导滤波参数
│   ├── registration.hpp           # 独立配准入口 registerImages() 与 RegistrationResult
│   ├── registration_options.hpp   # 配准算法、ECC 变换模型与参数
│   ├── pipeline.hpp               # 组合入口 registerAndFuse() 与 PipelineResult
│   └── progress.hpp               # 共用进度回调与取消异常
└── src/
    ├── pipeline.cpp               # 顺序调用配准和融合，换算总进度
    ├── common/
    │   ├── image_stack.hpp        # 图像栈校验、归一化与位深范围声明
    │   ├── image_stack.cpp        # 两个公开入口共用的输入处理
    │   ├── grayscale.hpp          # 配准与融合共用的灰度转换
    │   └── progress.hpp           # 共用进度报告与取消检查
    ├── fusion/
    │   ├── fusion.cpp             # 公开 fuse()、方法分派、输入与输出整理
    │   ├── method_result.hpp      # 浮点图像及方法自行生成的诊断信息
    │   ├── guided_filter/
    │   │   ├── guided_filter.hpp  # 仅接收 GuidedFilterOptions 的方法入口和校验声明
    │   │   └── guided_filter.cpp  # 本方法校验、两组权重、基础/细节层融合与诊断
    │   ├── laplacian_pyramid/
    │   │   ├── laplacian_pyramid.hpp # 仅接收 LaplacianPyramidOptions 的入口和校验声明
    │   │   └── laplacian_pyramid.cpp # 本方法校验、权重、金字塔融合与诊断
    │   ├── dct/                      # 块方差选帧与中值一致性检查
    │   ├── dtcwt/                    # 双树复小波引擎、六方向系数融合
    │   ├── gfgfgf/                   # 梯度筛帧、差异响应、两阶段官方引导滤波
    │   └── common/
    │       ├── focus_measure.hpp/.cpp # 清晰度校验、评分、灰度图与初始决策图
    │       ├── guided_filter.hpp/.cpp # OpenCV ximgproc 引导滤波的权重适配
    │       └── weight_map.hpp/.cpp    # 权重归一化、通道扩展与来源索引
    └── registration/
        ├── registration.hpp      # 内部 Estimator 约定与方法工厂
        ├── registration.cpp      # 公开 registerImages()、参数校验、变换与裁剪
        ├── ecc.cpp               # 同一个 ECC 算法，按变换模型估计平移、仿射或单应性
        └── sift_homography.cpp   # SIFT 匹配与 RANSAC 单应性估计
```

`mif/export.hpp` 由 CMake 在构建目录生成并随 SDK 安装，不在上述源码树中维护。

## 各模块负责什么

- **`pipeline.cpp`** 实现 `mif::registerAndFuse()`，把独立配准的图像交给纯融合入口，
  将两阶段的进度换算为总进度，返回融合结果和配准元数据。
- **`fusion/`** 实现 `mif::fuse()`，融合已对齐的输入。`fusion.cpp` 分派当前方法的校验
  和执行，处理输入归一化、输出位深与诊断信息的保留。每种方法拥有自己的目录、
  参数类型和校验函数，接收自身配置，完成权重、重建与诊断，不读取另一种方法的参数。
  `fusion/common/` 只提供可复用算子；具体方法按需调用，公共入口不强制预先生成清晰度或权重。
- **`registration/`** 实现 `mif::registerImages()`，只校验配准参数，返回保留原位深的
  图像副本、共同裁剪区域和变换矩阵。每种估计方法各一个 `.cpp`，
  共同的坐标换算、重采样与输出整理留在 `registration.cpp`。
  `RegistrationMethod` 选择 ECC 或 SIFT；ECC 的 `MotionModel` 只改变求解模型，
  三个模型复用同一个 `ecc.cpp`，不按模型新增算法文件。SIFT 固定求解单应性。
- **`common/`** 放共用的输入校验、归一化、灰度转换和进度工具。

依赖方向为 **`pipeline → fusion / registration → common`**。
融合与配准模块之间不互相引用；内部命名空间分别为
`mif::detail::fusion` 和 `mif::detail::registration`。

### 容易混淆的名称

| 名称 | 作用 |
|---|---|
| `include/mif/fusion.hpp` | 调用者使用的纯融合接口 |
| `src/fusion/method_result.hpp` | 各融合方法内部返回结果的约定 |
| `include/mif/registration.hpp` | 调用者使用的独立配准接口 |
| `src/registration/registration.hpp` | 项目内部的变换估计器约定 |
| `fusion/common/focus_measure.cpp` | 清晰度评分与初始决策工具；指标本身不是完整融合方法 |
| `fusion/common/guided_filter.cpp` | 调用官方 `cv::ximgproc::guidedFilter` 并裁剪权重 |
| `fusion/guided_filter/guided_filter.cpp` | 完整的双尺度融合方法，包含校验、基础层、细节层及其权重和重建 |

`FocusOptions` 供 GFF 和拉普拉斯金字塔复用，两者各持有自己的 `focus` 对象。
其他方法使用各自的评分或变换规则，不强制引入 `FocusOptions`。
`FusionOptions` 只保存方法选择、各方法参数对象和输出开关；仅选中的方法参与参数校验。

## 阅读顺序

1. [纯融合接口](include/mif/fusion.hpp)、[独立配准接口](include/mif/registration.hpp) 及各自的参数头；共用回调见 [progress.hpp](include/mif/progress.hpp)。
2. [公开组合接口](include/mif/pipeline.hpp) 和 [pipeline.cpp](src/pipeline.cpp)：了解如何串联两个独立阶段。
3. 按关注点进入 [融合入口](src/fusion/fusion.cpp) 或 [配准入口](src/registration/registration.cpp)，再阅读对应的方法文件。
4. 理解具体步骤时查阅 [清晰度计算](src/fusion/common/focus_measure.cpp)、[引导滤波算子](src/fusion/common/guided_filter.cpp)、[权重处理](src/fusion/common/weight_map.cpp) 和跨模块的 `src/common/`。

公式与参数见 [算法说明](../docs/algorithm.md)，新增方法的步骤见
[架构与扩展方式](../docs/architecture.md#融合扩展方式)。旧接口的替换方法见
[SDK 接口迁移](../docs/sdk.md#接口迁移)。
