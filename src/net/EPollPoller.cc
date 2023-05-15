
#include "EPollPoller.h"
#include "Logging.h"
#include "Channel.h"
#include "EventLoop.h"

#include <assert.h>

const int kNew = -1;                            // 某个channel还没添加至EPoller          // channel的成员index_初始化为-1
const int kAdded = 1;                           // 某个channel已经添加至EPoller
const int kDeleted = 2;                         // 某个channel已经从EPoller删除

EPollPoller::EPollPoller(EventLoop *loop)
    : ownerLoop_(loop)                          // 记录EPollPoller属于哪个EventLoop
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL << "epoll_create() error:" << errno;
    }
    else
    {
        LOG_DEBUG << "create a new epollfd, fd = " << epollfd_;
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}


Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // epoll_wait把检测到的事件都存储在events_数组中
    size_t numEvents = ::epoll_wait(epollfd_, &(*events_.begin()), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;
    Timestamp now(Timestamp::now());
    // 有事件产生
    if (numEvents > 0)
    {
        fillActiveChannels(numEvents, activeChannels); // 填充活跃的channels
        // 对events_进行扩容操作
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    // 超时
    else if (numEvents == 0)
    {
        LOG_DEBUG << "timeout!";
    }
    // 出错
    else
    {
        // 不是终端错误
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR << "EPollPoller::poll() failed";
        }
    }
    return now;
}

// 根据channel在EPoller中的当前状态来更新channel的状态，比如channel还没添加到EPoller中，则就添加进来
void EPollPoller::updateChannel(Channel *channel)
{
    // 获取参数channel在epoll的状态，还未添加到EPoller中则为kNew，已经添加到则为kAdded，已经移除则为kDeleted
    const int index = channel->index();
    
    // 未添加状态和已删除状态都有可能会被再次添加到epoll中
    if (index == kNew || index == kDeleted)
    {
        int fd = channel->fd();
        if (index == kNew)
        {
            channels_[fd] = channel;      /// 添加到键值对 
        }
        else // index == kDeleted
        {
            assert(channels_.find(fd) != channels_.end());   // 这个处于kDeleted的channel还在channels_中，则继续执行
            assert(channels_[fd] == channel);                // channels_中记录的该fd对应的channel没有发生变化，则继续执行
        }
        // 修改channel的状态，此时是已添加状态
        channel->set_index(kAdded);
        // 向epoll对象加入channel
        update(EPOLL_CTL_ADD, channel);
    }
    // channel已经在poller上注册过
    else
    {
        // 没有感兴趣事件说明可以从epoll对象中删除该channel了
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        // 还有事件说明之前的事件删除，但是被修改了
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 当连接销毁时，从EPoller移除channel，这个移除并不是销毁channel，而只是把chanel的状态修改一下
// 这个移除指的是EPoller再检测该channel上是否有事件发生了
void EPollPoller::removeChannel(Channel *channel)
{
    // 从Map中删除
    int fd = channel->fd();
    channels_.erase(fd); 


    int index = channel->index();
    if (index == kAdded)
    {
        // 如果此fd已经被添加到EPoller中，则还需从EPoller对象中删除
        update(EPOLL_CTL_DEL, channel);
    }
    // 重新设置channel的状态为未被Poller注册
    channel->set_index(kNew);
}


// 判断参数channel是否在当前poller当中
bool EPollPoller::hasChannel(Channel *channel) const
{
    // 可以在map中找到该fd（键），并且it->second==channel（值）
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}


void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        // 获得events_对应的channel，并把events_[i].events赋值给该channel中用于记录实际发生事件的属性revents_
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}

void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    ::memset(&event, 0, sizeof(event));

    int fd = channel->fd();
    event.events = channel->events();
    // 把channel的地址给event.data.ptr，便于调用epoll_wait的时候能够从events_中得知是哪个channel发生的事件，
    // 具体见EPollPoller::fillActiveChannels函数
    event.data.ptr = channel;   

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR << "epoll_ctl() del error:" << errno;
        }
        else
        {
            LOG_FATAL << "epoll_ctl add/mod error:" << errno;
        }
    }
}