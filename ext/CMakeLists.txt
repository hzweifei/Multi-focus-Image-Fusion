# 第三方源码只按需构建；子模块版本由父仓库的 gitlink 固定，不在配置时自动更新。
if(MIF_BUILD_PYTHON)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nanobind/CMakeLists.txt")
        message(FATAL_ERROR "Initialize dependencies: git submodule update --init --recursive")
    endif()
    # 不构建 nanobind 自带的额外目标，仅让 _mif 拉入它实际需要的支持库。
    add_subdirectory(nanobind EXCLUDE_FROM_ALL)
endif()
