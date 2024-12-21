#pragma once

#include "noncopyable.h"
#include "Thread.h"
#include <mutex>
#include <deque>
#include <condition_variable>
#include <vector>

/**
 * 线程池类。
 * 与EventLoopThreadPool不同，EventLoopThreadPool管理的每个子线程都是用于IO的，也就是子Reactor，
 * 而ThreadPool管理的线程是为了处于耗时业务的逻辑的。
*/

class ThreadPool : noncopyable
{
public:
    using Task = std::function<void()>;
    explicit ThreadPool(const std::string& name = std::string("ThreadPool"));
    ~ThreadPool();

    // void setThreadInitCallback(const ThreadFunction& cb) { threadInitCallback_ = cb; }
    // void setThreadSize(const int& num) { threadSize_ = num; }
    // 启动numThreads个线程
    void start(int numThreads);
    void stop();

    const std::string& name() const { return name_; }
    size_t queueSize() const;

    void run(Task f);
    
private:
    bool isFull() const;
    void runInThread();
    
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::string name_;
    Task threadInitCallback_;
    std::vector<std::unique_ptr<Thread>> threads_;
    std::deque<Task> queue_;
    std::atomic_bool running_;
};
