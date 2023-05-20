#pragma once

#include "FixedBuffer.h"

/**
 * GeneralTemplate:用于LogStream<<时间，sourceFIle等
*/
class GeneralTemplate : noncopyable
{
public:
    GeneralTemplate()
        : data_(nullptr),
          len_(0)
    {}

    explicit GeneralTemplate(const char* data, int len)
        : data_(data),
          len_(len)
    {}

    const char* data_;
    int len_;
};

/**
 * LogStream： 实现类似cout的效果，便于输出日志信息，即：LogStream << A << B << ...
 * 要实现类似cout的效果，就需要重载“<<”
*/

class LogStream : noncopyable
{
public:
    using SmallBuffer = FixedBuffer<kSmallBuffer>;   // 4KB大小

    // 像SmallBuffer中添加数据，如果buffer_，已满，则什么也不做
    void append(const char* data, int len) { buffer_.append(data, len); }

    // 获得SmallBuffer
    const SmallBuffer& buffer() const { return buffer_; }

    void resetBuffer() { buffer_.reset(); }

    // 重载运算符"<<"，以便可以像cout那样 << 任意类型
    LogStream& operator<<(short);
    LogStream& operator<<(unsigned short);
    LogStream& operator<<(int);
    LogStream& operator<<(unsigned int);
    LogStream& operator<<(long);
    LogStream& operator<<(unsigned long);
    LogStream& operator<<(long long);
    LogStream& operator<<(unsigned long long);
    LogStream& operator<<(float v);
    LogStream& operator<<(double v);
    LogStream& operator<<(char c);
    LogStream& operator<<(const void* p);
    LogStream& operator<<(const char* str);
    LogStream& operator<<(const unsigned char* str);
    LogStream& operator<<(const std::string& str);
    LogStream& operator<<(const SmallBuffer& buf);
    LogStream& operator<<(const GeneralTemplate& g); // 相当于(const char*, int)的重载
private:
    // 数字（整数或者浮点数）最终是要被写入buffer_中的，因此要把数字转化为字符串，
    // 像buffer_中写入数字对应的字符串时，buffer_需要通过kMaxnumericSize来判断
    // 自己是否还能够写的下
    static const int kMaxNumericSize = 48;           // 数字（整数或者浮点数）转换为字符串后最大的长度
    SmallBuffer buffer_;                             // 4KB，重载的所有运算符都是在向buffer中写数据，

    // 对于整型需要特殊处理成字符串，因为任意类型最终是要转换成字符串并添加到buffer_中的，
    // 整形有很多种，如int，short，long等等，因此用模板函数比较合适
    template<typename T>
    void formatInteger(T);                           // 将整数处理成字符串并添加到buffer_中
};

