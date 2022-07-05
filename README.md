# 实现顺序
noncopyable->Logger->CurrentThread->Poller->EPollPoller->Channel和EventLoop同时写->Thread->EventLoopThread->EventLoopThreadPool->

InetAddress->Socket->Callbacks->Buffer->TcpConnection->Acceptor->TcpServer->

# 问题
muduo是怎么保证某个连接不会被多个线程同时处理的，即是怎么保证每次该连接的事件都能被分发到同一个线程进行处理的

EventLoop EventLoopThread EventLoopThreadPool之间是怎样的关系，谁管理subLoop，baseLoop是否和subLoop一起管理的

muduo把接受的连接分发给subLoop后,subLoop的epoll是怎么知道有新的channel进来的

TcpConnection中的sockfd到底是干什么的，在那里会用到

EPollPoller::fillActiveChannels注释分析可能有错误，因为很有可能一个fd的多个事件，比如EPOLLIN和EPOLLOUT，epoll_wait不会把该fd的
两个事件分别返回，而是将EPOLLIN和EPOLLOUT按位与，即EPOLLIN & EPOLLOUT，就相当于一个整数中包含了该fd的多个事件

channel socket TcpConnection这三个类之间的关系是什么，尤其是为什么每个连接都有个socket，起什么作用，socket是封装的那个步骤

channel的注册的可写事件什么时候会消失（从epoll_create中移除），是发送完数据自动消失还是怎么样（疑问来自TcpConnection::sendInLoop以及TcpConnection::shutdownInLoop的注释）
答：之所以注销可写事件确实是在发送完毕数据后手动注销的，具体见TcpConnection::handleWrite()


怎么判断客户已经断开，为什么TcpConnection::handleRead中读如到0字节就认为客户端断开连接# mymuduo
# mymuduo
