#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

#include "noncopyable.h"
#include "Thread.h"

class EventLoop;

// EventLoopThread类绑定了一个EventLoop和一个线程
class EventLoopThread : noncopyable
{
    
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;
    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(), const std::string &name = std::string());
    ~EventLoopThread();

    EventLoop *startLoop();

private:
    void threadFunc();

    EventLoop *loop_;
    bool exiting_;
    Thread thread_;
    std::mutex mutex_;              // 互斥锁
    std::condition_variable cond_;  // 条件变量,为什么要条件变量,是为了防止什么现象发生????????
    ThreadInitCallback callback_;

};

