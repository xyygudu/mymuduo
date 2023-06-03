#include "ThreadPool.h"
#include "Logging.h"


ThreadPool::ThreadPool(const std::string& name)
    : mutex_()
    , cond_(),
    , name_(name)
    , running_(false)
{
}

ThreadPool::~ThreadPool()
{
    if (running_)
    {
        stop();
    }
}

void ThreadPool::start(int numThreads)
{
    running_ = true;
    threads_.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i)
    {
        char id[32];
        snprintf(id, sizeof(id), "%d", i+1);
        threads_.emplace_back(new Thread(std::bind(&ThreadPool::runInThread, this), name_+id));
        threads_[i]->start();
    }
    // 如果不创建线程则直接在本线程中执行回调
    if (numThreads == 0 && threadInitCallback_)
    {
        threadInitCallback_();
    }
}

void ThreadPool::stop()
{
    running_ = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.notify_all(); // 唤醒所有线程
    }
    // 等待所有子线程退出
    for (auto& thr : threads_)
    {
        thr->join();
    }
}

size_t ThreadPool::queueSize() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
}

// 如果线程池中有线程，则把任务添加到任务队列就行，如果线程池中没有线程，则直接执行任务
void ThreadPool::run(Task task)
{
    if (threads_.empty()) { task(); return; }
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.push_back(std::move(task));
    cond_.notify_one();
}

void ThreadPool::runInThread()
{
    try 
    {
        if (threadInitCallback_) { threadInitCallback_(); }
        
        while (running_)
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_.wait(lock, [](){ return !queue_.empty() && running_; });
                if (!queue_.empty())
                {
                    task = queue_.front();
                    queue_.pop_front();
                }
            }
            if (task) { task(); }
        }
    } 
    catch(...) 
    {
        LOG_WARN << "runInThread throw exception";
    }
}