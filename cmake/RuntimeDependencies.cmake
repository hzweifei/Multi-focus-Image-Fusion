# 安装阶段脚本，由 Deliverables.cmake 生成的逐配置脚本调用。
# 输入 MIF_* 变量包含目标文件名、安装子目录、依赖搜索目录及当前 Qt/编译配置。
# 此时主程序/模块已安装到位；先部署 Qt，再递归收集真正需要的 Windows DLL。
# DESTDIR 兼容临时安装根目录；CMAKE_INSTALL_PREFIX 是当前交付组件自己的前缀。
set(destination "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${MIF_RUNTIME_DESTINATION}")
set(binary "${destination}/${MIF_BINARY_NAME}")
if(MIF_QT_BIN)
    set(deploy "${MIF_QT_BIN}/windeployqt.exe")
    if(NOT EXISTS "${deploy}")
        message(FATAL_ERROR "Qt deployment tool missing: ${deploy}")
    endif()
    if(MIF_IS_DEBUG)
        set(configuration --debug)
    else()
        set(configuration --release)
    endif()
    execute_process(COMMAND "${deploy}" ${configuration} --no-translations --no-compiler-runtime --no-plugins "${binary}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Qt deployment failed: ${output}\n${error}")
    endif()
    # 图片由 OpenCV 解码，只启用窗口平台和 SVG 图标插件，避免引入无关的 PDF 等插件。
    if(MIF_IS_DEBUG)
        set(plugin_suffix d)
    else()
        set(plugin_suffix "")
    endif()
    foreach(plugin "platforms/qwindows${plugin_suffix}.dll" "iconengines/qsvgicon${plugin_suffix}.dll")
        set(source "${MIF_QT_BIN}/../plugins/${plugin}")
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Required Qt plugin missing: ${source}")
        endif()
        get_filename_component(folder "${plugin}" DIRECTORY)
        file(INSTALL "${source}" DESTINATION "${destination}/${folder}" TYPE SHARED_LIBRARY)
    endforeach()
endif()

# 插件由 Qt 动态加载，不会出现在 EXE 的静态导入表中，必须作为额外扫描入口。
# 目录中的 OpenCV 组件同样作为入口，确保 SDK 公开依赖的间接 DLL 被覆盖。
file(GLOB_RECURSE plugins "${destination}/*.dll")
list(REMOVE_ITEM plugins "${binary}")
set(plugin_arguments)
if(plugins)
    list(APPEND plugin_arguments LIBRARIES ${plugins})
endif()
# 排除 Windows API 集、系统目录和宿主 Python DLL：它们由目标系统或解释器提供。
file(GET_RUNTIME_DEPENDENCIES
    ${MIF_RUNTIME_KIND} "${binary}"
    ${plugin_arguments}
    DIRECTORIES "${destination}" ${MIF_RUNTIME_SEARCH_DIRS}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*" "[Pp]ython[0-9]+(_d)?\\.dll"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*")
if(unresolved)
    # 缺失依赖会直接中止安装，避免把看似完整但无法启动的目录交付出去。
    message(FATAL_ERROR "Unresolved runtime DLLs for ${binary}: ${unresolved}")
endif()
foreach(name IN LISTS conflicts_FILENAMES)
    # 交付根目录中的 DLL 优先；windeployqt 会修改复制后的 QtCore 路径信息，
    # 因此 QtCore 的部署副本可能与原始文件内容不同，应保留已部署版本。
    if(EXISTS "${destination}/${name}")
        list(APPEND resolved "${destination}/${name}")
    else()
        # 不在交付根目录的同名候选必须逐一校验内容；仅允许完全一致的重复副本。
        set(candidates "${conflicts_${name}}")
        list(GET candidates 0 chosen)
        file(SHA256 "${chosen}" expected_hash)
        foreach(candidate IN LISTS candidates)
            file(SHA256 "${candidate}" actual_hash)
            if(NOT actual_hash STREQUAL expected_hash)
                message(FATAL_ERROR "Conflicting runtime dependency ${name}: ${candidates}")
            endif()
        endforeach()
        list(APPEND resolved "${chosen}")
    endif()
endforeach()
# 安装目录在整个循环中不变，只解析一次真实路径以减少重复工作。
file(REAL_PATH "${destination}" real_destination)
foreach(dependency IN LISTS resolved)
    get_filename_component(parent "${dependency}" DIRECTORY)
    file(REAL_PATH "${parent}" parent)
    if(NOT parent STREQUAL real_destination)
        # 已在目标目录中的文件无需自我复制；其他依赖按时间戳增量安装。
        file(INSTALL "${dependency}" DESTINATION "${destination}" TYPE SHARED_LIBRARY)
    endif()
endforeach()
