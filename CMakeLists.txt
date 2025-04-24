cmake_minimum_required(VERSION 3.1)

project(safe-udp)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")

set(CMAKE_INSTALL_RPATH "${PROJECT_BINARY_DIR}/lib")

# 添加 chainbuffer 头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/chainbuffer)
# 添加 udp_transport 头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/udp_transport)

# 定义 chainbuffer 源文件
set(CHAINBUFFER_SRC
    chainbuffer/chain_buffer.c
)

# 添加 chainbuffer 静态库
add_library(chainbuffer STATIC ${CHAINBUFFER_SRC})

add_subdirectory(udp_transport)
add_subdirectory(test)