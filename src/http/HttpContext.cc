#include "Buffer.h"
#include "HttpContext.h"
#include "Logging.h"

bool HttpContext::processRequestLine(const char* begin, const char* end)
{
    // 完整的请求如下；本函数只解析请求行（第一行）:
    // GET /index.html?id=acer HTTP/1.1
    // Host: 127.0.0.1:8002
    // User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:102.0) Gecko/20100101 Firefox/102.0
    // Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8
    // Accept-Language: en-US,en;q=0.5
    // Accept-Encoding: gzip, deflate, br
    // Connection: keep-alive
    
    bool succeed = false;
    const char *start = begin;
    const char *space = std::find(start, end, ' ');         // 返回空格所在位置
    if (space != end && request_.setMethod(start, space))   // 判断请求方法是否有效
    {
        start = space + 1;                                  // 跳过空格
        space = std::find(start, end, ' ');                 // 继续寻找下一个空格
        if (space != end)
        {
            const char* question = std::find(start, space, '?');    // path和query是用"？"隔开的
            if (question != space)
            {
                request_.setPath(start, question);
                request_.setQuery(question, space);
            }
            else
            {
                request_.setPath(start, space);
            }
            start = space + 1;
            succeed = end-start == 8 && std::equal(start, end-1, "HTTP/1.");
            if (succeed)
            {
                if (*(end-1) == '1')
                {
                    request_.setVersion(HttpRequest::kHttp11);
                }
                else if (*(end-1) == '0')
                {
                    request_.setVersion(HttpRequest::kHttp10);
                }
                else
                {
                    succeed = false;
                }
            }
        }
    }
    return succeed;
}

bool HttpContext::parseRequest(Buffer *buf, Timestamp receiveTime)
{
    bool ok = true;                                 // 表示解析是否成功
    bool hasMore = true;                            // 表示是否还有更多的信息需要我解析, 
    while (hasMore)
    {
        if (state_ == kExpectRequestLine)
        {
            const char *crlf = buf->findCRLF();     // CRLF表示“\r\n", 如果找到了就返回"\r\n"所在位置
            if (crlf)                               // 如果buffer的可读缓冲区有"\r\n"
            {
                ok = processRequestLine(buf->peek(), crlf); // peek()返回readerIndex_
                if (ok)
                {
                    request_.setReceiveTime(receiveTime);
                    buf->retrieveUntil(crlf + 2);   // 从buffer中取出一行，crlf+2的原因是，crlf自生存储的\r\n占用两个字符，所以+2才能移动到请求行的尾部
                    state_ = kExpectHeaders;        // 接下来希望解析请求头
                }
                else
                {
                    hasMore = false;
                }
            }
            else
            {
                hasMore = false;
            }
        }
        else if (state_ == kExpectHeaders)
        {
            const char *crlf = buf->findCRLF();
            if (crlf)
            {
                // 找到“：”位置
                const char *colon = std::find(buf->peek(), crlf, ':');
                if (colon != crlf)
                {
                    request_.addHeader(buf->peek(), colon, crlf);
                }
                else
                {
                    // 头部结束后的空行
                    state_ = kGotAll;
                    hasMore = false;
                }
                buf->retrieveUntil(crlf + 2);
            }
            else
            {
                hasMore = false;
            }
        }
        else if (state_ == kExpectBody)
        {
        
        }
    }
    return ok;
}