#include "HttpServer.h"
#include "Logging.h"
#include "HttpContext.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

using namespace std;
using namespace std::placeholders;

/**
 * 默认的http回调函数
 * 设置响应状态码，响应信息并关闭连接
 */
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
    server_.setConnectionCallback(std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void HttpServer::start()
{
    LOG_INFO << "HttpServer[" << server_.name().c_str() << "] starts listening on " << server_.ipPort().c_str();
    server_.start();
}

void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected())
    {
        LOG_DEBUG << "new Connection arrived";
    }
    else 
    {
        LOG_DEBUG << "Connection closed, name = " << conn->name();
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime)
{
    std::unique_ptr<HttpContext> context(new HttpContext);

    if (!context->parseRequest(buf, receiveTime))
    {
        LOG_INFO << "parseRequest failed!";
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
    // 判断长连接还是短连接
    bool close = connection == "close" ||
        (req.version() == HttpRequest::kHttp10 && connection != "Keep-Alive");
    HttpResponse response(close);
    httpCallback_(req, &response);      // httpCallback_由用户给定，便于用户可以自定义当请求到来时，应该给客户端返回什么信息
    Buffer buf;
    response.appendToBuffer(&buf);
    conn->send(&buf);
    if (response.closeConnection())
    {
        LOG_DEBUG << "the server close http connection, named: " << conn->name();
        conn->shutdown();
    }
}

