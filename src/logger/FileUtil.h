#pragma once

#include <stdio.h>
#include <string>

/**
 * FileUtil：工具类，用于打开一个文件fp_，向fp_指定buffer_中写入数据，把buffer_中的数据写入文件中，在LogFile中用到
*/
class FileUtil
{

public:
    // 构造时会把文件指针fp_的缓冲区设置为本地的buffer_
    explicit FileUtil(std::string &fileName);
    ~FileUtil();

    // 向buffer_中添加数据
    void append(const char* data, size_t len);

    // 刷新（相当于把buffer_中的数据写入到fp_打开的文件中）
    void flush();

    // 返回已经写入了多少字节，以便LogFile根据写入数据量来判断是否需要滚动日志
    off_t writtenBytes() const { return writtenBytes_; }

private:
    size_t write(const char *data, size_t len);

    FILE *fp_;                  // 文件指针
    char buffer_[64*1024];      // 64KB缓冲区
    off_t writtenBytes_;        // 指示文件偏移量(指示当前已经文件(注意不是buffer_)写入多少字节)
};


