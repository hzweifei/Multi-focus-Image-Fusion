# C++ SDK

目录结构：

```text
include/mif/   fusion.hpp、options.hpp、export.hpp（公开接口）
lib/           mif_core.lib（Windows DLL 的导入库；Debug 为 mif_cored.lib）
bin/           mif_core.dll 和依赖的运行库（Debug 为 mif_cored.dll）
lib/cmake/Mif/  find_package(Mif) 所需的配置
licenses/      第三方声明
```

默认构建动态库。可用 `MIF_BUILD_SHARED=OFF` 构建静态库，此时不会生成 mif_core DLL。
请让调用方与 SDK 的操作系统、架构、编译器 ABI、C++ 运行库和 Debug/Release 配置一致。
Windows 当前版本使用 MSVC x64。

公开接口使用 `cv::Mat`，因此开发者还需提供与 SDK 构建版本相同的 OpenCV **开发包**。
SDK 中包含运行所需 DLL，不复制 OpenCV 的头文件或第三方导入库。

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyFusionApp LANGUAGES CXX)
find_package(Mif CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE mif::core)
```

配置时用 `-DCMAKE_PREFIX_PATH=<SDK目录>`，并按自己的依赖管理方式设置 OpenCV。
例如 vcpkg 用户同时指定自己的 toolchain。无需手写源项目路径。

```cpp
#include <mif/fusion.hpp>

cv::Mat combine(const std::vector<cv::Mat>& images) {
    return mif::fuse(images).image;
}
```

Windows 运行时将 `bin/` 中的 DLL 复制到调用程序旁边，或将 SDK 的 `bin/` 加入
该程序的 DLL 搜索路径。`mif_core.lib` 只用于链接，不能替代运行时 DLL。
通过 `mif::core` 链接时会自动传递头文件路径和依赖信息。
