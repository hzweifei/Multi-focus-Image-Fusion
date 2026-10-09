# 构建、运行与交付

## 环境与依赖

- C++17 编译器，例如 Visual Studio 2022、GCC 或 Clang。
- CMake 3.21+，OpenCV 4.4+ 开发包，包含 `core`、`imgproc`、`imgcodecs`、`video`、
  `features2d`、`calib3d`、`ximgproc`。`ximgproc` 来自 opencv_contrib，用于官方引导滤波。
- 桌面应用：Qt 6 Widgets 或 Qt 5.15 Widgets。
- Python 绑定：Python 3.9+ 开发文件、NumPy，以及 `ext/nanobind` 子模块。

```sh
git submodule update --init --recursive
```

Windows 使用 vcpkg 时安装包含扩展模块的 OpenCV：

```powershell
vcpkg install "opencv4[contrib]:x64-windows" --recurse
```

配置 CMake 时增加 `-DCMAKE_TOOLCHAIN_FILE=<vcpkg目录>/scripts/buildsystems/vcpkg.cmake`。
Qt 必须与编译器套件一致，例如 MSVC x64 配合 MSVC x64 Qt。
DTCWT 使用项目内的 C++ 变换引擎，无需 Python dtcwt/scipy 或 AI 模型。

## 构建方式

### 桌面程序与 C++ SDK

```sh
cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<Qt安装前缀>;<OpenCV安装前缀>"
cmake --build build/desktop --config Release --parallel
```

也可使用仓库提供的 `default`、`core` 预设。依赖路径放在本机环境或不提交的
`CMakeUserPresets.json` 中；只指定 Qt 配置目录时，可用 `-DQT_DIR=<Qt前缀>/lib/cmake/Qt6`
（Qt 5 改为 `Qt5`）。

| 开关 | 默认值 | 作用 |
|---|---|---|
| `MIF_BUILD_GUI` | ON | Qt 桌面应用 |
| `MIF_BUILD_PYTHON` | OFF | nanobind Python 扩展 |
| `MIF_BUILD_TESTS` | OFF | 核心、已启用的桌面与 Python 测试 |
| `MIF_BUILD_EXAMPLES` | ON | C++ 调用示例 |
| `MIF_BUILD_SHARED` | ON | 核心动态库；OFF 为静态库 |
| `MIF_STAGE_OUTPUTS` | ON | 完整构建后自动整理交付文件；wheel 内部构建默认为 OFF |

### 仅编译核心库与测试

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DMIF_BUILD_GUI=OFF -DMIF_BUILD_TESTS=ON
cmake --build build/core --config Release --parallel
ctest --test-dir build/core -C Release --output-on-failure
```

关闭 Qt 或 Python 时，不查找对应依赖。完整验证步骤见 [验证指南](verification.md)。

### Python 包与 wheel

```sh
cmake -S . -B build/python -DCMAKE_BUILD_TYPE=Release -DMIF_BUILD_GUI=OFF -DMIF_BUILD_PYTHON=ON -DMIF_BUILD_TESTS=ON -DPython_EXECUTABLE=<Python解释器路径>
cmake --build build/python --config Release --parallel
cmake --build build/python --config Release --target mif_wheel
```

普通构建更新 `outputs/Release/python/mif/`；`mif_wheel` 单独生成 Release wheel 到
`outputs/Release/python/wheels/`，不会在每次普通编译时重新打包。
在仓库根目录也可以直接安装源码：

```sh
python -m pip install . -Ccmake.define.OpenCV_DIR=<OpenCVConfig.cmake所在目录>
```

使用未安装的交付包时：

```powershell
$env:PYTHONPATH = "$PWD/outputs/Release/python"
python -c "import mif; print(mif.__version__)"
```

Python 版本和架构须与扩展匹配，并安装 NumPy。Windows 包会注册包内 DLL 目录，
通常不需要设置 OpenCV PATH。接口说明见 [Python 绑定](../bindings/python/README.md)。

### 本机快捷配置

本工作区的 `local` 预设位于不提交的 `CMakeUserPresets.json`，启用 Qt、Python、
示例和测试，使用 Visual Studio 2022、vcpkg OpenCV 4.12.0、Anaconda Qt 5 与 Python。
其他机器需要按自己的安装路径创建该预设。

```powershell
cmake --preset local
cmake --build --preset local --parallel
ctest --preset local
cmake --build build/local --config Release --target mif_wheel
outputs/Release/app/mif_desktop.exe
```

## 交付目录

完整构建会通过 `mif_outputs` 按配置整理产物；未启用的组件不会生成。

```text
outputs/
├── Release/
│   ├── app/                  mif_desktop.exe、核心/Qt/OpenCV DLL、Qt 插件和 licenses/
│   ├── python/
│   │   ├── mif/              __init__.py、扩展模块和 Windows DLL
│   │   ├── wheels/           显式运行 mif_wheel 后生成的 .whl
│   │   ├── README.md
│   │   └── licenses/
│   ├── sdk/
│   │   ├── include/mif/      公开头文件
│   │   ├── lib/              导入库/静态库，以及 cmake/Mif/ 接入配置
│   │   ├── bin/              核心 DLL 与 OpenCV 运行依赖
│   │   ├── README.md
│   │   └── licenses/
│   └── examples/             C++ 示例程序及运行依赖
├── Debug/                    构建 Debug 后生成，与 Release 分开
└── demo/                     按下面命令生成的合成样例
```

把完整 `app/`、`sdk/` 或对应 Python 包交付给使用者。C++ 接入见 [SDK 说明](sdk.md)：
公开接口使用 `cv::Mat`，调用方仍需与 SDK 构建版本相同、包含 `ximgproc` 的 OpenCV
开发包。Windows DLL 位于 `bin/`，不能只交付链接用的 `.lib`。

Windows 构建会调用 Qt 的 `windeployqt` 并扫描运行依赖，为应用、Python、SDK 和示例
收集所需 DLL；目标机器仍需兼容的 Visual C++ 运行库。Linux/macOS 目前安装本项目的库，
第三方运行库由使用者环境提供，尚未验证独立打包。

交付根目录可用 `-DMIF_OUTPUT_ROOT=<绝对路径>` 修改。不同工具链或 Python 版本使用
不同输出目录；切换依赖版本时使用新目录，避免旧 DLL 混入。
`-DMIF_STAGE_OUTPUTS=OFF` 关闭默认整理，仍可手动构建 `mif_outputs`，或按组件安装：

```powershell
cmake --build build/local --config Release --target mif_outputs
cmake --install build/local --config Release --component SDK --prefix outputs/Release/sdk
cmake --install build/local --config Release --component Desktop --prefix outputs/Release/app
cmake --install build/local --config Release --component Python --prefix outputs/Release/python
```

## 生成示例图片

```powershell
outputs/Release/examples/mif_example.exe --demo outputs/demo
outputs/Release/app/mif_desktop.exe outputs/demo/focus_01.png outputs/demo/focus_02.png
```

生成两张互补清晰的 `focus_01.png`、`focus_02.png`，一张全清晰 `reference.png`，
以及 `fused_guided.png`、`fused_pyramid.png`、`fused_block_variance.png`、`fused_dtcwt.png`、
`fused_gfg_fgf.png` 五种融合结果。桌面程序只导入两张 `focus_*.png` 作为输入；
导入整个示例目录会把参考图和结果图也加入图像栈。

样例由 C++ 示例程序生成，不维护重复的图片数据目录。Qt 支持中文图片路径；
C++ 命令行示例使用 OpenCV 文件路径接口，在 Windows 上建议使用 ASCII 路径。

## 打包 Windows 测试版

GitHub Release 附件使用完整的便携 ZIP。使用者解压后运行 `mif_desktop.exe`；
面向 Windows 10/11 x64，附带 Qt/OpenCV DLL、必要插件、MSVC 运行库和第三方说明。
构建目录和二进制附件仍放在被 Git 忽略的 `outputs/`，不提交到源码仓库。

准备好 MSVC 安装目录中可再分发的 `Microsoft.VC143.CRT` 文件夹，以及本次实际依赖
对应的补充许可证与源码说明目录，再从仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 `
  -Version v0.2.0-preview.2 `
  -ExpectedCommit (git rev-parse HEAD) `
  -RuntimeDirectory '<Visual Studio>/VC/Redist/MSVC/<版本>/x64/Microsoft.VC143.CRT' `
  -AdditionalNoticesDirectory 'outputs/release-notices' `
  -BuildPreset local -BuildDirectory build/local
```

脚本要求 x64 MSVC 构建已配置，并由同名构建及测试预设启用桌面程序、交付目录和测试。
算法、界面、构建输入和子模块必须与指定提交一致；允许尚未提交的发布文档修改仅限
`README.md`、本页及打包脚本，并在包内记录。补充说明须覆盖实际随包依赖；例如 Qt、
ICU、ANGLE、OpenSSL、zstd 和 MSVC 的许可与源码来源，不能只依据过时的包管理器
元信息推断 DLL 版本。现有 `app/licenses/` 中的说明会一并保留。

脚本会构建 Release、执行测试、核对交付 EXE/DLL 与构建结果一致，加入可再分发的
MSVC CRT，再扫描 DLL 导入表。Windows 提供的 `D3Dcompiler_47.dll` 不复制进包；当前
Widgets 界面使用栅格绘制，也不附带可选的软件 OpenGL 后备库 `opengl32sw.dll`。
ZIP 生成后解压并逐文件比较 SHA256，在仅包含包目录及 Windows 系统目录的 PATH 下
启动解压后的程序，检查本地 Qt 插件和已加载 DLL 的来源，结束检查后关闭进程。
这属于开发机器上的隔离启动检查，不能替代独立干净机器上的测试。

产物位于 `outputs/releases/`：

- `Multi-focus-Image-Fusion-<版本>-windows-x64.zip`
- 同名 `.zip.sha256` 校验文件

ZIP 内含 UTF-8 中文 `README.txt`、`BUILD_INFO.json`、`FILES.sha256` 和三张示例图。
仅将两张 `focus_*.png` 用作输入；`reference.png` 用于观察预期清晰效果。
脚本拒绝覆盖已存在的同名发行包，并清除本次专用临时目录。确认附件、源码提交和
版本标签一致后，将 ZIP 和校验文件上传为 GitHub **Pre-release**，供其他人下载测试。

## 开发文件与清理

- `build/<预设>/` 保存构建缓存、中间文件和测试程序，可在停止相关进程后删除并重新配置。
- `build/verification/` 用于临时 wheel 安装和外部 SDK 验证，验证后可删除。
- `outputs/<配置>/` 是当前交付文件；`outputs/demo/` 可由示例命令重新生成。
- 构建日志、临时截图和性能探针不提交到 Git；长期回归检查写入 `tests/` 或 Python 测试。
- `CMakeUserPresets.json` 保存本机依赖路径，`ext/` 保存第三方子模块，不属于构建缓存。

`build/` 和 `outputs/` 均被 Git 忽略。清理交付文件前，先保存自行放入的采集图片和结果。
