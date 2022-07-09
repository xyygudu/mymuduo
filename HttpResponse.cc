#include <stdio.h>

#include "HttpResponse.h"
#include "Buffer.h"

void HttpResponse::appendToBuffer(Buffer * output) const{
    /*典型的响应消息： 
     *   HTTP/1.1 200 OK 
     *   Date:Mon,31Dec200104:25:57GMT 
     *   Server:Apache/1.3.14(Unix) 
     *   Content-type:text/html 
     *   Last-modified:Tue,17Apr200106:46:28GMT 
     *   Etag:"a030f020ac7c01:1e9f" 
     *   Content-length:39725426 
     *   Content-range:bytes554554-40279979/40279980
    */
    char buf[32];
    // 先把"HTTP/1.0 200 OK\r\n"添加到buffer中
    snprintf(buf, sizeof(buf), "HTTP/1/1 %d ", statusCode_);    
    // muduo重载了append函数，我这里没重载如果出错请检查这里
    output->append(buf, sizeof(buf));
    output->append(statusMessage_.c_str(), statusMessage_.size());
    output->append("\r\n", sizeof("\r\n"));

    if (closeConnection_)
    {
        output->append("Connection: close\r\n", sizeof("Connection: close\r\n"));
    }
    else
    {
        snprintf(buf, sizeof(buf), "Content-Length: %zd\r\n", body_.size());
        output->append(buf, sizeof(buf));
        output->append("Connection: Keep-Alive\r\n", sizeof("Connection: Keep-Alive\r\n"));
    }

    for (const auto& header : headers_)
    {
        output->append(header.first.c_str(), header.first.size());
        output->append(": ", sizeof(": "));
        output->append(header.second.c_str(), header.second.size());
        output->append("\r\n", sizeof("\r\n"));
    }

    output->append("\r\n", sizeof("\r\n"));
    output->append(body_.c_str(), body_.size());
}