# Multi-focus Image Fusion

使用 **C++17、OpenCV、CMake 和 Qt Widgets** 实现的多聚焦图像融合工具，
面向显微图像与工业检测图像栈。提供独立算法库和可选的 nanobind Python 绑定。

## 已实现

- 引导滤波双尺度融合、拉普拉斯金字塔融合。
- 改进拉普拉斯和 Tenengrad 清晰度指标。
- 可选 SIFT + RANSAC 单应性配准，以及 ECC 平移 / 仿射 / 单应性配准，裁剪共同有效区域。
- 独立的配准和融合接口、参数与结果，可分步调用或通过组合入口连续执行。
- 灰度 / BGR，8 位、16 位无符号整数和 `[0, 1]` float32 输入。
- Qt 中文界面：图片与文件夹导入、拖放、原图与结果预览、缩放平移、参数设置、
  后台处理、进度、取消和导出。
- 两侧预览同步缩放、拖动与适应窗口；导入文件夹自动替换当前对焦批次。
- Windows 中文图片路径，16 位 PNG/TIFF 导出和 float32 TIFF 导出。
- Python NumPy 接口，C++、Python 调用示例和回归测试。

**当前仅包含传统算法，不使用 AI 模型。** 设计参考
[OpenFocus](https://github.com/Xinzhe99/OpenFocus)，具体借鉴范围见
[参考说明](docs/references.md)。

## 目录

```text
algorithms/       独立 C++ 算法库，include/mif/ 为公开接口
  src/pipeline.cpp   组合独立的配准与融合入口
  src/common/        共用输入校验、归一化、灰度转换与进度工具
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

### 桌面程序

1. 添加至少两张同场景、不同焦点的图片，尺寸、通道和位深需一致。
   导入或拖入文件夹会替换当前批次并清除旧结果；单独添加图片会继续追加。
2. 在“处理设置 → 配准”页先选择配准方法，再编辑参数。图片已对齐时选“关闭”。
   ECC 显示迭代上限和收敛阈值；SIFT 显示特征点上限、匹配距离比、RANSAC 阈值和最低内点比。
   两类方法共用“工作最长边”，用于限制估计变换的分辨率，最终仍在原始分辨率上对齐。
3. 切换到“融合”页选择融合方法并调整参数，默认使用引导滤波。
4. 点击“开始融合”，在右侧预览结果；通过“导出结果”保存。
   在任一预览中滚轮缩放、拖动或双击，另一侧会同步；切换输入图片保留当前对比位置。

参数页可上下滚动，“开始融合”按钮固定在页外。滚轮经过参数控件时只滚动页面，
不会误改数值或方法。切换配准方法会保留已填写的值；“恢复默认参数”重置全部配准参数，
保留当前配准方法。融合页可用“恢复融合默认值”恢复设置。

合成示例生成方法见 [data/samples](data/samples/README.md)。
配准失败会明确报错，不会静默跳过图片。来源索引不代表物理深度。

### C++

图像已对齐时使用 `mif::fuse(images, fusion_options)`。需要配准时，两组参数分别设置：

```cpp
#include <mif/pipeline.hpp>

mif::RegistrationOptions registration;
registration.method = mif::Alignment::FeatureHomography;
mif::FusionOptions fusion;

auto result = mif::registerAndFuse(images, registration, fusion); // images: std::vector<cv::Mat>
cv::Mat fused = result.fusion.image;
// result.crop、result.transforms 保存配准元数据。
```

也可先 `auto registered = mif::registerImages(images, registration);`，检查或保存
`registered.images` 后调用 `mif::fuse(registered.images, fusion)`。配准图像保留原位深，
两种调用方式采用相同数据路径。两组参数省略时均使用各自默认值，配准默认关闭。

### Python

```python
import mif

registration = mif.RegistrationOptions()
registration.method = mif.Alignment.FEATURE_HOMOGRAPHY
fusion = mif.FusionOptions()
fusion.method = mif.FusionMethod.LAPLACIAN_PYRAMID
result = mif.register_and_fuse([image_near, image_far], registration, fusion)
fused = result["image"]  # NumPy；彩色为 BGR
# Python 组合结果为平坦字典，另有 focus_indices、weights、crop、transforms。
```

只融合时调用 `mif.fuse(images, fusion)`，返回图像数组；需要诊断信息时调用
`mif.fuse_detailed(images, fusion)`。独立配准使用 `mif.register_images(images, registration)`，
返回 `images`、`crop`、`transforms` 字典。

旧版含配准参数的 `FusionOptions` 和 `fuse()` 调用需调整；SDK 调用方须重新编译。
具体替换方式与整数中间图像的舍入变化见 [接口迁移](docs/sdk.md#接口迁移)。

更多内容：[架构](docs/architecture.md) · [算法与参数](docs/algorithm.md) ·
[验证记录](docs/verification.md) · [第三方声明](THIRD_PARTY_NOTICES.md)

自有 C++、Python 和构建代码提供中文说明；建议按
[算法模块导航](algorithms/README.md#阅读顺序) 从公开接口开始阅读。

当前版本整栈驻留内存，尚未实现大图分块、批量任务、DCT/DTCWT/GFG-FGF 或安装包。
实际显微和工业图像的效果仍需使用真实采集数据评估。
