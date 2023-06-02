在第二部分介绍完muduo的主要内及其作用后，我们从一个简单的EchoServer开始，按照启动服务器、连接建立、消息收发、连接关闭的顺序讲解muduo网络库工作流程。<br />先看下EchoServer的代码：
```cpp
class EchoServer
{
public:
    EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
        : loop_(loop)
        , server_(loop, addr, name)
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        // 设置合适的subloop线程数量
        server_.setThreadNum(3);
    }
    void start()
    {
        server_.start();
    }
private:
    // 连接建立或断开的回调函数
    void onConnection(const TcpConnectionPtr &conn)   
    {
        // 自定义要干的事
    }
    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(msg);
        // conn->shutdown();   // 关闭写端
    }
    EventLoop *loop_;
    TcpServer server_;
};

int main() 
{
    EventLoop loop;
    InetAddress addr(8002);
    EchoServer server(&loop, addr, "EchoServer");
    server.start();
    loop.loop();
    return 0;
}
```
main函数中开辟一个EventLoop对象loop，该对象的threadId_成员在构造函数中就会和当前线程（主线程）的tid相互绑定，为什么主线程也要一个EventLoop呢？因为主线程也有个EPoller，专门负责listenfd上的可读事件。
<a name="U6kbg"></a>
## 启动服务器
当main函数中调用`server.start()`时，就启动了所有子线程（子Reactor），此时所有子线程都启动后，都处于阻塞在EPoller上，等待EPoller负责的Channel上有事件发生。下面详细看下是如何到达这一步的。<br />在主线程中我们调用`server.start();`，它调用了`EventLoopThreadPool::start`函数
```cpp
void TcpServer::start()
{
    if (started_++ == 0)
    {
        // 启动底层的lopp线程池
        threadPool_->start(threadInitCallback_);
        // acceptor_.get()绑定时候需要地址
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}
```
我们继续来看下`threadPool_->start(threadInitCallback_);`是怎么启动子线程的，代码如下：
```cpp
void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    // 循环创建线程
    for(int i = 0; i < numThreads_; ++i)
    {
        EventLoopThread *t = new EventLoopThread(cb, buf);
        // 加入此EventLoopThread入容器
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        // 底层创建线程 绑定一个新的EventLoop 并返回该loop的地址
        // 此时已经开始执行新线程了
        loops_.push_back(t->startLoop());                           
    }
    // 整个服务端只有一个线程运行baseLoop
    if(numThreads_ == 0 && cb)                                      
    {
        // 那么不用交给新线程去运行用户回调函数了
        cb(baseLoop_);
    }
}
```
在上面代码的循环中，会开辟一个`EventLoopThread`对象t，并调用了`t->startLoop()`，`startLoop()`代码如下：
```cpp
EventLoop* EventLoopThread::startLoop()
{
    // 启动一个新线程。在构造函数中，thread_已经绑定了回调函数threadFunc
    // 此时，子线程开始执行threadFunc函数
    thread_.start();
    EventLoop *loop = nullptr;
    {
        // 等待新线程执行创建EventLoop完毕，所以使用cond_.wait
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == nullptr)
        {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}


void EventLoopThread::threadFunc()
{
    EventLoop loop;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop; // 等到生成EventLoop对象之后才唤醒主线程，即startLoop()函数才能继续执行
        cond_.notify_one();
    }
    // 执行EventLoop的loop() 开启了底层的EPoller的poll()
    // 这个是subLoop
    loop.loop();   
    // loop是一个事件循环，如果往下执行说明停止了事件循环，需要关闭eventLoop
    // 此处是获取互斥锁再置loop_为空
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}
```
可以看到，`startLoop`函数启动了子线程，子线程绑定的回调函数是`EventLoopThread::threadFunc`，所以，此时子线程正则执行`EventLoopThread::threadFunc`，主线程继续往下执行`startLoop`，可以看到，主线程（`startLoop`函数中）中，通过条件变量等待`loop_`不为空。<br />那么为什么要等待`loop_`不为空？<br />主要是因为`startLoop`需要返回子线程所属的EventLoop，所以就必须等待子线程成功创建出一个EventLoop对象。具体来说，从子线程执行的`EventLoopThread::threadFunc()`可见，子线程会创建一个loop局部变量，由于loop是在子线程中创建的，`loop`的`threadId_`成员就会在`EventLoop`的构造函数中被初始化为子线程的tid。子线程创建好`loop`后，把自己的地址传给`loop_`，然后通知主线程自己已经创建好了（绑定了）EventLoop，这样，主线程也就会解除阻塞，顺利返回子线程绑定的EventLoop。<br />在子线程中，会执行`loop.loop();`，这时，子线程进入了死循环，只要死循环不退出（即`loop.loop()`不执行结束）`EventLoopThread::threadFunc`也不会继续往下执行。`EventLoop::loop`代码如下：
```cpp
void EventLoop::loop()
{
    quit_ = false;
    while (!quit_)
    {
        activeChannels_.clear();
        // 有事件发生的channel都添加到activeChannels_中，poll函数内部其实就是epoll_wait
        epollReturnTime_ = epoller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            channel->handleEvent(epollReturnTime_);
        }
        ...省略代码
    }
}
```
此时，由于还没有任何TCP连接到来，子线程的epoller上还没有注册任何sockfd上的感兴趣事件，所以会被阻塞在`epoller_->poll`函数这里。<br />到这里，main函数的`server.start()`就执行结束了，接下来main函数（主线程）继续执行`loop.loop()`，同样，主线程也阻塞在了`epoller_->poll`中，等待listenfd变得可读。
<a name="Ydayy"></a>
## 连接建立
服务器启动后，主线程和子线程都处于阻塞状态，当有新连接到来时，主线程的EPoller检测到listenfd可读，主线程解除阻塞，执行listenfd对应Channel上的可读事件回调函数，即`Acceptor::handleRead()`，该函数接收连接得到新连接的sockfd，并执行`TcpServer::newConnection`函数，该函数最核心的就是把该连接分发给其中一个子Reactor来处理该连接上发生的事件。
```cpp
// 有一个新用户连接，acceptor会执行这个回调操作，负责将mainLoop接收到的请求连接(acceptChannel_会有读事件发生)通过回调轮询分发给subLoop去处理
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 1.轮询算法 选择一个subLoop 来管理connfd对应的channel
    EventLoop *ioLoop = threadPool_->getNextLoop();
    ...省略代码
    // 2.把新连接sockfd打包成TcpConnection
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    connections_[connName] = conn;
    // 3. 为该连接设置对应回调函数
    // 下面的回调都是用户设置给TcpServer => TcpConnection的，至于Channel绑定的则是TcpConnection设置的四个，handleRead,handleWrite... 这下面的回调用于handlexxx函数中
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
	// 4.在subloop（这里的ioLoop）建立连接（其实就是在ioLoop的EPoller中为该连接注册可读事件，这就是分发）
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}
```
上面代码中，ioLoop就是轮询选择出来的子EventLoop(子Reactor)，当调用`ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));`时，runInLoop函数会唤醒ioLoop所在的子线程，也就是让子线程**不在阻塞**在`epoller_->poll`上，解除阻塞后，子线程可以执行`TcpConnection::connectEstablished`函数（至于怎么唤醒的，我会在第四部分："怎么保证one loop per thread"中讲解），该函数就是把新连接conn上的可读事件注册到ioLoop对应的EPoller上，代码如下，其中channel_就是新连接conn对应sockfd封装而成的。
```cpp
// 连接建立（执行在子线程）
void TcpConnection::connectEstablished()
{
    setState(kConnected); // 建立连接，设置一开始状态为连接态
    channel_->tie(shared_from_this());
    channel_->enableReading(); // 向Epoller注册channel的EPOLLIN读事件
    // 新连接建立 执行回调，即EchoServer::onConnection函数
    connectionCallback_(shared_from_this());
}
```
`TcpConnection::connectEstablished`执行结束后，子线程由于处于死循环中，又会继续阻塞在`epoller_->poll`处，需要指出的是，此时子线程的Epoller上已经开始关注新连接对应channel上的可读事件了。另外，主线程执行完`TcpServer::newConnection`函数，也会继续阻塞在`epoller_->poll`，等待新连接到来（等待listenfd可读）。
<a name="RVXLT"></a>
## 收发消息
<a name="xCDWT"></a>
### 接收消息
当上面新接收的连接有消息到来时，管理这个连接的子线程的EPoller就会检测到该连接对应的channel可读，于是就会解除阻塞，去执行该channel上的可读事件，即执行`TcpConnection::handleRead`，核心代码如下：
```cpp
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)                      // 从fd读到了数据，并且放在了inputBuffer_上
    {
        // 已建立连接的用户有可读事件发生了 调用用户传入的回调操作onMessage shared_from_this就是获取了TcpConnection的智能指针
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
}
```
该函数会把该连接对应sockfd上的数据读取出来，存储到我们自己设计的`inputBuffer_`中，然后执行`messageCallback_`回调函数，`messageCallback_`就是绑定的`EchoServer::onMessage`函数，在`onMessage`函数中，我们从`inputBuffer_`中取出所有数据，这样就得到了对方发送来的数据。
:::tips
为什么要`inputBuffer_`，请看第二部分：Muduo的Buffer
:::
<a name="gRIUj"></a>
### 发送消息
在`EchoServer::onMessage`函数中，获得对方接收的消息后，我们继续调用`conn->send(msg)`发送数据，`send`函数核心代码如下，其基本思路是：先判断outputBuffer_中是否还有数据需要发送，如果没有，则直接把要发送的数据msg发送出去。如果有，则把data中的数据添加到outputBuffer_中，并注册可写事件，便于下一次继续发送（注意，本例中`EchoServer::onMessage`是在子线程中执行的哦，不要以为EchoServer所有函数都运行在主线程，另外，更多关于send函数的解释见第二部分）。
```cpp
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;         // 已经发送的数据长度
    size_t remaining = len;     // 还剩下多少数据需要发送
	// 如果注册了可写事件并且之前的数据已经发送完毕
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        ...省略代码
    }

    // 如果数据没有全部发送出去，则把剩余的数据都添加到outputBuffer_中，并向Epoller中注册可写事件
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();  // 目前发送缓冲区剩余的待发送的数据的长度
        // 判断待写数据是否会超过设置的高位标志highWaterMark_
        ...省略代码
        // 将data中剩余还没有发送的数据最佳到buffer中
        outputBuffer_.append((char *)data + nwrote, remaining);  
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件 否则Epoller不会给channel通知epollout
        }
    }
}
```
<a name="Gxh8l"></a>
## 连接关闭
当某个连接关闭时，服务器上管理该连接的子线程的EPoller就检测到该连接对应channel上有EPOLLHUP事件发生，就会执行相应回调`TcpConnection::handleClose`。由于连接使用的是共享指针，该函数执行完后，该连接的引用计数减为0，成功析构该TcpConnection。`handleClose`更具体的解释见第二部分。
```cpp
void TcpConnection::handleClose()
{
    setState(kDisconnected);
    channel_->disableAll();
    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 执行连接关闭的回调(本例中是EchoServer::onConnection)
    closeCallback_(connPtr);      // 执行关闭连接的回调 执行的是TcpServer::removeConnection回调方法   // must be the last line
}
```
