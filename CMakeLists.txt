# 指定使用 CMake 的最低版本号
cmake_minimum_required(VERSION 3.0)

# 设置项目名称
project(mymuduo C CXX)

# 添加搜索路径
include_directories(
    ${PROJECT_SOURCE_DIR}/src/base
    ${PROJECT_SOURCE_DIR}/src/net
    ${PROJECT_SOURCE_DIR}/src/logger
    ${PROJECT_SOURCE_DIR}/src/http
    )

# 包含这些目录下的.cc文件
aux_source_directory(${PROJECT_SOURCE_DIR}/src/base SRC_BASE)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/net SRC_NET)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/logger SRC_LOG)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/http SRC_HTTP)


# mymuduo最终编译成so动态库 设置动态库的路径 放置项目根目录的lib文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 设置编译选项
set(CXX_FLAGS -g -Wall -std=c++11)

# 生成动态库 mymuduo
add_library(mymuduo SHARED  ${SRC_BASE} ${SRC_NET} ${SRC_LOG} ${SRC_HTTP})

# 目标动态库所需连接的库（这里需要连接libpthread.so）
target_link_libraries(mymuduo pthread)

# 设置安装的默认路径
# set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR})
# install(TARGETS mymuduo LIBRARY DESTINATION lib)
# # 将目录安装到 include/tiny_network 下（只安装头文件）
# install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION include
#         FILES_MATCHING PATTERN "*.h")


