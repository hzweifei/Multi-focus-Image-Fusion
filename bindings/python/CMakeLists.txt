# 模块入口、接口注册与数组转换集中在同一个文件，转换工具仅在文件内使用。
nanobind_add_module(_mif src/bindings.cpp)
target_link_libraries(_mif PRIVATE mif::core)
# 开发时让纯 Python 包和扩展模块位于同一目录；交付时再按配置复制到 outputs。
set(MIF_PYTHON_OUTPUT "${CMAKE_BINARY_DIR}/python/mif")
set_target_properties(_mif PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${MIF_PYTHON_OUTPUT}"
    RUNTIME_OUTPUT_DIRECTORY "${MIF_PYTHON_OUTPUT}"
)
# 多配置生成器默认附加 Release/Debug 子目录，显式覆盖以保持 import mif 的包布局。
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(_mif PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${MIF_PYTHON_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MIF_PYTHON_OUTPUT}"
    )
endforeach()
file(MAKE_DIRECTORY "${MIF_PYTHON_OUTPUT}")
configure_file(python/mif/__init__.py "${MIF_PYTHON_OUTPUT}/__init__.py" COPYONLY)
# 此组件既用于普通交付目录，也用于 wheel；两者都只安装可导入的包及所需运行库。
install(TARGETS _mif LIBRARY DESTINATION mif COMPONENT Python RUNTIME DESTINATION mif COMPONENT Python)
install(FILES python/mif/__init__.py DESTINATION mif COMPONENT Python)
mif_install_runtime(_mif mif Python)
if(NOT SKBUILD)
    install(FILES README.md DESTINATION . COMPONENT Python)
    mif_install_notices(Python)
else()
    # wheel 的第三方说明放在包内，避免向 site-packages 写入通用名称的顶层目录。
    mif_install_notices(Python mif/licenses)
endif()

