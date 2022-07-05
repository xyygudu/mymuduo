#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

#include "EventLoop.h"
#include "Logger.h"
#include "Channel.h"
#include "Poller.h"

// 通过__thread 修饰的变量，在线程中地址都不一样，__thread变量每一个线程有一份独立实体，各个线程的值互不干扰.
// t_loopInThisThread的作用：防止一个线程创建多个EventLoop, 是如何防止的呢，请继续读下面注释：
// 当一个eventloop被创建起来的时候,这个t_loopInThisThread就会指向这个Eventloop对象。
// 如果这个线程又想创建一个EventLoop对象的话这个t_loopInThisThread非空，就不会再创建了。
__thread EventLoop *t_loopInThisThread = nullptr;   

const int kPollTimeMs = 10000;                      // 定义默认的Poller IO复用接口的超时时间,0000毫秒 = 10秒钟

/* 创建线程之后主线程和子线程谁先运行是不确定的。
 * 通过一个eventfd在线程之间传递数据的好处是多个线程无需上锁就可以实现同步。
 * eventfd支持的最低内核版本为Linux 2.6.27,在2.6.26及之前的版本也可以使用eventfd，但是flags必须设置为0。
 * 函数原型：
 *     #include <sys/eventfd.h>
 *     int eventfd(unsigned int initval, int flags);
 * 参数说明：
 *      initval,初始化计数器的值。
 *      flags, EFD_NONBLOCK,设置socket为非阻塞。
 *             EFD_CLOEXEC，执行fork的时候，在父进程中的描述符会自动关闭，子进程中的描述符保留。
 * 场景：
 *     eventfd可以用于同一个进程之中的线程之间的通信。
 *     eventfd还可以用于同亲缘关系的进程之间的通信。
 *     eventfd用于不同亲缘关系的进程之间通信的话需要把eventfd放在几个进程共享的共享内存中（没有测试过）。
 */
// 创建wakeupfd 用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK |EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error: %d\n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())               // 获取当前线程的tid
    , poller_(Poller::newDefaultPoller(this))       // 获取一个封装着控制epoll操作的对象
    , wakeupFd_(createEventfd())                    // 创建wakeupfd
    , wakeupChannel_(new Channel(this, wakeupFd_))  // 为wakeupFd_创建一个Channel对象，因为一个Channel是fd及其事件的封装
{
    LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
    if (t_loopInThisThread)                         // 如果当前线程已经绑定了某个EventLoop对象了，那么该线程就无法创建新的EventLoop对象了
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 用了functtion就要用bind，别用什么函数指针函数地址之类的。
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handelRead, this));  // 设置wakeupfd的事件类型以及发生事件后的回调操作
    //每一个EventLoop都将监听wakeupChannel的EpollIN读事件了。
    //mainReactor通过给wakeupFd_给sbureactor写东西。
    wakeupChannel_->enableReading();                // 设置wakeupChannel_的感兴趣事件为：EPOLLIN | EPOLLPRI;
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();   // 给Channel移除所有感兴趣的事件
    wakeupChannel_->remove();       // 把Channel从EventLoop上删除掉, 其实本质上还是通过Poller调用EPOLL_CTL_DEL移除的
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping\n", this);

    while (!quit_)
    {
        // 在我们的Epoller里面poll方法里面，我们把EventLoop的ActiveChannels传给了poll方法,
        // 当poll方法调用完了epoll_wait()之后，把有事件发生的channel都装进了这个ActiveChannels数组里面,
        // 这样 EventLoop就可以对ActiveChannels挨个进行处理实际发生的事件
        activeChannels_.clear();
        // 监听两类fd，一个是client的fd，一个是wakeupfd，用于mainloop和subloop的通信
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);

        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些channel发生了事件 然后上报给EventLoop 通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        /**
         * 执行当前EventLoop事件循环需要处理的回调操作 对于线程数 >=2 的情况 IO线程 mainloop(mainReactor) 主要工作：
         * accept接收连接 => 将accept返回的connfd打包为Channel => TcpServer::newConnection通过轮询将TcpConnection对象分配给subloop处理
         *
         * mainloop调用queueInLoop将回调加入subloop（该回调需要subloop执行 但subloop还在poller_->poll处阻塞） queueInLoop通过wakeup将subloop唤醒
         **/
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}

/**
 * 退出事件循环
 * 1. 如果loop在自己的线程中调用quit成功了 说明当前线程已经执行完毕了loop()函数的poller_->poll并退出
 * 2. 如果不是当前EventLoop所属线程中调用quit退出EventLoop 需要唤醒EventLoop所属线程的epoll_wait
 * 这个quit可以是自己的loop调用，也可以是别的loop调用。比如mainloop调用subloop的quit
 * 比如在一个subloop(worker)中调用mainloop(IO)的quit时 需要唤醒mainloop(IO)的poller_->poll 让其执行完loop()函数
 *
 * ！！！ 注意： 正常情况下 mainloop负责请求连接 将回调写入subloop中 通过生产者消费者模型即可实现线程安全的队列
 * ！！！       但是muduo通过wakeup()机制 使用eventfd创建的wakeupFd_ notify 使得mainloop和subloop之间能够进行通信
 **/

// 为什么会出现别的loop调用自己的quit()的奇怪现象？？？？？？？？？？？
void EventLoop::quit()
{
    quit_ = true;
    if(!isInLoopThread())
    {
        wakeup();
    }
}

// 该函数在TcpConnection中调用的
// 这个cb到底是要做的是什么事情？？？？？？？？
void EventLoop::runInLoop(Functor cb)
{
    // 保证了调用这个cb一定是在其EventLoop线程中被调用。
    if (isInLoopThread())   // 如果当前调用runInLoop的线程正好是EventLoop的运行线程，则直接执行此函数
    {
        cb();
    }
    else                    // 在非当前EventLoop线程中执行cb，就需要唤醒EventLoop所在线程执行cb
    {
        queueInLoop(cb);
    }
}

// 把cb放入队列中 唤醒loop所在的线程执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {   //大括号的作用是为了限制lock的作用域，这样lock在退出大括号后就会自动释放锁 C++11
        // 之所以要加索是因为可能不止一个线程在向pendingFunctors_容器中插入对调函数。
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }
    /**
     * callingPendingFunctors_标识当前loop是否有需要执行的回调操作
     * || callingPendingFunctors的意思是 当前loop正在执行回调中 但是loop的pendingFunctors_中又加入了新的回调 需要通过wakeup写事件
     * 唤醒相应的需要执行上面回调操作的loop的线程 让loop()下一次poller_->poll()不再阻塞（阻塞的话会延迟前一次新加入的回调的执行），然后
     * 继续执行pendingFunctors_中的回调函数
     **/
    if (!isInLoopThread() || callingPendingFunctors_)  //callingPendingFunctors_在doPendingFunctors函数中被置为true
    {
        /***
        为什么要唤醒 EventLoop，我们首先调用了 pendingFunctors_.push_back(cb), 
        将该函数放在 pendingFunctors_中。EventLoop 的每一轮循环在最后会调用 
        doPendingFunctors 依次执行这些函数。而 EventLoop 的唤醒是通过 epoll_wait 实现的，
        如果此时该 EventLoop 中迟迟没有事件触发，那么 epoll_wait 一直就会阻塞。 
        这样会导致，pendingFunctors_中的任务迟迟不能被执行了。
        所以必须要唤醒 EventLoop ，从而让pendingFunctors_中的任务尽快被执行。
        ***/
        wakeup();   
    }
}

// wakeupChannel_可读事件的回调函数
void EventLoop::handelRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));  // mainReactor给subreactor发消息，subReactor通过wakeupFd_感知。
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8\n", n);
    }
}

// 用来唤醒loop所在线程 向wakeupFd_写一个数据 wakeupChannel就发生读事件 当前loop线程就会被唤醒。
void EventLoop::wakeup()
{
    /**
     *在 EventLoop 建立之后，就创建一个 eventfd，并将其可读事件注册到 EventLoop 中。
    wakeup() 的过程本质上是对这个 eventfd 进行写操作，以触发该 eventfd 的可读事件。
    这样就起到了唤醒 EventLoop 的作用。
     * 
     */
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8\n", n);
    }
}

// EventLoop的方法 => Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

// 判断当前channel是否已经注册到了Poller中
bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        /***
        这里面又有精华！！
        我们在queueInLoop里面往pendingFunctors_数组里面插入新回调，
        这里定义了一个局部的functors数组，每次执行doPendingFunctors的时候都和pendingFunctors_
        交换，相当于把pendingFunctors_的对象全部导入到functors数组里面，让那后把pendingFunctors
        置为空，这样的好处是避免频繁的锁，因为如果你不用这种机制的话，生产者queueInLoop函数插入新回调，
        消费者doPendingFunctors消费回调，他们共用一个pendingFunctors_，这样生产者插入和消费者消费
        就会不停的互相触发锁机制。
        ***/
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);  // 交换的方式减少了锁的临界区范围 提升效率 同时避免了死锁 如果执行functor()在临界区内 且functor()中调用queueInLoop()就会产生死锁
    }
    for (const Functor &functor : functors)
    {
        functor();
    }
    callingPendingFunctors_ = false;  
}