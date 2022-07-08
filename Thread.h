#pragma once

#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>

#include "noncopyable.h"

class Thread : noncopyable
{
   
public:
    using ThreadFunc = std::function<void()>;
    explicit Thread(ThreadFunc, const std::string &name = std::string());
    ~Thread();

    void start();
    void join();

    bool started() { return started_; }
    pid_t tid() const {return tid_; }
    const std::string &name() const { return name_; }

    static int numCreated() { return numCreated_;}

private:
    void setDefaultName();

    bool started_;
    bool joined_;
    std::shared_ptr<std::thread> thread_;   // （暂未看懂）注意这里，如果你直接定义一个thread对象，那这个线程就直接开始运行了，所以这里定义一个智能指针，在需要运行的时候再给他创建对象。
    pid_t tid_;                             // 线程id，在线程创建时再绑定
    ThreadFunc func_;                       // 线程回调函数
    std::string name_;
    static std::atomic_int numCreated_;

};


