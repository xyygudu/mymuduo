#include "TcpServer.h"
#include "Logging.h"
#include "LogFile.h"
#include "CircularBuffer.h"

#include <string>
#include <unordered_set>

class EchoServer
{
public:
    EchoServer(EventLoop *loop, const InetAddress &addr, int idleSeconds, const std::string &name)
        : loop_(loop)
        , server_(loop, addr, name)
        , connectionBuckets_(idleSeconds)
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置合适的subloop线程数量
        server_.setThreadNum(3);

        loop->runEvery(1.0, std::bind(&EchoServer::onTimer, this));
        dumpConnectionBuckets();
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
            // 建立连接后，把这个连接加入到时间轮
            LOG_INFO << "Connection UP : " << conn->peerAddress().toIpPort().c_str();
            EntryPtr entry(new Entry(conn));
            connectionBuckets_.back().insert(entry);
            // 记录当前连接属于哪个Entry
            WeakEntryPtr weakEntry(entry);
            conn->setContext(weakEntry);
        }
        else
        {
            LOG_INFO << "Connection DOWN : " << conn->peerAddress().toIpPort().c_str();
            WeakEntryPtr weakEntry(std::any_cast<WeakEntryPtr>(conn->getContext()));
            LOG_DEBUG << "Entry use_count = " << weakEntry.use_count();
        }
        
        dumpConnectionBuckets();
    }

    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(msg);

        // 收到消息，说明连接存活，继续连接加入到时间轮的尾部
        // 取出该连接对应的EntryPtr，如果这个连接在时间轮中还没到8s，则继续把这个WeakEntry lock为shared_ptr，
        // 防止EntryPtr被析构，即延长Entry的生命周期，防止该连接在Entry析构时执行了shutdown()误关闭了该连接
        WeakEntryPtr weakEntry(std::any_cast<WeakEntryPtr>(conn->getContext()));
        EntryPtr entry(weakEntry.lock());
        if (entry)
        {
            connectionBuckets_.back().insert(entry);
            dumpConnectionBuckets();
        }
    }

    
    void onTimer() 
    {
        // 定时器超时后，每次都向队尾插入一个空桶，以便让队首元素弹出，从而关闭链接
        connectionBuckets_.push_back(Bucket());
        dumpConnectionBuckets();
    }

    // 打印bucket中的链接情况
    void dumpConnectionBuckets()
    {
        LOG_INFO << "size = " << connectionBuckets_.size();
        int idx = 0;
        for (WeakConnectionList::iterator bucketI = connectionBuckets_.begin();
            bucketI != connectionBuckets_.end();
            ++bucketI, ++idx)
        {
            const Bucket& bucket = *bucketI;
            LOG_DEBUG << "the " << idx << "th bucket" << " has " << bucket.size() << " connections";
            // 查看当前桶中有几个链接（包括存活着的和已经释放了的）
            for (const auto& it : bucket)
            {
                bool connectionDead = it->weakConn_.expired();
                LOG_DEBUG << "the connection (" << static_cast<void*>(it.get()) << ") use_count is " << it.use_count() << (connectionDead ? " (DEAD)" : " (ALIVE)");
            }
        }
    }
    
    using WeakTcpConnectionPtr = std::weak_ptr<TcpConnection>;

    struct Entry
    {
        explicit Entry(const WeakTcpConnectionPtr& weakConn)
            : weakConn_(weakConn)
        {
        }

        ~Entry()
        {
            TcpConnectionPtr conn = weakConn_.lock();
            if (conn)
            {
                conn->shutdown();
            }
        }

        WeakTcpConnectionPtr weakConn_;
    };

    using EntryPtr = std::shared_ptr<Entry>;
    using WeakEntryPtr = std::weak_ptr<Entry>;
    using Bucket = std::unordered_set<EntryPtr>;
    using WeakConnectionList = CircularBuffer<Bucket>;

    EventLoop *loop_;
    TcpServer server_;
    WeakConnectionList connectionBuckets_;
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
    Logger::setLogLevel(Logger::DEBUG); // 设置日志等级
    EventLoop loop;
    InetAddress addr(8002);
    EchoServer server(&loop, addr, 8, "EchoServer");
    server.start();
    loop.loop();
    return 0;
}