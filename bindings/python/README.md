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
registration_options.method = mif.RegistrationMethod.ECC
registration_options.motion_model = mif.MotionModel.TRANSLATION
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
`RegistrationOptions.method` 默认为 `RegistrationMethod.NONE`；即使跳过配准，
`register_images` 仍返回每张图的独立副本。配准结果保持原始位深，裁剪到共同
有效矩形。矩阵方向为第一张原始图像到各源图像；详细坐标约定见
`help(mif.register_images)`。

## 配准算法与运动模型

`method` 选择算法，`motion_model` 只选择 ECC 允许的几何变换。默认模型为
`MotionModel.TRANSLATION`，三种合法模型都可以保存在参数对象中。

| `method` | `motion_model` | 返回变换 |
|---|---|---|
| `RegistrationMethod.NONE` | 忽略合法模型值 | 2×3 单位变换 |
| `RegistrationMethod.ECC` | `MotionModel.TRANSLATION` | 2×3 平移 |
| `RegistrationMethod.ECC` | `MotionModel.AFFINE` | 2×3 仿射 |
| `RegistrationMethod.ECC` | `MotionModel.HOMOGRAPHY` | 3×3 单应性 |
| `RegistrationMethod.SIFT` | 忽略合法模型值，固定求解单应性 | 3×3 单应性 |

例如，SIFT 配准只需设置 `registration_options.method = mif.RegistrationMethod.SIFT`。
ECC 需要较好的初始对齐；SIFT 需要足够的可匹配纹理。七个数值参数保持不变：
`iterations`、`epsilon` 供 ECC 使用，`max_features`、`match_ratio`、
`ransac_threshold`、`min_inlier_ratio` 供 SIFT 使用，`max_size` 控制估计分辨率。
配准入口仍校验所有配置；忽略合法模型值不代表接受非法模型枚举。

## 从旧接口迁移

`Alignment` 及其旧成员已移除，不提供兼容别名。按下表拆成算法与模型：

| 旧 `Alignment` 值 | `RegistrationOptions.method` | `RegistrationOptions.motion_model` |
|---|---|---|
| `NONE` | `RegistrationMethod.NONE` | 保持默认 |
| `TRANSLATION` | `RegistrationMethod.ECC` | `MotionModel.TRANSLATION` |
| `AFFINE` | `RegistrationMethod.ECC` | `MotionModel.AFFINE` |
| `ECC_HOMOGRAPHY` | `RegistrationMethod.ECC` | `MotionModel.HOMOGRAPHY` |
| `FEATURE_HOMOGRAPHY` | `RegistrationMethod.SIFT` | 保持默认，SIFT 固定单应性 |

更早版本的配准与融合混合配置仍按以下方式迁移，不提供属性转发：

| 旧用法 | 新用法 |
|---|---|
| `FusionOptions.alignment` | 按上表设置 `RegistrationOptions.method` 与 `motion_model` |
| `FusionOptions.alignment_iterations`、`alignment_epsilon` | `RegistrationOptions.iterations`、`epsilon` |
| `FusionOptions.alignment_max_size`、`alignment_max_features` | `RegistrationOptions.max_size`、`max_features` |
| `FusionOptions.alignment_match_ratio`、`alignment_ransac_threshold`、`alignment_min_inlier_ratio` | `RegistrationOptions.match_ratio`、`ransac_threshold`、`min_inlier_ratio` |
| 通过 `fuse` 的配置同时配准 | 改为显式两步或 `register_and_fuse`；组合结果的图像位于 `result["image"]` |
| 从 `fuse_detailed` 读取 `crop`／`transforms` | 从 `register_images` 或 `register_and_fuse` 结果读取 |

`FusionOptions` 只控制融合，`RegistrationOptions` 只控制配准；单独运行某个阶段
不会校验另一阶段的参数。参数类型传错会抛出 `TypeError`，非法值抛出
`ValueError`，无法求解的配准抛出 `RuntimeError`。

`examples/python/fuse_images.py` 演示纯融合，`examples/python/register_images.py`
演示单独配准并保存整组图像。命令行算法为 `--method none/ecc/sift`，默认 `ecc`；
ECC 模型为 `--motion-model translation/affine/homography`，默认 `translation`。
选择 `sift` 时固定求解单应性，`--motion-model` 不改变其行为。

