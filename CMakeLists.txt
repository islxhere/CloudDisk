cmake_minimum_required(VERSION 3.15)

# 项目名称
#   项目可以包含多个目标(target)
#   target: 可执行程序, 静态库, 动态库
project(CloudDisk)

# 指定C++的版本: -std=c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置可执行程序生成的路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../)

# 生成的可执行程序
add_executable(server
    AuthRoutes.cpp
    Config.cpp
    CloudDiskServer.cpp
    CryptoUtil.cpp
    FileRoutes.cpp
    OssManager.cpp
    UserRoutes.cpp
    main.cpp)

# 设置编译选项
# PRIVATE: 传播属性，一般填PRIVATE
target_compile_options(server PRIVATE -g -fno-rtti)

# -lcrypto -lssl -ljwt -lwfrest
target_link_libraries(server PRIVATE crypto ssl jwt wfrest sodium)
target_link_libraries(server PRIVATE alibabacloud-oss-cpp-sdk curl crypto pthread)
