# 顶层负责构建开关、编译选项、依赖查找和模块装配；各模块管理自己的源文件与安装规则。
cmake_minimum_required(VERSION 3.21)
project(MultiFocusImageFusion VERSION 0.2.0 LANGUAGES CXX)

# 未启用的模块不会查找其专用依赖，因此仅编译算法时无需 Qt 或 Python。
option(MIF_BUILD_GUI "构建 Qt 桌面程序" ON)
option(MIF_BUILD_PYTHON "构建 nanobind Python 扩展" OFF)
option(MIF_BUILD_TESTS "构建回归测试" OFF)
option(MIF_BUILD_EXAMPLES "构建 C++ 调用示例" ON)
option(MIF_BUILD_SHARED "将核心算法构建为动态库" ON)
# pip/scikit-build-core 有自己的临时安装目录，不能把 wheel 的构建写进日常交付目录。
if(SKBUILD)
    set(_mif_stage_default OFF)
else()
    set(_mif_stage_default ON)
endif()
option(MIF_STAGE_OUTPUTS "完整构建后自动整理 outputs 交付目录" ${_mif_stage_default})
set(MIF_OUTPUT_ROOT "${PROJECT_SOURCE_DIR}/outputs" CACHE PATH "交付根目录；不同构建配置分别存放")
# Visual Studio 在构建时选择配置；Ninja/Make 等单配置生成器默认使用 Release。
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# 静态库可能被 Python 扩展等动态模块链接，统一启用位置无关代码。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# 将开发用可执行文件和核心 DLL 放在一起，方便 Windows 测试进程加载；交付副本另行安装。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
if(MSVC)
    # 源文件统一按 UTF-8 编译，保证中文界面、字符串与注释不受系统代码页影响。
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 核心依赖：基础图像处理、ECC、SIFT、RANSAC 及官方引导滤波。
# ximgproc 属于 opencv_contrib；vcpkg 使用 opencv4[contrib] 安装，不需要 AI 框架。
find_package(OpenCV 4.4 REQUIRED COMPONENTS core imgproc imgcodecs video features2d calib3d ximgproc)
if(MIF_BUILD_GUI)
    # 优先使用 Qt 6；已有 Qt 5 开发环境时也可构建同一套 Widgets 界面。
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()
if(MIF_BUILD_PYTHON)
    # 只需要解释器与扩展模块开发文件，不要求用于嵌入解释器的 Python 库。
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
endif()

# 定义公共安装函数，再按“核心 → 调用方 → 测试”的方向添加模块。
include(cmake/Deliverables.cmake)
add_subdirectory(ext)
add_subdirectory(algorithms)
if(MIF_BUILD_GUI)
    add_subdirectory(apps/desktop)
endif()
if(MIF_BUILD_PYTHON)
    add_subdirectory(bindings/python)
endif()
if(MIF_BUILD_EXAMPLES)
    add_subdirectory(examples/cpp)
endif()
if(MIF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 所有目标定义完毕后，再创建自动交付目标及可选的 wheel 打包目标。
mif_add_output_targets()

