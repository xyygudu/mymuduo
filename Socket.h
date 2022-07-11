#pragma once

#include "noncopyable.h"


class InetAddress;

class Socket : noncopyable
{
private:
    const int sockfd_;                              // 服务器监听的套接字文件描述符
public:
    explicit Socket(int sockfd) : sockfd_(sockfd) {}
    ~Socket();

    int fd() const { return sockfd_; }
    void bindAddress(const InetAddress &localaddr); // 调用bind绑定服务器IP和端口
    void listen();                                  // 监听套接字
    int accept(InetAddress *peeraddr);              // 调用accept接受新客户连接请求

    void shutdownWrite();                           // 关闭服务器写通道

    /**  下面四个函数都是调用setsockopt来设置一些socket选项  **/
    void setTcpNoDelay(bool on);
    void setReuseAddr(bool on);
    void setReusePort(bool on);
    void setKeepAlive(bool on);
};


