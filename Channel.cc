#include <sys/epoll.h>

#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"


// 初始化 Channel类的static变量 
const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , index_(-1)        // -1 = kNew, 表示channel还没有添加至Poller，kNew定义在EPollPoller.cc中
    , tied_(false)
{

}

Channel::~Channel()
{
}


// channel的tie方法什么时候调用过?  TcpConnection => channel
/**
 * TcpConnection中注册了Chnanel对应的回调函数，传入的回调函数均为TcpConnection
 * 对象的成员方法，因此可以说明一点就是：Channel的结束一定早于TcpConnection对象！
 * 此处用tie去解决TcoConnection和Channel的生命周期时长问题，从而保证了Channel对
 * 象能够在TcpConnection销毁前销毁。
 **/
void Channel::tie(const std::shared_ptr<void> &obj)
{
    // obj是一个TcpConnection对象，如果TcpConnection没有和Channel绑定，那就无法为该channel上的事件执行回调，
    // 因为channel上的事件执行回调绑定的是TcpConnection的成员函数（在TcpConnection的构造函数中绑定的）
    tie_ = obj;      
    tied_ = true;
}

void Channel::update()
{
    /**
     * 当改变channel所对应的fd的events事件后，update负责在poller里面更改
     * fd相应的事件epoll_ctl
     * 通过channel所属的EventLoop，调用poller的相应方法，注册fd的events事件
     * channel和poller通过EventLoop进行连接。
     */    
    loop_->updateChannel(this); 
}

// 在channel所属的EventLoop中把当前的channel删除掉
void Channel::remove()
{
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        // 如果当前 weak_ptr 已经过期，则该函数会返回一个空的 shared_ptr 指针；反之，该函数返回一个和当前 weak_ptr 指向相同的 shared_ptr 指针。
        std::shared_ptr<void> guard = tie_.lock();  //尝试将weak_ptr提升为share_ptr
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
}

// 根据poller通知的channel发生的具体事件类型，由channel负责调用具体的回调操作。
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents: %d\n", revents_)
    // 当TcpConnection对应Channel 通过shutdown 关闭写端 epoll触发EPOLLHUP.
    // 关闭
    if ((revents_ & EPOLLHUP && !(revents_ & EPOLLIN))) 
    {
        if (closeCallback_)     // 如果指定了回调函数，或者说回调函数不为空，则执行回调
        {
            closeCallback_();
        }
    }
    // 错误
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }
    // 读
    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }
    // 写
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}