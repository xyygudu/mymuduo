#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
    ~EventLoopThreadPool();

    void setThreadNum(int numThreads) { numThreads_ = numThreads; }

    // 启动线程池
    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    // 主Reactor将新接受的连接分发给子Reactor时，通过轮训的方式获取应该分发给哪个子Reactor（EventLoop）
    EventLoop *getNextLoop();

    std::vector<EventLoop*> getAllLoops();

    bool started() const { return started_; }

    const std::string name() const { return name_; }


private:
    EventLoop *baseLoop_;        // 主线程（主Reactor）对应的EventLoop
    std::string name_;          
    bool started_;              // 是否已经开启线程池
    int numThreads_;
    size_t next_;               // 轮训的下标
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};