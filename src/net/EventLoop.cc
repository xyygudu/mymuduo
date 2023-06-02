
#include "EventLoop.h"
#include "Channel.h"
#include "EPollPoller.h"
#include "TimerQueue.h"

#include <sys/eventfd.h>



// 通过__thread 修饰的变量，在线程中地址都不一样，__thread变量每一个线程有一份独立实体，各个线程的值互不干扰.
// t_loopInThisThread的作用：防止一个线程创建多个EventLoop, 是如何防止的呢，请继续读下面注释：
// 当一个eventloop被创建起来的时候,这个t_loopInThisThread就会指向这个Eventloop对象。
// 如果这个线程又想创建一个EventLoop对象的话这个t_loopInThisThread非空，就不会再创建了。
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的EPoller IO复用接口的超时时间
const int kPollTimeMs = 10000;

int createEventfd()
{
    int evfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd < 0) LOG_FATAL << "eventfd error: " << errno;
    else LOG_DEBUG << "create a new wakeupfd, fd = " << evfd;
    return evfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , epoller_(new EPollPoller(this))
    , timerQueue_(new TimerQueue(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG << "EventLoop created " << this << ", the threadId is " << threadId_;
    if (t_loopInThisThread)
    {
        LOG_FATAL << "Another EventLoop" << t_loopInThisThread << " exists in this thread " << threadId_;
    }
    else
    {
        t_loopInThisThread = this;
    }
    // 设置wakeupChannel_可读事件的回调函数，并向当前loop的EPoller中注册可读事件
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    // channel移除所有感兴趣事件
    wakeupChannel_->disableAll();
    // 将channel从EPollPoller中删除，同时让epollfd不在关注wakeupFd_上发生的任何事件
    wakeupChannel_->remove();
    // 关闭 wakeupFd_
    ::close(wakeupFd_);
    // 指向EventLoop指针为空
    t_loopInThisThread = nullptr;
}

void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    LOG_INFO << "EventLoop " << this << " start looping";

    while (!quit_)
    {
        activeChannels_.clear();
        // 有事件发生的channel都添加到activeChannels_中，poll函数内部其实就是epoll_wait
        epollReturnTime_ = epoller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            channel->handleEvent(epollReturnTime_);
        }
        /**
         * 执行当前EventLoop事件循环需要处理的回调操作 对于线程数 >=2 的情况 IO线程 mainloop(mainReactor) 主要工作：
         * accept接收连接 => 将accept返回的connfd打包为Channel => TcpServer::newConnection通过轮询将TcpConnection对象分配给subloop处理
         * mainloop调用queueInLoop将回调加入subloop的pendingFunctors_中（该回调需要subloop执行 但subloop还在epoller_->poll处阻塞）
         * queueInLoop通过wakeup将subloop唤醒，此时subloop就可以执行pendingFunctors_中的保存的函数了
         **/
        // 执行其他线程添加到pendingFunctors_中的函数
        doPendingFunctors();
    }
    looping_ = false;
}

void EventLoop::quit()
{
    quit_ = true;
    // 从loop()函数中可见，要退出loop()函数中的死循环，就必须再次执行到while处，
    // 但是如果当前loop没有任何事件发生，此时会阻塞在epoller_->poll()函数这里，
    // 也就是说loop函数无法再次执行到while处，因此，需要向wakeupFd_写入数据，这样
    // 就可以解除阻塞，达到退出的目的
    if (!isInLoopThread()) wakeup();
}

void EventLoop::runInLoop(Functor cb)
{
    // 如果当前调用runInLoop的线程正好是EventLoop绑定的线程，则直接执行此函数,
    // 否则就将回调函数通过queueInLoop()存储到pendingFunctors_中
    if (isInLoopThread())
    {
        cb();
    }
    else
    {
        queueInLoop(cb);
    }
}

// 把cb放入队列中 唤醒loop所在的线程执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {   
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }
    // callingPendingFunctors_ 比较有必要，因为正在执行回调函数（假设是cb）的过程中，
    // cb可能会向pendingFunctors_中添加新的回调,
    // 则这个时候也需要唤醒，否则就会发生有事件到来但是仍被阻塞住的情况。
    // callingPendingFunctors_在doPendingFunctors函数中被置为true
    if (!isInLoopThread() || callingPendingFunctors_)  
    {
        wakeup();   
    }
}

void EventLoop::wakeup()
{
    // wakeup() 的过程本质上是对wakeupFd_进行写操作，以触发该wakeupFd_上的可读事件,
    // 这样就起到了唤醒 EventLoop 的作用。
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR << "EventLoop::wakeup writes " << n << " bytes instead of 8";
    }
}

// wakeupChannel_可读事件的回调函数
void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
    }
}

void EventLoop::updateChannel(Channel *channel)
{
    epoller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    epoller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return epoller_->hasChannel(channel);    
}

void EventLoop::runAt(Timestamp time, Functor&& cb) {
    timerQueue_->addTimer(std::move(cb), time, 0.0);
}

void EventLoop::runAfter(double delay, Functor&& cb) {
    Timestamp time(addTime(Timestamp::now(), delay)); 
    runAt(time, std::move(cb));
}

void EventLoop::runEvery(double interval, Functor&& cb) {
    Timestamp timestamp(addTime(Timestamp::now(), interval)); 
    timerQueue_->addTimer(std::move(cb), timestamp, interval);
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        // 通过局部变量functors取出pendingFunctors_中的数据，这样可以提前释放锁。
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);  // 交换的方式减少了锁的临界区范围 提升效率 同时避免了死锁 如果执行functor()在临界区内 且functor()中调用queueInLoop()就会产生死锁
    }
    for (const Functor &functor : functors)
    {
        functor();
    }
    callingPendingFunctors_ = false;  
}