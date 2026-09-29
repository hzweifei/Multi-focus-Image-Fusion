# 算法库同时服务 Qt、Python 和外部 C++ 项目；公开头文件集中在 include/mif。
if(MIF_BUILD_SHARED)
    set(_mif_library_type SHARED)
else()
    set(_mif_library_type STATIC)
endif()
add_library(mif_core ${_mif_library_type}
    # 公开接口和完整处理流程。
    include/mif/fusion.hpp
    include/mif/fusion_options.hpp
    include/mif/registration.hpp
    include/mif/registration_options.hpp
    include/mif/pipeline.hpp
    include/mif/progress.hpp
    src/pipeline.cpp
    # 仅存放被多个模块使用的基础工具。
    src/common/grayscale.hpp
    src/common/progress.hpp
    src/common/image_stack.hpp
    src/common/image_stack.cpp
    # 融合入口、融合专用工具及具体方法。
    src/fusion/fusion.hpp
    src/fusion/fusion.cpp
    src/fusion/focus_measure.hpp
    src/fusion/focus_measure.cpp
    src/fusion/weight_map.hpp
    src/fusion/weight_map.cpp
    src/fusion/guided_filter.cpp
    src/fusion/laplacian_pyramid.cpp
    # 配准入口及具体变换估计方法。
    src/registration/registration.hpp
    src/registration/registration.cpp
    src/registration/ecc.cpp
    src/registration/sift_homography.cpp
)
# Visual Studio 等 IDE 按磁盘目录展示源码和头文件，便于按模块浏览。
get_target_property(_mif_sources mif_core SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_mif_sources})
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
target_include_directories(mif_core
    # 内部引用统一写成 common/...、fusion/...、registration/...，不向 SDK 使用者暴露。
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PUBLIC
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
# 公开头文件包含 UTF-8 中文注释，MSVC 消费方也需要相同编码设置。
# 使用接口属性让 find_package(Mif) 导出的目标自动传递，避免系统代码页误读头文件。
target_compile_options(mif_core INTERFACE $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)

# SDK 中 DLL 放 bin，导入库/静态库放 lib；内部 src 头文件不对外安装。
install(TARGETS mif_core EXPORT MifTargets
    RUNTIME DESTINATION bin COMPONENT SDK
    LIBRARY DESTINATION lib COMPONENT SDK
    ARCHIVE DESTINATION lib COMPONENT SDK
)
# 原地更新旧 SDK 时，移除已拆分的公开头，避免两份参数类型定义同时残留。
# 只删除本项目明确废弃的文件，不清空使用者的整个 include 目录。
install(CODE [[
    file(REMOVE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/include/mif/options.hpp")
]] COMPONENT SDK)
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

