# Python 绑定

通过 `-DMIF_BUILD_PYTHON=ON` 开启绑定。完整构建后，包会整理到
`outputs/Release/python/mif`；把 `outputs/Release/python` 加入 `PYTHONPATH`
即可导入。运行时需使用与扩展构建版本和架构匹配的 Python。

`src/bindings.cpp` 集中定义模块入口、枚举、两类参数及各阶段函数的注册；
`src/array_utils.*` 单独处理 NumPy 与 `cv::Mat` 的数据转换和内存所有权；
`python/mif/__init__.py` 提供用户调用的包装接口，并整理非连续输入数组。

打包入口 `pyproject.toml` 位于仓库根目录，便于源码分发包一起包含算法与第三方
依赖。初始化子模块并准备 OpenCV 开发包后，可在根目录执行
`python -m pip install .`；依赖路径设置见 `docs/build.md`。

构建 `mif_wheel` 目标会把 Release 安装包写入 `outputs/Release/python/wheels`。
Windows 包和 wheel 包含核心库及 OpenCV DLL，导入时自动注册包内 DLL 目录。
运行环境仍需 NumPy 和兼容的 Visual C++ 运行库；Linux/macOS 当前需由环境
提供 OpenCV 共享库。

## 独立的配准与融合接口

| 接口 | 参数类型 | 返回内容 |
|---|---|---|
| `fuse(images, options=None)` | `FusionOptions` | 融合后的 NumPy 数组 |
| `fuse_detailed(images, options=None)` | `FusionOptions` | `image`、`focus_indices`、`weights` |
| `register_images(images, options=None)` | `RegistrationOptions` | `images`、`crop`、`transforms` |
| `register_and_fuse(images, registration_options=None, fusion_options=None)` | 两类独立参数 | 融合详细结果加 `crop`、`transforms`，采用平坦字典 |

两个阶段均要求至少两张同尺寸、同精度、同通道图像，支持灰度或 BGR、
`uint8`／`uint16`／`float32`。浮点值必须有限且在 `[0, 1]` 内；接受只读数组
和非连续切片，输入与参数在释放 GIL 前复制。返回数组自行持有底层存储。

只融合已对齐图片时直接调用 `fuse`。需要检查配准结果，或对同一组配准图反复
尝试不同融合设置时，显式分成两步：

```python
import mif

registration_options = mif.RegistrationOptions()
registration_options.method = mif.Alignment.TRANSLATION
registered = mif.register_images(images, registration_options)

fusion_options = mif.FusionOptions()
fusion_options.method = mif.FusionMethod.LAPLACIAN_PYRAMID
fusion_options.keep_weight_maps = True
fused = mif.fuse_detailed(registered["images"], fusion_options)
```

`images` 是调用方读取的 NumPy 数组列表。仅需最终结果时可调用组合入口：

```python
result = mif.register_and_fuse(images, registration_options, fusion_options)
```

组合入口与上述显式两步使用相同的配准图，包括整数图像的重采样舍入。
`RegistrationOptions.method` 默认为 `Alignment.NONE`；即使跳过配准，
`register_images` 仍返回每张图的独立副本。配准结果保持原始位深，裁剪到共同
有效矩形。矩阵方向为第一张原始图像到各源图像；详细坐标约定见
`help(mif.register_images)`。

## 从旧接口迁移

此次拆分明确移除旧混合参数，不提供属性转发：

| 旧用法 | 新用法 |
|---|---|
| `FusionOptions.alignment` | `RegistrationOptions.method` |
| `FusionOptions.alignment_iterations`、`alignment_epsilon` | `RegistrationOptions.iterations`、`epsilon` |
| `FusionOptions.alignment_max_size`、`alignment_max_features` | `RegistrationOptions.max_size`、`max_features` |
| `FusionOptions.alignment_match_ratio`、`alignment_ransac_threshold`、`alignment_min_inlier_ratio` | `RegistrationOptions.match_ratio`、`ransac_threshold`、`min_inlier_ratio` |
| 通过 `fuse` 的配置同时配准 | 改为显式两步或 `register_and_fuse`；组合结果的图像位于 `result["image"]` |
| 从 `fuse_detailed` 读取 `crop`／`transforms` | 从 `register_images` 或 `register_and_fuse` 结果读取 |

`FusionOptions` 只控制融合，`RegistrationOptions` 只控制配准；单独运行某个阶段
不会校验另一阶段的参数。参数类型传错会抛出 `TypeError`，非法值抛出
`ValueError`，无法求解的配准抛出 `RuntimeError`。

`examples/python/fuse_images.py` 演示纯融合，`examples/python/register_images.py`
演示单独配准并保存整组图像。

