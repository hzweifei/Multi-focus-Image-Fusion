# 验证指南

本文说明如何复现当前测试，以及功能和性能验证的关键结果。自动测试使用固定合成输入，
不依赖下载图像；实图性能测试另行执行。运行环境和交付配置见 [构建说明](build.md)。

## 已验证环境与结果

最近一次完整验证为 2026-10-09 的计算与并行优化及发布前整理，保留当前 GFG-FGF 自定义 G 优先规则：Release 构建及
**27/27 项 CTest** 通过，覆盖核心、Qt 流程与预览、Python 绑定（含 23 项 Python 用例）。
`core.gfgfgf_priority` 还验证了原分段评分反选模糊图的低对比度条纹案例：下采样 1 和 4
经过两阶段滤波后，内部测试区域均选择清晰帧。具体覆盖范围见下一节。

本次还验证了静态链接和安装后的 SDK：

- 静态库另行通过 9 项针对性测试，覆盖两套注册表、并发首次调用、配准组合、GFG-FGF、快速引导滤波及 DTCWT。
- 安装后的 SDK 由独立工程编译、链接并运行，检查公开头、具体参数类型、克隆与 DLL 分派。
- 重新生成 wheel 并安装至独立目录，排除开发 OpenCV/Qt DLL 路径后，**23/23 项 Python 测试**通过。
  Python 数组转换已合并至绑定入口，`0.2.0rc2` wheel 通过相同的独立安装检查。
  Qt 程序、SDK 和 Python 目录包均已重新整理到 `outputs/Release/`。

本机验证环境为 Windows x64、Visual Studio 2022 / MSVC 19.44、CMake 4.0.2、
OpenCV 4.12.0（含 ximgproc）、Qt 5.15.2、Python 3.12.4、NumPy 2.2.6、nanobind 2.9.2。
此前还完成 Windows 平台插件下的桌面流程及五种参数表单、小窗口截图检查；
自动测试默认使用 `offscreen`，不能替代真实样式的目视检查。
Qt 6、Linux 和 macOS 尚未实际编译验证；本次实图性能范围见后文。

后文保留 DTCWT 独立参考精度和官方引导滤波的历史数值、耗时记录。
GFG-FGF 的测试检查手算响应、自定义 G 优先规则与工程边界，不代表重现论文的实拍指标。
复现不依赖旧日志或临时目录。

## 复现自动测试

在仓库根目录执行，按 [构建说明](build.md) 补充本机依赖路径及 Python 解释器：

```powershell
git submodule update --init --recursive
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release -DMIF_BUILD_GUI=ON -DMIF_BUILD_PYTHON=ON -DMIF_BUILD_TESTS=ON
cmake --build build/local --config Release --parallel
ctest --test-dir build/local -C Release --output-on-failure
```

若已配置本机 `local` 预设，也可使用 `cmake --preset local`、
`cmake --build --preset local` 和 `ctest --preset local`。
只验证核心时可关闭 GUI 和 Python；测试数量会相应减少。

| 测试范围 | 主要检查 |
|---|---|
| 五种融合方法 | 灰度/BGR、8/16/float32、恒等与互补清晰输入、奇数尺寸、独立参数校验 |
| 方法注册与配置 | 新参数类型注册、重复/未知类型拒绝、并发首次调用、具体类型快照隔离 |
| 方法诊断 | 支持诊断的方法检查权重归一化、原始输入顺序与 257 帧索引；GFG-FGF 默认全部焦面、可选筛帧零权，DTCWT 空诊断 |
| GFG-FGF 自定义决策 | 手算 G/R、原始 G 阈值等号、G 优先与全 R 回退、有符号响应、同路 Sobel 合法候选、平局与换序对称、下采样 1/4 的条纹反选回归 |
| 快速引导滤波 | 完整分辨率对照 OpenCV、1–16 倍下采样、奇数/小图、有符号响应；同一引导图缓存复用、ROI 和异常后再次调用 |
| 帧并行与 DTCWT 内核并行 | 奇数尺寸和非连续 ROI；单/多线程图像、权重、索引或小波系数逐元素一致；回调线程、原事件序列、取消和异常收尾 |
| GFG-FGF 决策优化 | 冻结的旧实现作为独立对照，96 组场景及输入反序；负响应、单精度容差相邻值、唯一候选、稀疏与密集平局、ROI 父图边界 |
| 块方差累加 | 块级累加与完整像素权重重建逐元素相同；灰度/BGR、8/16/float32、ROI、残块、并列及诊断开关 |
| 配准与组合 | ECC 三种模型、SIFT 单应性、共同有效区域、16 位精度、独立两步与组合入口一致；关闭配准时的内部借用与公开副本所有权 |
| 数据与任务生命周期 | 输入不变、结果独立持有、进度单调、取消、回调异常传播 |
| Qt 参数与文件流程 | 五表单独立保存与重置、条目重排后传参、忙时锁定、滚轮保护、小窗口滚动、文件夹批次替换 |
| Qt 预览 | 两侧缩放/平移/适应、不同图像与面板尺寸、空图、换图与窗口调整 |
| Python | 非连续与只读数组、嵌套配置复制和生命周期、结果所有权、异常映射、DTCWT 的 `None`/`[]` |

测试入口与分组见 [tests/CMakeLists.txt](../tests/CMakeLists.txt)。例如，单独检查 DTCWT
和引导滤波数值边界：

```powershell
ctest --test-dir build/local -C Release -R "core\.(dtcwt_.*|guided_filter_numerics)" --output-on-failure
```

CTest 默认以 `offscreen` 平台运行桌面测试。检查真实 Windows 样式和布局时，可在
Qt 与 OpenCV 运行库可用的终端中执行：

```powershell
New-Item -ItemType Directory -Force build/verification | Out-Null
$env:PATH = "$PWD/outputs/Release/app;$env:PATH"
$env:QT_PLUGIN_PATH = "$PWD/outputs/Release/app"
$env:QT_QPA_PLATFORM = "windows"
build/local/bin/Release/mif_gui_tests.exe apps/desktop/resources/style.qss build/verification/desktop.png
```

该程序保存配准页、五种融合页、长表单底部和小窗口截图；需要人工查看文字、预览对齐和控件是否可达。
上述可执行文件路径适用于 Visual Studio 多配置构建；其他生成器按实际输出路径运行。

## 验证交付包

### Python wheel

使用与扩展匹配的 Python，并准备 NumPy。先生成 wheel，再把本次生成的文件安装到独立目录：

```powershell
cmake --build build/local --config Release --target mif_wheel
python -m pip install --no-deps --upgrade --target build/verification/wheel "<本次生成的 wheel 完整路径>"
$env:PYTHONPATH = "$PWD/build/verification/wheel"
$env:MIF_TEST_DLL_DIRS = ""
python bindings/python/tests/test_fusion.py
```

wheel 位于 `outputs/Release/python/wheels/`。检查 DLL 自包含性时，还应从运行进程的
PATH 中移除开发环境的 OpenCV/Qt 目录，避免借用外部运行库。包安装与测试不会修改全局 Python 包。

### C++ SDK

以下工程只通过已安装的 SDK 查找接口和库，不引用核心源码：

```powershell
cmake -S tests/sdk_consumer -B build/verification/sdk-consumer -DMif_DIR="$PWD/outputs/Release/sdk/lib/cmake/Mif"
cmake --build build/verification/sdk-consumer --config Release
$env:PATH = "$PWD/outputs/Release/sdk/bin;$env:PATH"
build/verification/sdk-consumer/Release/sdk_consumer.exe
```

配置仍需能找到匹配的 OpenCV 开发包。测试源码见 [sdk_consumer](../tests/sdk_consumer/main.cpp)；
可先将 SDK 复制到另一个目录，再用新的 `Mif_DIR` 重复配置，以检查 SDK 的可搬移性。
运行时同样应排除开发目录中的 DLL；Qt 交付程序应使用 `outputs/Release/app/` 内的 DLL 和平台插件启动。

## 实图性能对比

### 2026-10-09：缩短计算时间

本轮基线是 **10 月 8 日优化完成后的版本**，包含上一轮全部内存优化及当前 G/R 决策规则。
优化前冻结核心 DLL 与源码快照，新旧核心使用同一份基准程序和完全相同的输入顺序、参数。
数据仍为四个独立批次，每批 5 张 544×544 的 8 位灰度 PNG，全部使用原分辨率。
设备与软件环境同下方历史记录，关闭配准、权重诊断和 OpenCL。

分别固定 1、4、8 个 OpenCV 线程，五种方法与四批图片逐项串行测试，新旧版本交替先后顺序。
每个进程预热 1 次、正式计时 9 次；每批取中位数，再对四批取平均。计时覆盖核心组合入口，
不含解码、指纹和保存。下表为 8 线程结果；峰值是进程工作集，包含运行库、解码和预热。

| 方法 | 优化前 | 优化后 | 耗时减少 | 峰值内存：优化前 → 后 |
|---|---:|---:|---:|---:|
| GFF 引导滤波 | 73.38 ms | 45.36 ms | 38.2% | 37.96 → 56.43 MiB |
| 拉普拉斯金字塔 | 52.00 ms | 35.44 ms | 31.8% | 34.24 → 52.79 MiB |
| GFG-FGF | 72.88 ms | 37.45 ms | 48.6% | 48.93 → 63.16 MiB |
| 块方差 | 9.75 ms | 9.53 ms | 2.3% | 21.14 → 21.14 MiB |
| DTCWT | 168.58 ms | 139.79 ms | 17.1% | 56.15 → 56.92 MiB |

本轮未修改块方差计算，约 2% 的差异视为运行波动。GFF、金字塔、GFG-FGF 并行处理帧时，
同时使用更多滤波缓冲区，本组峰值增加约 14～19 MiB，换得更短的处理时间。
在 4 线程下，这四种被修改的方法分别减少约 24%、18%、38%、15% 耗时。
单线程下 GFG-FGF 仍减少约 28%，说明减少决策扫描和不必要的导数计算本身有效；
其他方法的单线程差异约在 -1.2%～3.1%，不据此宣称稳定变化。

另在本机 22 个逻辑处理器对应的 22 线程配置下，对四批图片重复同一流程：GFF、金字塔、GFG-FGF、
DTCWT 的耗时分别由 76.80、52.32、70.67、162.02 ms 降至 43.57、34.86、36.56、146.14 ms。
这额外 20 组输出及可用来源图也完全一致，记录位于 `thread22-check/`。
线程数更高并不保证更快，核心保留调用方的 OpenCV 设置，不在任务内修改全局线程数。

基准另做 3 次带进度回调的阶段测量，放在正式耗时和峰值采样之后。
例如基线第一批的 GFF 权重阶段约 49 ms，GFG-FGF 决策与权重阶段约 40 ms，支持优先优化这两段。
阶段值是按公开回调划分的独立估计，包含回调记录开销，不能当作每个滤波算子的精确耗时。
并行后每批开始前连续报告多个帧事件，相邻事件的间隔不再表示单帧处理时间。

正确性核对包括：

- **60 组**新旧实图融合输出逐元素相同，最大绝对差为 0；支持来源图的 **48 组**索引也完全一致。
- 另用 5 张 515×515 非连续、只读输入，覆盖灰度/BGR、uint8/uint16/float32 与五种方法，共 **30 组**。
  新旧图像、完整权重、来源索引、变换与裁剪信息的哈希全部相同。
- 分阶段测量带回调的输出与正式测量无回调输出一致；源文件哈希与测试前清单一致。
- CTest 检查多线程回调仍在调用线程执行，事件顺序保持不变；取消或异常返回后没有遗留任务。

本轮保持原参数、边界、滤波精度和跨帧累加顺序；没有通过减少输入帧或降低图像分辨率换取速度。
这些结果只代表本机及这批输入；大图、长序列、彩色实图和开启 ECC/SIFT 时的性能仍需单独测试。

本机记录位于 `outputs/performance/20261009/`：`comparison.json` / `comparison.csv` 包含三种线程配置的完整结果，
`baseline/`、`optimized/` 保存逐批报告与融合图片，`baseline-formats.json` / `optimized-formats.json` 保存位深与通道核对。
构建日志、输入清单和基线源码位于 `build/verification/performance-20261009/`。
基线核心 SHA-256 前缀为 `81c1daff50914a260`，优化后为 `50b99d5f7a09a697`，完整哈希保存在比较报告中。

### 2026-10-08：减少复制和缓冲区分配

#### 测试条件

2026-10-08 使用用户提供的 `images/1`～`images/4` 四个独立批次；每批 5 张
544×544 的 8 位灰度 PNG，共 20 张，原分辨率计算。图片不随仓库提交。
基线是本轮优化前的本地实现，**已包含当前 GFG-FGF 的 G/R 优先规则**；不是旧 release。
优化前先构建并冻结核心 DLL，新旧核心由同一份基准程序分别加载。

- 设备：Intel Core Ultra 9 185H，32 GB 内存；Windows x64 / MSVC Release / OpenCV 4.12.0。
- 调用 `registerAndFuse(NoRegistrationOptions, 默认融合参数)`，关闭权重诊断，分别固定 1 和 8 个 OpenCV 线程，关闭 OpenCL。
- 五种方法与四批图片逐项串行运行，新旧版本交替先后顺序，每个进程预热 1 次、正式测量 5 次。
- 计时包括校验、关闭配准阶段、归一化、融合及原位深恢复；不含读图、输入指纹和结果保存。
- 每批取 5 次耗时的中位数，下表再取四批中位数的平均值。内存为四个进程的峰值工作集平均值，
  包含运行库、解码和预热，采样在导出结果之前；不能当作算法单次调用的额外分配量。

#### 结果

| 方法 | 1 线程耗时：优化前 → 后 | 8 线程耗时：优化前 → 后 | 8 线程峰值内存：优化前 → 后 |
|---|---:|---:|---:|
| GFF 引导滤波 | 70.44 → 72.81 ms | 71.51 → 73.51 ms | 44.05 → 37.89 MiB |
| 拉普拉斯金字塔 | 50.07 → 50.01 ms | 49.60 → 49.50 ms | 38.71 → 34.23 MiB |
| GFG-FGF | 82.68 → 82.79 ms | 74.68 → 73.75 ms | 50.20 → 48.82 MiB |
| 块方差 | 13.56 → 9.34 ms | 13.38 → 9.20 ms | 29.51 → 21.14 MiB |
| DTCWT | 172.00 → 169.04 ms | 170.03 → 168.05 ms | 56.36 → 54.97 MiB |

块方差在两种线程设置下均约快 **1.45 倍**，耗时减少约 **31%**，峰值内存减少约 **28%～29%**。
GFF 和金字塔的主要收益是内存，8 线程峰值分别减少约 **14%** 和 **12%**。
GFG-FGF 的引导统计原本就在下采样尺寸计算，本批小图中，复用统计对总耗时的影响有限。
DTCWT 的变换实现未改动，本轮只受益于组合入口减少中间副本。

GFF 首轮小幅变慢，因此又对全部四批、两种线程设置各运行两轮独立进程，每轮预热 1 次、计时 15 次。
复测平均耗时为 1 线程 **69.46 → 70.84 ms**、8 线程 **71.37 → 71.71 ms**，分别增加约 2.0% 和 0.5%。
逐批差异有正有负，本轮保留其内存收益，不能据此宣称 GFF 提速。其他方法约 0%～2% 的时间差
也不足以证明稳定加速。这里没有测量大图、长序列、彩色实图或开启 ECC/SIFT 时的性能。

40 组新旧对照保存的融合图全部逐元素相同，最大绝对像素差为 **0**；
支持来源图的 32 组索引也完全一致，DTCWT 不返回该诊断。GFF 复测结果同样一致。
源文件 SHA-256 与测试前清单一致，程序也检查每次测试前后的解码输入未被修改。
这些检查证明本批输入的结果保持一致，不代表已有全清晰真值可评价所有方法的成像质量。

本机完整记录位于 `outputs/performance/20261008/`：`comparison.json` / `comparison.csv` 保存比较结果，
`baseline/` 和 `optimized/` 保存逐批 JSON、融合 PNG、保真 NPY 及来源索引 NPY，
`gff-recheck/` 保存加测记录。基线 DLL 的 SHA-256 前缀为 `ab41695589b88b66`，
优化后为 `81c1daff50914a260`，完整哈希在比较报告中；输入清单和构建日志位于
`build/verification/performance/`。这些本机产物不提交到 Git。

### 复现基准

基准工具只有一个源码文件 [benchmark_fusion.cpp](../tests/benchmark_fusion.cpp)，
仅在显式请求时构建，不加入普通构建或 CTest。启用 `MIF_BUILD_TESTS` 后可执行：

```powershell
cmake --build --preset local --target mif_benchmark
$env:PATH = "$PWD/outputs/Release/sdk/bin;$env:PATH"
build/local/bin/Release/mif_benchmark.exe "<图片批次目录>/1" gfg outputs/performance/check/batch1-gfg 9 8 1
```

参数依次为批次目录、方法、输出前缀、正式计时次数、OpenCV 线程数及可选的阶段测量开关；
最后一项默认 0，设为 1 时额外执行三次带回调测量，将阶段时间写入 JSON 的 `profile_runs`。
额外运行不计入正式耗时和峰值工作集，导出文件仍使用正式测量的结果。
方法可取 `gff`、`pyramid`、`gfg`、`block`、`dtcwt`。一次只测一个批次，避免把不同场景合并。
文件按自然名称顺序读取，负号视为普通字符，实际顺序完整记录在 JSON 中。
JSON 还包含所有融合参数、实际加载的核心 DLL 路径和 OpenCV 构建信息；长文本以字符串数组保存，
直接连接各片段即可还原。比较两个版本时应检查这些设置相同，并避免同时运行构建或其他计算任务。
NPY 保留原位深和完整索引；float32 输入的 PNG 仅作 16 位显示预览，精确比较应读取 NPY。

## DTCWT 独立参考与精度

除正逆变换互相验证外，[DTCWT 测试](../tests/test_dtcwt.cpp) 内置了 Python dtcwt 0.14.0
的 `near_sym_a/qshift_a` 三层参考系数，检查复数相位、方向和低频输出。普通 CTest
不需要安装该 Python 参考包；滤波器来源见 [FILTERS.md](../algorithms/src/fusion/dtcwt/FILTERS.md)。

此前本机独立核对还包括：

- 32 组极小、窄图、奇偶尺寸及 1/2/4/16 层请求的往返测试，最大误差约 `5.6e-16`。
- 128×192、四层变换与上述 Python 参考实现对照：24 个复方向子带的最大绝对差为
  `2.17e-15`，低频最大差为 `3.56e-15`。
- 129×193 两帧融合转换为 float32 后，与参考逐元素相同。

这些数值是固定输入与滤波器配置下的误差记录，不代表其他方法或真实图像的融合质量。

## 官方引导滤波的数值与耗时

官方 float32 引导滤波在平坦输入、`epsilon<=1e-8` 时可能产生 NaN，权重裁剪无法修复。
核心因此要求引导正则项位于 `[1e-6, FLT_MAX]`，Qt 常用范围为 `[1e-6, 1]`；ECC 不受此限制。
[数值测试](../tests/test_guided_filter.cpp) 覆盖常量、近常量、局部平坦、小图及半径
1/3/15/255，检查有限输出与归一化权重。Qt 测试另检查最小合法正则项的实际传入与浮点输出。

此前本机在 MSVC Release、OpenCV 4.12.0 下，对旧手写实现和官方实现做过独立滤波微基准。
两者都裁剪权重至 `[0,1]`；输入为固定随机种子的 float32 灰度图和二值权重。
预热三次后交替运行，每种实现记录 11 次并取中位数，半径 3/15 分别使用正则项 0.0001/0.01：

| OpenCV 线程数 | 图像大小 | 半径 | 旧实现 | 官方实现 | 旧耗时 / 新耗时 |
|---|---|---|---|---|---|
| 1 | 512×512 | 3 | 5.71 ms | 3.57 ms | 1.60 |
| 1 | 1920×1080 | 15 | 48.37 ms | 31.42 ms | 1.54 |
| 8 | 1920×1080 | 3 | 45.81 ms | 26.46 ms | 1.73 |
| 8 | 1920×1080 | 15 | 47.56 ms | 27.41 ms | 1.73 |

全部八组设置中，单独滤波环节约快 1.4–1.7 倍。该结果不代表完整融合流程或其他设备的固定提速，
也不是当前 CTest 的性能门槛。官方 `BORDER_REFLECT` 与旧 `BORDER_REFLECT_101` 在边缘处有差异，
不承诺与旧实现逐像素一致。

## 尚待验证

当前合成测试不替代真实采集数据评估。噪声、倍率变化、视差、曝光差异、细微结构与反光区域
仍需使用实际显微或工业焦点序列检查；整栈驻留内存，大图性能和内存占用也需按目标设备评估。
