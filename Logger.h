#pragma once
#include <string>

#include "noncopyable.h"

/*
日志级别：INFO ERROR FATAL DEBUG
*/


//LOG_INFO(%s %d, agr1, arg2)
// do while是为了保证宏定义的所有操作作为一个整体来是实现，具体百度
#define LOG_INFO(logmsgFormat, ...)                         \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(INFO);                           \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
    } while (0);                                            \
    

#define LOG_ERROR(logmsgFormat, ...)                        \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(ERROR);                          \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
    } while (0);                                            \


#define LOG_FATAL(logmsgFormat, ...)                        \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(FATAL);                          \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
        exit(-1);                                           \
    } while (0);                                            \


#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...)                        \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(DEBUG);                          \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
    } while (0)                                             
#else
#define LOG_DEBUG(logmsgFormat, ...)
#endif

// 定义日志级别：INFO ERROR FATAL DEBUG
enum LogLevel
{
    INFO,   //普通信息
    ERROR,  //错误信息
    FATAL,  //core dump信息
    DEBUG,  //调试信息
};

class Logger : noncopyable
{
private:
    int logLevel_;
public:
    static Logger &instance();
    // 设置日志级别
    void setLogLevel(int level);
    // 写日志
    void log(std::string msg);
};


