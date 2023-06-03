<a name="zLEiE"></a>
## 核心类
<a name="Uw9rY"></a>
### Channel类
一个fd有自己感兴趣的事件和实际发生的事件，感兴趣的事件（比如EPOLLIN）发生后要执行相应的处理函数，于是Channel类就将fd感兴趣的事件和实际发生的事件封装在一起，同时保存了事件发生时对应的回调函数（**注意，一个Channel自始至终只负责一个fd**）。Channel类的主要属性如下：
```cpp
EventLoop *loop_;           // 记录当前Channel属于的EventLoop
const int fd_;              // 记录自己管理的哪个fd
int events_;                // 注册fd感兴趣的事件
int revents_;               // epoller返回的实际发生的事件

// 保存事件到来时的回调函数。EventCallback是std::function类型，可以粗暴看作是函数指针
ReadEventCallback readCallback_;    // 绑定的是TcpConnection::handleRead(Timestamp receiveTime)
EventCallback writeCallback_;       // 绑定的是TcpConnection::handleWrite()
EventCallback closeCallback_;       // 绑定的是TcpConnection::handleClose()
EventCallback errorCallback_;       // 绑定的是TcpConnection::handleError()
```
fd感兴趣的事件是通过epoll_ctl函数注册的，实际发生的事件是epoll_wait函数返回的。既然fd感兴趣的事件需要注册，并且有事件发生后要处理，那干脆就将注册感兴趣事件和处理实际发生的事件作为Channel的方法。Channel类的主要方法如下：
```cpp
public:
	// fd得到EPoller通知以后，处理事件的回调函数
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数, 使用std::move()，避免了拷贝操作
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }
    // 向epoll中注册、删除fd感兴趣的事件，update()其本质调用epoll_ctl
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ &= kNoneEvent; update(); }
    // 从EPoller中移除自己，也就是让EPoller停止关注自己感兴趣的事件，
    void remove();
private:
    // 把fd感兴趣的事件注册到epoller，本质就是调用epoll_ctl
    void update();	
    void handleEventWithGuard(Timestamp receiveTime);
```
假设**需要向Epoller上注册fd的可读事件**，以往需要调用`epoll_ctl`函数，**现在就只需要调用**`**enableReading()**`。这个`enableReading()`内部调用了`Channel::update()`函数，这个函数是做什么的呢，我们看下源码：
```cpp
void Channel::update()
{
    // 通过该channel所属的EventLoop，调用EPoller对应的方法，注册fd的events事件
    loop_->updateChannel(this);
}
void EventLoop::updateChannel(Channel *channel)
{
    epoller_->updateChannel(channel);
}
// 根据channel在EPoller中的当前状态来更新channel的状态，比如channel还没添加到EPoller中，则就添加进来
void EPollPoller::updateChannel(Channel *channel)
{
	if (channel还没有被添加到Epoller中)
    {
        // 向epoll对象加入channel
        update(EPOLL_CTL_ADD, channel);
    }
    ...省略很多代码
}
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
	int fd = channel->fd();
    event.events = channel->events();
    // 把channel的地址给event.data.ptr，便于调用epoll_wait的时候能够从events_中得知是哪个channel发生的事件，
    // 具体见EPollPoller::fillActiveChannels函数
    event.data.ptr = channel; 
    ::epoll_ctl(epollfd_, operation, fd, &event);
}
```
我们发现，`Channel::update()`最终就是调用的epoll_ctl来实现的。<br />现在已经知道怎么Channel具有了注册fd感兴趣事件的功能，那事件发生后怎么处理呢，请看`Channel::handleEvent`函数：
```cpp
void Channel::handleEvent(Timestamp receiveTime)
{
    ...省略很多代码
    handleEventWithGuard(receiveTime);
    ...省略很多代码
}
// 根据相应事件执行回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    // 对方关闭连接会触发EPOLLHUP，此时需要关闭连接
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_) closeCallback_();
    }

    // 错误事件
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_) errorCallback_();
    }
    // EPOLLIN表示普通数据和优先数据可读，EPOLLPRI表示高优先数据可读，EPOLLRDHUP表示TCP连接对方关闭或者对方关闭写端
    // (个人感觉只需要EPOLLIN就行），则处理可读事件
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (readCallback_) readCallback_(receiveTime);
    }

    // 写事件发生，处理可写事件
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_) writeCallback_();
    }
}
```
**我们发现**`**Channel::handleEvent**`**函数就根据不同的事件类型执行相应的回调函数**。<br />我开始看源码的时候，被各种回调函数绕的晕头转向，都不知道这个回调函数在哪绑定的以及绑定的谁，为了更快查找这些回调函数，我总结了下表：

| 回调函数 | 在哪绑定的（在哪初始化的） | 绑定的谁（实际执行的谁） |
| --- | --- | --- |
| readCallback_ | TcpConnection的构造函数 | TcpConnection::handleRead(Timestamp receiveTime) |
| writeCallback_ | TcpConnection的构造函数 | TcpConnection::handleWrite() |
| closeCallback_ | TcpConnection的构造函数 | TcpConnection::handleClose() |
| errorCallback_ | TcpConnection的构造函数 | TcpConnection::handleError() |

:::tips
上表中，各个回调函数绑定的函数（实际执行的函数）的具体实现暂时不需要知道，后面讲TcpConnection类的时候再介绍。现在需要关注的是：

- **Channel类主要有如下功能：**
   - **具有在Epoller上注册fd感兴趣事件的功能。**
   - **事件发生后，执行相应的回调。**
- 虽然Channel只负责一个fd，但是**Channel并不负责fd的生命周期**（P281, 8.1.1节），fd的生命周期是交给Socket类来管理的。
- 由于Channel对fd上的事件进行了封装，而且**fd和Channel是一一对应的关系**，后面各个类和基本都是在和Channel打交道而很少直接用到fd，所以后面看大Channel可以就把他想象成一个fd就行。
:::
<a name="b5jqJ"></a>
### EPollPoller类
官方muduo支持poll和epoll两种io多路复用（默认使用epoll），我重构的版本简化了一下，只支持epoll，不过不影响理解官方源码，官方源码只是把epoll和poll共有的部分抽象成了一个基类Poller，EPollPoller和PollPoller都继承自Poller而已。下面我就讲自己重构版本的EPollPoller类。<br />EPollPoller类是对IO多路复用epoll的封装。EPollPoller类应该都有哪些成员呢？首先用脚趾头都想得到的就是`epollfd_`，然后，由于EPoller上关注了很多fd，epoll_wait会返回很多fd上实际发生的事件（`events_`），我得要个容器存储这些事件，以便我挨个处理。EPollPoller主要成员属性如下：
```cpp
using ChannelMap = std::unordered_map<int, Channel*>;
using EventList = std::vector<epoll_event>;
// 储存channel 的映射，（sockfd -> channel*）,即，通过fd快速找到对应的channel
ChannelMap channels_;	// 其实可以去掉，因为没什么实际作用
// 定义EPoller所属的事件循环EventLoop
EventLoop *ownerLoop_; 
// 每个EPollPoller都有一个epollfd_，epollfd_是epoll_create在内核创建空间返回的fd
int epollfd_;       
// 用于存放epoll_wait返回的所有发生的事件
EventList events_;
```
那EPollPoller类有哪些方法呢？那必然是封装一个名为`poll`的方法用来调用epoll_wait函数，并通过该方法返回有事件发生的Channel对象（这种有事件发生的Channel我们把它称为`ActiveChannel`），除此之外，如果要为Channel对象管理的fd添加、修改、删除感兴趣的事件，就必然要调用epll_ctl函数，那么EPollPoller就还需要封装`updateChannel`和`removeChannel`函数来更改Channel感兴趣的事件。该类主要方法如下：
```cpp
public:
	// 内部就是调用epoll_wait，将有事件发生的channel通过activeChannels返回
    Timestamp poll(int timeoutMs, ChannelList *activeChannels);
    // 更新channel上感兴趣的事件
    void updateChannel(Channel *channel);
    // 当连接销毁时，从EPoller移除channel
    void removeChannel(Channel *channel);
private:  
    // 把有事件发生的channel添加到activeChannels中
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道，本质是调用了epoll_ctl
    void update(int operation, Channel *channel);
```
`updateChannel`函数已经在讲Channel类的时候展示了核心代码，这里就不展示了，`removeChannel`函数和`updateChannel`相似，本质都是掉要用epoll_ctl函数，这里也不展开，重点是`poll`函数和`fillActiveChannels`函数，核心代码如下：
```cpp
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // epoll_wait把检测到的事件都存储在events_数组中
    size_t numEvents = ::epoll_wait(epollfd_, &(*events_.begin()), static_cast<int>(events_.size()), timeoutMs);
    Timestamp now(Timestamp::now());
    // 有事件产生
    if (numEvents > 0)
    {
        fillActiveChannels(numEvents, activeChannels); // 填充活跃的channels
        ...省略对events_进行扩容操作的代码
    }
    ...省略numEvents<=0的情况
    return now;
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        // 获得events_对应的channel，并把events_[i].events赋值给该channel中用于记录实际发生事件的属性revents_
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}
```
简单来说，`poll`函数就是通过参数的方式返回有事件发生的Channel，便于后续EventLoop类中对这些Channel实际发生的事件进行处理，这里不详细展开，请具体看EventLoop类。`fillActiveChannels`函数其实就是把epll_wait返回的实际发生的事件赋值给对应的Channel的`revents_`，然后把自己添加进`activeChannels`中。
<a name="Ly3FT"></a>
### EventLoop类
EventLoop主要做两件事：

- 启动或者退出事件循环。在线程中，我们需要不断的处理fd上发生的事件，所以需要一个死循环，不断调用`EPollPoller::poll`函数来检测哪些Channel上有事件要处理。
- 保证one loop per thread。one loop per thread意思就是一个线程只能对应一个EventLoop对象，因此在EventLoop的构造函数中记住自己所在的线程id，它的生命周期和它所在的线程一样长（P278，8.0节），另外，有些操作（比如对某个fd的读写操作）不允许跨线程执行，因此EventLoop还提供了runInLoop和queueInLoop两个函数来保证这些操作不会跨线程调用（**这个有点小复杂，我们专门留到第四部分的“怎么保证one loop per thread”讲解**）。

下面就来看下EventLoop怎么启动和退出循环的。该类提供了`loop()`和`quit()`函数，分别用来启动和退出循环。核心代码如下：
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
        // 执行其他线程添加到pendingFunctors_中的函数
        // （doPendingFunctors与one loop per thread相关，
        // 暂时不需要理解，第四部分会详细解释）
        doPendingFunctors();
    }
    looping_ = false;
}


void EventLoop::quit()
{
    quit_ = true;
    // 从loop()函数中可见，要退出loop()函数中的死循环，就必须再次执行到while处，
    // 但是如果当前loop没有任何事件发生，此时会阻塞在epoller_->poll()函数这里，
    // 也就是说loop函数无法再次执行到while处，因此，需要向wakeupFd_写入数据，这样
    // 就可以解除阻塞，达到退出的目的
    if (!isInLoopThread()) wakeup();
}
```
`activeChannels_`就是一个Channel*的Vector容器，用于保存有事件发生的Channel。从`loop`函数可见，该函数就是一个循环，循环中，不断调用`EPollPoller::poll`函数以获得有事件发生的Channel，并把他们存储在`activeChannels_`中，然后遍历`activeChannels_`，挨个处理每个Channel上发生的事件。`quit`函数也很简单，只需要修改退出标志`quit_`变量为true，下次`loop`函数再次循环到while处就会退出循环。
<a name="pPgE9"></a>
## 其他重要类
<a name="vcTJi"></a>
### Socket类
Socket类只有一个成员sockfd，该类的作用就是用于管理TCP连接对应的sockfd的生命周期（析构的时候close该sockfd），以及提供一些函数来修改sockfd上的选项，比如Nagel算法、设置地址复用等。Socket类如下所示：
```cpp
class Socket : noncopyable
{
public:
    explicit Socket(int sockfd) : sockfd_(sockfd) {}
    ~Socket();
    // 获取sockfd
    int fd() const { return sockfd_; }
    // 绑定sockfd，就是调用::bind函数
    void bindAddress(const InetAddress &localaddr);
    // 就是调用::listen函数
    void listen();
    // 接受连接
    int accept(InetAddress *peeraddr);
    // 设置半关闭
    void shutdownWrite();
    void setTcpNoDelay(bool on);    // 设置Nagel算法 
    void setReuseAddr(bool on);     // 设置地址复用
    void setReusePort(bool on);     // 设置端口复用
    void setKeepAlive(bool on);     // 设置长连接
private:
    const int sockfd_;
};
```
下面主要讲一下`shutdownWrite`函数。如下代码所示，`shutdownWrite`就是关闭sockfd的写端，此时不能再向sockfd写入任何数据，但是此时还可以继续接收数据，muduo之所以采用这种半关闭的方式，是为了防止数据漏收，具体见第四部分。
```cpp
Socket::~Socket()
{
    ::close(sockfd_);
}

void Socket::shutdownWrite()
{
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR << "shutdownWrite error";
    }
}
```
<a name="JwYT7"></a>
### TcpConnection类
TcpConnection表示的是一个Tcp连接，既然是一个连接，它应该有成员：

- 它一定知道该连接对应的fd，由于fd的生命周期是Socket类负责的，所以它必然有个`socket_`成员，同时fd上的事件都是Channel类负责的，所以有一个`channel_`成员，而且一个连接只对应一个`socket_`和`channel_`，所以使用了std::unique_ptr。
- 需要知道自己和对方的地址，即`localAddr_`和`peerAddr_`。
- 用户可能需要自定义连接刚建立、收到消息、消息发送完毕、连接关闭这几种情况下应该处理什么事，于是需要几个回调函数与用户自定义的函数绑定，以便相应的情况发生时能够调用用户自定义的函数。另外还有一个回调`highWaterMarkCallback_`，它是在Buffer存储的待发送或者待读取处理的数据超过我们设定的上线后执行的回调。
- 每个连接都需要读写缓冲区，读缓冲区`inputBuffer_`是为了及时的把TCP可读缓冲区中取出来放入`inputBuffer_`，以免频繁触发EPOLLIN事件，写缓冲区`outputBuffer_`是为了以防TCP发送缓冲区大小不够，就把发送不完的数据暂时存储在`outputBuffer_`中下次再发送，以免阻塞。（更多关于Buffer的信息请看"Muduo的Buffer"）
```cpp
EventLoop *loop_;                               // 属于哪个subLoop（如果是单线程则为baseLoop）
std::atomic_int state_;                         // 连接状态
std::unique_ptr<Socket> socket_;                // 把fd封装成socket，这样便于socket析构时自动关闭fd
std::unique_ptr<Channel> channel_;              // fd对应的channel
const InetAddress localAddr_;                   // 本服务器地址
const InetAddress peerAddr_;                    // 对端地址
/**
 * 用户自定义的这些事件的处理函数，然后传递给 TcpServer 
 * TcpServer 再在创建 TcpConnection 对象时候设置这些回调函数到TcpConnection中
 */
ConnectionCallback connectionCallback_;         // 有新连接时的回调
MessageCallback messageCallback_;               // 有读写消息时的回调
WriteCompleteCallback writeCompleteCallback_;   // 消息发送完成以后的回调
CloseCallback closeCallback_;                   // 客户端关闭连接的回调
HighWaterMarkCallback highWaterMarkCallback_;   // Buffer超出水位实现的回调
size_t highWaterMark_;

Buffer inputBuffer_;                            // 读取数据的缓冲区
Buffer outputBuffer_;                           // 发送数据的缓冲区
```
了解TcpConnection的成员后，我们也不难想到一个连接必不可少的方法：建立连接`connectEstablished`、发送数据`send和handleWrite`、读取对方发来的数据`handleRead`、关闭连接`connectDestroyed和handleClose`。
```cpp
// 注册到channel上有事件发生时，其回调函数就是绑定的下面这些函数
void handleRead(Timestamp receiveTime);
void handleWrite();
void handleClose();
void handleError();
```
<a name="PzH4S"></a>
#### 建立连接函数`connectEstablished`到底在干嘛呢？
当我们的主线程（主Reactor或TcpServer）接收一个连接后（`connectEstablished`函数是在TcpServer类的`newConnection`函数中调用的），肯定要等待接收该连接上的数据，所以**这个**`**connectEstablished**`**函数实际上就是向该连接所属的子Reactor的Epoller上，注册该连接的EPOLLIN事件，这样EPoller才会在有数据可读时触发可读事件，**顺便执行一下用户自定义的连接到来时应该执行的回调函数`connectionCallback_`。
```cpp
void TcpConnection::connectEstablished()
{
    setState(kConnected); // 建立连接，设置一开始状态为连接态
    channel_->tie(shared_from_this());
    channel_->enableReading(); // 向Epoller注册channel的EPOLLIN读事件
    // 新连接建立 执行回调
    connectionCallback_(shared_from_this());
}
```
当服务给客户端发送信息的时候（即调用`send`函数的时候）具体干了什么呢？`TcpConnection::send`函数其实就是调用了`TcpConnection::sendInLoop`，**该函数简单来说就做了这样一件事：先检查该连接上是否还有上次没发送完的数据（outputBuffer_中有数据就是之前没发送完毕），如果发送完毕了（即**`**outputBuffer_.readableBytes() == 0**`**），则直接调用**`**::write**`**系统调用发送给对方，如果数据没有**`**::write**`**完毕，则直接把剩余的数据存储到outputBuffer_中，同时告诉EPoller关注该连接的可写事件，便于下次接着发送刚才没发送完的数据。**
:::tips
为什么要`sendInLoop`呢？因为发送数据是可以在任意线程发送的，即`send`函数可以在任意线程中调用，但是我们又说过，one loop per thread就是要保证sockfd的写操作不会跨线程进行，于是就有了`sendInLoop`函数。至于怎么实现的，在第四部分讲怎么保证one loop per thread就明白了，现在不需要关注太多细节，主要明白send函数是怎么把数据发送出去的）
:::
```cpp
/**
 * 在当前所属loop中发送数据data。发送前，先判断outputBuffer_中是否还有数据需要发送，如果没有，
 * 则直接把要发送的数据data发送出去。如果有，则把data中的数据添加到outputBuffer_中，并注册可
 * 写事件
 **/
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
<a name="L08H9"></a>
#### 那剩余没发送完的数据什么时候发送出去呢？
在`sendInLoop`函数中，如果数据没发送完毕，会在EPoller中注册该连接的可写事件，这样，当TCP可写缓冲区空闲的时候，就会触发该连接对应Channel上的可写事件，即，**最终调用**`**TcpConnection::handleWrite**`**函数。该函数就是负责把剩余未发送的数据（即outputBuffer_中的数据）发送出去.**
```cpp
void TcpConnection::handleWrite()
{
    if (channel_->isWriting())  // 如果该channel在EPoller上注册了可写事件
    {
        int savedErrno = 0;
        // 把outputBuffer_中的数据发送出去
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);          // 把outputBuffer_的readerIndex往前移动n个字节，因为outputBuffer_中readableBytes已经发送出去了n字节
            // 如果数据全部发送完毕
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();     //数据发送完毕后注销写事件，以免epoll频繁触发可写事件，导致效力低下
                ...省略部分代码
            }
        }
    }
}
```
<a name="eDzDd"></a>
#### 当对方有数据发送过来的时候，即调用的`handleRead`函数在干嘛呢？
**其实该函数就是把TCP可读缓冲区的数据读入到inputBuffer_中，以腾出TCP可读缓冲区，避免反复触发EPOLLIN事件（可读事件），同时执行用户自定义的消息到来时候的回调函数**`**messageCallback_**`**。**
```cpp
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    // 把TCP缓冲区的数据读入到inputBuffer_中
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)                      // 从fd读到了数据，并且放在了inputBuffer_上
    {
        // 已建立连接的用户有可读事件发生了 调用用户传入的回调操作onMessage shared_from_this就是获取了TcpConnection的智能指针
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    // n=0表示对方关闭了
    else if (n == 0) 
    {
        handleClose();
    }
}
```
<a name="TBN6x"></a>
#### 关闭连接`handleClose`做了什么呢？
什么时候会关闭连接？

- 对方主动关闭连接、或者对方意外关机，我方通过心跳机制给对方发送探测报文时，会触发了EPOLLHUP事件，此时该连接对应的Channel会调用`TcpConnection::handleClose`函数
- `TcpConnection::handleRead`中读取到0字节数据也表示对方主动关闭连接，也会调用`TcpConnection::handleClose`函数

`**TcpConnection::handleClose**`**函数就是把该连接对应Chanel上所有感兴趣的事件都从EPoller中注销，并且将自己从TcpServer中存储的所有连接中移除。**代码如下：
```cpp
void TcpConnection::handleClose()
{
    setState(kDisconnected);
    channel_->disableAll();	// 注销所有感兴趣的事件
    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 执行连接关闭的回调(用户自定的，而且和新连接到来时执行的是同一个回调)
    // 将自己从TcpServer中存储的所有连接中移除
    closeCallback_(connPtr);      // 执行关闭连接的回调 执行的是TcpServer::removeConnection回调方法   // must be the last line
}
```
上面代码中`closeCallback_`实际执行的就是`TcpServer::removeConnection`，`TcpServer::removeConnection`又会调用`TcpConnection::connectDestroyed`，该函数主要也就是从EPoller中注销感兴趣的事件。
```cpp
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 把channel的所有感兴趣的事件从poller中删除掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove(); // 把channel从epoller中注销掉
}
```
下面给出了TcpConnection类中的回调函数绑定情况，不知道为啥highWaterMarkCallback_没有绑定，那Buffer中数据堆积过多怎么办？书本P322的8.9.3节给出了解答：Buffer中数据堆积量超过了我们设定的值，就在highWaterMarkCallback_中停止继续向Buffer中添加数据，在writeCompleteCallback_中恢复。

| 回调函数 | 在哪绑定的（在哪初始化的） | 绑定的谁（实际执行的谁） |
| --- | --- | --- |
| connectionCallback_ | TcpServer::newConnection | 需要自定义，没有则执行defaultConnectionCallback函数 |
| messageCallback_ | TcpServer::newConnection | 需要自定义，没有则执行defaultMessageCallback函数 |
| writeCompleteCallback_ | TcpServer::newConnection | 需要自定义，没有则什么也不做 |
| closeCallback_ | TcpServer::newConnection | TcpServer::removeConnection |
| highWaterMarkCallback_ | 未绑定 | 需要自定义，没有则什么也不做 |

<a name="rVXTc"></a>
### Acceptor
**该类运行在主线程，主要负责监听并接收连接，接收的连接分发给其他的子Reactor（子线程）**。由于监听也需要一个listenfd，我们也和其他连接对应的fd一样把他封装成`acceptChannel_`，这样当有新连接到来时`acceptChannel_`可读，最终就会调用`Acceptor::handleRead`。Acceptor类主要成员和函数如下：
```cpp
class Acceptor
{
public:
    // 接受新连接的回调函数
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;
    void setNewConnectionCallback(const NewConnectionCallback &cb)
    {
        NewConnectionCallback_ = cb;
    }
    void listen();
private:
    void handleRead();
    EventLoop *loop_;       // Acceptor用的就是用户定义的mainLoop
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback NewConnectionCallback_;
    int idleFd_;            // 防止连接fd数量超过上线，用于占位的fd
};
```
调用listen函数时，就做了两件事，一是监听，二是让在EPoller上注册`acceptChannel_`的可读事件。<br />**handleRead函数就是接收连接，并将连接分发给子Reactor（子线程）**，分发是在`NewConnectionCallback_`回调函数中完成的，它绑定的是TcpServer的newConnection函数，我会在讲TcpServer类的时候介绍怎么分发的。下面来看下handleRead函数的代码：
```cpp
void Acceptor::listen()
{
    // 表示正在监听
    listenning_ = true;
    acceptSocket_.listen();
    // 将acceptChannel的读事件注册到poller
    acceptChannel_.enableReading();
}


// listenfd有事件发生了，就是有新用户连接了
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    // 接受新连接
    int connfd = acceptSocket_.accept(&peerAddr);
    // 确实有新连接到来
    if (connfd >= 0)
    {
        // TcpServer::NewConnectionCallback_
        if (NewConnectionCallback_)
        {
            // 轮询找到subLoop 唤醒并分发当前的新客户端的Channel
            NewConnectionCallback_(connfd, peerAddr); 
        }
    }
    else
    {
        LOG_ERROR << "accept() failed";
        if (errno == EMFILE)
        {
            ...省略fd耗尽的处理代码
        }
    }
}
```
Acceptor类的回调函数绑定信息如下表：

| 回调函数 | 在哪绑定的（在哪初始化的） | 绑定的谁（实际执行的谁） |
| --- | --- | --- |
| NewConnectionCallback_ | TcpServer构造函数 | TcpServer::newConnection |

<a name="nS2H0"></a>
### TcpServer类
TcpServer的主要属性和方法如下。TcpServer表示的是服务器，一个服务器肯定需要知道自己有哪些连接，因此使用一个std::map变量（即`connections_`）来保存连接，服务器还需要监听，因此需要一个Acceptor对象`acceptor_`，当然，还需要保存用户自定义的各种回调，比如连接建立时的回调`connectionCallback_`、消息到来时的回调`messageCallback_`等。除了保存上述信息外，服务器还需要启动，于是有`start()`方法，还需要分发连接到子线程，即调用`newConnection`，连接断开时还需要将`connections_`保存的连接移除，即调用`removeConnection`。
```cpp
public:
	// 开启服务器
    void start();
    // 轮询选择一个子线程（子Reactor或子EventLoop）
    EventLoop* getLoop() const { return loop_; }
private:
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;
    // 新连接到来时的处理函数（acceptor_可读时绑定的回调函数）
    void newConnection(int sockfd, const InetAddress &peerAddr);
    void removeConnection(const TcpConnectionPtr &conn);
	// 保证移除连接是在主线程中进行的
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    EventLoop *loop_;                               // 用户定义的mainLoop
    std::unique_ptr<Acceptor> acceptor_;            // 用于监听和接收新连接的Acceptor
    std::shared_ptr<EventLoopThreadPool> threadPool_;  // 其实就是所有子Reactor(不包括主Reactor)
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;               // 有读写消息时的回调函数
    WriteCompleteCallback writeCompleteCallback_;   // 消息发送完成以后的回调函数
    ThreadInitCallback threadInitCallback_;         // loop线程初始化的回调函数

    int nextConnId_;            
    ConnectionMap connections_;                     // 保存所有的连接
```
下面重点介绍一下start、**newConnection、**removeConnection三个方法。<br />start函数做两件事：
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
首先，其实就是开启所有子线程（子Reactor）此时所有子线程启动后都阻塞在EventLoop的epoller_->poll这里，等待着子线程自己的EPoller上有事件发生并做相应的事件处理。
```cpp
void EventLoop::loop()
{
    while (!quit_)
    {
        activeChannels_.clear();
        // 有事件发生的channel都添加到activeChannels_中，poll函数内部其实就是epoll_wait
        epollReturnTime_ = epoller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            channel->handleEvent(epollReturnTime_);
        }
        ...省略部分代码
    }
}
```
然后，开始监听（调用listen系统调用并向主Reactor的EPoller注册listenfd的可读事件，这是在Acceptor::listen函数中完成的）<br />newConnection函数是listenfd可读时要执行的回调函数，其功能就是为了分发连接到子Reactor，大致分为4步完成：

- 通过轮询选择一个子线程（子Reactor）
- 把新连接对应的sockfd打包成TcpConnection
- 为新的TcpConnection设置回调，比如设置消息到来应该执行哪个函数
- 把TcpConnection分发到轮询时选择的子线程中去
```cpp
// 有一个新用户连接，acceptor会执行这个回调操作，负责将mainLoop接收到的请求连接(acceptChannel_会有读事件发生)通过回调轮询分发给subLoop去处理
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 1.轮询算法 选择一个subLoop 来管理connfd对应的channel
    EventLoop *ioLoop = threadPool_->getNextLoop();
    // 提示信息
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "-%s#%d", ipPort_.c_str(), nextConnId_);
    // 这里没有设置为原子类是因为其只在mainloop中执行 不涉及线程安全问题
    ++nextConnId_;  
    // 新连接名字
    std::string connName = name_ + buf;
    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local;
    ::memset(&local, 0, sizeof(local));
    socklen_t addrlen = sizeof(local);
    if(::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0)
    {
        LOG_ERROR << "sockets::getLocalAddr() failed";
    }


    InetAddress localAddr(local);
    // 2.把新连接sockfd打包成TcpConnection
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    connections_[connName] = conn;
    // 3. 为该连接设置对应回调函数
    // 下面的回调都是用户设置给TcpServer => TcpConnection的，至于Channel绑定的则是TcpConnection设置的四个，handleRead,handleWrite... 这下面的回调用于handlexxx函数中
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    // 设置了如何关闭连接的回调
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
	// 4.在subloop（这里的ioLoop）建立连接（其实就是在ioLoop的EPoller中为该连接注册可读事件，这就是分发）
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}
```
开始看源码的时候很难明白，怎么把一个连接分发到子线程啊，分发到底是干嘛啊？其实很简单：分发就是希望该连接对应fd（或者说Channel）上所有的事件都在指定的子线程（子Reactor）中执行，换句话说，就是只需要把该新来的连接感兴趣的事件注册到子线程（子Reactor或者上面代码的ioLoop）中的EPoller就行，这样当该连接上有事件发生时，该ioLoop就不会阻塞在`epoller_->poll`函数处了，也就可以在ioLoop上处理该连接发生的事件了。<br />**removeConnection的功能就是把TcpServer的connections_保存的连接从connections_中删掉**，其实本质调用的是`TcpServer::removeConnectionInLoop`函数，
```cpp
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    connections_.erase(conn->name());
    EventLoop *ioLoop = conn->getLoop();
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}
```
`TcpServer::removeConnectionInLoop`函数也是为了保证从connections_删掉某个TcpConnection不会跨线程，为什么会出现跨线程这种情况呢，下面细细道来：<br />当某个连接需要关闭时，会调用`TcpConnection::handleClose`，该函数是在子线程中执行的，该函数内部会执行`closeCallback_`回调函数，`closeCallback_`就是`TcpServer::removeConnection`函数，也就是说`TcpServer::removeConnection`实际是在子线程中执行的，那为了保证从connections_中移除某个连接不跨线程执行（即保证该操作只在主线程中执行），就需要借助`TcpServer::removeConnectionInLoop`函数来做到这一点，它本质就是调用EventLoop的runInLoop实现的，至于实现思路是什么，同样还是见第四部分：怎么保证one loop per thread。<br />TcpServer回调函数绑定信息如下表：

| 回调函数 | 在哪绑定的（在哪初始化的） | 绑定的谁（实际执行的谁） |
| --- | --- | --- |
| connectionCallback_ | 用户自定义服务器的构造函数，如EchoServer | 需要自定义，没有则执行defaultConnectionCallback函数 |
| messageCallback_ | 用户自定义服务器的构造函数，如EchoServer | 需要自定义，没有则执行defaultMessageCallback函数 |
| writeCompleteCallback_ | 未绑定 | 需要自定义，没有则什么也不做 |
| threadInitCallback_ | 未绑定 | 需要自定义，没有则什么也不做 |

