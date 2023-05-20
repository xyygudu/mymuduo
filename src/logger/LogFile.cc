#include "LogFile.h"
// #include <iostream>


LogFile::LogFile(const std::string &basename, off_t rollSize, bool threadSafe, int flushInterval, int checkEveryN)
    : basename_(basename)
    , rollSize_(rollSize)
    , flushInterval_(flushInterval)
    , checkEveryN_(checkEveryN)
    , count_(0)
    , mutex_(threadSafe ? new std::mutex : nullptr)  // 同步日志需要锁，而异步日志不需要锁
    , startOfPeriod_(0)
    , lastRoll_(0)
    , lastFlush_(0)
{
    rollFile();
}

void LogFile::append(const char* logline, int len)
{
    // 开启异步日志时似乎不用加锁，因为异步日志在apend的时候是将前端日志指针buffer_和后端异步日志指针bufferToWrite交换了的
    if (mutex_)   // 从构造函数可见，mutex_不为空，说明要考虑线程安全，默认是同步日志，日志可能来自多个线程，所以要锁
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        append_unlocked(logline, len);
    }
    else
    {
        append_unlocked(logline, len);
    }
}

void LogFile::append_unlocked(const char* logline, int len)
{
    file_->append(logline, len);
    // 如果file_指向的缓冲区buffer_已经写入的数据超过了设定的滚动日志的阈值rollSize_，就开始滚动日志
    if (file_->writtenBytes() > rollSize_)
    {
        // std::cout << "file_->writtenBytes()" << file_->writtenBytes() << std::endl;
        rollFile();
    }
    else
    {
        ++count_;
        if (count_ >= checkEveryN_)
        {
            count_ = 0;
            time_t now = ::time(NULL);
            // kRollPerSeconds_默认是1天，now / kRollPerSeconds_ 会自动取整，即得到从纪元
            // 开始计算到今天的天数，这个天数乘以kRollPerSeconds_就得到从纪元到今天0点的秒数
            time_t thisPeriod = now / kRollPerSeconds_ * kRollPerSeconds_;
            // 尽管之前创建的文件还没达到rollSize_(可理解为还没写满)，但是，如果上次创建的日志文件和当前并不是同一天，
            // 即thisPeriod != startOfPeriod_，那么还是会创建新的日志文件，简单来说就是:不管之前创建的(最开始创建的)
            // 日志文件是否写满,日志系统每天都会创建新的日志文件，
            if (thisPeriod != startOfPeriod_)  // 如果今天没有创建日志文件，则创建一个
            {
                rollFile();
            }
            else if (now - lastFlush_ > flushInterval_)
            {
                lastFlush_ = now;
                file_->flush();
            }
        }
    }
}

void LogFile::flush()
{
    if (mutex_)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        file_->flush();
    }
    else
    {
        file_->flush();
    }
}

// 滚动日志（创建一个新的文件）
// basename + time + hostname + pid + ".log"
// 会发生混动日志的情况：
// 1）日志大小超过了设定的阈值rollSize_
// 2）过了0点（注意不是每天0点准时创建，而是在append的时候判断刚才正在使用的日志文件是否是昨天/更久之前创建的，
// 只要今天还没有创建日志文件，那么就会在append的时候创建一个）
bool LogFile::rollFile()
{
    time_t now = 0;
    std::string filename = getLogFileName(basename_, &now);
    // 计算现在是第几天 now/kRollPerSeconds求出现在是第几天，再乘以秒数相当于是当前天数0点对应的秒数
    time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;

    if (now > lastRoll_)
    {
        lastRoll_ = now;
        lastFlush_ = now;
        startOfPeriod_ = start;  // 更新一下startOfPeriod_，表示今天已经创建日志了
        // 让file_指向一个名为filename的文件，相当于新建了一个文件
        file_.reset(new FileUtil(filename));
        return true;
    }
    return false;
}

// 根据当前时间生成一个文件名并返回
std::string LogFile::getLogFileName(const std::string& basename, time_t* now)
{
    std::string filename;
    filename.reserve(basename.size() + 64);
    filename = basename;

    char timebuf[32];
    struct tm tm;
    *now = time(NULL);
    localtime_r(now, &tm);
    // 写入时间
    strftime(timebuf, sizeof timebuf, ".%Y%m%d-%H%M%S", &tm);
    filename += timebuf;

    filename += ".log";

    return filename;
}