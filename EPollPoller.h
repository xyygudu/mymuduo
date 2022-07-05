#pragma once

#include <vector>
#include <sys/epoll.h>

#include "Poller.h"
#include "Timestamp.h"

/**
 * epoll的使用:
 * 1. epoll_create
 * 2. epoll_ctl (add, mod, del)
 * 3. epoll_wait
 **/
/*
 * epoll的使用:https://www.cnblogs.com/fnlingnzb-learner/p/5835573.html
    1. int epoll_create(int size);
        创建一个epoll的句柄，size用来告诉内核这个监听的数目一共有多大,
       当创建好epoll句柄后，它就是会占用一个fd值
    2.  int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
        epoll的事件注册函数在这里先注册要监听的事件类型。第一个参数是epoll_create()的返回值，第二个参数表示动作，用三个宏来表示：
            EPOLL_CTL_ADD：注册新的fd到epfd中；
            EPOLL_CTL_MOD：修改已经注册的fd的监听事件；
            EPOLL_CTL_DEL：从epfd中删除一个fd；
        第三个参数是需要监听的fd，第四个参数是告诉内核需要监听什么事
        struct epoll_event结构如下:
        typedef union epoll_data {
            void *ptr;
            int fd;
            __uint32_t u32;
            __uint64_t u64;
        } epoll_data_t;

        struct epoll_event {
            __uint32_t events; // Epoll events
            epoll_data_t data;
        };
        events可以是以下几个宏的集合：
        EPOLLIN ：表示对应的文件描述符可以读（包括对端SOCKET正常关闭）；
        EPOLLOUT：表示对应的文件描述符可以写；
        EPOLLPRI：表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）；
        EPOLLERR：表示对应的文件描述符发生错误；
        EPOLLHUP：表示对应的文件描述符被挂断；
        EPOLLET： 将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)来说的。
        EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里
    3. int epoll_wait(int epfd, struct epoll_event * events, int maxevents, int timeout);
        等待事件的产生，类似于select()调用。参数events用来从内核得到事件的集合，
        maxevents告之内核这个events有多大，这个maxevents的值不能大于创建epoll_create()
        时的size，参数timeout是超时时间（毫秒，0会立即返回，-1将不确定，也有说法说是永久阻塞）。
        该函数返回需要处理的事件数目，如返回0表示已超时。
 */

class Channel;

class EPollPoller : public Poller
{

public:
    EPollPoller(EventLoop *loop);
    ~EPollPoller() override; //子类设计者可以在其析构函数后增加关键字override，一旦父类缺少关键字virtual就会被编译器发现并报错


    // 重写基类虚函数
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
    void updateChannel(Channel *channel) override; 
    void removeChannel(Channel *channel) override;   // 从channel_map删除这个channel


private:
    static const int kInitEventSize = 16;  // 事件列表的初始长度。

    // 将Poller的就绪事件填写给activeChannels
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;

    // 更新channel
    void update(int operation, Channel *channel);

    using EventList = std::vector<epoll_event>;
    int epollfd_;       // epoll事件循环本身要一个fd，即poll_create创建返回的fd保存在epollfd_中
    EventList events_;  // 用于存放epoll_wait返回的所有发生的事件的文件描述符事件集
};


