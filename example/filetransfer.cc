#include "../Logger.h"
#include "../EventLoop.h"
#include "../TcpServer.h"

using namespace std;

const char* g_file = NULL;

string readFile(const char *filename)
{
    string content;
    FILE *fp = ::fopen(filename, "rb");
    if (fp)
    {
        const int kBufferSize = 1024*1024;
        char iobuf[kBufferSize];
        ::setbuffer(fp, iobuf, sizeof(iobuf));

        char buf[kBufferSize];
        size_t nread = 0;
        while ((nread = ::fread(buf, 1, sizeof(buf), fp)) > 0)
        {
            content.append(buf, nread);
        }
        ::fclose(fp);
    }
    return content;
}


void onHighWaterMark(const TcpConnectionPtr  &conn, size_t len)
{
    LOG_INFO("high water mark %d\n", len);
}

void onConnection(const TcpConnectionPtr &conn)
{
    LOG_INFO("send start");
    conn->setHighWaterMarkCallback(onHighWaterMark, 64*1024);
    string fileContent = readFile(g_file);
    conn->send(fileContent);
    conn->shutdown();
    LOG_INFO("send done");
}

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        g_file = argv[1];

        EventLoop loop;
        InetAddress listenAddr(2021);
        TcpServer server(&loop, listenAddr, "FileServer");
        server.setConnectionCallback(onConnection);
        server.start();
        loop.loop();
    }
    else
    {
        fprintf(stderr, "Usage: %s file_for_downloading\n", argv[0]);
    }
}
