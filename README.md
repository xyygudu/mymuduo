# mymuduo
c++11实现简易muduo网络库

#muduo讲解
[Muduo概述](./muduo讲解/Muduo概述.md)

# 编译动态链接库

根目录的build.sh和example的Makefile中把项目路径修改为自己当前项目的路径，然后执行如下代码生成动态链接库

```
sudo ./build.sh
```

# 测试
```
cd examle
make
./EchoServer
```

如果执行./EchoServer报如下错误，表示找不到动态链接库

```
./EchoServer: error while loading shared libraries: libmymuduo.so: cannot open shared object file: No such file or directory
```
则需要执行下面代码临时指定动态链接库的目录

```
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:你项目的目录/lib
```


