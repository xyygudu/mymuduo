# mymuduo
c++17实现简易muduo网络库

由于用到了std::any，而std::any是在c++17才开始支持

# muduo讲解

[Muduo讲解](https://www.zhihu.com/column/c_1650978393704353792)



# 编译动态链接库

根目录的build.sh和example的Makefile中把项目路径修改为自己当前项目的路径，然后执行如下代码生成动态链接库

```
sudo ./build.sh
```

# 测试
```
cd example
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

如果测试日志打印到文件中，则可以把EchoServer.cc的main函数中，`g_logFile.reset(new LogFile("log", 500*1000*1000, true, 3, 1024));`最后一个参数1024改为10或者更小，这样在vscode中打开这个文件才能看到日志实时写入了文件


# 效果

EchoServer测试时间轮

![时间轮打印连接](https://github.com/xyygudu/mymuduo/blob/main/image/timewheel.png)


Http测试效果

![http](https://github.com/xyygudu/mymuduo/blob/main/image/http.png)
