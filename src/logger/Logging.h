#pragma once

#include "Timestamp.h"
#include "LogStream.h"

#include <stdio.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <functional>

// SourceFile的作用是提取文件名
class SourceFile
{
public:
    explicit SourceFile(const char* filename)
        : data_(filename)
    {
        /**
         * 找出data中出现/最后一次的位置，从而获取具体的文件名
         * 2022/10/26/test.log
         */
        const char* slash = strrchr(filename, '/');
        if (slash)
        {
            data_ = slash + 1;
        }
        size_ = static_cast<int>(strlen(data_));
    }

    const char* data_;
    int size_;
};

class Logger
{
public:
    enum LogLevel
    {
        TRACE,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL,
        LEVEL_COUNT,
    };

    // member function
    Logger(const char* file, int line);
    Logger(const char* file, int line, LogLevel level);
    Logger(const char* file, int line, LogLevel level, const char* func);
    ~Logger();

    // 返回的LogStream对象可以继续执行<<操作符，流是可以改变的，默认是stdout
    LogStream& stream() { return impl_.stream_; }

    static LogLevel logLevel();
    static void setLogLevel(LogLevel level);

    // 输出函数和刷新缓冲区函数
    using OutputFunc = std::function<void(const char* msg, int len)>;
    using FlushFunc = std::function<void()>;
    /**
     * 下面两个设置为静态函数的原因：本文件结尾的宏定义可见，每一条日志都是临时创建一个Logger对象，所以每条日志结束后马上析构
     * 而且宏定义中也没有setOutput函数，因此把setOutput设置成静态函数，只需要一个地方调用了setOutput，那么所有临时对象都会
     * 输出到setOutput指定的位置
    */
    static void setOutput(OutputFunc);   // 设置日志输出位置
    static void setFlush(FlushFunc);

private:
    // 内部类
    class Impl
    {
    public:
        using LogLevel = Logger::LogLevel;
        Impl(LogLevel level, int savedErrno, const char* file, int line);
        void formatTime();      // 把时间变成字符串的形式，并apend到LogStream的buffer_中
        void finish();          // 每条日志收尾的信息apend到LogStream的buffer_中，比如文件名、行号、换行符

        Timestamp time_;
        LogStream stream_;
        LogLevel level_;
        int line_;
        SourceFile basename_;
    };

    // Logger's member variable 
    Impl impl_;
};

extern Logger::LogLevel g_logLevel;

inline Logger::LogLevel logLevel()
{
    return g_logLevel;
}

// 获取errno信息
const char* getErrnoMsg(int savedErrno);

/**
 * 当日志等级小于对应等级才会输出
 * 比如设置等级为FATAL，则logLevel等级大于DEBUG和INFO，DEBUG和INFO等级的日志就不会输出
 */
#define LOG_DEBUG if (logLevel() <= Logger::DEBUG) \
  Logger(__FILE__, __LINE__, Logger::DEBUG, __func__).stream()
#define LOG_INFO if (logLevel() <= Logger::INFO) \
  Logger(__FILE__, __LINE__).stream()
#define LOG_WARN Logger(__FILE__, __LINE__, Logger::WARN).stream()
#define LOG_ERROR Logger(__FILE__, __LINE__, Logger::ERROR).stream()
#define LOG_FATAL Logger(__FILE__, __LINE__, Logger::FATAL).stream()