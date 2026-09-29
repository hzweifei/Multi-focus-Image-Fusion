# 将窗口与后台任务做成内部静态库，让桌面入口和集成测试复用同一套界面代码。
add_library(mif_desktop_ui STATIC
    main_window.cpp main_window.hpp
    image_io.cpp image_io.hpp
    widgets/image_view.cpp widgets/image_view.hpp
    widgets/fusion_settings.cpp widgets/fusion_settings.hpp
    widgets/registration_settings.cpp widgets/registration_settings.hpp
    widgets/parameter_form.hpp
    workers/fusion_worker.cpp workers/fusion_worker.hpp
)
# AUTOMOC 处理窗口和工作线程的信号槽；所有布局直接由 C++ 创建。
set_target_properties(mif_desktop_ui PROPERTIES AUTOMOC ON)
target_include_directories(mif_desktop_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mif_desktop_ui PUBLIC mif::core Qt${QT_VERSION_MAJOR}::Widgets)

# WIN32 隐藏额外控制台；AUTORCC 把样式与图标编译进程序，无需外置资源文件。
add_executable(mif_desktop WIN32 main.cpp resources/resources.qrc)
set_target_properties(mif_desktop PROPERTIES AUTOMOC ON AUTORCC ON)
target_link_libraries(mif_desktop PRIVATE mif_desktop_ui)

# 此组件安装前缀就是 app/；Windows 下的运行库收集同时处理核心 DLL 和 Qt 插件。
install(TARGETS mif_desktop RUNTIME DESTINATION . COMPONENT Desktop)
mif_install_runtime(mif_desktop . Desktop)
mif_install_notices(Desktop)

