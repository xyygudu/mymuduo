#include "Channel.h"
#include "EventLoop.h"

#include <sys/epoll.h>


const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
{

}

Channel::~Channel()
{
    if (loop_->isInLoopThread())
    {
        assert(!loop_->hasChannel(this));
    }
}


void Channel::tie(const std::shared_ptr<void> &obj)
{
    // 让tie_(weak_ptr)指向obj，后边处理fd上发生的事件时，就可以用tie_判断TcpConnection是否还活着
    tie_ = obj;
    tied_ = true;
}

/**
 * 更新fd感兴趣的事件，比如Channel刚创建时，需要让Channel管理的fd关注可读事件，此时就需要
 * 调用Channel::enableReading()，该函数内部再调用update()。
 * update()的本质是在EPoller类中，调用了epoll_ctl，来实现对感兴趣事件的修改
 */
void Channel::update()
{
    // 通过该channel所属的EventLoop，调用EPoller对应的方法，注册fd的events事件
    loop_->updateChannel(this);
}

void Channel::remove()
{
    // 把channel从EPollPoller的channels_中移除掉，同时把这个channel的状态（index_）标记为kNew
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    /**
     * TcpConnection::connectEstablished会调用channel_->tie(shared_from_this());
     * 调用了Channel::tie会设置tid_=true
     * 所以对于TcpConnection::channel_ 需要多一份强引用的保证以免用户误删TcpConnection对象
     */
    if (tied_)
    {
        // 变成shared_ptr增加引用计数，防止误删
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
        // guard为空情况，说明Channel的TcpConnection对象已经不存在了
    }
    else 
    {
        handleEventWithGuard(receiveTime);
    }
}


// 根据相应事件执行回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    // 对方关闭连接会触发EPOLLHUP，此时需要关闭连接
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_) closeCallback_();
    }

    // 错误事件
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_) errorCallback_();
    }

    // EPOLLIN表示普通数据和优先数据可读，EPOLLPRI表示高优先数据可读，EPOLLRDHUP表示TCP连接对方关闭或者对方关闭写端
    // (个人感觉只需要EPOLLIN就行），则处理可读事件
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (readCallback_) readCallback_(receiveTime);
    }

    // 写事件发生，处理可写事件
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_) writeCallback_();
    }

}