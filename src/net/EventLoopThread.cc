
#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : loop_(nullptr)
    , exiting_(false)
    , thread_(std::bind(&EventLoopThread::threadFunc, this), name) // 新线程绑定此函数
    , mutex_()
    , cond_()
    , callback_(cb) // 传入的线程初始化回调函数，用户自定义的
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}


EventLoop* EventLoopThread::startLoop()
{
    // 启动一个新线程。在构造函数中，thread_已经绑定了回调函数threadFunc
    // 此时，子线程开始执行threadFunc函数
    thread_.start();

    EventLoop *loop = nullptr;
    {
        // 等待新线程执行创建EventLoop完毕，所以使用cond_.wait
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == nullptr)
        {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}

void EventLoopThread::threadFunc()
{
    EventLoop loop;

    // 用户自定义的线程初始化完成后要执行的函数
    if (callback_)
    {
        callback_(&loop);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop; // 等到生成EventLoop对象之后才唤醒主线程，即startLoop()函数才能继续执行
        cond_.notify_one();
    }
    // 执行EventLoop的loop() 开启了底层的EPoller的poll()
    // 这个是subLoop
    loop.loop();   
    // loop是一个事件循环，如果往下执行说明停止了事件循环，需要关闭eventLoop
    // 此处是获取互斥锁再置loop_为空
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}