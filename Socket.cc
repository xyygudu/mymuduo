#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "Socket.h"
#include "Logger.h"
#include "InetAddress.h"

Socket::~Socket()
{
    ::close(sockfd_);
}

void Socket::bindAddress(const InetAddress &localaddr)
{
    if (0 != ::bind(sockfd_, (sockaddr*)localaddr.getSockAddr(), sizeof(sockaddr_in)))
    {
        LOG_FATAL("bind sockfd: %d fail\n", sockfd_);
    }
}

void Socket::listen()
{
    if (0 != ::listen(sockfd_, 1024))  // 设置最大监听数量为1024
    {
        LOG_FATAL("listen sockfd:%d fail\n", sockfd_);
    }
}

int Socket::accept(InetAddress *peeraddr)
{
    /**
     * 1. accept函数的参数不合法
     * 2. 对返回的connfd没有设置非阻塞
     * Reactor模型 one loop per thread
     * poller + non-blocking IO
     **/
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    ::memset(&addr, 0, sizeof(addr));
    int connfd = ::accept4(sockfd_, (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0)
    {
        peeraddr->setSockAddr(addr);
    }
    return connfd;
}


void Socket::shutdownWrite()
{
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR("shutdwonWrite error");
    }
}


void Socket::setTcpNoDelay(bool on)     //不启用naggle算法，增大对小数据包的支持（暂时不懂）
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    // SO_REUSEADDR允许在同一端口上启动同一服务器的多个实例，只要每个实例捆绑一个不同的本地IP地址即可。
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}


void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}
void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    // TCP保活机制
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}