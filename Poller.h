#pragma once
#include <vector>
#include <unordered_map>

#include "noncopyable.h"
#include "Timestamp.h"

class Channel;
class EventLoop;

// muduo库中多路事件分发器的核心IO复用模块
class Poller
{
public:
    using ChannelList = std::vector<Channel *>;
    Poller(EventLoop *loop);
    virtual ~Poller() = default;

    // 给所有IO复用提供统一接口
    // 调用epoll_wait，并将有事件发生的channel记录到activeChannels中，便于后续处理
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
    virtual void updateChannel(Channel *channel) = 0;
    virtual void removeChannel(Channel *channel) = 0;

    // 判断所给channel是否属于当前的Poller
    bool hasChannel(Channel *channel) const;

    // EventLoop可以通过该接口获取默认的IO复用的具体实现
    static Poller *newDefaultPoller(EventLoop *loop);

protected:
    // map的key是sockfd，value是sockfd所属的channel
    using ChannelMap = std:: unordered_map<int, Channel *>;
    ChannelMap channels_; // 记录该Poller中所有的channel，一个fd对应一个channel
private:
    EventLoop *ownerLoop_;  //定义Poller所属事件循环的EventLoop
};