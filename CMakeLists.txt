cmake_minimum_required(VERSION 3.15)

# 项目名称
#   项目可以包含多个目标(target)
#   target: 可执行程序, 静态库, 动态库
project(CloudDisk)
find_package(ppconsul CONFIG REQUIRED)

# 指定C++的版本: -std=c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置可执行程序生成的路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../)

# 生成的可执行程序
add_executable(server
        src/main.cpp
        src/infra/Config.cpp
        src/infra/BackupPublisher.cpp
        src/infra/CryptoUtil.cpp
        src/infra/LocalStorage.cpp
        src/infra/OssManager.cpp
        src/routes/AuthRoutes.cpp
        src/routes/FileRoutes.cpp
        src/routes/UserRoutes.cpp
        src/auth_server/auth.pb.cc
        src/server/CloudDiskServer.cpp)

target_include_directories(server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/infra
        ${CMAKE_CURRENT_SOURCE_DIR}/include/routes
        ${CMAKE_CURRENT_SOURCE_DIR}/include/server
        ${CMAKE_CURRENT_SOURCE_DIR}/include/auth_server)

# 设置编译选项
# PRIVATE: 传播属性，一般填PRIVATE
target_compile_options(server PRIVATE -g -fno-rtti)

# -lcrypto -lssl -ljwt -lwfrest
target_link_libraries(server PRIVATE crypto ssl jwt wfrest sodium)
target_link_libraries(server PRIVATE alibabacloud-oss-cpp-sdk curl crypto pthread SimpleAmqpClient)
target_link_libraries(server PRIVATE srpc lz4 snappy protobuf workflow)

add_executable(backup
        src/backup_client.cpp
        src/infra/BackupPublisher.cpp
        src/infra/Config.cpp
        src/infra/OssManager.cpp)

target_include_directories(backup PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/infra)

target_compile_options(backup PRIVATE -g -fno-rtti)
target_link_libraries(backup PRIVATE alibabacloud-oss-cpp-sdk curl crypto pthread SimpleAmqpClient)

add_executable(auth_server
        src/infra/Config.cpp
        src/infra/CryptoUtil.cpp
        src/auth_server/auth.pb.cc
        src/auth_server/AuthServer.cpp)

target_include_directories(auth_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/auth_server
        ${CMAKE_CURRENT_SOURCE_DIR}/include/infra)

target_link_libraries(auth_server PRIVATE crypto ssl jwt sodium)
target_link_libraries(auth_server PRIVATE srpc lz4 snappy protobuf workflow)
target_link_libraries(auth_server PRIVATE ppconsul)
