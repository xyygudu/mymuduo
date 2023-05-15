#pragma once

#include "noncopyable.h"
#include "Thread.h"

#include <mutex>
#include <condition_variable>


class EventLoop;

/**
 * 此类的作用是将EventLoop和Thread联系起来
*/
class EventLoopThread : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
                    const std::string& name = std::string());
    ~EventLoopThread();

    // 开启一个新线程
    EventLoop *startLoop(); 

private:
    // 线程执行函数
    void threadFunc();

    EventLoop *loop_;               // 子线程绑定的loop_
    bool exiting_;
    Thread thread_;
    std::mutex mutex_;              // 互斥锁,这里是配合条件变量使用
    std::condition_variable cond_;  // 条件变量, 主线程等待子线程创建EventLoop对象完成
    ThreadInitCallback callback_;   // 线程初始化完成后要执行的函数

};


