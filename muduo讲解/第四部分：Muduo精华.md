<a name="ns6UU"></a>
## 怎么保证one loop per thread?
保证one loop per thread需要有两个要求：1）保证一个线程只有一个EventLoop对象。2）保证不该跨线程调用的函数不会跨线程调用。
<a name="aH2Jy"></a>
### 保证一个线程只有一个EventLoop对象
我们只需要使用`__thread`关键字，通过`__thread`修饰的变量，`__thread`变量每一个线程有一份独立实体，各个线程的值互不干扰（更多关于该关键字的解释见P97，4.5节）。为了保证一个线程只有一个EventLoop对象，muduo定义了一个用该关键字修饰的EventLoop*变量，即`__thread EventLoop *t_loopInThisThread = nullptr`，当某个子线程启动时，我们只需要在子线程执行函数（即）中构造一个EventLoop对象，EventLoop的构造函数会把`t_loopInThisThread`初始化为this，这样一来，如果该线程中再次创建一个EventLoop对象时，就会在其构造函数中终止程序（即执行了`LOG_FATAL`）（参考P278，8.0节）。
```cpp
// EventLoopThread.cc 子线程（子Reactor）执行函数
void EventLoopThread::threadFunc()
{
    EventLoop loop;
    ...省略代码
}

// EventLoop.cc
__thread EventLoop *t_loopInThisThread = nullptr;
EventLoop::EventLoop() : threadId_(CurrentThread::tid())
{
    if (t_loopInThisThread)
    {
        LOG_FATAL << "Another EventLoop" << t_loopInThisThread << " exists in this thread " << threadId_;
    }
    else
    {
        t_loopInThisThread = this;
    }
    ...省略代码
}
```
<a name="oDMTP"></a>
### 保证不该跨线程调用的函数不会跨线程调用
<a name="ie1mv"></a>
#### 核心思想
muduo保证on loop per thread的核心就是：每个线程（EventLoop）都准备一个待执行队列，用于存储待执行函数，当其他线程需要调用某个不能跨线程调用的函数时（不能跨线程调用的函数比如有`TcpConnection::sendInLoop`、`TimerQueue::addTimerInLoop`、`TcpConnection::shutdownInLoop`、`TcpConnection::connectEstablished`，因为他们都涉及到对fd的操作），只需要把该函数通过runInLoop或者queInLoop加入到本线程的待执行队列中，然后由本线程从队列中取出该函数并执行。<br />![oneloopperthrea.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1685975882951-908107aa-9337-4b3e-841a-53525cfeff94.svg#clientId=ue5c8f490-c8e0-4&from=paste&height=290&id=u600768d8&originHeight=362&originWidth=636&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=18320&status=done&style=none&taskId=u523d5149-f19a-4134-ab47-13edf2511e4&title=&width=508.8)
<a name="hj1mJ"></a>
#### 以跨线程发送数据为例
遵循每个文件描述符只能由一个线程来操作的原则，我们对sockfd的操作只能在一个线程中进行，比如我们要向某个连接对应的sockfd发送消息（调用`TcpConnection::send`函数），这涉及到在sockfd上进行写操作，就不能跨线程进行，但是我们无法知道使用网络库的人会不会在跨线程调用`TcpConnection::send`，因此，就需要我们在网络库层面做到：即使用户跨线程调用`TcpConnection::send`也能够保证sockfd写操作不会跨线程进行。这怎么做到的呢？我们先来看下`TcpConnection::send`在干嘛。
```cpp
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {   
        // 如果当前执行的线程就是自己所属loop绑定的线程，则可以直接发送数据
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf));
        }
    }
}
```
可以看到send函数在调用sendInLoop发送数据之前，会先**判断调用send函数的线程和TcpConnection所属loop_绑定的线程是否是同一个**（每个TcpConnection都有个loop_成员变量记录自己所属哪个EventLoop，EventLoop在构造的时候也会用threadId_记录下自己属于哪个线程，所以`loop_->isInLoopThread()`就能判断loop_自己绑定的（或所属的）线程和当前调用`TcpConnection::send`函数的线程是否是同一个线程），如果是同一个，说明并没有跨线程调用`TcpConnection::send`，因此可以直接调用sendInLoop发送数据（sendInLoop函数内部会对sockfd进行写操作），**如果不是同一个**，则需要借助EventLoop的runInLoop来保证sendInLoop函数不会跨线程调用。为了进一步了解EventLoop的runInLoop是怎么保证sendInLoop函数不会跨线程调用的，我们先来了解一下EventLoop的两个关键函数：runInLoop和queueInLoop。
<a name="EQki0"></a>
#### EventLoop::runInLoop
EventLoop除了之前提到的loop和quit函数，还有两个用于保证线程安全的函数runInLoop和queueInLoop，代码如下，这里先展示runInLoop函数：
```cpp
void EventLoop::runInLoop(Functor cb)
{
    // 如果当前调用runInLoop的线程正好是EventLoop绑定的线程，则直接执行此函数,
    // 否则就将回调函数通过queueInLoop()存储到pendingFunctors_中
    if (isInLoopThread()) { cb(); }
    else { queueInLoop(cb); }
}
```
**runInLoop的功能是：如果调用runInLoop的线程和EventLoop初始化时绑定的线程是同一个线程，那么可以直接执行回调函数cb，否则，就调用**`queueInLoop`**把cb加入到待执行队列（即**`**pendingFunctors_**`**）中。**<br />接着以调用`TcpConnection::send`函数为例，如果是跨线程调用`TcpConnection::send`，那么`TcpConnection::send`函数一定会调用`loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf))`（见上面`TcpConnection::send`函数内部实现），也就是说，执行runInLoop函数的线程和执行`TcpConnection::send`函数的线程是同一个，而且runInLoop函数的形参cb此时就是sendInLoop函数，在runInLoop函数中，会先执行`isInLoopThread()`判断一下调用runInLoop的线程和EventLoop绑定的线程是否是同一个，这里举的例子由于是跨线程，因此不是同一个，于是就会执行`queueInLoop(cb)`。接下来我们看下queueInLoop做了什么。
<a name="bDbXy"></a>
#### EventLoop::queueInLoop
**queueInLoop的功能是：直接把回调函数加入**`**pendingFunctors_**`**中，并在必要的时候唤醒EventLoop绑定的线程**。其中，`pendingFunctors_`就是一个容器，类型为：std::vector<std::function<void()>>，用来存放待执行的函数。
```cpp
void EventLoop::queueInLoop(Functor cb)
{
    {   
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }
    if (!isInLoopThread() || callingPendingFunctors_)  
    {
        wakeup();   
    }
}
```
继续前面跨线程调用`TcpConnection::send`为例，由于`TcpConnection::send`调用`EventLoop::runInLoop`，`EventLoop::runInLoop`又调用`EventLoop::queueInLoop`，所以这里执行queueInLoop的线程就是执行`TcpConnection::send`的线程，即执行queueInLoop的线程**不是**EventLoop初始化时绑定的线程，也就满足的`!isInLoopThread()`，于是需要唤醒EventLoop绑定的线程，让它去从`pendingFunctors_`中取出cb（这里的例子cb就是sendInLoop函数），并执行。接下来看下wakeup()函数到底做了什么，是怎么唤醒的。
:::tips
`pendingFunctors_`是多线程共享资源，其他线程负责往`pendingFunctors_`中存储待执行函数，本线程就负责从`pendingFunctors_`把函数取出来执行，相当于一个生产者消费者模型，所以再向`pendingFunctors_`中添加待执行函数时需要加锁。
:::
<a name="xP71H"></a>
#### EventLoop::wakeup()
**为什么需要唤醒？**<br />如下`EventLoop::loop`函数所示，由于子Reactor（子线程）管理的fd上没有任何事件发生时，该线程的EPoller就会一直阻塞在poll函数这里，也就无法执行doPendingFunctors函数了（该函数功能是从`pendingFunctors_`中取出函数来执行）。
```cpp
void EventLoop::loop()
{
    while (!quit_)
    {
        activeChannels_.clear();
        epollReturnTime_ = epoller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            channel->handleEvent(epollReturnTime_);
        }
        // 执行其他线程添加到pendingFunctors_中的函数
        doPendingFunctors();
    }
    looping_ = false;
}
```
**怎么唤醒？**<br />我们要唤醒EventLoop所在线程，只需要让EventLoop不要阻塞在poll函数这里即可，要达到该目的，我们就需要让EPoller上注册的任意Channel有事件发生。具体如下：<br />每个EventLoop都有一个wakeupFd_，同样把该fd封装成Channel，变成wakeupChannel_。在EventLoop构造时我们就为wakeupChannel_在EPoller上注册可读事件，所以当在queueInLoop函数中调用wakeup时，只需要在wakeupFd_上写入数据(见下面wakeup代码)，这样EPoller就会检测到wakeupFd_可读，于是就解除了阻塞，进而执行`doPendingFunctors`，即取出`pendingFunctors_`中存储的sendInLoop函数并执行，这样就实现了：即使在别的线程调用`TcpConnection::send`函数，也能确保sendInLoop函数（即对sockfd的写操作）能在EventLoop所属的线程中执行。
```cpp
void EventLoop::wakeup()
{
    // wakeup() 的过程本质上是对wakeupFd_进行写操作，以触发该wakeupFd_上的可读事件,
    // 这样就起到了唤醒 EventLoop 的作用。
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
}
```

<a name="irJUt"></a>
## Channel的tie_涉及到的精妙之处
在没有tie_的时候，当客户端主动关闭连接时，服务器上该连接对应的sockfd上就会有事件发生，也就是该sockfd对应的Channel会调用Channel::handleEvent()来关闭连接，关闭连接就会释放释放TcpConnection，进而Channel对象也会析构，这就造成了Channel::handleEvent()还只执行到一半，Channel就已经被析构了，引发不可预测的后果，为了避免这个问题，可以使用弱指针tie_绑定到TcpConnection的共享指针上，如果tie_能够被转化为TcpConnection共享指针，这就延长了TcpConnection的生命周期，使之长过Channel::handleEvent()，这样一来，Channel::handleEvent()来关闭连接时，下面代码的guard变量依然持有一份TcpConnection，使其的引用计数不会减为0，也就是说Channel不会在执行完Channel::handleEvent()之前被析构（**参考P274 7.15.3节**）。
```cpp
void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
}

// 根据相应事件执行回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    // 对方关闭
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        // 如果没有tie_，可能程序执行到这里就已经释放了Channel对象，如果
        // 继续往下执行，会引发不可预知的后果
        if (closeCallback_) closeCallback_();
    }
    ...省略处理其他事件的代码（如可读、可写）
}
```
<a name="CDOJo"></a>
## 怎么处理fd耗尽的情况？
> 参考P238 7.7节

- 当TCP连接数超过设定值时，给客户端发送一个消息，告知客户端自己已经过载，然后调用`TcpConnection::shutdown`关闭连接。
- 打开一个空文件描述符`idleFd_`，用于占位，这是因为，如果连接过多，该进程可用的fd都被占用完毕，那么就无法接收新的连接了，既然没有接收该连接，那么此时服务器也无法告知客户端发自己生了什么情况，为了让客户端更好的处理，可以先暂时关闭`idleFd_`，然后接收这个连接，接收后，立马close它，这样就实现了优雅的拒绝新来的连接，关闭该连接后，重新打开一个空文件描述符，把坑位继续占住。
<a name="qhUdD"></a>
## 怎么保证关闭连接时数据不会漏收？
> （参考P137 6.4.1节和P192 7.2节)

先说下muduo是怎么关闭连接的。muduo永远都是被动关闭连接：即等待对方关闭后（无论是只shutdowWrite还是close），自己才关闭连接，即使muduo主动关闭连接，都还是只关闭自己的写端，等对方关闭后，在关闭自己的读端。所以这种关闭连接的方式要求对方read到0字节后（read到0字节表示对方已经关闭了）主动关闭连接。<br />**当muduo主动关闭连接时，即调用**`**TcpConnection::shutdown**`**函数时，并不会直接close掉该连接对应的sockfd，而是只关闭sockfd的写端，这样sockfd不可继续发送数据，如果路上还有数据客户端没有收到，客户端就会一直read，直到客户端read到0字节时，表示路上的所有数据已经收完，即避免了数据漏收。**<br />但是此时muduo还处于半关闭状态（即服务器该连接的读端还没有关闭），此时要求客户端read到0字节时，主动关闭连接（只关闭写端或者直接close都行），这样，服务器上该连接对应的Channel就会触发EPOLLHUP事件，该事件会执行`TcpConnection::handleClose()`，它执行完后会析构该连接对应的TcpConnection对象，TcpConnection析构时，其成员Socket也会析构，Socket析构的时候会调用close来完全关闭该连接。<br />当muduo被动关闭连接时，即客户端先关闭连接，此时服务器就一直handlRead，直到read到0字节时，说明客户端发送的数据已经全部接收了，此时，服务器也会调用`TcpConnection::handleClose()`关闭连接。
<a name="hVGGT"></a>
## 怎么保证数据关闭连接前数据发送完毕？
> （参考P192 7.2节)

**总的来说，muduo在关闭连接时，会看下output buffer中是否还有数据没有发送完毕，如果还有没发送完的，则等待数据发送完毕后再调用TcpConnection::shutdown关闭连接。**具体如下：<br />当用户调用TcpConnection::shutdown时（TcpConnection::shutdown只关闭自己的写端），该函数会判断EPoller是否还关注了该连接的可写事件，关注了，就说明还有数据没有发送完毕，此时该函数（即TcpConnection::shutdown）仅仅把连接状态改变成kDisconnecting就结束了，当再次有可写事件发生时（再次调用TcpConnection::handleWrite时），如果outputBuffer_中的数据发送完毕，则可以把可写事件从EPoller中注销掉，此时，如果连接状态是kDisconnecting，此时就可以再次调用TcpConnection::shutdownInLoop，该函数只关闭写端，要完整的关闭连接，此时就需要TcpConnection::handleRead读取到0字节或者fd上发生的事件是EPOLLHUP



