nanobind_add_module(_mif src/module.cpp src/bind_fusion.cpp src/array_utils.cpp)
target_link_libraries(_mif PRIVATE mif::core)
set(MIF_PYTHON_OUTPUT "${CMAKE_BINARY_DIR}/python/mif")
set_target_properties(_mif PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${MIF_PYTHON_OUTPUT}"
    RUNTIME_OUTPUT_DIRECTORY "${MIF_PYTHON_OUTPUT}"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(_mif PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${MIF_PYTHON_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MIF_PYTHON_OUTPUT}"
    )
endforeach()
file(MAKE_DIRECTORY "${MIF_PYTHON_OUTPUT}")
configure_file(python/mif/__init__.py "${MIF_PYTHON_OUTPUT}/__init__.py" COPYONLY)
install(TARGETS _mif LIBRARY DESTINATION mif RUNTIME DESTINATION mif)

