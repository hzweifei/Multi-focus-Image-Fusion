# 算法库同时服务 Qt、Python 和外部 C++ 项目；公开头文件集中在 include/mif。
if(MIF_BUILD_SHARED)
    set(_mif_library_type SHARED)
else()
    set(_mif_library_type STATIC)
endif()
add_library(mif_core ${_mif_library_type}
    src/fusion.cpp
    src/focus_measure.cpp
    src/weight_map.cpp
    src/blending.cpp
    src/registration.cpp
)
add_library(mif::core ALIAS mif_core)
# 构建树和安装后的 SDK 使用同一名称 mif::core；Debug 库名加 d 防止误用。
set_target_properties(mif_core PROPERTIES EXPORT_NAME core DEBUG_POSTFIX d)
include(GenerateExportHeader)
# 自动生成 MIF_EXPORT，分别处理 Windows DLL 导入/导出及静态库情形。
generate_export_header(mif_core
    BASE_NAME MIF
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/mif/export.hpp"
)
if(NOT MIF_BUILD_SHARED)
    target_compile_definitions(mif_core PUBLIC MIF_STATIC_DEFINE)
endif()
target_include_directories(mif_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# 开发环境绝对路径只写入构建接口；SDK 在使用者机器上重新查找 OpenCV。
foreach(directory IN LISTS OpenCV_INCLUDE_DIRS)
    target_include_directories(mif_core PUBLIC $<BUILD_INTERFACE:${directory}>)
endforeach()
target_link_libraries(mif_core PUBLIC ${OpenCV_LIBS})
target_compile_features(mif_core PUBLIC cxx_std_17)

# SDK 中 DLL 放 bin，导入库/静态库放 lib；内部 src 头文件不对外安装。
install(TARGETS mif_core EXPORT MifTargets
    RUNTIME DESTINATION bin COMPONENT SDK
    LIBRARY DESTINATION lib COMPONENT SDK
    ARCHIVE DESTINATION lib COMPONENT SDK
)
install(DIRECTORY include/mif DESTINATION include COMPONENT SDK)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/mif/export.hpp" DESTINATION include/mif COMPONENT SDK)
include(CMakePackageConfigHelpers)
# 生成可随整个 SDK 移动的 find_package(Mif) 配置，避免依赖本机源码路径。
configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/MifConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MifConfig.cmake" INSTALL_DESTINATION lib/cmake/Mif)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MifConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT MifTargets NAMESPACE mif:: DESTINATION lib/cmake/Mif COMPONENT SDK)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MifConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/MifConfigVersion.cmake" DESTINATION lib/cmake/Mif COMPONENT SDK)
install(FILES "${PROJECT_SOURCE_DIR}/docs/sdk.md" DESTINATION . RENAME README.md COMPONENT SDK)
mif_install_notices(SDK)
if(MIF_BUILD_SHARED)
    mif_install_runtime(mif_core bin SDK)
endif()

