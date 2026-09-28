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

Visual Studio 多配置构建的程序位于
`build/desktop/apps/desktop/Release/mif_desktop.exe`；Ninja/Make 单配置构建位于
`build/desktop/apps/desktop/mif_desktop`。
也可使用 `cmake --preset default` 和 `cmake --build --preset default`，在环境或
不提交的 `CMakeUserPresets.json` 中配置本机依赖路径。

Windows 使用 vcpkg 时，首次配置添加：

```text
-DCMAKE_TOOLCHAIN_FILE=<vcpkg目录>/scripts/buildsystems/vcpkg.cmake
```

Qt 必须使用与编译器兼容的套件，例如 MSVC x64 配合 MSVC x64 Qt，不能混用 MinGW Qt。
只指定 Qt 的配置目录时，可用 `-DQT_DIR=<Qt前缀>/lib/cmake/Qt6`（Qt 5 改为 Qt5）。

### Windows 运行依赖

开发环境中将 Qt 和 OpenCV 的 `bin` 加入当前终端的 `PATH`，确保 Qt 的
`plugins/platforms` 可被找到。准备双击运行的目录时，使用对应 Qt 安装的工具：

```powershell
windeployqt --release --no-translations --no-compiler-runtime build/desktop/apps/desktop/Release/mif_desktop.exe
```

同时确保 OpenCV DLL 及其依赖位于程序目录。vcpkg 常规动态库构建会复制其
运行依赖。目标机器还需要对应 Visual C++ 运行库。正式发布前还需补充许可证
材料、安装包和目标机器验证。

Anaconda 提供的 Qt 5 还依赖其 `Library/bin/zlib.dll` 和 `zstd.dll`，该版本
`windeployqt` 不会自动复制它们。本机已经把这两个文件复制到程序目录。

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

将 `build/python/python` 加入 `PYTHONPATH` 后可以 `import mif`。
安装到 Python 环境时，在仓库根目录运行：

```sh
python -m pip install . -Ccmake.define.OpenCV_DIR=<OpenCVConfig.cmake所在目录>
```

Windows Python 导入扩展前，应添加 OpenCV DLL 目录并保留句柄：

```python
import os
dll_directory = os.add_dll_directory(r"D:\path\to\opencv\bin")
import mif
```

运行 CTest 中的 Python 测试时，用 `MIF_TEST_DLL_DIRS` 提供 OpenCV 运行目录，
多个路径按系统分隔符分隔。测试脚本会调用 `os.add_dll_directory`。
生成的 wheel 依赖本地 OpenCV 运行库，尚未完成跨机器的依赖打包。

## 本机快捷配置

本机已创建不提交的 `CMakeUserPresets.json`，使用已有 Visual Studio 2022、
vcpkg OpenCV、Anaconda Qt 5 和 Python，无需修改全局环境。

```powershell
cmake --preset local
cmake --build --preset local
ctest --preset local
build/local/examples/cpp/Release/mif_example.exe --demo outputs/demo
build/local/apps/desktop/Release/mif_desktop.exe outputs/demo/focus_01.png outputs/demo/focus_02.png
```

其他机器需在自己的 UserPresets 中设置依赖路径。Qt 桌面程序支持中文路径；
C++ 命令行示例使用 OpenCV 文件路径接口，在 Windows 上建议使用 ASCII 路径。
