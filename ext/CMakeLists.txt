if(MIF_BUILD_PYTHON)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nanobind/CMakeLists.txt")
        message(FATAL_ERROR "Initialize dependencies: git submodule update --init --recursive")
    endif()
    add_subdirectory(nanobind EXCLUDE_FROM_ALL)
endif()

