#pragma once

#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "Callback.h"
#include "TcpConnection.h"

#include <unordered_map>

class TcpServer
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    enum Option
    {
        kNoReusePort,
        kReusePort,
    };


    TcpServer(EventLoop *loop,
                const InetAddress &ListenAddr,
                const std::string &nameArg,
                Option option = kNoReusePort);
    ~TcpServer();

    // 设置回调函数(用户自定义的函数传入)
    void setThreadInitCallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    // 设置底层subLoop的个数
    void setThreadNum(int numThreads);

    // 开启服务器
    void start();
    
    EventLoop* getLoop() const { return loop_; }

    const std::string name() { return name_; }

    const std::string ipPort() { return ipPort_; }

private:
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    // 新连接到来时的处理函数（acceptor_可读时绑定的回调函数）
    void newConnection(int sockfd, const InetAddress &peerAddr);

    void removeConnection(const TcpConnectionPtr &conn);

    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    EventLoop *loop_;                               // 用户定义的mainLoop
    const std::string ipPort_;                      // 传入的IP地址和端口号
    const std::string name_;                        // TcpServer名字
    std::unique_ptr<Acceptor> acceptor_;            // 用于监听和接收新连接的Acceptor
    std::shared_ptr<EventLoopThreadPool> threadPool_;  
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;               // 有读写消息时的回调函数
    WriteCompleteCallback writeCompleteCallback_;   // 消息发送完成以后的回调函数

    ThreadInitCallback threadInitCallback_;         // loop线程初始化的回调函数
    std::atomic_int started_;                

    int nextConnId_;            
    ConnectionMap connections_;                     // 保存所有的连接
};


