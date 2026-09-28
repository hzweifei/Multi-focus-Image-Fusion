# Multi-focus Image Fusion

使用 **C++17、OpenCV、CMake 和 Qt Widgets** 实现的多聚焦图像融合工具，
面向显微图像与工业检测图像栈。提供独立算法库和可选的 nanobind Python 绑定。

## 已实现

- 引导滤波双尺度融合、拉普拉斯金字塔融合。
- 改进拉普拉斯和 Tenengrad 清晰度指标。
- 可选 SIFT + RANSAC 单应性配准，以及 ECC 平移 / 仿射 / 单应性配准，裁剪共同有效区域。
- 灰度 / BGR，8 位、16 位无符号整数和 `[0, 1]` float32 输入。
- Qt 中文界面：图片与文件夹导入、拖放、原图与结果预览、缩放平移、参数设置、
  后台处理、进度、取消和导出。
- Windows 中文图片路径，16 位 PNG/TIFF 导出和 float32 TIFF 导出。
- Python NumPy 接口，C++、Python 调用示例和回归测试。

**当前仅包含传统算法，不使用 AI 模型。** 设计参考
[OpenFocus](https://github.com/Xinzhe99/OpenFocus)，具体借鉴范围见
[参考说明](docs/references.md)。

## 目录

```text
algorithms/       独立 C++ 算法库，include/mif/ 为公开接口
  src/pipeline.cpp   完整处理流程
  src/common/        共用灰度转换与进度工具
  src/fusion/        融合方法、清晰度计算与权重处理
  src/registration/  配准方法与共同区域处理
apps/desktop/     Qt 桌面应用
bindings/python/  nanobind 扩展与 Python 包
ext/              第三方 Git 子模块（nanobind）
cmake/            依赖查找
tests/            C++ / Qt 回归测试
examples/         C++ / Python 示例
data/samples/     示例数据说明
docs/             架构、算法、构建和参考文档
```

算法完整目录与阅读顺序见 [算法模块导航](algorithms/README.md)。

## 构建

准备 CMake 3.21+、C++17 编译器、OpenCV 4.4+ 和 Qt 6 或 Qt 5.15 开发包。

```sh
git submodule update --init --recursive
cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --config Release --parallel
```

依赖未在默认搜索路径时，用 `CMAKE_PREFIX_PATH`、`OpenCV_DIR` 或 vcpkg
toolchain 指定。[详细构建与运行方法](docs/build.md)

| 开关 | 默认值 | 作用 |
|---|---|---|
| `MIF_BUILD_GUI` | ON | Qt 桌面应用 |
| `MIF_BUILD_PYTHON` | OFF | Python 扩展 |
| `MIF_BUILD_TESTS` | OFF | 回归测试 |
| `MIF_BUILD_EXAMPLES` | ON | C++ 示例 |
| `MIF_BUILD_SHARED` | ON | 核心算法动态库 |
| `MIF_STAGE_OUTPUTS` | ON | 构建后自动整理交付目录 |

编译产物统一放在 `outputs/Release/`（Debug 单独分目录）：

- `app/`：Qt 程序、DLL 和插件，运行 `mif_desktop.exe`。
- `python/`：可导入的 `mif` 包；`mif_wheel` 目标生成 `wheels/*.whl`。
- `sdk/`：对外使用的 `include/`、`lib/`、`bin/` 和 CMake 配置。
- `examples/`：C++ 示例程序。

详见 [交付目录](docs/outputs.md) 和 [SDK 接入方法](docs/sdk.md)。

## 使用

1. 添加至少两张同场景、不同焦点的图片，尺寸、通道和位深需一致。
2. 默认使用引导滤波；图像已对齐时保持自动配准关闭。
3. 点击“开始融合”，在右侧预览结果；通过“导出结果”保存。

合成示例生成方法见 [data/samples](data/samples/README.md)。
配准失败会明确报错，不会静默跳过图片。来源索引不代表物理深度。

```cpp
#include <mif/fusion.hpp>
mif::FusionOptions options;
auto result = mif::fuse(images, options); // std::vector<cv::Mat>
```

```python
import mif
options = mif.FusionOptions()
options.method = mif.FusionMethod.LAPLACIAN_PYRAMID
result = mif.fuse([image_near, image_far], options)  # NumPy；彩色为 BGR
```

更多内容：[架构](docs/architecture.md) · [算法与参数](docs/algorithm.md) ·
[验证记录](docs/verification.md) · [第三方声明](THIRD_PARTY_NOTICES.md)

自有 C++、Python 和构建代码提供中文说明；建议按
[算法模块导航](algorithms/README.md#阅读顺序) 从公开接口开始阅读。

当前版本整栈驻留内存，尚未实现大图分块、批量任务、DCT/DTCWT/GFG-FGF 或安装包。
实际显微和工业图像的效果仍需使用真实采集数据评估。
