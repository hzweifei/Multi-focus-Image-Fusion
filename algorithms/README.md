# 算法模块导航

本目录构建 `mif_core`，供 Qt、Python 和外部 C++ 程序调用。
公开接口位于 `include/mif/`；`src/` 全部为内部实现，不随 SDK 安装。

## 目录

```text
algorithms/
├── CMakeLists.txt                 # 算法库的构建与 SDK 安装规则
├── README.md                      # 本模块导航
├── include/mif/
│   ├── fusion.hpp                 # 公开 fuse()、结果、进度与异常约定
│   └── options.hpp                # 公开算法枚举与参数
└── src/
    ├── pipeline.cpp               # 完整流程：校验、归一化、配准、融合、整理输出
    ├── common/
    │   ├── grayscale.hpp          # 配准与融合共用的灰度转换
    │   └── progress.hpp           # 共用进度报告与取消检查
    ├── fusion/
    │   ├── fusion.hpp             # 内部融合入口、方法声明与 MethodResult
    │   ├── fusion.cpp             # 按 FusionMethod 分派
    │   ├── focus_measure.hpp      # 清晰度与 FocusMaps 声明
    │   ├── focus_measure.cpp      # 清晰度评分、灰度引导图与初始决策图
    │   ├── weight_map.hpp         # 权重处理函数声明
    │   ├── weight_map.cpp         # 引导滤波、归一化、通道扩展与来源索引
    │   ├── guided_filter.cpp      # 双尺度引导滤波融合
    │   └── laplacian_pyramid.cpp  # 拉普拉斯金字塔融合
    └── registration/
        ├── registration.hpp      # 内部配准入口与 Estimator 约定
        ├── registration.cpp      # 方法选择、坐标换算、重采样与共同区域裁剪
        ├── ecc.cpp               # ECC 平移、仿射与单应性估计
        └── sift_homography.cpp   # SIFT 匹配与 RANSAC 单应性估计
```

`mif/export.hpp` 由 CMake 在构建目录生成并随 SDK 安装，不在上述源码树中维护。

## 各模块负责什么

- **`pipeline.cpp`** 实现公开的 `mif::fuse()`，串起整个处理流程并恢复输出位深。
- **`fusion/`** 接收配准后的浮点图像，返回融合图像和细节权重。
  `fusion.cpp` 只选择方法，每种方法在自己的 `.cpp` 中生成权重并重建图像。
- **`registration/`** 估计图像之间的变换，统一执行重采样和有效区域裁剪。
  每种估计方法各一个 `.cpp`，共同的坐标处理留在 `registration.cpp`。
- **`common/`** 只放两个模块都需要的灰度转换和进度工具，均为内联函数。

依赖方向为 **`pipeline → fusion / registration → common`**。
融合与配准模块之间不互相引用；内部命名空间分别为
`mif::detail::fusion` 和 `mif::detail::registration`。

### 容易混淆的名称

| 名称 | 作用 |
|---|---|
| `include/mif/fusion.hpp` | 调用者使用的公开接口 |
| `src/fusion/fusion.hpp` | 项目内部的融合方法入口与结果约定 |
| `focus_measure.cpp` | 用改进拉普拉斯或 Tenengrad 评分，生成初始清晰区域选择；这些指标本身不是完整融合方法 |
| `weight_map.cpp` 中的 `guidedFilter()` | 平滑权重的引导滤波算子，两种融合方法都会使用 |
| `guided_filter.cpp` | 完整的双尺度融合方法，包含基础层、细节层及其权重和重建 |

## 阅读顺序

1. [options.hpp](include/mif/options.hpp) 和 [公开 fusion.hpp](include/mif/fusion.hpp)：输入、参数、结果和回调约定。
2. [pipeline.cpp](src/pipeline.cpp)：了解从输入到输出的完整流程。
3. 按关注点进入 [融合入口](src/fusion/fusion.cpp) 或 [配准入口](src/registration/registration.cpp)，再阅读对应的方法文件。
4. 理解具体步骤时查阅 [清晰度计算](src/fusion/focus_measure.cpp)、[权重处理](src/fusion/weight_map.cpp) 和 `common/`。

公式与参数见 [算法说明](../docs/algorithm.md)，新增方法的步骤见
[架构与扩展方式](../docs/architecture.md#融合扩展方式)。目录调整不改变公开 API、算法计算或输出约定。
