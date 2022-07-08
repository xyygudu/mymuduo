cmake_minimum_required(VERSION 2.5)
PROJECT(mymuduo)

# mymuduo最终编译成so动态库 设置动态库的路径 放置项目根目录的lib文件夹下面
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置调试信息  以及启动C++11语言标准
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGES} -g -std=c++11")


# 定义参与编译的源代码文件
AUX_SOURCE_DIRECTORY(. SRC_LIST)  # "."表示当前文件

# 编译动态库
ADD_LIBRARY(mymuduo SHARED ${SRC_LIST})

# add_executable(testinet Timestamp.cc)