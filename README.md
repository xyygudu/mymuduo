# mymuduo
c++11实现简易muduo网络库

# muduo讲解
[Muduo概述](./muduo讲解/Muduo概述.md)
[第一部分：从最基本例子开始](./muduo讲解/第一部分：从最基本例子开始.md)
[第二部分（1）：Muduo的主要类](./muduo讲解/第二部分（1）：Muduo的主要类.md)
[第二部分（2）：Muduo的Buffer](./muduo讲解/第二部分（2）：Muduo的Buffer.md)
[第二部分（3）：Muduo日志库](./muduo讲解/第二部分（3）：Muduo日志库.md)
[第二部分（4）：Muduo时间轮](./muduo讲解/第二部分（4）：Muduo时间轮.md)
[第三部分：从EchoServer开始](./muduo讲解/第三部分：从EchoServer开始.md)
[第四部分：Muduo精华](./muduo讲解/第四部分：Muduo精华.md)


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


