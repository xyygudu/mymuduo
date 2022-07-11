#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

const int kNew = -1;     // 某个channel还没有添加至Poller，channel的成员index_初始化为-1
const int kAdded = 1;    // 某个channel已经添加到Poller中了
const int kDeleted = 2;  // 某个cahnnel已经从Poller中删除

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventSize)   // vector<epoll_event>(16)
{
    /*
    epoll_create()创建一个epoll的事例，通知内核需要监听size个fd。size指的并不是最大的后备存储设备，
    而是衡量内核内部结构大小的一个提示。当创建成功后，会占用一个fd，所以记得在使用完之后调用close()，
    否则fd可能会被耗尽。
    Note:自从Linux2.6.8版本以后，size值其实是没什么用的，不过要大于0，因为内核可以动态的分配大小，
    所以不需要size这个提示了。
    int epoll_create1(int flag);它和epoll_create差不多，不同的是epoll_create1函数的参数是flag，
    当flag是0时，表示和epoll_create函数完全一样，不需要size的提示了。
    当flag = EPOLL_CLOEXEC，创建的epfd会设置FD_CLOEXEC
    当flag = EPOLL_NONBLOCK，创建的epfd会设置为非阻塞
    一般用法都是使用EPOLL_CLOEXEC。
    关于FD_CLOEXEC它是fd的一个标识说明，用来设置文件close-on-exec状态的。当close-on-exec状态为0时，调用exec时，
    fd不会被关闭；状态非零时则会被关闭，这样做可以防止fd泄露给执行exec后的进程。因为默认情况下子进程是会继承父进程
    的所有资源的。
     */
    if (epollfd_ < 0)  // epoll_create创建失败则fatal error退出
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)  // 该函数将在EventLoop中被调用
{
    LOG_INFO("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());

    // 返回就绪的事件数量，events_初始化大小为16
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveError = errno;
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happend\n", numEvents);     // LOG_DEBUG最合理
        fillActiveChannels(numEvents, activeChannels);  // 将有事件的fd及其对应的事件记录到activeChannels中
        
        //扩容
        if (numEvents == events_.size())  //events_.size()初始值为16
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)   // 如果没有就绪的fd，说明epoll_wait超时了都还没有事件就绪
    {
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
        if (saveError != EINTR)  // EINTR表示在任何请求的事件发生或超时到期之前，信号处理程序中断了该调用
        {
            errno = saveError;
            LOG_ERROR("EPollPoller::poll() err!");
        }
    }
    return now;
}

// channel update remove => EventLoop updateChannel removeChannel => Poller updateChannel removeChannel
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();    // 获取当前channel的状态，状态有3种，kNew, kAdded, kDeleted
    LOG_INFO("fd=%d events=%d index=%d\n", channel->fd(), channel->events(), index);

    if (index == kNew || index == kDeleted)
    {
        if (index == kNew)  //这个channel从来都没有添加到poller中，那么就添加到poller的channel_map中
        {
            int fd = channel->fd();
            // 绑定sockfd和channel
            channels_[fd] = channel;  // channels_是channel_map类型，fd是key，channel是值
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else    // channel已经在Poller中注册过了
    {
        int fd = channel->fd();
        if (channel->isNoneEvent())  // 这个channel已经对任何事件都不感兴趣了
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 从Poller中删除channel
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);    // 从map中移除这个channel

    LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);

    int index = channel->index();
    if (index== kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

// 填写有事件发生的channel
// 思考：一个fd可能有多个事件要处理，muduo是在怎么把epoll_wait返回的事件和fd以及channel挂钩的
// 一个fd有多个事件，但是不是对应多个整数，而是多个事件按位与操作得到的一个整数,e.g., EPOLLIN & EPOLLOUT，所以
// numEvents就是epoll返回的文件描述符的数量
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        // 思考：为什么这里没有为这个新建的channel绑定fd？？？？？？？
        // 因为一个channel在添加进Poller中时，使用的是EPollPoller::update函数，从该函数可以看到
        // 给定channel后，channel指针已经赋值给了event.data.ptr，所以epoll_wait返回时，只要得到
        // event.data.ptr中保存的channel，那么该chennel所有的信息都可以得到
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);  
    }
}


// 更新channel通道 其实就是调用epoll_ctl add/mod/del
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    ::memset(&event, 0, sizeof(event));

    int fd = channel->fd();
    event.events = channel->events();
    event.data.fd = fd;
    event.data.ptr = channel;

    if(::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }

}
