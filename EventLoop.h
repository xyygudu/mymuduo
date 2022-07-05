#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;

// 事件循环类 主要包含了两个大模块 Channel Poller(epoll的抽象)
class EventLoop : noncopyable
{

public:
    using Functor = std::function<void()>;
    EventLoop();
    ~EventLoop();

    void loop();                            // 开启事件循环
    void quit();                            // 退出事件循环

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    void runInLoop(Functor cb);             // 在当前loop中执行
    void queueInLoop(Functor cb);           // 把上层注册的回调函数cb放入队列中 唤醒loop所在的线程执行cb

    void wakeup();                          // 通过eventfd唤醒loop所在的线程

    void updateChannel(Channel *channel);   // 调用的三Poller的updateChannel方法
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);
    
    // 判断EventLoop对象是否在自己的线程里
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); } // threadId_为EventLoop创建时的线程id， CurrentThread::tid()为当前线程id

private:

    void handelRead();                         // 处理唤醒相关的逻辑。
    void doPendingFunctors();                  // 执行上层回调

    using ChannelList = std::vector<Channel *>;
    std::atomic_bool looping_;                 // 进入loop循环
    std::atomic_bool quit_;                    // 退出loop，

    const pid_t threadId_;                      // 当前loop所在的线程的id, 即标识了当前EventLoop的所属线程id

    Timestamp pollReturnTime_;                  // poller返回发生事件的channels的时间点
    std::unique_ptr<Poller> poller_;            // 一个EventLoop需要一个poller，这个poller其实就是操控这个EventLoop的对象。

    int wakeupFd_;                              // 作用：当mainLoop获取一个新用户的Channel 需通过轮询算法选择一个subLoop 通过该成员唤醒subLoop处理Channel
    std::unique_ptr<Channel> wakeupChannel_;    

    ChannelList activeChannels_;                // 返回Poller检测到当前有事件发生的所有Channel列表

    std::atomic_bool callingPendingFunctors_;   // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_;      // 存储loop需要执行的所有回调操作
    std::mutex mutex_;                          // 互斥锁 用来保护上面vector容器的线程安全操作
};


