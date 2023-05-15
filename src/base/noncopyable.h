#pragma once


/**
 * 删除了拷贝构造和赋值构造，只要继承自本类，都无法实现拷贝
 * 
*/
class noncopyable
{

public:
    noncopyable(const noncopyable &) = delete;
    noncopyable &operator = (const noncopyable &) = delete;
protected:
    // 设置为protect权限的成员函数可以让派生类继承派生类对象可以正常的构造和析构
    noncopyable() = default;
    ~noncopyable() = default;

};


