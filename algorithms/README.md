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
│   ├── fusion_options.hpp         # 融合方法、清晰度指标与参数
│   ├── registration.hpp           # 独立配准入口 registerImages() 与 RegistrationResult
│   ├── registration_options.hpp   # 配准模式与参数
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
    │   ├── fusion.hpp             # 内部融合入口、方法声明与 MethodResult
    │   ├── fusion.cpp             # 公开 fuse()、融合参数校验、方法分派和输出整理
    │   ├── focus_measure.hpp      # 清晰度与 FocusMaps 声明
    │   ├── focus_measure.cpp      # 清晰度评分、灰度引导图与初始决策图
    │   ├── weight_map.hpp         # 权重处理函数声明
    │   ├── weight_map.cpp         # 引导滤波、归一化、通道扩展与来源索引
    │   ├── guided_filter.cpp      # 双尺度引导滤波融合
    │   └── laplacian_pyramid.cpp  # 拉普拉斯金字塔融合
    └── registration/
        ├── registration.hpp      # 内部 Estimator 约定与方法工厂
        ├── registration.cpp      # 公开 registerImages()、参数校验、变换与裁剪
        ├── ecc.cpp               # ECC 平移、仿射与单应性估计
        └── sift_homography.cpp   # SIFT 匹配与 RANSAC 单应性估计
```

`mif/export.hpp` 由 CMake 在构建目录生成并随 SDK 安装，不在上述源码树中维护。

## 各模块负责什么

- **`pipeline.cpp`** 实现 `mif::registerAndFuse()`，把独立配准的图像交给纯融合入口，
  将两阶段的进度换算为总进度，返回融合结果和配准元数据。
- **`fusion/`** 实现 `mif::fuse()`，只校验融合参数并融合已对齐的输入。
  `fusion.cpp` 处理输入归一化、方法分派及输出位深和诊断信息；
  每种方法在自己的 `.cpp` 中处理浮点图像、生成权重并重建。
- **`registration/`** 实现 `mif::registerImages()`，只校验配准参数，返回保留原位深的
  图像副本、共同裁剪区域和变换矩阵。每种估计方法各一个 `.cpp`，
  共同的坐标换算、重采样与输出整理留在 `registration.cpp`。
- **`common/`** 放共用的输入校验、归一化、灰度转换和进度工具。

依赖方向为 **`pipeline → fusion / registration → common`**。
融合与配准模块之间不互相引用；内部命名空间分别为
`mif::detail::fusion` 和 `mif::detail::registration`。

### 容易混淆的名称

| 名称 | 作用 |
|---|---|
| `include/mif/fusion.hpp` | 调用者使用的纯融合接口 |
| `src/fusion/fusion.hpp` | 项目内部的融合方法入口与结果约定 |
| `include/mif/registration.hpp` | 调用者使用的独立配准接口 |
| `src/registration/registration.hpp` | 项目内部的变换估计器约定 |
| `focus_measure.cpp` | 用改进拉普拉斯或 Tenengrad 评分，生成初始清晰区域选择；这些指标本身不是完整融合方法 |
| `weight_map.cpp` 中的 `guidedFilter()` | 平滑权重的引导滤波算子，两种融合方法都会使用 |
| `guided_filter.cpp` | 完整的双尺度融合方法，包含基础层、细节层及其权重和重建 |

## 阅读顺序

1. [纯融合接口](include/mif/fusion.hpp)、[独立配准接口](include/mif/registration.hpp) 及各自的参数头；共用回调见 [progress.hpp](include/mif/progress.hpp)。
2. [公开组合接口](include/mif/pipeline.hpp) 和 [pipeline.cpp](src/pipeline.cpp)：了解如何串联两个独立阶段。
3. 按关注点进入 [融合入口](src/fusion/fusion.cpp) 或 [配准入口](src/registration/registration.cpp)，再阅读对应的方法文件。
4. 理解具体步骤时查阅 [清晰度计算](src/fusion/focus_measure.cpp)、[权重处理](src/fusion/weight_map.cpp) 和 `common/`。

公式与参数见 [算法说明](../docs/algorithm.md)，新增方法的步骤见
[架构与扩展方式](../docs/architecture.md#融合扩展方式)。旧接口的替换方法见
[SDK 接口迁移](../docs/sdk.md#接口迁移)。
