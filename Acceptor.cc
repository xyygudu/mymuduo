#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking())
    , acceptChannel_(loop, acceptSocket_.fd())  // 这里的loop是baseLoop_
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr);
    // TcpServer::start() => Acceptor.listen() 如果有新用户连接 要执行一个回调(accept => connfd => 打包成Channel => 唤醒subloop)
    // baseloop监听到有事件发生 => acceptChannel_(即listenfd) => 执行该回调函数
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();                // 把从Poller中感兴趣的事件删除掉
    acceptChannel_.remove();                    // 调用EventLoop->removeChannel => Poller->removeChannel 把Poller的ChannelMap对应的部分删除
}

void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();         // listen
    acceptChannel_.enableReading(); // acceptChannel_注册至Poller !重要
}

// listenfd有事件发生了，即有新用户连接
void Acceptor::handleRead()
{
    // 当有新用户连接了又会调用Socket::accpet函数，该函数底层真正调用了sccket编程的accept函数
    // 并且把peerAddr设置好后就传递给newConnectionCallback_来调用真正的新用户连接的处理函数。
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd > 0)
    {
        if (newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddr);   // 轮询找到subLoop 唤醒并分发当前的新客户端的Channel
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}