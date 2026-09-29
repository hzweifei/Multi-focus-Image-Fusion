# Python 绑定

通过 `-DMIF_BUILD_PYTHON=ON` 开启绑定。完整构建后，包会整理到
`outputs/Release/python/mif`；把 `outputs/Release/python` 加入 `PYTHONPATH`
即可导入。运行时需使用与扩展构建版本和架构匹配的 Python。

`src/bindings.cpp` 集中定义模块入口、枚举、参数结构体及各阶段函数的注册；
`src/array_utils.*` 单独处理 NumPy 与 `cv::Mat` 的数据转换和内存所有权；
`python/mif/__init__.py` 提供用户调用的包装接口，并整理非连续输入数组。

打包入口 `pyproject.toml` 位于仓库根目录，便于源码分发包一起包含算法与第三方
依赖。初始化子模块并准备包含 `opencv_contrib/ximgproc` 的 OpenCV 开发包后，可在根目录执行
`python -m pip install .`；依赖路径设置见 [构建说明](../../docs/build.md)。

构建 `mif_wheel` 目标会把 Release 安装包写入 `outputs/Release/python/wheels`。
Windows 包和 wheel 收集核心库及 OpenCV DLL，包括官方引导滤波所需的 `ximgproc`，
导入时自动注册包内 DLL 目录。
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
fusion_options.laplacian_pyramid.focus.window = 7
fusion_options.laplacian_pyramid.levels = 4
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

## 各融合方法的独立配置

`FusionOptions` 的 `method` 选择方法，`keep_weight_maps` 控制是否保留该方法提供的权重。
五个配置成员独立存值；只有 GFF 和拉普拉斯金字塔包含 `FocusOptions`，分别保存
清晰度指标 `measure` 和统计窗口 `window`。

| `FusionMethod` 成员 | 配置成员 | 方法 |
|---|---|---|
| `GUIDED_FILTER` | `guided_filter` | 双尺度引导滤波，使用 OpenCV 官方引导滤波 |
| `LAPLACIAN_PYRAMID` | `laplacian_pyramid` | 拉普拉斯金字塔 |
| `DCT` | `dct` | 块方差选择与一致性处理，不显式融合 DCT 系数 |
| `DTCWT` | `dtcwt` | 双树复小波系数融合 |
| `GFGFGF` | `gfgfgf` | 梯度筛帧、局部差异和两阶段引导滤波 |

| 参数结构体 | 字段与默认值 |
|---|---|
| `FocusOptions` | `measure=FocusMeasure.MODIFIED_LAPLACIAN`、`window=9` |
| `GuidedFilterOptions` | `focus=FocusOptions()`、`base_radius=15`、`detail_radius=3`、`base_epsilon=0.01`、`detail_epsilon=0.0001` |
| `LaplacianPyramidOptions` | `focus=FocusOptions()`、`detail_radius=3`、`detail_epsilon=0.0001`、`levels=5` |
| `DctOptions` | `block_size=8`、`consistency_window=7` |
| `DtcwtOptions` | `levels=4`、`activity_window=3` |
| `GfgfgfOptions` | `difference_window=7`、`selection_ratio=0.15`、`difference_threshold=0.005`、`guided_radius=5`、`guided_epsilon=0.3` |

DCT 的 `block_size` 为 `[2,128]`，`consistency_window` 为 `[1,31]` 内的奇数，单位是块。
DTCWT 的 `levels` 为 `[1,16]`，`activity_window` 为 `[1,31]` 内的奇数，作用于各层系数。
GFG-FGF 的 `difference_window` 为 `[1,255]` 内的奇数，`selection_ratio` 和
`difference_threshold` 为 `[0,1]` 内的有限数，`guided_radius` 为 `[1,255]`，
`guided_epsilon` 为 `[1e-6, float32 最大值]` 范围内的有限数，最大值约为 `3.4e38`。
筛帧比例相对于最高梯度分数，差异阈值作用于归一化图像。
GFF 的 `base_epsilon`、`detail_epsilon` 和金字塔的 `detail_epsilon` 采用相同范围；
该范围适应官方引导滤波的浮点精度，防止平坦区域除零或转换为 float32 时溢出。
ECC 的 `RegistrationOptions.epsilon` 是独立的收敛阈值，不受此融合参数范围影响。

默认选择 `FusionMethod.GUIDED_FILTER`，`keep_weight_maps=False`。仅选中方法
的参数参与计算和校验；其他方法暂时含有非法值也不影响本次调用，切换到该方法后
会被拒绝。切换方法会保留所有配置。

```python
options = mif.FusionOptions()
options.guided_filter.focus.measure = mif.FocusMeasure.TENENGRAD
options.guided_filter.focus.window = 7
options.guided_filter.base_radius = 11
options.laplacian_pyramid.focus.window = 13
options.laplacian_pyramid.levels = 4
options.gfgfgf.difference_window = 9
options.gfgfgf.guided_radius = 5
options.method = mif.FusionMethod.GFGFGF
image = mif.fuse(images, options)
```

读取嵌套属性会得到内部对象的引用，所以上例中的编辑直接改变 `options`。
暂存子对象或 `focus` 后删除父变量也可继续使用，绑定会维持父对象的生命周期。
整体赋值采用值复制，例如 `options.guided_filter = mif.GuidedFilterOptions()`；
之后修改赋值来源不会改变目标，赋值前取得的目标内部引用仍然有效。
进入计算前会复制完整配置，包括嵌套值，再释放 GIL。

`fuse_detailed` 始终提供 `image`、`focus_indices`、`weights` 三个键，诊断内容随方法变化：

| 方法 | `focus_indices` | `keep_weight_maps=True` 时的 `weights` |
|---|---|---|
| GFF、拉普拉斯金字塔 | 最大细节权重的原始输入索引 | 归一化细节权重 |
| DCT 块方差 | 经过一致性处理的块来源索引 | 选块权重 |
| GFG-FGF | 最大最终权重的原始输入索引 | 最终融合权重，排除帧为全零 |
| DTCWT | `None` | `[]` |

索引数组为 `int32`，从零开始；权重列表存在时按原始输入顺序排列，筛帧不改变下标。
默认不开启权重保留，此时 `weights=[]`。DTCWT 在不同尺度和方向选择系数，
没有单一空间来源图，即使开启权重保留也返回空诊断。组合入口遵循相同约定。

```python
options.method = mif.FusionMethod.DTCWT
options.dtcwt.levels = 4
options.dtcwt.activity_window = 3
options.keep_weight_maps = True
result = mif.fuse_detailed(images, options)
image = result["image"]
weights = result["weights"]  # DTCWT 始终为空列表。
indices = result["focus_indices"]
if indices is not None:  # 通用诊断代码先检查方法是否提供来源图。
    print(indices.shape)
```

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

新增 `DCT`、`DTCWT`、`GFGFGF` 枚举及三个配置成员后，原有 `GUIDED_FILTER=0`、
`LAPLACIAN_PYRAMID=1` 保持原值。包装文件、扩展模块与核心 DLL 必须成套更新；
不要将旧扩展与新参数结构混用。引导滤波改用 OpenCV 官方 `ximgproc` 实现，
边界处理随之更新，不承诺与旧版本逐像素相同。诊断代码需允许 DTCWT 返回 `None`。

旧扁平融合字段已移除，不提供别名。下表中“方法配置”指
`options.guided_filter` 或 `options.laplacian_pyramid`；如果希望两种方法保留相同
的清晰度或细节设置，需要分别赋值。

| 旧 `FusionOptions` 字段 | 新字段 |
|---|---|
| `focus_measure` | 方法配置的 `focus.measure` |
| `focus_window` | 方法配置的 `focus.window` |
| `detail_radius`、`detail_epsilon` | 方法配置的同名字段 |
| `base_radius`、`base_epsilon` | `guided_filter.base_radius`、`guided_filter.base_epsilon` |
| `pyramid_levels` | `laplacian_pyramid.levels` |
| `method`、`keep_weight_maps` | 仍在顶层，含义不变 |

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

