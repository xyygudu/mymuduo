#pragma once

#include "noncopyable.h"
#include "Thread.h"
#include "FixedBuffer.h"
#include "LogStream.h"
#include "LogFile.h"

#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>

class AsyncLogging : noncopyable
{
    
public:
    AsyncLogging(const std::string& basename, off_t rollSize, int flushInterval=3);
    ~AsyncLogging()
    {
        if (running_)
        {
            stop();
        }
    }

    // 前端调用 append 写入日志
    void append(const char* logling, int len);

    void start()
    {
        running_ = true;
        thread_.start();
    }

    void stop()
    {
        running_ = false;
        cond_.notify_one();
        thread_.join();
    }

private:
    using Buffer = FixedBuffer<kLargeBuffer>;    // 4M
    using BufferVector = std::vector<std::unique_ptr<Buffer>>;
    using BufferPtr = BufferVector::value_type;

    void threadFunc();

    const int flushInterval_;           // 刷新时间间隔（s）
    std::atomic<bool> running_;         
    const std::string basename_;        // 日志文件名的公共的部分
    const off_t rollSize_;              // 日志文件超过多大时创建新的日志文件
    Thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;

    BufferPtr currentBuffer_;           // 前端日志就是写入这个缓冲区 4M
    BufferPtr nextBuffer_;              // 备用缓冲区，currentBuffer_写不下了就写入这个
    BufferVector buffers_;              // 用于存储已经写满的BufferPtr，便于后台线程写入文件，每个元素对应的缓冲区都是4M

};



