#pragma once

#include <memory>
#include <string>
#include <atomic>

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"
#include "HttpContext.h"

class Channel;
class EventLoop;
class Socket;

// 为了将Http解析请求的类HttpContext的生命周期和TcpConnection绑定，
// 所以才引入HttpContext，muduo用的时boost::any，但是c++11没有，所以这里就做个特例
// class HttpContext;      


/**暂时看不懂
原文链接：https://blog.csdn.net/breadheart/article/details/112451022 
1. 为什么要用enable_shared_from_this?
    * 需要在类对象的内部中获得一个指向当前对象的shared_ptr 对象
    * 如果在一个程序中，对象内存的生命周期全部由智能指针来管理。在这种情况下，
        要在一个类的成员函数中，对外部返回this指针就成了一个很棘手的问题。
2. 什么时候用？
    * 当一个类被 share_ptr 管理，且在类的成员函数里需要把当前类对象作为参数传给其他函数时，
        这时就需要传递一个指向自身的 share_ptr。
3. 效果：
    TcpConnection类继承 std::enable_shared_from_this ，则会为该类TcpConnection提供成员函数
    shared_from_this。TcpConnection对象 t 被一个为名为 pt 的 std::shared_ptr 类对象管理时，
    调用 T::shared_from_this 成员函数，将会返回一个新的 std::shared_ptr 对象，
    它与 pt 共享 t 的所有权。
*/

/**
 * 一个TcpConnection绑定一个Channel
 * TcpServer => Acceptor => 有一个新用户连接，通过accept函数拿到connfd
 * => TcpConnection设置回调 => 设置到Channel => Poller => Channel回调
 **/
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
    
public:
    TcpConnection(EventLoop *loop, 
                const std::string &nameArg, int sockfd, 
                const InetAddress &localAddr, 
                const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const std::string &name() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }

    bool connected() const { return state_ == kConnected; }

    // 发送数据
    void send(const std::string &buf);
    void send(Buffer* buf);     // HttpServer用到这个函数，其实都可以稍改代码写上面的send就行
    // 关闭连接
    void shutdown();
    /*************HttpServer 用到****************/
    // muduo使用的是boost::any类型，因为保存上下文毕竟不只有HttpContext，
    // 有可能还有文件上下文,但是这没考虑那么多，只为实现http
    void setContext(const HttpContext& context) { context_ = context; }
    const HttpContext& getContext() const { return context_; }
    HttpContext *getMutableContext() { return &context_; }

    // 设置回调
    void setConnectionCallback(const ConnectionCallback &cb)
    { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb)
    { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    { writeCompleteCallback_ = cb; }
    void setCloseCallback(const CloseCallback &cb)
    { closeCallback_ = cb; }
    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
    { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }
    // 连接建立
    void connectEstablished();
    // 连接销毁
    void connectDestroyed();

private:
    enum StateE
    {
        kDisconnected, // 已经断开连接
        kConnecting,   // 正在连接
        kConnected,    // 已连接
        kDisconnecting // 正在断开连接
    };
    void setState(StateE state) { state_ = state; }

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void *data, size_t len);
    void shutdownInLoop();

    EventLoop *loop_;  // 这里是baseloop还是subloop由TcpServer中创建的线程数决定 若为多Reactor 该loop_指向subloop 若为单Reactor 该loop_指向baseloop
    const std::string name_;
    std::atomic_int state_;
    bool reading_;

    // Socket Channel 这里和Acceptor类似, 不同的是Acceptor => mainloop    TcpConnection => subloop
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;   
    const InetAddress peerAddr_;

    // 这些回调TcpServer也有 用户自定义的回调函数传给TcpServer注册 TcpServer再将注册的回调传递给TcpConnection TcpConnection再将回调注册到Channel中
    ConnectionCallback connectionCallback_;         // 有新连接时的回调
    MessageCallback messageCallback_;               // 有读写消息时候的回调
    WriteCompleteCallback writeCompleteCallback_;   // 消息发送完成以后的回调
    HighWaterMarkCallback highWaterMarkCallback_;   // 发送和接收缓冲区超过水位线的话容易导致溢出，这是很危险的。
    CloseCallback closeCallback_;
    size_t highWaterMark_;

    // 数据缓冲区
    Buffer inputBuffer_;                            // 接收数据的缓冲区
    Buffer outputBuffer_;                           // 发送数据的缓冲区 用户send向outputBuffer_发

    /*************HttpServer 用到****************/
    HttpContext context_;  // muduo使用的是boost::any类型，因为保存上下文毕竟不只有HttpContext，有可能还有文件上下文,但是这没考虑那么多，只为实现http
};

