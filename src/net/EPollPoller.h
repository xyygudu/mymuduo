#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>

class Channel;
class EventLoop;

class EPollPoller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;
    EPollPoller(EventLoop *Loop);
    ~EPollPoller();


    // 内部就是调用epoll_wait，将有事件发生的channel通过activeChannels返回
    Timestamp poll(int timeoutMs, ChannelList *activeChannels);

    // 更新channel上感兴趣的事件
    void updateChannel(Channel *channel);

    // 当连接销毁时，从EPoller移除channel
    void removeChannel(Channel *channel);

    // 判断channel是否已经注册到EPoller
    bool hasChannel(Channel *channel) const;

private:  
    using ChannelMap = std::unordered_map<int, Channel*>;
    using EventList = std::vector<epoll_event>;
    // 把有事件发生的channel添加到activeChannels中
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道，本质是调用了epoll_ctl
    void update(int operation, Channel *channel);
    
    // 默认监听事件数量
    static const int kInitEventListSize = 16; 
    // 储存channel 的映射，（sockfd -> channel*）,即，通过fd快速找到对应的channel
    ChannelMap channels_;
    // 定义EPoller所属的事件循环EventLoop
    EventLoop *ownerLoop_; 
    // 每个EPollPoller都有一个epollfd_，epollfd_是epoll_create在内核创建空间返回的fd
    int epollfd_;       
    // 用于存放epoll_wait返回的所有发生的事件
    EventList events_;  
};