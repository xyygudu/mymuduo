#!/bin/bash

set -e

# 如果没有build目录 创建该目录
if [ ! -d `pwd`/build ]; then
    mkdir `pwd`/build
fi


# # 如果没有 include 目录 创建该目录
# if [ ! -d `pwd`/include ]; then
#     mkdir `pwd`/include
# fi

# 如果没有 lib 目录 创建该目录
if [ ! -d `pwd`/lib ]; then
    mkdir `pwd`/lib
fi

# 删除存在 build 目录生成文件并执行 cmake 命令
rm -fr `pwd`/build/*
cd  `pwd`/build &&
    cmake .. &&
    make


# 回到项目根目录
cd ..


# 告诉系统libmymuduo.so的目录(记得更改为自己项目的根目录/lib)
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/acer/projects/mymuduo/lib

# # 把头文件拷贝到 /usr/include/mymuduo       .so库拷贝到 /usr/lib
# if [ ! -d /usr/include/mymuduo ]; then
#     mkdir /usr/include/mymuduo
# fi

# # 将头文件复制到 /usr/include
# cp `pwd`/include/ -r /usr/include/mymuduo

# # 将动态库文件复制到/usr/lib
# cp `pwd`/lib/libmymuduo.so /usr/lib

ldconfig
