#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include "Logging.h"


#include <functional>
#include <memory>

class EventLoop;

/**
* Channel封装了fd和其感兴趣的event，如EPOLLIN,EPOLLOUT，还提供了修改fd可读可写等的函数，
* 当fd上有事件发生了，Channel还提供了处理事件的方法
*/
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;
    // Channel要管理fd，而且还需要知道自己属于哪个loop，因此初始化需要知道fd和loop
    Channel(EventLoop *loop, int fd);
    ~Channel();

    // fd得到EPoller通知以后，处理事件的回调函数
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象, 使用std::move()，避免了拷贝操作
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 将TcpConnection的共享指针和Channel的成员弱指针绑定tie_，便于在Channel在处理事件时，
    // 防止TcpConnection已经被析构了（即连接已经关闭了）
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }                    // 返回封装的fd
    int events() const { return events_; }            // 返回感兴趣的事件
    void set_revents(int revt) { revents_ = revt; }  // 设置Poller返回的发生事件

    // 向epoll中注册、删除fd感兴趣的事件，update()其本质调用epoll_ctl
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ &= kNoneEvent; update(); }

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }   // 是否注册了可写事件
    bool isReading() const { return events_ & kReadEvent; }    // 是否注册了可读事件

    /**
     * 返回fd在EPoller中的状态
     * for EPoller
     * const int kNew = -1;     // fd还未被Epoller监视 
     * const int kAdded = 1;    // fd正被Epoller监视中
     * const int kDeleted = 2;  // fd被从Epoller中移除
     */ 
    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // 返回Channel自己所属的loop
    EventLoop* ownerLoop() { return loop_; }
    // 从EPoller中移除自己，也就是让EPoller停止关注自己感兴趣的事件，
    // 这个移除不是销毁Channel，而是只改变channel的状态，即index_,
    // Channel的生命周期和TcpConnection一样长，因为Channel是TcpConnection的成员，
    // TcpConnection析构的时候才会释放其成员Channel
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    /**
     * const int Channel::kNoneEvent = 0;
     * const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
     * const int Channel::kWriteEvent = EPOLLOUT;
     */
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_;           // 当前Channel属于的EventLoop
    const int fd_;              // fd, Poller监听对象
    int events_;                // 注册fd感兴趣的事件
    int revents_;               // poller返回的具体发生的事件
    int index_;                 // 在EPoller上注册的状态（状态有kNew,kAdded, kDeleted）

    std::weak_ptr<void> tie_;   // 弱指针指向TcpConnection(必要时升级为shared_ptr多一份引用计数，避免用户误删)
    bool tied_;                 // 标志此 Channel 是否被调用过 Channel::tie 方法

    // 保存事件到来时的回调函数
    ReadEventCallback readCallback_;    // 绑定的是TcpConnection::handleRead(Timestamp receiveTime)
    EventCallback writeCallback_;       // 绑定的是TcpConnection::handleWrite()
    EventCallback closeCallback_;       // 绑定的是TcpConnection::handleClose()
    EventCallback errorCallback_;       // 绑定的是TcpConnection::handleError()
};