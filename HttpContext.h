#pragma once

#include "copyable.h"
#include "HttpRequest.h"

class Buffer;


// 用于解析请求，并将解析后的信息存入到HttpRequest类中
class HttpContext : public copyable
{
public:
    enum HttpRequestParseState         // 解析状态
    {
        kExpectRequestLine,             // 表示接下来希望buffer中存储的时请求行
        kExpectHeaders,                 // 
        kExpectBody,
        kGotAll,
    };

    HttpContext() : state_(kExpectRequestLine) {}

    bool parseRequest(Buffer* buf, Timestamp receiveTime);  // 解析请求，只要出错就返回false

    bool gotAll() const { return state_ == kGotAll; }

    void reset()
    {
        state_ = kExpectRequestLine;
        HttpRequest dummy;
        request_.swap(dummy);
    }

    const HttpRequest& request() const { return request_; }

    HttpRequest& request() { return request_; }


private:
    // 处理请求行
    bool processRequestLine(const char* begin, const char* end);
    HttpRequestParseState state_;       // 解析状态
    HttpRequest request_;
};