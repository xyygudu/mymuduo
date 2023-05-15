#pragma once

#include "TcpServer.h"

class HttpRequest;
class HttpResponse;

class HttpServer : noncopyable
{

public:
    using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;

    HttpServer(EventLoop *loop, 
                const InetAddress &listenAddr, 
                const std::string &name, 
                TcpServer::Option option = TcpServer::kNoReusePort);

    EventLoop *getLoop() const { return server_.getLoop(); }

    // 实际就是调用的用户自定义的onRequest
    void setHttpCallback(const HttpCallback &cb)
    {
        httpCallback_ = cb;
    }

    void setThreadNum(int numThreads)
    {
        server_.setThreadNum(numThreads);
    }

    void start();

private:
    void onConnection(const TcpConnectionPtr &connn);
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp receiveTime);
    void onRequest(const TcpConnectionPtr &, const HttpRequest &);

    TcpServer server_;
    HttpCallback httpCallback_;   // 请求到来时候执行的回调函数
};
