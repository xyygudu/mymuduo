#pragma once

// 继承noncopyable的类无法进行拷贝构造和赋值构造
class noncopyable
{
private:
    /* data */
public:
    noncopyable(const noncopyable &) = delete;
    noncopyable &operator = (const noncopyable &) = delete;
    
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};



