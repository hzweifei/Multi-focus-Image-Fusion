# Python 绑定

通过 `-DMIF_BUILD_PYTHON=ON` 开启绑定。完整构建后，包会整理到
`outputs/Release/python/mif`；把 `outputs/Release/python` 加入 `PYTHONPATH`
即可导入。运行时使用与扩展构建版本和架构匹配的 Python。

`src/bindings.cpp` 注册参数类与阶段函数；`src/array_utils.*` 处理 NumPy 与
`cv::Mat` 转换及内存所有权；`python/mif/__init__.py` 提供默认参数并整理非连续输入。

打包入口 `pyproject.toml` 位于仓库根目录，便于源码分发包一起包含算法与第三方依赖。
初始化子模块并准备包含 `opencv_contrib/ximgproc` 的 OpenCV 开发包后，可在根目录执行
`python -m pip install .`；依赖路径见[构建说明](https://github.com/hzweifei/Multi-focus-Image-Fusion/blob/HEAD/docs/build.md)。

构建 `mif_wheel` 目标会把 Release 安装包写入 `outputs/Release/python/wheels`。
Windows 包和 wheel 收集核心库及 OpenCV DLL，导入时自动注册包内 DLL 目录。
运行环境仍需 NumPy 和兼容的 Visual C++ 运行库；Linux/macOS 当前由环境提供 OpenCV 共享库。

## 统一入口，参数类型选择方法

| 接口 | 接收的配置 | 返回内容 |
|---|---|---|
| `fuse(images, options=None)` | 具体融合参数对象 | 融合后的 NumPy 数组 |
| `fuse_detailed(images, options=None)` | 具体融合参数对象 | `image`、`source_index_map`、`weight_maps` |
| `register_images(images, options=None)` | 具体配准参数对象 | `images`、`crop_region`、`transforms` |
| `register_and_fuse(images, registration_options=None, fusion_options=None)` | 两类独立参数对象 | 融合详细结果加 `crop_region`、`transforms`，采用平坦字典 |

两个阶段均要求至少两张同尺寸、同精度、同通道图像，支持灰度或 BGR、
`uint8`／`uint16`／`float32`。浮点值必须有限且在 `[0, 1]` 内。
接受只读数组和非连续切片，返回数组自行持有底层存储。

只融合已对齐图片时直接调用 `fuse(images)`，默认使用引导滤波融合。
需要检查配准结果或复用同一组配准图时，可以分两步调用：

```python
import mif

# 参数类型决定配准方法，ECC 的运动模型单独选择。
registration_options = mif.EccRegistrationOptions()
registration_options.motion_model = mif.MotionModel.AFFINE
registration_options.max_iterations = 200
registration_result = mif.register_images(images, registration_options)

# 将需要的方法参数直接交给统一融合入口。
fusion_options = mif.LaplacianPyramidFusionOptions()
fusion_options.focus.window_size = 7
fusion_options.max_levels = 4
fusion_options.include_weight_maps = True
fusion_result = mif.fuse_detailed(registration_result["images"], fusion_options)
```

`images` 是调用方读取的 NumPy 数组列表。仅需最终结果时，可调用组合入口：

```python
result = mif.register_and_fuse(images, registration_options, fusion_options)
```

组合入口与显式两步使用相同的配准图，包括整数图像的重采样舍入。
未传配准参数时使用 `NoRegistrationOptions`，仍返回每张图的独立副本。
配准结果保持原始位深，裁剪到共同有效矩形。矩阵方向为第一张原始图像到各源图像；
完整坐标约定见 `help(mif.register_images)`。

## 融合参数

五种配置均继承 `FusionOptionsBase`，共有字段 `include_weight_maps=False`。
基类用于统一传参，请实例化具体方法的参数类。每次调用只携带该方法所需的参数。

| 参数类型 | 方法与主要字段默认值 |
|---|---|
| `GuidedFilterFusionOptions` | 双尺度引导滤波：`focus=FocusMeasureOptions()`、`base_radius=15`、`detail_radius=3`、`base_epsilon=0.01`、`detail_epsilon=0.0001` |
| `LaplacianPyramidFusionOptions` | 拉普拉斯金字塔：`focus=FocusMeasureOptions()`、`detail_radius=3`、`detail_epsilon=0.0001`、`max_levels=5` |
| `BlockVarianceFusionOptions` | 块方差选择与一致性处理：`block_size=8`、`consistency_window_size=7` |
| `DtcwtFusionOptions` | 双树复小波系数融合：`max_levels=4`、`activity_window_size=3` |
| `GfgFgfFusionOptions` | 四邻域聚焦度量与两次快速引导滤波：`local_mean_window_size=7`、`selection_ratio=0`、`gfg_threshold=0.005`、`guided_radius=5`、`guided_epsilon=0.3`、`guided_subsample_factor=4` |

`FocusMeasureOptions` 包含 `measure=FocusMeasure.MODIFIED_LAPLACIAN` 与 `window_size=9`。
也可以选择 `FocusMeasure.TENENGRAD`。GFF 与金字塔各自持有独立的清晰度配置。

块方差的 `block_size` 范围为 `[2,128]`，`consistency_window_size` 为 `[1,31]` 内的奇数，
窗口单位是块。该方法对应参考仓库的 `dct` 标识，当前实现按空间域块方差选图。
DTCWT 的 `max_levels` 为 `[1,16]`，`activity_window_size` 为 `[1,31]` 内的奇数，作用于各层系数。

GFG-FGF 的 `local_mean_window_size` 为 `[1,255]` 内的奇数；`selection_ratio` 和
`gfg_threshold` 为 `[0,1]` 内的有限数；`guided_radius` 为 `[1,255]`；
`guided_subsample_factor` 为 `[1,16]`。
筛帧比例相对于最高 Scharr 梯度分数，默认 0 保留全部输入。
梯度达到 `gfg_threshold` 时采用梯度响应，较弱时采用图像与局部均值的绝对差。
下采样倍数为 1 时使用完整分辨率，较大倍数在低分辨率计算系数，再结合原分辨率引导图生成输出。
均值窗口、滤波半径、正则项和下采样倍数是项目默认设置，论文没有给出这些参数的完整取值。

所有引导滤波 epsilon 的合法范围是 `[1e-6, float32 最大有限值]`，最大值约为 `3.4e38`。
这个范围适应 OpenCV 官方引导滤波的浮点精度，防止平坦区域除零或转换为 float32 时溢出。
ECC 的 `convergence_tolerance` 是配准收敛阈值，采用自己的校验规则。

```python
options = mif.GfgFgfFusionOptions()
options.local_mean_window_size = 9
options.guided_radius = 5
options.guided_subsample_factor = 4
image = mif.fuse(images, options)
```

读取 `options.focus` 得到内部对象的引用，编辑其字段会直接更新所属配置；
保留 `focus` 引用后删除父变量也可继续使用，绑定会维持父对象的生命周期。
整体赋值采用值复制，例如 `options.focus = mif.FocusMeasureOptions()`，之后修改赋值来源不会改变目标。
输入图像和具体参数在释放 GIL 前取得独立副本，克隆保留具体参数类型及所有嵌套值。

### 诊断结果

`fuse_detailed` 始终提供 `image`、`source_index_map`、`weight_maps` 三个键：

| 方法 | `source_index_map` | `include_weight_maps=True` 时的 `weight_maps` |
|---|---|---|
| GFF、拉普拉斯金字塔 | 最大细节权重的原始输入索引 | 归一化细节权重 |
| 块方差 | 经过一致性处理的块来源索引 | 选块权重 |
| GFG-FGF | 最大最终权重的原始输入索引 | 最终融合权重，排除帧为全零 |
| DTCWT | `None` | `[]` |

索引数组为 `int32`，从零开始；权重列表存在时按原始输入顺序排列，筛帧不改变下标。
默认 `weight_maps=[]`。DTCWT 在不同尺度和方向选择系数，没有单一空间来源图，
即使开启权重返回也使用空诊断。组合入口遵循相同约定。

```python
options = mif.DtcwtFusionOptions()
options.max_levels = 4
result = mif.fuse_detailed(images, options)
source_index_map = result["source_index_map"]
if source_index_map is not None:
    print(source_index_map.shape)
```

## 配准参数

三种配置继承 `RegistrationOptionsBase`。共有字段 `max_working_dimension=1200`
指定估计变换时工作图像的最长边上限；最终在原始分辨率重采样。

| 参数类型 | 参数与默认值 | 返回变换 |
|---|---|---|
| `NoRegistrationOptions` | 保持输入坐标 | 2×3 单位变换 |
| `EccRegistrationOptions` | `motion_model=MotionModel.TRANSLATION`、`max_iterations=150`、`convergence_tolerance=1e-5` | 平移/仿射为 2×3，单应性为 3×3 |
| `SiftRegistrationOptions` | `max_features=4000`、`match_ratio_threshold=0.75`、`ransac_reprojection_threshold=3.0`、`min_inlier_ratio=0.25` | 3×3 单应性 |

`motion_model` 只存在于 ECC 参数，支持 `TRANSLATION`、`AFFINE` 和 `HOMOGRAPHY`。
SIFT 固定求解单应性，无需设置运动模型。ECC 需要较好的初始对齐，SIFT 需要足够的可匹配纹理。
SIFT 的重投影阈值单位是工作分辨率像素；配准入口只检查所传具体方法的参数。

## 错误、扩展与迁移

参数类型传错会抛出 `TypeError`，非法图像或参数值抛出 `ValueError`，
无法求解的配准抛出 `RuntimeError`。单独运行某个阶段不检查另一阶段的参数。
仓库的 `examples/python/fuse_images.py` 演示纯融合，`register_images.py` 演示单独配准与整组保存。

新增 C++ 方法后，需在绑定中暴露其具体参数类，并在 Python 包中导出；
四个 Python 入口和数组转换层可以继续复用。Python 中继承参数基类本身不能创建新的底层算法。

更新时成套替换 Python 包装文件、扩展模块和核心 DLL。这次统一命名移除了旧聚合配置及方法枚举：

- `FusionOptions.method` 加嵌套参数，改为直接传入具体的 `*FusionOptions`。
- `RegistrationOptions.method` 改为 `NoRegistrationOptions`、`EccRegistrationOptions` 或 `SiftRegistrationOptions`。
- `DctOptions` 改为 `BlockVarianceFusionOptions`，`GfgfgfOptions` 改为 `GfgFgfFusionOptions`。
- `FocusOptions.window` 改为 `FocusMeasureOptions.window_size`；各层数上限改为 `max_levels`。
- `keep_weight_maps` 改为 `include_weight_maps`；结果中的 `focus_indices`、`weights`、`crop`
  分别改为 `source_index_map`、`weight_maps`、`crop_region`。

完整字段对照见[接口迁移](https://github.com/hzweifei/Multi-focus-Image-Fusion/blob/HEAD/docs/sdk.md#接口迁移)。
