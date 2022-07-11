#include <functional>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>

#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null \b", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                             const std::string &nameArg,
                             int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64 * 1024 * 1024)  // 64M
{
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::creator[%s] at fd = %d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::deletor[%s] at fd=%d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}


//发送数据
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())    // 通过tid判断是否在当前线程运行，如果单个reactor的情况 用户调用conn->send时 loop_即为当前线程
        {
            sendInLoop(buf.data(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
        }
    }
}

void TcpConnection::send(Buffer* buf)   
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            // std::cout << "TcpConnection::send(Buffer* buf)---inLoopthread-------" << endl;
            sendInLoop(buf->peek(), buf->readableBytes());
            buf->retrieveAll();
            // std::string msg = buf->retrieveAllAsString();
            // sendInLoop(msg.data(), msg.size());
        }
        else
        {   // 这个函数和muduo源码有一定差别，还不太确定是否正确,即：不知道这样是否会影响buffer的readerIndex_, 应该是不影响的
            // std::cout << "TcpConnection::send(Buffer* buf)-----not in loopthread-----" << endl;
            // 方式一和方式二应该是等价的
            // 方式一:
            std::string msg = buf->retrieveAllAsString();
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, msg.data(), msg.size()));
            // 方式二:
            // loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf->peek(), buf->readableBytes()));
            // buf->retrieveAll();
        }
    }
}

/**
 * 发送数据 应用写的快 而内核发送数据慢 需要把待发送数据写入缓冲区，而且设置了水位回调
 **/
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;         // 已经发送的数据长度
    size_t remaining = len;     // 还剩下多少数据需要发送
    bool faultError = false;    // 记录是否产生错误

    if (state_ == kDisconnected)//  之前调用过该connection的shutdown 不能再进行发送了（不明白为什么kDisconnected表示调用过shutdown）
    {
        LOG_ERROR("disconnected, give up writing");
        return;
    }

    // 表示channel_第一次开始写数据或者缓冲区没有待发送数据，isWriting用于判断fdchannel是否注册了可写事件
    // 疑问：在什么时候chanel的感兴趣事件被设置为可写，什么时候又才会出现isWriting返回false，是把channel的可写事件会移除了还是这个channel从来还没写过数据（还处于初始状态，没有设置感兴趣事件）
    // 答：刚初始化的channel和数据发送完毕的channel都是没有可写事件在epoll上的，对于后者，见本类的handlWrite函数，发现只要把数据发送完毕，他就注销了可写事件
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 不明白为什么这里要用queueInLoop而不用runInLoop()？？？？？？？？？？？
                // 下面的注释也不明白，为什么数据发送完毕后难道channel会自动注销感兴趣的“写”事件？答：handleWrite函数中，数据发送完毕后手动注销的写事件
                // 既然一次性发送完了就不用再给channel设置epollout事件了，即不用设置enableWriting
                // 这样epoll_wait就不用监听可写事件并且执行handleWrite了。这算是提高效率吧
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)   // EWOULDBLOCK表示非阻塞情况下没有数据后的正常返回 等同于EAGAIN
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }
    /**
     * 说明当前这一次write并没有把数据全部发送出去或者之前buffer中就还有数据没有发送完毕 剩余的数据需要保存到缓冲区当中
     * 然后给channel注册EPOLLOUT事件，Poller发现tcp的发送缓冲区有空间后会通知
     * 相应的sock->channel，调用channel对应注册的writeCallback_回调方法，
     * channel的writeCallback_实际上就是TcpConnection设置的handleWrite回调，
     * 把发送缓冲区outputBuffer_的内容全部发送完成
     **/
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();  // 目前发送缓冲区剩余的待发送的数据的长度
        // 判断待写数据是否会超过设置的高位标志highWaterMark_
        if (oldLen +remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_)
        {
            // 不明白为什么这里要用queueInLoop而不用runInLoop()？？？？？？？？？？？
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        outputBuffer_.append((char *)data + nwrote, remaining);     // 将data中剩余还没有发送的数据最佳到buffer中
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件 否则poller不会给channel通知epollout
        }
    }
}

void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        // 这里设置成kDisconnecting是怕缓冲区还有数据没发完
        // 在handleWrite函数中，如果发送完数据会检查state_是不是KDisconnecting，如果是就设为KDisconnected
        setState(kDisconnecting);   
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting())     // 说明当前outputBuffer_的数据全部向外发送完毕
    {
        socket_->shutdownWrite();
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);

    //tie(const std::shared_ptr<void> &obj);
    //shared_from_this()返回的是shared_ptr<TcpConnection>，这能被tie函数接受？
    //这个shared_ptr<void>实际上底层就是一个void*指针，而shared_ptr<TcpConnection>
    //实际上是一个TcpConnection*指针
    //Channel类里面有一个weak_ptr会指向这个传进来的shared_ptr<TcpConnection>
    //如果这个TcpConnection已经被释放了，那么Channel类中的weak_ptr就没办法在
    //Channel::handleEvent 就没法提升weak_ptr。
    /**
     * @brief 我倒是觉得这个思想很不错，当你的TcpConnection对象以shared_ptr的形式
     * 进入到Channel中被绑定，然后Channel类通过调用handleEvent把Channel类中的
     * weak_ptr提升为shared_ptr共同管理这个TcpConnection对象，这样的话，即使
     * 外面的这个TcpConnection智能指针被释放析勾删除，Channel类里面还有一个智能指针
     * 指向这个对象，这个TcpConnection对象也不会被释放。因为引用计数没有变为0.
     * 这个思想超级好，防止你里面干得好好的，外边却突然给你釜底抽薪
     */
    channel_->tie(shared_from_this());          // 将channel和TcpConnection绑定

    channel_->enableReading();                  // 向poller注册channel的EPOLLIN读事件
    connectionCallback_(shared_from_this());    // 新连接建立 执行回调 the user defined onConnection function
}

// 连接销毁
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll();     // 把channel的所有感兴趣的事件从poller中删除掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove();             // 把channel从poller的ChannelMap中删除掉
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)                      // 从fd读到了数据，并且放在了inputBuffer_上
    {
        // 已建立连接的用户有可读事件发生了 调用用户传入的回调操作onMessage shared_from_this就是获取了TcpConnection的智能指针
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    // 客户端断开(为什么读到0字节表示客户端断开？？？？？？？？？)
    // 因为默认客户端没有数据发送了就应该断开，因为muduo关闭连接的过程是：先关闭写端，便于接受还在路上的数据，接受完毕之后，
    // 如果没有数据可读，说明对方没有发送数据了，即客户端关闭连接了，所以服务器也要关闭
    else if (n == 0) 
    {
        handleClose();
    }
    else // 出错了
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);          // 把outputBuffer_的readerIndex往前移动n个字节，因为outputBuffer_中readableBytes已经发送出去了n字节
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();     //数据发送完毕后注销写事件，以免epoll频繁触发可写事件，导致效力低下
                if (writeCompleteCallback_)
                {
                    // TcpConnection对象在其所在的subloop中 向pendingFunctors_中加入回调
                    // 不明白为什么这里要用queueInLoop而不用runInLoop()？？？？？？？？？？？
                    // https://github.com/yyg192/Cpp11-Muduo-MultiReactor 说可以用runInLoop(),但是我不知道为什么
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();           // 关闭写端，而非直接关闭连接，是为了保证已经发送除去的数据客户端还能够完整接收
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing", channel_->fd());
    }
}


void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d\n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();
    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 执行连接关闭的回调
    closeCallback_(connPtr);      // 执行关闭连接的回调 执行的是TcpServer::removeConnection回调方法   // must be the last line
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof(optval);
    int err = 0;
    //《Linux高性能服务器编程》page88，获取并清除socket错误状态,getsockopt成功则返回0,失败则返回-1
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d\n", name_.c_str(), err);
}