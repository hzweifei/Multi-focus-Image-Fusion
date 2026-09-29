# Multi-focus Image Fusion

使用 **C++17、OpenCV、CMake 和 Qt Widgets** 实现的多聚焦图像融合工具，
面向显微与工业检测图像栈。提供独立 C++ 算法库、Qt 中文桌面程序和可选的 nanobind Python 绑定。

## 功能

- 五种传统融合方法：双尺度引导滤波（GFF）、拉普拉斯金字塔、块方差（DCT）、DTCWT 双树复小波、GFG-FGF。
  各方法独立配置，引导滤波调用 OpenCV `ximgproc` 官方实现。
- 可选 ECC 配准（平移 / 仿射 / 单应性）或 SIFT + RANSAC 单应性配准，自动裁剪共同有效区域。
  配准与融合可独立调用，也可一步完成。
- 支持灰度 / BGR、8 位和 16 位无符号整数、`[0,1]` float32；输出保留输入位深与通道数。
- Qt 支持图片和文件夹导入、拖放、双侧同步缩放与平移、后台处理、进度、取消和导出。
  Windows 中文路径、16 位 PNG/TIFF 与 float32 TIFF 读写已覆盖测试。
- 提供 NumPy 接口、C++ / Python 示例、核心与桌面回归测试。

**当前仅包含传统算法，不使用 AI 模型。** 设计参考
[OpenFocus](https://github.com/Xinzhe99/OpenFocus)，具体原理、参数及借鉴范围见 [算法说明](docs/algorithm.md)。

## 文档导航

| 需要做什么 | 文档 |
|---|---|
| 编译、运行、整理交付文件与生成示例 | [构建与交付](docs/build.md) |
| 理解算法、选择方法和调整参数 | [算法说明](docs/algorithm.md) |
| 查找代码和阅读实现 | [算法模块导航](algorithms/README.md) · [架构与扩展](docs/architecture.md) |
| 在其他程序中调用 | [C++ SDK](docs/sdk.md) · [Python 接口](bindings/python/README.md) |
| 运行测试与了解验证边界 | [验证指南](docs/verification.md) |

## 目录

```text
algorithms/       C++ 算法库；include/mif/ 为公开接口，src/ 为内部实现
apps/desktop/     Qt 桌面应用
bindings/python/  nanobind 扩展与 Python 包
ext/              第三方 Git 子模块
cmake/            依赖查找、安装与交付规则
tests/            C++ / Qt 回归测试与外部 SDK 消费方
examples/         C++ / Python 调用示例
docs/             算法、架构、构建、接口与验证说明
build/            编译中间文件
outputs/          Qt 程序、Python 包、SDK 和示例等交付文件
```

## 快速构建

准备 CMake 3.21+、C++17 编译器、含 `opencv_contrib/ximgproc` 的 OpenCV 4.4+
和 Qt 6 或 Qt 5.15 开发包：

```sh
git submodule update --init --recursive
cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --config Release --parallel
```

依赖未在默认搜索路径时，用 `CMAKE_PREFIX_PATH`、`OpenCV_DIR` 或 vcpkg toolchain
指定；开关与平台配置见 [构建说明](docs/build.md)。默认交付目录为 `outputs/Release/`：
`app/` 放 Qt 程序，`python/` 放启用后生成的 Python 包，`sdk/` 放公开头文件与库，
`examples/` 放示例程序。详见 [交付目录](docs/build.md#交付目录)。

## 使用

### 桌面程序

1. 添加至少两张同场景、不同焦点的图片，尺寸、通道和位深需一致。
   导入或拖入文件夹会替换当前批次并清除旧结果；单独添加文件继续追加。
2. 在“配准”页选择关闭 / ECC / SIFT。ECC 可选择变换模型；SIFT 固定使用单应性。
   两者的“工作最长边”只限制估计分辨率，最终在原始分辨率上对齐。
3. 在“融合”页选择方法并调整对应参数。切换方法保留各套配置，恢复默认只重置当前融合方法。
   配准的恢复按钮保留当前方法与 ECC 模型。
4. 点击“开始融合”，检查结果并导出。任一预览的滚轮、拖动或双击都会同步另一侧；
   切换输入图片保留当前对比位置。

参数页可滚动，运行按钮固定在页外；滚轮经过参数控件不会误改数值。
配准失败会报错。合成输入生成方法见 [生成示例图片](docs/build.md#生成示例图片)。

### C++

图片已对齐时直接使用 `mif::fuse(images, fusion)`；需要配准时：

```cpp
#include <mif/pipeline.hpp>

mif::RegistrationOptions registration;
registration.method = mif::RegistrationMethod::Ecc;
registration.motion_model = mif::MotionModel::Affine;
mif::FusionOptions fusion;
fusion.guided_filter.focus.window = 9;

auto result = mif::registerAndFuse(images, registration, fusion); // images: std::vector<cv::Mat>
cv::Mat fused = result.fusion.image;
// result.crop、result.transforms 保存配准元数据。
```

也可先 `registerImages()` 再 `fuse()`，与组合入口采用相同数据路径。
完整结果约定、SDK 接入及旧接口迁移见 [C++ SDK](docs/sdk.md)。

### Python

```python
import mif

registration = mif.RegistrationOptions()
registration.method = mif.RegistrationMethod.ECC
fusion = mif.FusionOptions()
fusion.method = mif.FusionMethod.LAPLACIAN_PYRAMID
fusion.laplacian_pyramid.levels = 5
result = mif.register_and_fuse([image_near, image_far], registration, fusion)
fused = result["image"]  # NumPy；彩色为 BGR
```

`mif.fuse()` 只返回图像；`mif.fuse_detailed()` 额外返回方法支持的诊断；
`mif.register_images()` 仅配准。DTCWT 没有单一空间来源图，`focus_indices=None`、`weights=[]`。
配置、所有权和返回字典见 [Python 接口](bindings/python/README.md)。

当前整栈驻留内存，尚未实现大图分块、批量任务或安装包。真实采集图像的效果仍需评估，
来源索引不能当作物理深度；测试范围见 [验证指南](docs/verification.md)。第三方许可见
[第三方声明](THIRD_PARTY_NOTICES.md)。
