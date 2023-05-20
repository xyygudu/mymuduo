#include "AsyncLogging.h"
#include "Logging.h"
#include "Timestamp.h"

#include <stdio.h>
#include <unistd.h>

static const off_t kRollSize = 8*1024*1024;
AsyncLogging* g_asyncLog = NULL;
std::unique_ptr<LogFile> g_logFile;
// msg就是LogStream::buffer_中存储的一条日志信息，现在需要把这条日志信息apend到FileUtil的buffer_中
void dummyOutput(const char* msg, int len)
{	
	if (g_logFile) {
    	g_logFile->append(msg, len);
    }
}


void test_Logging()
{
    const long n = 1024*1024;
    auto start = std::chrono::system_clock::now();
    for (long i = 0; i < n; ++i) {
        // if (i == 500) sleep(1);     // 
        LOG_INFO << "Hello, " << i << " abc...xyz";     
    }
    auto end = std::chrono::system_clock::now();
    std::chrono::milliseconds dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "log cost time " << dur.count() << " milliseconds" << std::endl;
}

void asyncLog(const char* msg, int len)
{
    if (g_asyncLog)
    {
        g_asyncLog->append(msg, len);
    }
}

int main(int argc, char* argv[])
{
    printf("pid = %d\n", getpid());

    // 异步日志测试：输出到文件，耗时3487ms
    // AsyncLogging log(::basename(argv[0]), kRollSize);
    // g_asyncLog = &log;
    // Logger::setOutput(asyncLog); // 为Logger设置输出回调, 重新配接输出位置
    // log.start(); // 开启日志后端线程
    // test_Logging();
    // sleep(1);
    // log.stop();

    // 同步日志测试：输出到stdout，耗时27890ms
    // test_Logging();

    // 同步日志测试：输出到文件，耗时3716ms
    g_logFile.reset(new LogFile(argv[0], kRollSize, true));
    Logger::setOutput(dummyOutput); 
    test_Logging();
    
    return 0;
}