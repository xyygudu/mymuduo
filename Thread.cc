#include <semaphore.h>

#include "Thread.h"
#include "CurrentThread.h"

std::atomic_int Thread::numCreated_(0);

Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false)
    , joined_(false)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    setDefaultName();
}

Thread:: ~Thread()
{
    if (started_ && !joined_)   // 如果线程已经开启并且不join
    {
        thread_->detach();      // 分离线程（主线程结束后，子线程仍会运行）
    }
}


void Thread::start()
{
    started_ = true;
    sem_t sem;
    sem_init(&sem, false, 0);
    // 开启线程
    thread_ = std::shared_ptr<std::thread>(new std::thread([&](){       //匿名函数，引用传传值
        tid_ = CurrentThread::tid();    // 子线程获取当前所在线程的tid，注意执行到这一行已经是在新创建的子线程里面了。
        sem_post(&sem);
        func_();                        // 子线程中执行线程回调
    }));
    // 这里必须等待上面的新线程获取了它的tid值才能继续执行。
    sem_wait(&sem);
}

// C++ std::thread 中join()和detach()的区别：https://blog.nowcoder.net/n/8fcd9bb6e2e94d9596cf0a45c8e5858a
void Thread::join()
{
    joined_ = true;
    thread_->join();                    // 子线程join后，只有主线程只有阻塞，一直等到子线程结束后才进入主线程
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if(name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf, sizeof(buf), "Thread%d", num);  // 给这个线程一个名字
        name_ = buf;
    }    
}