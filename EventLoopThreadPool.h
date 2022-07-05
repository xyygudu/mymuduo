#pragma once

#include <functional>
#include <string>
#include <vector>
#include <memory>

#include "noncopyable.h"


class EventLoop;
class EventLoopThread;
/*
    管理EventLoopThread的线程池
*/
class EventLoopThreadPool : noncopyable
{
    
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
    ~EventLoopThreadPool();

    void setThreadNum(int numThreads) { numThreads_ = numThreads; }
    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    // 如果工作在多线程中，baseLoop_(mainLoop)会默认以轮询的方式分配Channel给subLoop
    EventLoop *getNextLoop();

    std::vector<EventLoop *> getAllLoops();

    bool started() const { return started_;}
    const std::string name() const { return name_; }



private:
    EventLoop *baseLoop_;   // 如果你没有通过setThreadNum来设置线程数量，那整个网络框架就只有一个线程，这唯一的一个线程就是这个baseLoop_，既要处理新连接，还要处理已连接的事件监听。
    std::string name_;
    bool started_;
    int numThreads_;
    int next_;              // 轮询的下标
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop *> loops_;        // 包含了所有子EventLoop线程的指针
};

