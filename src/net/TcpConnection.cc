
#include "TcpConnection.h"
#include "Logging.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>


// 提供一个默认的ConnectionCallback，如果自定义的服务器（比如EchoServer）没有注册
// ConnectionCallback的话，就使用这个默认的(声明在Callback.h)
void defaultConnectionCallback(const TcpConnectionPtr& conn)
{
    LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
}

// 提供一个默认的MessageCallback，如果自定义的服务器（比如EchoServer）没有注册
// MessageCallback的话，就使用这个默认的(声明在Callback.h)
void defaultMessageCallback(const TcpConnectionPtr&, Buffer* buf, Timestamp)
{
    LOG_TRACE << "receive " << buf->readableBytes() << " bytes: " << buf->retrieveAllAsString();
}

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    // 如果传入EventLoop没有指向有意义的地址则出错
    // 正常来说在 TcpServer::start 这里就生成了新线程和对应的EventLoop
    if (loop == nullptr)
    {
        LOG_FATAL << "mainLoop is null!";
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
    // 绑定channel_各个事件发生时要执行的回调函数
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO << "TcpConnection::creator[" << name_.c_str() << "] at fd =" << sockfd;
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO << "TcpConnection::deletor[" << name_.c_str() << "] at fd = " << channel_->fd() << " state=" << static_cast<int>(state_);
}


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
            loop_->runInLoop(std::bind((void(TcpConnection::*)(const std::string&))&TcpConnection::sendInLoop, this, buf));
        }
    }
}


void TcpConnection::send(Buffer* buf)   
{
    // 如果连接是已经建立的，就把buffer中的数据取出来发送出去
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf->retrieveAllAsString());
        }
        else
        {   
            std::string msg = buf->retrieveAllAsString();
            loop_->runInLoop(std::bind((void(TcpConnection::*)(const std::string&))&TcpConnection::sendInLoop, this, msg));
        }
    }
}

void TcpConnection::sendInLoop(const std::string& message)
{
    sendInLoop(message.data(), message.size());
}


/**
 * 在当前所属loop中发送数据data。发送前，先判断outputBuffer_中是否还有数据需要发送，如果没有，
 * 则直接把要发送的数据data发送出去。如果有，则把data中的数据添加到outputBuffer_中，并注册可
 * 写事件
 **/
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;         // 已经发送的数据长度
    size_t remaining = len;     // 还剩下多少数据需要发送
    bool faultError = false;    // 记录是否产生错误

    if (state_ == kDisconnected)//  之前调用过该connection的shutdown 不能再进行发送了）
    {
        LOG_ERROR << "disconnected, give up writing";
        return;
    }

    // 当channel_没有注册可写事件并且outputBuffer_中也没有数据要发送了，则直接*data中的数据发送出去
    // 疑问：什么时候isWriting返回false?
    // 答：刚初始化的channel和数据发送完毕的channel都是没有可写事件在epoll上的,即isWriting返回false，
    // 对于后者，见本类的handlWrite函数，发现只要把数据发送完毕，他就注销了可写事件
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)   // EWOULDBLOCK表示非阻塞情况下没有数据后的正常返回 等同于EAGAIN
            {
                LOG_ERROR << "TcpConnection::sendInLoop";
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }

    // 如果数据没有全部发送出去，则把剩余的数据都添加到outputBuffer_中，并向Epoller中注册可写事件
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();  // 目前发送缓冲区剩余的待发送的数据的长度
        // 判断待写数据是否会超过设置的高位标志highWaterMark_
        if (oldLen +remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_)
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        outputBuffer_.append((char *)data + nwrote, remaining);     // 将data中剩余还没有发送的数据最佳到buffer中
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件 否则Epoller不会给channel通知epollout
        }
    }
}

// 关闭连接 
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting()) // 说明当前outputBuffer_的数据全部向外发送完成
    {
        socket_->shutdownWrite();
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected); // 建立连接，设置一开始状态为连接态

    channel_->tie(shared_from_this());
    channel_->enableReading(); // 向Epoller注册channel的EPOLLIN读事件

    // 新连接建立 执行回调
    connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 把channel的所有感兴趣的事件从poller中删除掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove(); // 把channel从poller中删除掉
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
    // n=0表示对方关闭了
    else if (n == 0) 
    {
        handleClose();
    }
    else // 出错了
    {
        errno = savedErrno;
        LOG_ERROR << "TcpConnection::handleRead() failed";
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
            LOG_ERROR << "TcpConnection::handleWrite() failed";
        }
    }
    else
    {
        LOG_ERROR << "TcpConnection fd=" << channel_->fd() << " is down, no more writing";
    }
}


void TcpConnection::handleClose()
{
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
    LOG_ERROR << "TcpConnection::handleError name:" << name_.c_str() << " - SO_ERROR:" << err;
}