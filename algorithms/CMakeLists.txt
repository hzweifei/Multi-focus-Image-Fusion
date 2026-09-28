add_library(mif_core STATIC
    src/fusion.cpp
    src/focus_measure.cpp
    src/weight_map.cpp
    src/blending.cpp
    src/registration.cpp
)
add_library(mif::core ALIAS mif_core)
target_include_directories(mif_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(mif_core PUBLIC ${OpenCV_LIBS})
target_compile_features(mif_core PUBLIC cxx_std_17)

