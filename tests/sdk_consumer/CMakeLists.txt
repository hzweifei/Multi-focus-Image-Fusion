# 独立消费方工程：只能通过已安装 SDK 获取接口与库，用于检查 SDK 是否可搬移。
cmake_minimum_required(VERSION 3.21)
project(MifSdkConsumer LANGUAGES CXX)
# 声明本工程使用的接口系列，避免与不同参数布局的 SDK 混用。
find_package(Mif 0.3 CONFIG REQUIRED)
add_executable(sdk_consumer main.cpp)
target_link_libraries(sdk_consumer PRIVATE mif::core)
