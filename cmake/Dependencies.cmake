# 核心依赖：矩阵操作、滤波与金字塔、图像编解码、ECC 配准。
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs video)
if(MIF_BUILD_GUI)
    # 优先使用 Qt 6；已有 Qt 5 开发环境时也可构建同一套 Widgets 界面。
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()
if(MIF_BUILD_PYTHON)
    # 只需要解释器与扩展模块开发文件，不要求用于嵌入解释器的 Python 库。
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
endif()

