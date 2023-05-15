#include "Timer.h"

void Timer::restart(Timestamp now)
{
    if (repeat_)
    {
        // 如果定时器是可重复的，则设置新的到期时间为现在的时间+时间间隔
        expiration_ = addTime(now, interval_);
    }
    else
    {
        // 如果定时器是不可重复的，就把到期时间设置为0
        expiration_ = Timestamp::invalid();
    }
}