cmake_minimum_required(VERSION 3.10)
project(ctpl_distributed VERSION 1.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 寻找依赖库
find_package(Boost REQUIRED COMPONENTS lockfree)
find_package(ZeroMQ REQUIRED)

# 头文件目录
include_directories(${Boost_INCLUDE_DIRS} ${ZeroMQ_INCLUDE_DIRS})

# 创建静态库
add_library(ctpl STATIC ctpl.h ctpl.cpp)
add_library(ctpl_distributed STATIC ctpl_distributed.h ctpl_distributed.cpp)

# 链接库
target_link_libraries(ctpl_distributed ${Boost_LIBRARIES} ${ZeroMQ_LIBRARIES} ws2_32.lib Iphlpapi.lib)

# 创建示例可执行文件
add_executable(example example.cpp)
target_link_libraries(example ctpl)

add_executable(example_distributed example_distributed.cpp)
target_link_libraries(example_distributed ctpl_distributed ctpl)

# 安装配置
install(TARGETS ctpl ctpl_distributed example example_distributed
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(FILES ctpl.h ctpl_distributed.h DESTINATION include/ctpl)
