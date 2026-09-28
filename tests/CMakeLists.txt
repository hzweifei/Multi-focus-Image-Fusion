add_executable(mif_tests test_fusion.cpp test_focus_measure.cpp)
target_include_directories(mif_tests PRIVATE "${PROJECT_SOURCE_DIR}/algorithms/src")
target_link_libraries(mif_tests PRIVATE mif::core)
foreach(case focus quality identity validation weights cancellation alignment large_stack)
    add_test(NAME core.${case} COMMAND mif_tests ${case})
endforeach()

if(MIF_BUILD_GUI)
    add_executable(mif_gui_tests test_desktop.cpp)
    target_link_libraries(mif_gui_tests PRIVATE mif_desktop_ui)
    add_test(NAME desktop.workflow COMMAND mif_gui_tests)
    set_tests_properties(desktop.workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()

if(MIF_BUILD_PYTHON)
    add_test(NAME python.bindings COMMAND "${Python_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/bindings/python/tests/test_fusion.py")
    set_tests_properties(python.bindings PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python" TIMEOUT 60)
endif()

