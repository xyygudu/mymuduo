#pragma once

#include <functional>
#include <memory>

#include "noncopyable.h"
#include "Timestamp.h"


class EventLoop;

/*
Chanel类就是fd及其对应的感兴趣的事件的封装，比如EPOLLIN、EPOLLOUT事件 
还绑定了poller返回的具体事件（实际发生的事件）,可以简单理解为一个Channel就是一个fd和其事件。一个channel只管一个fd
*/
class Channel
{

public:
    using EventCallback = std::function<void()>;  //相当于typedef定义函数指针
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // Poller上注册的fd有事件发生时（比如可读可写事件发生，就需要处理），handleEvent在EventLoop::loop()中调用
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数，当fd有事件发生时候，会执行回调函数
    void setReadCallback(ReadEventCallback cb) {readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) {writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) {closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) {errorCallback_ = std::move(cb); }

    // 防止channel被手动删除了还在执行回调
    void tie(const std::shared_ptr<void> &);

    int fd() const { return fd_; }
    int events() const { return events_; }
    //通过这个设置得知当前channel的fd发生了什么事件类型（可读、可写、错误、关闭）
    void set_revents(int revt){ revents_ = revt; }  //将fd上实际发生的事件赋值给Channel的revents_

    // 设置fd相应的事件状态 相当于epoll_ctl add delete
    void enableReading() { events_ |= kReadEvent; update(); } //设置fd相应的事件状态，比如让这个fd监听可读事件，你就必须要用epoll_ctl设置它！
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回fd当前事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }    // 判断当前注册事件中是否有可写事件（也就是判断fd是否可写）
    bool isReading() const { return events_ & kReadEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // one loop per thread
    EventLoop *ownerLoop() { return loop_; }    //返回当前fd（channel）所属的EventLoop
    void remove();

private:

    //注册fd的events事件，只要是fd上的事件新增（修改或删除）了，都需要告诉Poller调用epoll_ctl进行ADD（MOD或DEL）该fd上的某个事件
    void update();  
    
    void handleEventWithGuard(Timestamp receiveTime);    
    

    // 下面三个变量代表这个fd对哪些事件类型感兴趣
    static const int kNoneEvent;    // 0
    static const int kReadEvent;    // = EPOLLIN | EPOLLPRI; EPOLLPRI带外数据，和select的异常事件集合对应
    static const int kWriteEvent;   // = EPOLLOUT

    EventLoop *loop_;   //记录fd所属事件循环
    const int fd_;      //fd是文件描述符，Poller监听的对象
    int events_;        //注册fd感兴趣的事件
    int revents_;       //Poller返回的具体发生的事件（该fd实际发生的事件）

    // const int kNew = -1;     // 某个channel还没有添加至Poller，channel的成员index_初始化为-1
    // const int kAdded = 1;    // 某个channel已经添加到Poller中了
    // const int kDeleted = 2;  // 某个cahnnel已经从Poller中删除
    int index_;                 // index_用于记录当前channel的状态，取值是上面的三个之一：kNew,kAdded,kDeleted,定义在EPollPoller.cc中
    std::weak_ptr<void> tie_;   // 作用：为了防止TcpConnection已经被删除了，但是Channel还在执行回调，更详细的解释见Channel.cc的tie()方法
    bool tied_;                 // tie翻译为捆绑，我估计是用于判断Channel类是否已经和TcpConnection类绑定了

    //为什么readCallback要单独设置？ 因为只有readCallback_函数要接受一个Timestamp参数
    // 当fd（chanel对象）中有具体的事件发生时，执行对应的回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_; 
    EventCallback errorCallback_;
};


