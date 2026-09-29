# 构建阶段的交付规则：定义“安装什么、安装到哪个组件”，不在配置时复制运行文件。
# 所有目的地均相对组件自己的安装前缀，因此整个 SDK 复制或改名后仍能使用。

# 收集第三方说明。第二个参数可覆盖默认 licenses/ 路径，供 wheel 放到包内部。
function(mif_install_notices component)
    if(ARGC GREATER 1)
        set(notice_directory "${ARGV1}")
    else()
        set(notice_directory licenses)
    endif()
    install(FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION "${notice_directory}" COMPONENT ${component})
    # 双树滤波器的数据出处和数学约定随各交付组件保留，方便使用者追溯实现来源。
    install(FILES "${PROJECT_SOURCE_DIR}/algorithms/src/fusion/dtcwt/FILTERS.md"
        DESTINATION "${notice_directory}/dtcwt" COMPONENT ${component})
    if(component STREQUAL "Python")
        install(FILES "${PROJECT_SOURCE_DIR}/ext/nanobind/LICENSE" DESTINATION "${notice_directory}/nanobind" COMPONENT ${component})
        install(FILES "${PROJECT_SOURCE_DIR}/ext/nanobind/ext/robin_map/LICENSE" DESTINATION "${notice_directory}/robin-map" COMPONENT ${component})
    endif()
    # 使用 vcpkg 时附带这些已知依赖的版权文件；其他依赖供应方式保留根目录声明。
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
        foreach(package opencv4 abseil protobuf flatbuffers libjpeg-turbo liblzma libpng libwebp quirc tiff zlib)
            if(EXISTS "${_share}/${package}/copyright")
                install(FILES "${_share}/${package}/copyright" DESTINATION "${notice_directory}/vcpkg/${package}" COMPONENT ${component})
            endif()
        endforeach()
    endif()
endfunction()

# 为一个已声明安装规则的目标补齐核心库及第三方运行依赖。
# target：CMake 目标；destination：相对安装目录；component：交付组件名。
# 目标类型从 CMake 自身读取，避免调用方重复维护 EXECUTABLES/MODULES/LIBRARIES。
function(mif_install_runtime target destination component)
    if(MIF_BUILD_SHARED AND NOT target STREQUAL "mif_core")
        # SDK 的核心库已自行安装；应用/扩展需把核心运行库放在自己旁边。
        install(TARGETS mif_core
            RUNTIME DESTINATION "${destination}" COMPONENT ${component}
            LIBRARY DESTINATION "${destination}" COMPONENT ${component})
    endif()
    if(NOT WIN32)
        # Linux/macOS 通过相对 RPATH 查找同目录核心库；第三方库仍由使用者环境提供。
        if(APPLE)
            set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@loader_path")
        else()
            set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
        endif()
        return()
    endif()

    # Windows 可执行文件、Python 模块和普通 DLL 使用不同的依赖扫描入口。
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
        set(kind EXECUTABLES)
    elseif(target_type STREQUAL "MODULE_LIBRARY")
        set(kind MODULES)
    elseif(target_type STREQUAL "SHARED_LIBRARY")
        set(kind LIBRARIES)
    else()
        message(FATAL_ERROR "Cannot collect runtime dependencies for target type ${target_type}")
    endif()

    # 先安装公开 OpenCV 组件，再从这些 DLL 扫描间接依赖。
    # 即使核心库暂未调用某个组件，其 DLL 仍可能被 SDK 消费者直接使用。
    set(search_dirs "$<TARGET_FILE_DIR:${target}>;$<TARGET_FILE_DIR:mif_core>")
    foreach(dependency IN LISTS OpenCV_LIBS)
        if(TARGET ${dependency})
            list(APPEND search_dirs "$<TARGET_FILE_DIR:${dependency}>")
            get_target_property(dependency_type ${dependency} TYPE)
            if(dependency_type STREQUAL "SHARED_LIBRARY")
                install(FILES "$<TARGET_FILE:${dependency}>" DESTINATION "${destination}" COMPONENT ${component})
            endif()
        endif()
    endforeach()
    set(qt_bin "")
    if(component STREQUAL "Desktop")
        # 只有桌面组件需要 Qt 部署工具，Python 和 SDK 不引入 Qt 依赖。
        set(qt_bin "$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>")
        list(APPEND search_dirs "${qt_bin}")
    endif()
    if(MSVC)
        # 安装可能从普通终端发起，提前记录工具绝对路径，不依赖开发者命令行的 PATH。
        get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(MIF_DUMPBIN dumpbin HINTS "${compiler_dir}" REQUIRED)
        set(tool dumpbin)
        set(command "${MIF_DUMPBIN}")
    else()
        set(tool objdump)
        set(command "${CMAKE_OBJDUMP}")
    endif()
    # 生成阶段展开目标路径和配置名；具体 DLL 扫描在目标编译并安装后执行。
    # 使用括号引号保存路径，避免空格或分号在脚本生成时被意外拆分。
    set(script "${CMAKE_CURRENT_BINARY_DIR}/deploy-${component}-$<CONFIG>.cmake")
    file(GENERATE OUTPUT "${script}" CONTENT
"set(MIF_BINARY_NAME [==[$<TARGET_FILE_NAME:${target}>]==])
set(MIF_RUNTIME_DESTINATION [==[${destination}]==])
set(MIF_RUNTIME_KIND ${kind})
set(MIF_RUNTIME_SEARCH_DIRS [==[${search_dirs}]==])
set(MIF_QT_BIN [==[${qt_bin}]==])
set(MIF_IS_DEBUG $<IF:$<CONFIG:Debug>,TRUE,FALSE>)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL ${tool})
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND [==[${command}]==])
include([==[${PROJECT_SOURCE_DIR}/cmake/RuntimeDependencies.cmake]==])
")
    install(SCRIPT "${script}" COMPONENT ${component})
endfunction()

# 所有子目录添加完成后调用一次，建立 outputs 整理目标和可选 wheel 目标。
function(mif_add_output_targets)
    if(SKBUILD)
        # wheel 的临时 CMake 构建只执行安装，不能再次创建一个 wheel 打包任务。
        return()
    endif()
    set(prefix "${MIF_OUTPUT_ROOT}/$<CONFIG>")
    set(components SDK)
    set(dependencies mif_core)
    set(SDK_dir sdk)
    set(Desktop_dir app)
    set(Python_dir python)
    set(Examples_dir examples)
    # 明确列出组件与其构建目标，保证关闭可选功能后不会安装并不存在的文件。
    if(MIF_BUILD_GUI)
        list(APPEND components Desktop)
        list(APPEND dependencies mif_desktop)
    endif()
    if(MIF_BUILD_PYTHON)
        list(APPEND components Python)
        list(APPEND dependencies _mif)
    endif()
    if(MIF_BUILD_EXAMPLES)
        list(APPEND components Examples)
        list(APPEND dependencies mif_example)
    endif()
    set(commands)
    # 各组件单独安装，SDK 根目录始终保留标准 include/lib/bin 布局。
    foreach(component IN LISTS components)
        list(APPEND commands COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
            --config "$<CONFIG>" --prefix "${prefix}/${${component}_dir}" --component ${component})
    endforeach()
    if(MIF_STAGE_OUTPUTS)
        # 加入 ALL 后，普通完整构建会更新交付副本；关闭此开关仍能显式构建 mif_outputs。
        set(all ALL)
    else()
        set(all)
    endif()
    add_custom_target(mif_outputs ${all} ${commands}
        DEPENDS ${dependencies}
        COMMENT "Collecting app, Python package and SDK in ${prefix}"
        VERBATIM)

    if(MIF_BUILD_PYTHON)
        # wheel 由 pip 的独立构建环境生成，沿用当前 OpenCV/toolchain，始终输出 Release 包。
        set(wheel_args
            "-Ccmake.define.OpenCV_DIR=${OpenCV_DIR}"
            "-Ccmake.define.MIF_STAGE_OUTPUTS=OFF"
            "-Ccmake.define.CMAKE_BUILD_TYPE=Release")
        if(CMAKE_TOOLCHAIN_FILE)
            list(APPEND wheel_args "-Ccmake.define.CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
        endif()
        if(VCPKG_TARGET_TRIPLET)
            list(APPEND wheel_args "-Ccmake.define.VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
        endif()
        add_custom_target(mif_wheel
            COMMAND "${CMAKE_COMMAND}" -E env "CMAKE_GENERATOR=${CMAKE_GENERATOR}"
                "${Python_EXECUTABLE}" -m pip wheel "${PROJECT_SOURCE_DIR}" --no-deps
                --wheel-dir "${MIF_OUTPUT_ROOT}/Release/python/wheels" ${wheel_args}
            COMMENT "Building the release Python wheel under outputs/Release/python/wheels"
            VERBATIM)
    endif()
endfunction()
