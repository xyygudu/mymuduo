#pragma once

#include "noncopyable.h"

#include <thread>
#include <functional>
#include <memory>
#include <string>
#include <atomic>

class Thread
{
   
public:
    using ThreadFunc = std::function<void()>;
    explicit Thread(ThreadFunc, const std::string &name = std::string());
    ~Thread();

    void start();                   // 创建/开始启动线程
    void join();

    bool started() const { return started_; }
    pid_t tid() const { return tid_; }
    const std::string& name() const { return name_; }
    static int numCreated() { return numCreated_; }

private:
    void setDefaultName();

    bool started_;                  // 线程是否已经启动
    bool joined_;
    std::shared_ptr<std::thread> thread_;
    pid_t tid_;                     // 线程id
    ThreadFunc func_;               // 线程运行函数
    std::string name_;
    static std::atomic_int32_t numCreated_; // 线程索引
};
