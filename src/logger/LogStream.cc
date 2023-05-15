#include "LogStream.h"
#include <algorithm>

// 用于把数字转化位对应的字符
static const char digits[] = {'9', '8', '7', '6', '5', '4', '3', '2', '1', '0',
                               '1', '2', '3', '4', '5', '6', '7', '8', '9'};
// 用于打印16进制的指针类型
const char digitsHex[] = "0123456789ABCDEF";

size_t convertHex(char buf[], uintptr_t value)
{
    uintptr_t i = value;
    char* p = buf;

    do
    {
        int lsd = static_cast<int>(i % 16);
        i /= 16;
        *p++ = digitsHex[lsd];
    } while (i != 0);

    *p = '\0';
    std::reverse(buf, p);

    return p - buf;
}

// 将整数处理成字符串并添加到buffer_中
template <typename T>
void LogStream::formatInteger(T num)
{
    if (buffer_.avail() >= kMaxNumericSize)
    {
        char *start = buffer_.current();
        char *cur = start;
        const char *zero = digits + 9;
        bool negative = (num < 0);  // 是否为负数
        
        // 从数字的末位开始转化成字符，最后在翻转字符串
        do {
            int remainder = static_cast<int>(num % 10);
            *(cur++) = zero[remainder];
            num = num / 10;
        } while (num != 0);

        if (negative) {             // 如果是负数就添加负号
            *(cur++) = '-';
        }
        *cur = '\0';

        std::reverse(start, cur);
        buffer_.add(static_cast<int>(cur - start)); 
    }
}


LogStream& LogStream::operator<<(short v)
{
  *this << static_cast<int>(v);
  return *this;
}

LogStream& LogStream::operator<<(unsigned short v)
{
    *this << static_cast<unsigned int>(v);
    return *this;
}

LogStream& LogStream::operator<<(int v)
{
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned int v)
{
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(long v)
{
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long v)
{
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(long long v)
{
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long long v)
{
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(float v) 
{
    *this << static_cast<double>(v);
    return *this;
}

LogStream& LogStream::operator<<(double v) 
{
    if (buffer_.avail() >= kMaxNumericSize)
    {
        char buf[32];
        // int snprintf(char *str, size_t size, const char *format, ...) 设将可变参数(...)
        // 按照 format 格式化成字符串，并将字符串复制到 str 中，
        // size 为要写入的字符的最大数目，超过 size 会被截断，最多写入 size-1 个字符
        int len = snprintf(buffer_.current(), kMaxNumericSize, "%.12g", v); 
        buffer_.add(len);
    }
    return *this;
}

LogStream& LogStream::operator<<(char c)
{
    buffer_.append(&c, 1);
    return *this;
}

// 输出指针的值
LogStream& LogStream::operator<<(const void* p) 
{
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    if (buffer_.avail() >= kMaxNumericSize)
    {
        char* buf = buffer_.current();
        buf[0] = '0';
        buf[1] = 'x';
        size_t len = convertHex(buf+2, v);
        buffer_.add(len+2);
    }
    return *this;
}

LogStream& LogStream::operator<<(const char* str)
{
    if (str)
    {
        buffer_.append(str, strlen(str));
    }
    else 
    {
        buffer_.append("(null)", 6);
    }
    return *this;
}

LogStream& LogStream::operator<<(const unsigned char* str)
{
    return operator<<(reinterpret_cast<const char*>(str));
}

LogStream& LogStream::operator<<(const std::string& str)
{
    buffer_.append(str.c_str(), str.size());
    return *this;
}

LogStream& LogStream::operator<<(const SmallBuffer& buf)
{
    *this << buf.toString();        // 调用的是LogStream::operator<<(const std::string& str)
    return *this;
}

LogStream& LogStream::operator<<(const GeneralTemplate& g)
{
    buffer_.append(g.data_, g.len_);
    return *this;
}


#if 0
// 编译命令：g++ LogStream.cc -o testlogstream -I ../base
#include <iostream>
using namespace std;
int main()
{
    // 把kSamllBuffer改为50，下面的字符串长一点，就会出现buffer存不下从而丢弃的情况
    LogStream logstream;
    logstream << -3.1415926 << &logstream << 46546465465465;
    const LogStream::SmallBuffer &buffer = logstream.buffer();
    cout << buffer.data() << endl;
    return 0;
}
#endif