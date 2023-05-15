
#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>




/**
 * @description: 从socket读到缓冲区的方法是使用readv先读至buffer_，
 * buffer_空间如果不够会读入到栈上65536个字节大小的空间，然后以append的
 * 方式追加入buffer_。既考虑了避免系统调用带来开销，又不影响数据的接收(理由如下)。
 **/

/**
 ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
 使用read()将数据读到不连续的内存、使用write将不连续的内存发送出去，需要多次调用read和write，
 如果要从文件中读一片连续的数据到进程的不同区域，有两种方案，要么使用read一次将它们读到
 一个较大的缓冲区中，然后将他们分成若干部分复制到不同的区域，要么调用read若干次分批把他们读至
 不同的区域，这样，如果想将程序中不同区域的连续数据块写到文件，也必须进行类似的处理。
 频繁系统调用和拷贝开销比较大，所以Unix提供了另外两个函数readv()和writev()，它们只需要
 一次系统调用就可以实现在文件和进程的多个缓冲区之间传送数据，免除多次系统调用或复制数据的开销。
 readv叫散布读，即把若干连续的数据块读入内存分散的缓冲区中，
 writev叫聚集写，把内存中分散的若干缓冲区写到文件的连续区域中
 */

const char Buffer::kCRLF[] = "\r\n";

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
        append(extrabuf, n - writable);             // 对buffer_扩容 并将extrabuf存储的另一部分数据追加至buffer_
    }
    return n;

}

// 注意：
// inputBuffer_.readFd表示将对端数据读到inputBuffer_中，移动writerIndex_指针
// outputBuffer_.writeFd表示将数据写入到outputBuffer_中，从readerIndex_开始，可以写readableBytes()个字节
// 具体理由见Buffer.h顶部的注释
ssize_t Buffer::writeFd(int fd, int *saveErrno)
{
    // 向socket fd上写数据，假如TCP发送缓冲区满
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}