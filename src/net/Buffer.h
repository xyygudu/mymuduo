#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <stddef.h>
#include <assert.h>

// 网络库底层的缓冲区类型定义
/*
Buffer的样子！！！
+-------------------------+----------------------+---------------------+
|    prependable bytes    |    readable bytes    |    writable bytes   |
|                         |      (CONTENT)       |                     |
+-------------------------+----------------------+---------------------+
|                         |                      |                     |
0        <=           readerIndex     <=     writerIndex             size
*/

// 注意，readable bytes空间才是要服务端要发送的数据，writable bytes空间是从socket读来的数据存放的地方。
// 具体的说：是write还是read都是站在buffer角度看问题的，不是站在和客户端通信的socket角度。当客户socket有数据发送过来时，
// socket处于可读，所以我们要把socket上的可读数据写如到buffer中缓存起来，所以对于buffer来说，当socket可读时，是需要向buffer中
// write数据的，因此 writable bytes 指的是用于存放socket上可读数据的空间, readable bytes同理

class Buffer
{

public:
    
    /*static const int可以在类里面初始化，是因为它既然是const的，那程序就不会再去试图初始化了。*/
    static const size_t kCheapPrepend = 8;      // 记录数据包的长度的变量长度，用于解决粘包问题
    static const size_t kInitialSize = 1024;    // 缓冲区长度（不包括kCheapPrepend）

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)
        , readerIndex_(kCheapPrepend)
        , writerIndex_(kCheapPrepend)
    {

    }

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const { return readerIndex_; }

    // 返回缓冲区中可读数据的起始地址
    const char *peek() const { return begin() + readerIndex_; }

    // 查找buffer中是否有"\r\n", 解析http请求行用到
    const char* findCRLF() const
    {
        const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF+2);
        return crlf == beginWrite() ? NULL : crlf;
    }

    // 一直取到end位置. 在解析请求行的时候从buffer中读取一行后要移动指针便于读取下一行
    void retrieveUntil(const char* end)
    {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(end - peek());
    }
    
    // 从可读区域中取出数据
    void retrieve(size_t len)           
    {
        if (len < readableBytes())      // 数据没有全部取出
        {
            readerIndex_ += len;
        }
        else
        {
            retrieveAll();
        }
    }

    // 就是把readerIndex_和writerIndex_移动到初始位置
    void retrieveAll()
    {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    // 把onMessage函数上报的Buffer数据 转成string类型的数据返回
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes()); }
    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len);      // 上面一句把缓冲区中可读的数据已经读取出来 这里肯定要对缓冲区进行复位操作
        return result;
    }

    // 通过扩容或者其他操作确保可写区域是写的下将要写的数据（具体操作见makeSpace方法）
    void ensureWritableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len); // 通过移动可读数据来腾出可写空间或者直接对buffer扩容
        }
    }

    void append(const std::string &str)
    {
        append(str.data(), str.size());
    }

    // 把[data, data+len]内存上的数据添加到writable缓冲区当中
    void append(const char* data, size_t len)
    {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }
    char *beginWrite() { return begin() + writerIndex_; }
    const char *beginWrite() const { return begin() + writerIndex_; }

    // 从fd上读取数据
    ssize_t readFd(int fd, int *saveErrno);
    // 通过fd发送数据
    ssize_t writeFd(int fd, int *saveErrno);


private:
    // vector底层数组首元素的地址 也就是数组的起始地址
    char* begin() { return &*buffer_.begin(); }     // 先调用begin()返回buffer_的首个元素的迭代器，然后再解引用得到这个变量的，再取地址，得到这个变量的首地址。   
    const char* begin() const { return &*buffer_.begin(); }

    void makeSpace(size_t len)                      // 调整可写的空间
    {
        /**
         * | kCheapPrepend |xxx| reader | writer |                     // xxx标示reader中已读的部分
         * | kCheapPrepend | reader ｜          len          |
         **/
        // 当len > xxx + writer的部分，即：能用来写的缓冲区大小 < 我要写入的大小len，那么就要扩容了
        if (writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else  // 如果能写的缓冲区大小 >= 要写的len，那么说明要重新调整一下Buffer的两个游标了. p-kC表示调整前prependable bytes - kCheapPrepend。
        {
            // p-kC(prependable减去kCheapPrepend)的长度与writable bytes的和就是可写的长度，如果len<=可写的部分,说明还能len长的数据还能写的下，但是可写的空间是p-kC的长度与writable bytes
            // 共同构成，而且p-kC的长度与writable bytes不连续，所以需要将readable bytes向前移动p-kC个单位，使得可写部分空间是连续的
            // 调整前：
            // +-------------------------+----------------------+---------------------+
            // |    prependable bytes    |    readable bytes    |    writable bytes   |
            // | kCheapPrepend |  p-kC   |      (CONTENT)       |                     |
            // +-------------------------+----------------------+---------------------+
            // |                         |                      |                     |
            // 0        <=           readerIndex     <=     writerIndex             size
            // 调整后：
            // +--------------------+--------------------+----------------------------+
            // | prependable bytes  |  readable bytes    |     新的writable bytes      |
            // | = kCheapPrepend    |    (CONTENT)       | = p-kC+之前的writable bytes |
            // +--------------------+--------------------+----------------------------+
            // |                    |                    |                            |
            // 0        <=      readerIndex     <=  writerIndex                     size
            size_t readable = readableBytes();  
            std::copy(begin() + readerIndex_,
                      begin() + writerIndex_,       // 把这一部分数据拷贝到begin+kCheapPrepend起始处
                      begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }

    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;

    static const char kCRLF[];  // 存储"\r\n"
};

