<a name="UYeJa"></a>
## 为什么应用层要Buffer？
每一个连接都需要两个应用层的Buffer，一个output buffer和一个input buffer。output buffer用于存储待发送的数据。input buffer用于存放接收到的数据。<br />**为什么要output buffer？**<br />当网络不好的时候，服务器内核中TCP发送缓冲区可能不够了，如果用户有很多数据要发送，那就只能等待内核发送缓冲区空闲，但是你也不知道要等多久，因为取决于对方接收了多少，解决方案是：如果操作系统无法把用户要发送的数据一次性发送完毕，那就把剩余的数据暂存在output buffer中，下次再发送，这样就可以立马让出控制权去处理其他业务。(更详细请参考P205, 7.4.2)<br />**为什么要input buffer？**<br />TCP接收缓冲区收到数据，如果应用层一次性不能读取完毕，就会频繁触发EPOLLIN事件，降低效率，所以需要尽可能一次性把TCP接收缓冲区的数据全部读取出来，然而，尽管把数据全部读取出来了，但是可能接收的依然不是完整的消息，如果我们等着接收完整的数据就太浪费时间了，所以可以把取出来的数据暂时存储再input buffer中，然后就可以立马处理别的业务，当input buffer中收到完整的消息的时候，在通知程序进行业务处理。(更详细请参考P206, 7.4.2)
<a name="t14TO"></a>
## 谁用Buffer？
对于input buffer，网络库会把socket上的数据读出来并写入input buffer，然后客户代码（使用网络库的人）就可以直接从input buffer中读取数据，也就是客户代码要读取某个连接上的数据都是直接和input buffer打交道，而不需要和系统调用`read`或者`readv`打交道。（P207, 7.3.4）<br />对于output buffer，客户代码会把要发送的数据写入网络库output buffer，然后由网络库负责把output buffer中的数据通过系统调用`write`发送给对方，给客户代码的感觉就是：我只要往output buffer写数据，就等同于发送给了对方。<br />![buffer0.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684392559964-73943252-4f62-463d-b6d3-4f87d798e589.svg#clientId=uc879991e-c194-4&from=drop&height=236&id=uccf7f7e5&originHeight=181&originWidth=471&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=14183&status=done&style=none&taskId=ue20de393-4304-434b-9355-a3590a31c3a&title=&width=613)
<a name="EDaK1"></a>
## Buffer数据结构
Buffer内部是个std::vector<char>，前8个字节预留着用于记录数据长度，readIndex指向可读取数据的初始位置，writerIndex指向空闲区的起始位置。下图展示了Buffer初始状态、写入部分数据和读取部分数据的情况。<br />![buffer2.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684472631663-e7a08860-16d6-4177-9a8a-a137b0c88a49.svg#clientId=u5669da65-f7aa-4&from=paste&height=373&id=u0c94baf6&originHeight=371&originWidth=461&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=33520&status=done&style=none&taskId=u67f0e8b6-a509-4050-81b7-255809022e8&title=&width=463.8000183105469)<br />在上图的基础上，如果接下来要继续写入大于"size减writerIndex"的数据怎么办？（见Buffer::makeSpace函数）

1. 如果要写入的数据只比"空闲区②"大，但是比“①+②”小，此时可以把未读取的数据往前挪，剩余的控件就足够写下要写的数据了。

![buffer2.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684397022994-aff7d9cd-26df-4b33-8588-7d5d0bae9949.svg#clientId=uc879991e-c194-4&from=drop&id=uae6e674f&originHeight=371&originWidth=461&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=33524&status=done&style=none&taskId=ue496c524-a263-428c-94b5-8d2fd684195&title=)

2. 如果要写入的数据大于“①+②”，则对Buffer进行扩容，扩容至刚好可以容纳需要写入的数据。

![buffer3.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684396972237-39013278-e1a1-40f7-89f1-0a883fb15877.svg#clientId=uc879991e-c194-4&from=drop&id=u14e34eaf&originHeight=372&originWidth=522&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=32377&status=done&style=none&taskId=u0bde9526-ceac-439e-b253-a86314abe56&title=)
<a name="eM1KP"></a>
## 把socket上的数据写入Input Buffer
当socket上有数据可读时，会调用`TcpConnection::handleRead`函数，在该函数中，inputBuffer_会调用`Buffer::readFd`函数，把socket上的数据写入inputBuffer_中，核心代码如下：
```cpp
void TcpConnection::handleRead(Timestamp receiveTime)
{
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)                      // 从fd读到了数据，并且放在了inputBuffer_上
    {
        // 已建立连接的用户有可读事件发生了 调用用户传入的回调操作onMessage shared_from_this就是获取了TcpConnection的智能指针
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
}

ssize_t Buffer::readFd(int fd, int *saveErrno)
{
    // 栈额外空间，用于从套接字往出读时，当buffer_暂时不够用时暂存数据，待buffer_重新分配足够空间后，在把数据交换给buffer_。
    char extrabuf[65536] = {0};                     // 栈上内存空间 65536/1024 = 64KB

    struct iovec vec[2];                            // 使用iovec指向两个缓冲区
    const size_t writable = writableBytes();        // 可写缓冲区大小

    // 第一块缓冲区，指向可写空间
    vec[0].iov_base = begin() + writerIndex_;       // 当我们用readv从socket缓冲区读数据，首先会先填满这个vec[0], 也就是我们的Buffer缓冲区
    vec[0].iov_len = writable;
    // 第二块缓冲区，指向栈空间
    vec[1].iov_base = extrabuf;                     // 第二块缓冲区，如果Buffer缓冲区都填满了，那就填到我们临时创建的栈空间
    vec[1].iov_len = sizeof(extrabuf);

    // 如果Buffer缓冲区大小比extrabuf(64k)还小，那就Buffer和extrabuf都用上
    // 如果Buffer缓冲区大小比64k还大或等于，那么就只用Buffer。
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);     // Buffer存不下，剩下的存入暂时存入到extrabuf中

    if (n < 0)
    {
        *saveErrno = errno;
    }
    else if (n <= writable)                         // Buffer的可写缓冲区已经够存储读出来的数据了
    {
        writerIndex_ += n;
    }
    else                                            // Buffer存不下，对Buffer扩容，然后把extrabuf中暂存的数据拷贝（追加）到Buffer
    {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);             // 根据情况对buffer_扩容 并将extrabuf存储的另一部分数据追加至buffer_
    }
    return n;

}
```
`Buffer::readFd`函数的功能就是把socket上的数据写入input buffer，**为了能够尽可能把socket上的数据都读取出来，同时节约一点内存空间，muduo借用了栈上的空间来达到这一目的**，具体如下：<br />**没有栈空间，是怎么导致空间浪费的？**<br />需要指出的是`writableBytes()`函数在计算input buffer可写空间大小时，只计算了从空闲块②的大小，没有把空闲块①计算进来。所以，如果没有栈上的空间，并且通过`writableBytes()`函数来指明input buffer空闲大小，那么从socket上读取的数据大于空闲块②的大小时，就需要扩容input buffer，就变成了下面扩容后的图，显然，空闲块①还没有利用，这就是直接扩容导致了空间的浪费。<br />![bf.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684410100449-e9de7aaa-4cc9-4599-89fc-7b2ed892dd9c.svg#clientId=uc879991e-c194-4&from=paste&height=89&id=u6496fcaa&originHeight=91&originWidth=461&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=12584&status=done&style=none&taskId=u61bfda14-e236-4720-8423-dbea3868d82&title=&width=449.8000183105469)<br />![bf5.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684410491578-953c36e7-9dbb-4167-a1c4-d7feb5caaf43.svg#clientId=uc879991e-c194-4&from=drop&id=ue64d244e&originHeight=91&originWidth=521&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=12568&status=done&style=none&taskId=u9af00be1-bcbd-4777-b9d6-8cb452085fe&title=)
:::tips
个人认为，其实也没必要用栈空间，只需要修改writableBytes()，让其返回的是空闲块①和②的大小即可，只不过改了writableBytes()，其他调用writableBytes()的函数，比如Buffer::readFd，就变得复杂了。
:::
**有栈空间时，是怎么减少空间浪费的？**
:::tips
见Buffer::apend()函数，apend函数会调用Buffer::makeSpace函数，makeSpace会根据空闲区域的大小和要写入数据的大小，来判断是否需要扩容
:::
栈空间能够减少空间的浪费，只存在于这种情况：要写入input buffer的数据比"空闲区②"大，但是比“①+②”小。如果要写入的数据大于“①+②”，即使使用了栈空间，最终都是避免不了要扩容input buffer，所以和不用栈空间没什么区别。下面就说明：要写入的数据比"空闲区②"大，但是比“①+②”小时，是怎么减少内存浪费的。<br />![bfreadfd.drawio.svg](https://cdn.nlark.com/yuque/0/2023/svg/27222704/1684411936566-210667e8-7641-4d25-9534-49effe4f432e.svg#clientId=uc879991e-c194-4&from=drop&id=ud489d560&originHeight=482&originWidth=761&originalType=binary&ratio=1.25&rotation=0&showTitle=false&size=51623&status=done&style=none&taskId=u82e1262f-765e-48c5-b45b-10117c177d7&title=)<br />从上图可见，和没有栈空间的情况相比，用栈上的空间可以在“要写入的数据只比"空闲区②"大，但是比“①+②”小”的情况下，避免input buffer扩容。（步骤2和3是在append函数中完成的，可以去看源码）
<a name="Wfyap"></a>
## 把用户数据通过output buffer发送给对方
以EchoServer为例，如下代码所示，当收到客户端发送过来的信息后，我们要回复客户端，即在EchoServer::onMessage函数中调用TcpConnection::send函数把消息发送给客户端，接下来就看下TcpConnection::send函数是怎么通过output buffer发送给对方的。
```cpp
// 可读写事件回调
void EchoServer::onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
{
    // 把客户端发送的数据取出来
    std::string msg = buf->retrieveAllAsString();
    conn->send(msg);
}

void TcpConnection::send(const std::string &msg)   
{
    ...省略代码
    // 如果连接是已经建立的，就把buffer中的数据取出来发送出去
    sendInLoop(msg.c_str(), msg.size());
    ...省略代码
}

void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ...省略代码
    // 把要发送的数据写入outputBuffer_
    outputBuffer_.append((char *)data, len);
    // 如果没有向Epoll注册可写事件，则注册可写事件
    if (!channel_->isWriting())
    {
        channel_->enableWriting(); 
    }
    ...省略代码
}
```
从上面代码可见，TcpConnection::send函数并没有把数据发送给对方，只是把要发送的数据写入了output buffer，并且告知Epoll关注当前连接的可写事件，那什么时候发送呢？<br />由于现在Epoll现在已经关注该连接的可写事件，当该链接的TCP发送缓冲区空闲时，Epoll就会触发该连接的可写事件，也就是会调用TcpConnection::handleWrite函数，该函数核心代码如下：
```cpp
void TcpConnection::handleWrite()
{
    if (channel_->isWriting())  // 如果该连接关注了可写事件
    {
        int savedErrno = 0;
        // 把outputBuffer_中的数据发送给对方
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)   // 如果发送了部分数据
        {
            // 把outputBuffer_的readerIndex往前移动n个字节，
            // 因为outputBuffer_刚已经发送出去了n字节
            outputBuffer_.retrieve(n);  // readerIndex往前移动n个字节        
            if (outputBuffer_.readableBytes() == 0)
            {
                //outputBuffer_中的数据全部发送完毕后注销写事件，
                // 以免epoll频繁触发可写事件，导致效力低下
                channel_->disableWriting();     
            }
        }
    }
}
```
该函数核心思想就是把output buffer中的数据读取出来发送到对方的socket上（其他代码的意思这里不展开，请自己看注释，不看也不影响后续理解），发送这一行为是调用Buffer::writeFd实现的，其核心代码如下：
```cpp
ssize_t Buffer::writeFd(int fd, int *saveErrno)
{
    // 向socket fd上写数据，假如TCP发送缓冲区满
    ssize_t n = ::write(fd, peek(), readableBytes());
}
```
可以看到Buffer::writeFd函数调用了系统调用函数`write`，把output buffer中的数据发送给对方了。
