# 设置cmake的最低版本
cmake_minimum_required(VERSION 3.13)

# 设置项目名
project(demo_test)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -Wall -Ofast -Wfatal-errors -D_MWAITXINTRIN_H_INCLUDED -g")

set(CMAKE_BUILD_TYPE debug)

# 添加头文件路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/
)

# 添加依赖库路径
link_directories(
    /usr/local/lib/
)

# 添加可执行目标
add_executable(udp_demo  ./udp_demo.cpp)
target_link_libraries(udp_demo LINK_PRIVATE pthread)