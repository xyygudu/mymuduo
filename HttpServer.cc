#include "HttpServer.h"
#include "Logger.h"
#include "HttpContext.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

using namespace std;
using namespace std::placeholders;

void defaultHttpCallback(const HttpRequest&, HttpResponse* resp)
{
    resp->setStatusCode(HttpResponse::k404NotFound);
    resp->setStatusMessage("Not Found");
    resp->setCloseConnection(true);
}

HttpServer::HttpServer(EventLoop *loop, 
                const InetAddress &listenAddr, 
                const std::string &name, 
                TcpServer::Option option)
    : server_(loop, listenAddr, name, option)
    , httpCallback_(defaultHttpCallback)
{
    server_.setConnectionCallback(std::bind(&HttpServer::onConnection, this, _1));
    server_.setMessageCallback(std::bind(&HttpServer::onMessage, this, _1, _2, _3));
    // server_.setWriteCompleteCallback(std::bind(&HttpServer::onWriteCompleted, this, _1));
}


void HttpServer::start()
{
    LOG_INFO("HttpServer[%s] start listening on %s", server_.name().c_str(), server_.ipPort().c_str());
    server_.start();
}

void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        // 不像书p187-189提到的传文件例子那样，个人感觉HttpContext没有必要绑定到conn，
        // 书上传文件保存上下文是为了记住一个每次连接读到那里了
        conn->setContext(HttpContext());    // 开辟一个HttpContext对象和conn绑定起来，这样可以保证conn析构的时候，HttpContext也被释放
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime)
{
    HttpContext* context = conn->getMutableContext();
    if (!context->parseRequest(buf, receiveTime))
    {
        conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
        conn->shutdown();
    }

    if (context->gotAll())
    {
        onRequest(conn, context->request());
        context->reset();
    }
}

void HttpServer::onRequest(const TcpConnectionPtr& conn, const HttpRequest& req)
{
    const string& connection = req.getHeader("Connection");
    bool close = connection == "close" ||
        (req.getVersion() == HttpRequest::kHttp10 && connection != "Keep-Alive");
    HttpResponse response(close);
    httpCallback_(req, &response);      // httpCallback_由用户给定，便于用户可以自定义当请求到来时，应该给客户端返回什么信息
    Buffer buf;
    response.appendToBuffer(&buf);
    conn->send(&buf);
    if (response.closeConnection())
    {
        conn->shutdown();
    }
}

// void HttpServer::onWriteCompleted(const TcpConnectionPtr &conn)
// {
//     cout << "Http服务器已经写完" << endl;
// }