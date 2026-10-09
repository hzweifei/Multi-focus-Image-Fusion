# 测试使用自身的检查函数，Release 下也会执行断言逻辑，不依赖额外测试框架。
# 清晰度函数属于 DLL 私有实现，单独编译其源码供内部单元测试使用，不对 SDK 导出。
# 同一源文件的焦点评估流程还依赖权重工具，因此一起编译，保持私有符号的链接完整。
add_executable(mif_tests test_fusion.cpp test_focus_measure.cpp test_registration.cpp test_pipeline.cpp
    test_traditional_fusion.cpp test_dtcwt.cpp test_guided_filter.cpp test_gfgfgf.cpp
    "${PROJECT_SOURCE_DIR}/algorithms/src/fusion/common/focus_measure.cpp"
    "${PROJECT_SOURCE_DIR}/algorithms/src/fusion/common/weight_map.cpp"
    "${PROJECT_SOURCE_DIR}/algorithms/src/fusion/common/fast_guided_filter.cpp"
    "${PROJECT_SOURCE_DIR}/algorithms/src/fusion/gfg_fgf/focus_information.cpp"
    # DTCWT 引擎保持私有；仅测试目标单独编译，以检查六方向系数与正反变换。
    "${PROJECT_SOURCE_DIR}/algorithms/src/fusion/dtcwt/transform.cpp")
target_include_directories(mif_tests PRIVATE "${PROJECT_SOURCE_DIR}/algorithms/src")
target_link_libraries(mif_tests PRIVATE mif::core)
# 真实性能比较使用独立进程与原始图片，仅显式构建，不纳入日常构建和 CTest。
add_executable(mif_benchmark EXCLUDE_FROM_ALL benchmark_fusion.cpp)
target_link_libraries(mif_benchmark PRIVATE mif::core)
target_compile_definitions(mif_benchmark PRIVATE
    MIF_BENCHMARK_PROJECT_VERSION="${PROJECT_VERSION}"
    MIF_BENCHMARK_CONFIGURATION="$<CONFIG>")
if(WIN32)
    target_link_libraries(mif_benchmark PRIVATE psapi)
endif()
foreach(case registry registry_concurrency registration_registry focus quality identity validation method_options weights cancellation alignment registration_homography registration_ecc registration_failure pipeline large_stack dct gfgfgf gfgfgf_priority fast_guided_filter dtcwt_transform dtcwt_fusion dtcwt_options guided_filter_numerics)
    # 每个场景单独注册，CTest 可以精确指出哪个算法性质发生退化。
    add_test(NAME core.${case} COMMAND mif_tests ${case})
endforeach()

if(MIF_BUILD_GUI)
    add_executable(mif_gui_tests test_desktop.cpp)
    target_link_libraries(mif_gui_tests PRIVATE mif_desktop_ui)
    add_test(NAME desktop.workflow COMMAND mif_gui_tests)
    add_executable(mif_image_view_tests test_image_view.cpp)
    target_link_libraries(mif_image_view_tests PRIVATE mif_desktop_ui)
    add_test(NAME desktop.preview COMMAND mif_image_view_tests)
    # 使用真实 Qt 事件循环和后台线程，但无需屏幕窗口或人工操作。
    set_tests_properties(desktop.workflow desktop.preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()

if(MIF_BUILD_PYTHON)
    # 验证开发树里的扩展；测试脚本据此注册核心 DLL 所在目录。
    add_test(NAME python.bindings COMMAND "${Python_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/bindings/python/tests/test_fusion.py")
    set_tests_properties(python.bindings PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python;MIF_TEST_DLL_DIRS=$<TARGET_FILE_DIR:mif_core>" TIMEOUT 60)
endif()
