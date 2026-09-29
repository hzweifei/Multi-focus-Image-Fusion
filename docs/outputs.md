# 交付目录

正常执行完整构建后，`mif_outputs` 自动整理当前配置的交付文件：

```text
outputs/
├── Release/
│   ├── app/                    Qt 桌面程序，可直接运行 mif_desktop.exe
│   │   ├── mif_desktop.exe
│   │   ├── mif_core.dll
│   │   ├── *.dll              Qt、OpenCV（含 ximgproc）及其运行依赖
│   │   ├── platforms/         Windows 窗口插件
│   │   ├── iconengines/       SVG 图标插件
│   │   └── licenses/
│   ├── python/
│   │   ├── mif/              __init__.py、扩展模块及 Windows DLL
│   │   ├── wheels/           显式运行 mif_wheel 后生成的安装包
│   │   ├── README.md
│   │   └── licenses/
│   ├── sdk/
│   │   ├── include/mif/      融合、配准、组合流程及参数和进度的公开头文件
│   │   ├── lib/              mif_core.lib（DLL 导入库）
│   │   │   └── cmake/Mif/    外部 CMake 项目的接入配置
│   │   ├── bin/              mif_core.dll、OpenCV（含 ximgproc）运行依赖
│   │   ├── README.md
│   │   └── licenses/
│   └── examples/             C++ 调用示例及运行依赖
├── Debug/                    编译 Debug 时自动生成，不与 Release 混用
└── demo/                     合成输入和融合示例
```

`build/` 保留工程文件、目标文件、测试程序和编译中间产物。交付程序以 `outputs/`
为准。`outputs/` 已被 Git 忽略。

官方引导滤波需要 OpenCV 的 `ximgproc` 模块。Windows 部署脚本按实际依赖自动收集
该模块及其依赖的 DLL，覆盖桌面程序、Python 包、SDK 和示例；DLL 名称随 OpenCV
版本和构建方式变化。增加该依赖后，应重新整理完整交付目录。

## 常用命令

```powershell
cmake --preset local
cmake --build --preset local
ctest --preset local

# 构建并整理已有目标
cmake --build build/local --config Release --target mif_outputs

# 单独构建 Python Release wheel；需 Python/pip 和可用的构建依赖
cmake --build build/local --config Release --target mif_wheel

# 启动桌面程序
outputs/Release/app/mif_desktop.exe
```

wheel 只构建 Release 版本，输出到 `Release/python/wheels/`，不在每次普通编译时
重新打包。普通编译会更新可直接导入的 `python/mif/` 文件夹。

可用 `-DMIF_OUTPUT_ROOT=<其他绝对路径>` 修改根目录。多个不同工具链或 Python
版本应使用不同输出根目录；切换依赖版本时使用新输出目录，避免残留旧 DLL。
`-DMIF_STAGE_OUTPUTS=OFF` 关闭默认自动整理，此时仍可显式构建 `mif_outputs`。

也可按组件安装到任意位置：

```powershell
cmake --install build/local --config Release --component SDK --prefix outputs/Release/sdk
cmake --install build/local --config Release --component Desktop --prefix outputs/Release/app
cmake --install build/local --config Release --component Python --prefix outputs/Release/python
```

## Python 使用

```powershell
$env:PYTHONPATH = "$PWD/outputs/Release/python"
python -c "import mif; print(mif.__version__)"
```

使用与扩展匹配的 Python 版本和架构，并安装 NumPy。Windows 包会自行注册包内
DLL 目录，通常不需要设置 OpenCV PATH。wheel 中也包含这些运行库，可以把匹配
版本的 `.whl` 交给他人安装。

包提供纯融合 `fuse()` / `fuse_detailed()`、独立配准 `register_images()`，以及
组合入口 `register_and_fuse()`。配准和融合分别使用 `RegistrationOptions` 与
`FusionOptions`；组合入口返回含 `image`、`focus_indices`、`weights`、`crop`、`transforms`
的平坦字典。调用示例见 [项目说明](../README.md#使用)。
五种融合配置分别为 `guided_filter`、`laplacian_pyramid`、`dct`、`dtcwt`、`gfgfgf`。
选择 DTCWT 时，详细结果中的 `focus_indices=None`、`weights=[]`；图像照常返回。

## C++ 使用

把完整 `sdk/` 目录交给使用者，接入方式见 [SDK 说明](sdk.md)。调用者仍需相同
版本且包含 `ximgproc` 的 OpenCV 开发包，因为公开接口包含 `cv::Mat`；运行库由部署
脚本收集到 SDK 的 `bin/`。`find_package(Mif)` 也会查找 `ximgproc`。
公开入口分别位于 `fusion.hpp`、`registration.hpp`、`pipeline.hpp`；参数位于
`fusion_options.hpp` 和 `registration_options.hpp`；融合各方法参数位于 `include/mif/fusion/`，
由 `fusion_options.hpp` 一并包含，共用回调位于 `progress.hpp`，
另安装 CMake 生成的 `export.hpp`。旧 `options.hpp` 已移除，更新后需按
[接口迁移](sdk.md#接口迁移) 调整调用方并重新编译。
新增三个方法后，`FusionOptions` 的结构大小发生变化；即使原有方法的枚举值保持不变，
SDK 消费方也须重新编译。Python 的包装文件、扩展和核心 DLL 应成套更新。

Windows 运行依赖收集和 Qt 部署已实现。Linux/macOS 当前只安装本项目的库，
第三方运行库仍由使用者环境提供，尚未验证其独立打包。Windows 目标机器需有
对应的 Visual C++ 运行库；Qt/SDK 的 Debug 构建还要求依赖具有对应 Debug 版本。
