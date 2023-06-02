#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <functional>



class Channel;
class EPollPoller;
class TimerQueue;

class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop();
    void quit();

    Timestamp epollReturnTime() const  { return epollReturnTime_; }

    // 让回调函数cb在EventLoop绑定的线程中执行
    void runInLoop(Functor cb);
    // 让回调函数cb添加到EventLoop的pendingFunctors_中，以便以后在在EventLoop绑定的线程中执行
    void queueInLoop(Functor cb);

    // 唤醒EPoller，以免唤醒EPoller阻塞在poll函数上（poll函数内部其实就是阻塞在了epoll_wait函数上了），
    // 唤醒的方式：向wakeupFd_写入数据，这样，epoll_wait就会发生可读事件，也就不会继续阻塞了
    void wakeup();

    // 在EPoller中更新channel感兴趣的事件
    void updateChannel(Channel *channel);
    // 通过epoll_ctl把channel对应的fd从epollfd中delete，即EPoller以后不在关注该channel上的事件
    void removeChannel(Channel *channel);
    // 判断参数channel是否在当前EPoller中
    bool hasChannel(Channel *channel);

    // 判断EventLoop初始化时绑定的线程id是否和当前正在运行的线程id是否一致
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

    // 定时器相关函数
    // 在time时刻执行回调函数cb
    void runAt(Timestamp time, Functor&& cb); 
    // 在delay秒后执行回调函数cb
    void runAfter(double delay, Functor&& cb);
    // 每隔interval秒执行一次回调函数cb
    void runEvery(double interval, Functor&& cb); 

private:
    using ChannelList = std::vector<Channel*>;

    // wakeupChannel_可读事件的回调函数
    void handleRead();
    void doPendingFunctors();

    std::atomic_bool looping_;                  // 是否正在事件循环中
    std::atomic_bool quit_;                     // 是否退出事件循环
    std::atomic_bool callingPen nndingFunctors_;// 是否正在调用待执行的函数
    const pid_t threadId_;                      // 当前loop所属线程的id
    Timestamp epollReturnTime_;                 // EPoller管理的fd有事件发生时的时间（也就是epoll_wait返回的时间）
    std::unique_ptr<EPollPoller> epoller_;      // 
    std::unique_ptr<TimerQueue> timerQueue_;    // 管理当前loop所有定时器的容器

    // wakeupFd_用于唤醒EPoller，以免EPoller阻塞了无法执行pendingFunctors_中的待处理的函数
    int wakeupFd_;                              
    std::unique_ptr<Channel>wakeupChannel_;     // wakeupFd_对应的Channel
    ChannelList activeChannels_;                // 有事件发生的Channel集合
    std::mutex mutex_;                          // 用于保护pendingFunctors_线程安全操作(添加或取出)
    std::vector<Functor> pendingFunctors_;      // 存储loop跨线程需要执行的所有回调操作
    
};