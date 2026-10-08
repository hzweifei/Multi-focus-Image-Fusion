# 验证指南

本文说明如何复现当前测试，以及最近一次功能验证的关键结果。测试使用固定合成输入，
不依赖下载图像；运行环境和交付配置见 [构建说明](build.md)。

## 已验证环境与结果

最近一次完整验证记录为 2026-10-08 的参数类型、注册表及文件结构整理：Release 构建及
**27/27 项 CTest** 通过，覆盖核心、Qt 流程与预览、Python 绑定（含 23 项 Python 用例）。
具体覆盖范围见下一节。

- 静态库另行通过 7 项针对性测试，覆盖两套注册表、并发首次调用、配准组合及各融合方法。
- 安装后的 SDK 由独立工程编译、链接并运行，检查公开头、具体参数类型、克隆与 DLL 分派。
- wheel 安装至独立目录并排除开发 OpenCV/Qt DLL 路径后，**23/23 项 Python 测试**通过。

本机验证环境为 Windows x64、Visual Studio 2022 / MSVC 19.44、CMake 4.0.2、
OpenCV 4.12.0（含 ximgproc）、Qt 5.15.2、Python 3.12.4、NumPy 2.2.6、nanobind 2.9.2。
此前还完成 Windows 平台插件下的桌面流程及五种参数表单、小窗口截图检查；
自动测试默认使用 `offscreen`，不能替代真实样式的目视检查。
Qt 6、Linux 和 macOS 尚未实际编译验证，本次也未重新测量性能。

后文保留 DTCWT 独立参考精度和官方引导滤波的历史数值、耗时记录。
GFG-FGF 的公式测试检查手算响应与工程边界，不代表重现论文的实拍指标。
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
| GFG-FGF 论文公式 | 手算 GFG、阈值等号与残差回退、Sobel 仅在并列候选中比较、未消歧时对称等权 |
| 快速引导滤波 | 完整分辨率对照 OpenCV、1–16 倍下采样、奇数/小图、完整引导细节和有符号响应 |
| 配准与组合 | ECC 三种模型、SIFT 单应性、共同有效区域、16 位精度、独立两步与组合入口一致 |
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
