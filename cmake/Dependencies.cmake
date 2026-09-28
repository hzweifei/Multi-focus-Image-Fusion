# 核心依赖：基础图像处理、ECC、SIFT 特征及 RANSAC 单应性估计。
# SIFT 已属于 OpenCV 主仓库的 features2d 模块，无需 opencv-contrib 或 AI 框架。
find_package(OpenCV 4.4 REQUIRED COMPONENTS core imgproc imgcodecs video features2d calib3d)
if(MIF_BUILD_GUI)
    # 优先使用 Qt 6；已有 Qt 5 开发环境时也可构建同一套 Widgets 界面。
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()
if(MIF_BUILD_PYTHON)
    # 只需要解释器与扩展模块开发文件，不要求用于嵌入解释器的 Python 库。
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
endif()

