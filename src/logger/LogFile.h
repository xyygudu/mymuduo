#pragma once

#include "FileUtil.h"

#include <mutex>
#include <memory>


/**
 * LogFile类用于向文件中写入日志信息，具有在合适时机创建日志文件以及把数据写入文件的功能
*/
class LogFile
{

public:
    // threadSafe表示是否需要考虑线程安全，其的作用：同步日志需要锁，因为日志可能来自多个线程，异步日志不需要锁，
    // 因为异步日志的日志信息全部只来自异步线程（见AsyncLogging::threadFunc），无需考虑线程安全。
    // 由于默认是同步日志，因此threadSafe默认为true
    LogFile(const std::string &basename, off_t rollSize, bool threadSafe = true, int flushInterval = 3, int checkEveryN = 1024);
    ~LogFile() = default;

    // 向file_的缓冲区buffer_中继续添加日志信息
    void append(const char* logline, int len);
    void flush();       // 把文件缓冲区中的日志写入强制写入到文件中
    bool rollFile();    // 滚动日志，相当于创建一个新的文件，用来存储日志

private:
    // 根据当前时间生成一个文件名并返回
    static std::string getLogFileName(const std::string& basename, time_t* now);
    void append_unlocked(const char* logline, int len);

    const std::string basename_;
    const off_t rollSize_;                          // 是否滚动日志（创建新的文件存储日志）的阈值，file_的buffer_中的数据超过该值就会滚动日志
    const int flushInterval_;                       // 刷新时间间隔(s)
    // 文件缓冲区（file_的buffer_）写入数据的长度没超过rollSize_(可理解为没写满)的情况下，（见append函数实现）
    // 也会在往file_的buffer_写r入的次数达到checkEveryN_时，刷新一下（fflush），即把缓冲区的数据存储到文件中。
    const int checkEveryN_;                         // 记录往buffer_添加数据的次数,超过checkEveryN_次就fflush一下

    int count_;

    std::unique_ptr<std::mutex> mutex_;
    time_t startOfPeriod_;                          // 记录最后一个日志文件是哪一天创建的（单位是秒）
    time_t lastRoll_;                               // 最后一次滚动日志的时间（s）
    time_t lastFlush_;                              // 最后一次刷新的时间
    std::unique_ptr<FileUtil> file_;

    const static int kRollPerSeconds_ = 60*60*24;   // 一天
};


