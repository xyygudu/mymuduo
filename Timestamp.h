#pragma once

#include <iostream>
#include <string>

class Timestamp
{
private:
    int64_t microSecondsSinceEpoch_;
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch); // 使用explicit的构造函数只能显示调用
    static Timestamp now();
    std::string toString() const;   // const是为了防止该函数修改成员变量的值

    void swap(Timestamp& that)      // httprequest用到
    {
        std::swap(microSecondsSinceEpoch_, that.microSecondsSinceEpoch_);
    }
};


