<a name="rOxT7"></a>
## Muduo简介
muduo由陈硕大佬开发（源码链接放在文章最后），是一个基于非阻塞IO和事件驱动的C++高并发TCP网络库，使用的**线程模型是one loop per thread**，所谓one loop per thread指的是：

- 一个线程只能有一个事件循环（EventLoop）
- 一个文件描述符（file descriptor，通常简称fd）只能由一个线程进行读写，换句话说就是一个TCP连接必须归属于某个EventLoop管理。但返过来不一样，一个线程却可以管理多个fd。

**为什么要选one loop per thread模型**：（更详细的介绍请参考P62，3.3.1节；P98，4.6节）<br />如果一个TCP链接在多个线程中处理，会出现如下情况：

1. socket被意外关闭。A线程要从socket中读/写消息，但是该socket被B线程给close了，更糟的情况是，B close后，新的连接的socket刚好使用的是B关闭的socket，那A线程再次进行读/写早已经不是原来的那个连接了（这种现象叫串话）
2. 不考虑关闭，只考虑读写也有问题。比如A、B线程同时对一个TCP连接进行读操作，如果两个线程几乎同时各自收到一部分消息，那如何把数据拼接成完整的消息，如何知道哪部分数据先到达。如果同时写一个socket，每个线程只发送半条消息，接收方又该怎么处理，如果加锁，那还不如直接就让一个线程处理算了

**one loop per thread优点**：（P62，3.3.1节）

1. 线程数目基本固定，不用频繁创建或者销毁线程
2. 可以很方便的在各个线程之间进行负载调配
3. IO事件发生的线程基本是固定不变的，不必考虑TCP连接事件的并发（即fd读写都是在同一个线程进行的，不是A线程处理写事件B线程处理读事件）

**muduo采用的多Reactor结构**。在muduo中，每一个线程都可以看作是一个Reactor，主线程（主Reactor）只负责监听接收新连接，并将接收的连接对应的fd分发到子Reactor中，子线程（子Reactor）就负责处理主线程分发给自己的fd上的事件，比如fd上发生的可读、可写、错误等事件，另外从fd上读取数据后，要进行的业务逻辑也是由子线程负责的。基本框架如下图所示：<br />![muduo架构.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684649929046-f22fd0ee-356f-4e79-8811-9558a5f7cc5e.svg#clientId=uf5f0e048-0e5a-4&from=paste&height=324&id=ued9364af&originHeight=261&originWidth=375&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=39047&status=done&style=none&taskId=u8b19c9b7-11aa-4c3b-8733-0a64165316c&title=&width=466)
<a name="jjXCG"></a>
## 重构muduo网络库
Muduo基本是当前C++后端校招必修项目了，许多人都用C++11重构了Muduo网络库，我也重构了该网络库，主要是为了理解muduo设计的精髓之处，也是为了学习怎么实现高并发。
:::tips
和官方muduo相比，重构的muduo更多的是对官方muduo的一个简化，基本没做什么修改，主要是为了去除对boost库的依赖、降低理解难度，以及学习muduo优秀的设计（当然，如果能自己扩展或者修改更好）
:::
我在重构muduo时也参考了几位大佬重构的代码，比如`S1mpleBug`和`Shangyizhou`，都写的十分不错，但同他们重构的也有部分小问题，总的来说，我重构的muduo和其他重构的主要有这样几个区别：<br />和`S1mpleBug`相比较：

- 实现了Http请求GET请求的解析。
- 实现了异步日志，避免日志频繁的IO操作阻碍了网络IO的高并发
- 实现了定时器功能，可用于设计心跳机制，定时清理非活动连接。

和`Shangyizhou`相比较：

- 修复了日志写入文件不正确的BUG
- 对照官方源码更正了部分代码错误的情况
- 更正了部分回调函数没有初始化就调用的BUG
- 修改、添加了更详细的注释
<a name="Vt6hr"></a>
## Muduo讲解顺序
没有特别说明的情况下，我讲的都是自己重构版本的muduo。讲解主要从一下几个部分展开：

- 第一部分，从一个最基础的且有epoll的网络编程例子开始，一方面可以回顾一下网络编程的思路，另一方面可以引出muduo部分类的封装。
- 第二部分，介绍第一部分引出的类的实现，另外也会补充一些第一部分没有引出的类及其实现。
- 第三部分，了解muduo主要类的设计及其功能后，我们从一个简单的EchoServer开始，按照启动服务器、连接建立、消息收发、连接关闭的顺序讲解muduo网络库工作流程。
- 第四部分，知道muduo网络库的工作流程后，还有一些muduo网络库的设计精华以及编程细节就在本节讨论。

> 在正式讲解源码之前，默认已经学会下面的基本知识：（不会自己补习也很快）

> - 熟悉最基本的TCP网络通信编程流程和API，比如socket、bind、listen、accept、recv、send
> - 知道epoll相关函数的用法，比如epoll_create，epoll_wait
> - 了解Reactor事件处理模式
> - 知道C++11常见新特性，如只能指针，function和bind等

<a name="ORzFu"></a>
## 相关链接
陈硕（官方）：[https://github.com/chenshuo/muduo/](https://github.com/chenshuo/muduo/)<br />自己重构（带有大量注释）：[https://github.com/xyygudu/mymuduo](https://github.com/xyygudu/mymuduo)<br />Shangyizhou（应用到了实际场景，功能完整）：[https://github.com/Shangyizhou/A-Tiny-Network-Library](https://github.com/Shangyizhou/A-Tiny-Network-Library)<br />S1mpleBug（有视频讲解）：[https://github.com/S1mpleBug/muduo_cpp11](https://github.com/S1mpleBug/muduo_cpp11)<br />参考书目：《Linux多线程服务器编程-使用 muduo C++网络库》-陈硕、《Linux高性能服务器编程》-游双



