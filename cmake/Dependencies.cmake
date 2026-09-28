find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs video)
if(MIF_BUILD_GUI)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()
if(MIF_BUILD_PYTHON)
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
endif()

