#include "TimerQueue.h"
#include "EventLoop.h"
#include "Timer.h"
#include "Logging.h"  

#include <sys/timerfd.h>
#include <unistd.h>
#include <string.h>

int createTimerfd()
{
    // 创建timerfd
    // CLOCK_MONOTONIC表示绝对时间（最近一次重启到现在的时间，会因为系统时间的更改而变化）
    // CLOCK_REALTIME表示从1970.1.1到目前时间（更该系统时间对其没影响）
    int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    LOG_DEBUG << "create a timerfd, fd=" << timerfd;
    if (timerfd < 0) LOG_ERROR << "Failed in timerfd_create";
    return timerfd;
}

// 重置timerfd的超时时刻，重置后，如果超时时刻不为0，则内核会启动定时器，否则内核会停止定时器
void resetTimerfd(int timerfd_, Timestamp expiration)
{
    struct itimerspec newValue;
    struct itimerspec oldValue;
    memset(&newValue, '\0', sizeof(newValue));
    memset(&oldValue, '\0', sizeof(oldValue));

    // 计算多久后计时器超时（超时时刻 - 现在时刻）
    int64_t microSecondDif = expiration.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
    if (microSecondDif < 100)
    {
        microSecondDif = 100;
    }

    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(microSecondDif / Timestamp::kMicroSecondsPerSecond);
    ts.tv_nsec = static_cast<long>((microSecondDif % Timestamp::kMicroSecondsPerSecond) * 1000);
    newValue.it_value = ts;
    // 调用timerfd_settime会在内核启动定时器
    if (::timerfd_settime(timerfd_, 0, &newValue, &oldValue))
    {
        LOG_ERROR << "timerfd_settime failed";
    }
}

void readTimerfd(int timerfd)
{
    uint64_t howmany;
    ssize_t n = ::read(timerfd, &howmany, sizeof(howmany));
    if (n != sizeof(howmany))
    {
        LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
    }
}



TimerQueue::TimerQueue(EventLoop *loop)
    : loop_(loop)
    , timerfd_(createTimerfd())
    , timerfdChannel_(loop_, timerfd_)
    , timers_()
{
    // 为timerfd的可读事件设置回调函数并向epoll中注册timerfd的可读事件
    timerfdChannel_.setReadCallback(std::bind(&TimerQueue::handleRead, this));
    timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
    // 删除所有定时器
    for (const Entry& timer : timers_)
    {
        delete timer.second;
    }
}

void TimerQueue::addTimer(TimerCallback cb,Timestamp when, double interval)
{
    Timer *timer = new Timer(std::move(cb), when, interval);
    loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
    // 将timer添加到TimerList时，判断其超时时刻是否是最早的
    bool eraliestChanged = insert(timer);

    // 如果新添加的timer的超时时刻确实是最早的，就需要重置timerfd_超时时刻
    if (eraliestChanged)
    {
        resetTimerfd(timerfd_, timer->expiration());
    }
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    std::vector<Entry> expired;     // 存储到期的定时器
    Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
    // lower_bound返回第一个大于等于sentry的迭代器，
    // 这里的意思是在TimerList中找到第一个没有超时的迭代器
    TimerList::iterator end = timers_.lower_bound(sentry);
    // 把TimerList中所有超时的元素都拷贝到expired中，
    // back_inserter表示每个元素都插入expired的尾部
    std::copy(timers_.begin(), end, back_inserter(expired));
    // 把超时的元素从TimerList中移除掉
    timers_.erase(timers_.begin(), end);

    return expired;
}

void TimerQueue::handleRead()
{
    Timestamp now = Timestamp::now();
    readTimerfd(timerfd_);

    // 获取超时的定时器并挨个调用定时器的回调函数
    std::vector<Entry> expired = getExpired(now);
    callingExpiredTimers_ = true;
    for (const Entry& it : expired)
    {
        it.second->run();   // 执行该定时器超时后要执行的回调函数
    }
    callingExpiredTimers_ = false;

    // 这些已经到期的定时器中，有些定时器是可重复的，有些是一次性的需要销毁的，因此重置这些定时器
    reset(expired, now);
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
    Timestamp nextExpire;   // 记录timerfd下一次的超时时刻
    for (const Entry &it : expired)
    {
        // 如果定时器是可重复的，则继续插入到TimerList中
        if (it.second->repeat())
        {
            auto timer = it.second;
            // 重启定时器，其实就是重新设置一下timer的下次超时的时刻（now + timer.interval_）
            timer->restart(Timestamp::now());
            insert(timer);
        }
        else
        {
            delete it.second;
        }
    }

    // 如果重新插入了定时器，需要继续重置timerfd的超时时刻
    if (!timers_.empty())
    {
        // timerfd下一次可读时刻就是TimerList中第一个Timer的超时时刻
        nextExpire = timers_.begin()->second->expiration();
    }

    if (nextExpire.valid())
    {
        resetTimerfd(timerfd_, nextExpire);
    }
}

bool TimerQueue::insert(Timer* timer)
{
    bool earliestChanged = false;
    Timestamp when = timer->expiration();
    TimerList::iterator it = timers_.begin();
    if (it == timers_.end() || when < it->first)
    {
        // 说明最早的超时的定时器已经被替换了
        earliestChanged = true;
    }

    // 定时器管理红黑树插入此新定时器
    timers_.insert(Entry(when, timer));

    return earliestChanged;
}
