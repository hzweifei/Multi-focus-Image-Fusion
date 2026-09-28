# 示例仅通过公开 mif::core 接口调用算法，不引用内部实现头文件。
add_executable(mif_example fuse_images.cpp)
target_link_libraries(mif_example PRIVATE mif::core)
install(TARGETS mif_example RUNTIME DESTINATION . COMPONENT Examples)
mif_install_runtime(mif_example . Examples)
