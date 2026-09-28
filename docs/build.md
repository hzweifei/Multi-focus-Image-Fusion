# 编译与运行

## 环境

- C++17 编译器，例如 Visual Studio 2022、GCC 或 Clang。
- CMake 3.21+，OpenCV 4 开发包（core、imgproc、imgcodecs、video）。
- 桌面应用：Qt 6 Widgets 或 Qt 5.15 Widgets。
- 可选 Python 绑定：Python 3.9+ 开发文件和 NumPy。

```sh
git submodule update --init --recursive
```

## 桌面版

```sh
cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<Qt安装前缀>;<OpenCV安装前缀>"
cmake --build build/desktop --config Release --parallel
```

构建后自动整理到 `outputs/Release/`，桌面程序为
`outputs/Release/app/mif_desktop.exe`，C++ SDK 为 `outputs/Release/sdk/`。
Python 绑定开启时同时生成 `outputs/Release/python/mif/`。
完整目录和交付方法见 [交付目录](outputs.md)。
也可使用 `cmake --preset default` 和 `cmake --build --preset default`，在环境或
不提交的 `CMakeUserPresets.json` 中配置本机依赖路径。

Windows 使用 vcpkg 时，首次配置添加：

```text
-DCMAKE_TOOLCHAIN_FILE=<vcpkg目录>/scripts/buildsystems/vcpkg.cmake
```

Qt 必须使用与编译器兼容的套件，例如 MSVC x64 配合 MSVC x64 Qt，不能混用 MinGW Qt。
只指定 Qt 的配置目录时，可用 `-DQT_DIR=<Qt前缀>/lib/cmake/Qt6`（Qt 5 改为 Qt5）。

### Windows 运行依赖

构建时会调用对应 Qt 的 `windeployqt`，部署窗口和 SVG 图标插件，并扫描收集
OpenCV、Qt 和其他依赖 DLL，包括 Anaconda Qt 所需的 zlib/zstd。
目标机器仍需对应 Visual C++ 运行库；正式发布前需在目标机器验证。

## 仅编译核心库与测试

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DMIF_BUILD_GUI=OFF -DMIF_BUILD_TESTS=ON
cmake --build build/core --config Release --parallel
ctest --test-dir build/core -C Release --output-on-failure
```

桌面测试通过 `QT_QPA_PLATFORM=offscreen` 运行真实 Qt 窗口、后台线程和导出流程。

## Python 绑定

```sh
cmake -S . -B build/python -DCMAKE_BUILD_TYPE=Release -DMIF_BUILD_GUI=OFF -DMIF_BUILD_PYTHON=ON -DMIF_BUILD_TESTS=ON -DPython_EXECUTABLE=<Python解释器路径>
cmake --build build/python --config Release --parallel
```

将 `outputs/Release/python` 加入 `PYTHONPATH` 后可以 `import mif`。
安装到 Python 环境时，在仓库根目录运行：

```sh
python -m pip install . -Ccmake.define.OpenCV_DIR=<OpenCVConfig.cmake所在目录>
```

Windows 交付包和 wheel 会包含本项目及 OpenCV DLL，并在导入时注册包内 DLL
目录。开发构建的 CTest 会设置核心库搜索路径。NumPy 仍作为 Python 依赖安装。

生成 `.whl` 到 `outputs/Release/python/wheels/`：

```powershell
cmake --build build/local --config Release --target mif_wheel
```

## 本机快捷配置

本机已创建不提交的 `CMakeUserPresets.json`，使用已有 Visual Studio 2022、
vcpkg OpenCV、Anaconda Qt 5 和 Python，无需修改全局环境。

```powershell
cmake --preset local
cmake --build --preset local
ctest --preset local
outputs/Release/examples/mif_example.exe --demo outputs/demo
outputs/Release/app/mif_desktop.exe outputs/demo/focus_01.png outputs/demo/focus_02.png
```

其他机器需在自己的 UserPresets 中设置依赖路径。Qt 桌面程序支持中文路径；
C++ 命令行示例使用 OpenCV 文件路径接口，在 Windows 上建议使用 ASCII 路径。
