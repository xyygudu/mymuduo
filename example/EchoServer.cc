#include "TcpServer.h"
#include "Logging.h"
#include "LogFile.h"

#include <string>

class EchoServer
{
public:
    EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
        : loop_(loop)
        , server_(loop, addr, name)
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置合适的subloop线程数量
        server_.setThreadNum(3);
    }
    void start()
    {
        server_.start();
    }

private:
    // 连接建立或断开的回调函数
    void onConnection(const TcpConnectionPtr &conn)   
    {
        if (conn->connected())
        {
            LOG_INFO << "Connection UP : " << conn->peerAddress().toIpPort().c_str();
        }
        else
        {
            LOG_INFO << "Connection DOWN : " << conn->peerAddress().toIpPort().c_str();
        }
    }

    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(msg);
        // conn->shutdown();   // 关闭写端 底层响应EPOLLHUP => 执行closeCallback_
    }

    EventLoop *loop_;
    TcpServer server_;
};


std::unique_ptr<LogFile> g_logFile;
// msg就是LogStream::buffer_中存储的一条日志信息，现在需要把这条日志信息apend到FileUtil的buffer_中
void dummyOutput(const char* msg, int len)
{	
	if (g_logFile) {
    	g_logFile->append(msg, len);
    }
}

int main() 
{
    /***********同步日志到文件************/
    // g_logFile.reset(new LogFile("echoserver_log_file", 500*1000*1000, true));
    // Logger::setOutput(dummyOutput);		// 改变Logger的输出位置
    /************************************/
    // Logger::setLogLevel(Logger::TRACE); // 设置日志等级
    EventLoop loop;
    InetAddress addr(8002);
    EchoServer server(&loop, addr, "EchoServer");
    server.start();
    loop.loop();
    return 0;
}