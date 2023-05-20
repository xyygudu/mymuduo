#pragma once

#include <map>
#include <string>

class Buffer;

class HttpResponse
{

public:
    enum HttpStatusCode                 // 响应状态码
    {
        kUnknown,
        k200Ok = 200,
        k301MovedPermanently = 301,
        k400BadRequest = 400,
        k404NotFound = 404,
    };

    explicit HttpResponse(bool close)
        : statusCode_(kUnknown)
        , closeConnection_(close)
    {
    }

    void setStatusCode(HttpStatusCode code) { statusCode_ = code; }

    void setStatusMessage(const std::string& message) { statusMessage_ = message; }

    void setCloseConnection(bool on) { closeConnection_ = on; }

    bool closeConnection() const { return closeConnection_; }

    void setContentType(const std::string& contentType) { addHeader("Content-Type", contentType); }

    void addHeader(const std::string& key, const std::string& value) { headers_[key] = value; }

    void setBody(const std::string& body) { body_ = body; }

    void appendToBuffer(Buffer* output) const;
    
private:
    std::map<std::string, std::string> headers_;
    HttpStatusCode statusCode_;         // http返回客户端的状态码
    std::string statusMessage_;
    bool closeConnection_;
    std::string body_;                  // 响应体
};

