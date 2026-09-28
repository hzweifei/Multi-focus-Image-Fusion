add_library(mif_desktop_ui STATIC
    main_window.cpp main_window.hpp main_window.ui
    image_io.cpp image_io.hpp
    widgets/image_view.cpp widgets/image_view.hpp
    workers/fusion_worker.cpp workers/fusion_worker.hpp
)
set_target_properties(mif_desktop_ui PROPERTIES AUTOMOC ON AUTOUIC ON)
target_include_directories(mif_desktop_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mif_desktop_ui PUBLIC mif::core Qt${QT_VERSION_MAJOR}::Widgets)

add_executable(mif_desktop WIN32 main.cpp resources/resources.qrc)
set_target_properties(mif_desktop PROPERTIES AUTOMOC ON AUTORCC ON)
target_link_libraries(mif_desktop PRIVATE mif_desktop_ui)

