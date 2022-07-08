#pragma once


#include <map>
#include <assert.h>
#include <stdio.h>

#include "copyable.h"
#include "Timestamp.h"

using namespace std;

// 用于记录请求报文中的信息，比如请求方法、路径、协议, 请求头
class HttpRequest : public copyable
{   
public:
    enum Method
    {
        kInvalid, kGet, kPost, kHead, KPut, kDelete
    };

    enum Version
    {
        kUnknown, kHttp10, kHttp11
    };

    HttpRequest() : method_(kInvalid), version_(kUnknown) {}

    void setVersion(Version v) { version_ = v; }
    Version getVersion() const { return version_; }

    // 设置method_并检查method是否有效
    bool setMethod(const char *start, const char * end)
    {
        assert(method_ == kInvalid);
        string m(start, end);
        if (m == "GET")
        {
            method_ = kGet;
        }
        else if (m == "POST")
        {
            method_ = kPost;
        }
        else if (m == "HEAD")
        {
            method_ = kHead;
        }
        else if (m == "PUT")
        {
            method_ = KPut;
        }
        else if (m == "DELETE")
        {
            method_ = kDelete;
        }
        else
        {
            method_ = kInvalid;
        }
        return method_ != kInvalid;
    }

    Method method() const { return method_; }

    const char *methodString() const
    {
        const char *result = "UNKNOWN";
        switch(method_)
        {
            case kGet:
                result = "GET"; break;
            case kPost:
                result = "POST"; break;
            case kHead:
                result = "HEAD"; break;
            case KPut:
                result = "PUT"; break;
            case kDelete: result = "DELETE"; break;
            default: break;
        }
        return result;
    }

    void setPath(const char *start, const char *end)
    {
        path_.assign(start, end);
    }

    const string & path() const { return path_; }

    void setQuery(const char* start, const char* end)
    {
        query_.assign(start, end);
    }

    const string& query() const { return query_; }

    void setReceiveTime(Timestamp t) { receiveTime_ = t; }

    Timestamp receiveTime() const { return receiveTime_; }

    // 将头部键值对添加到map中
    void addHeader(const char *start, const char *colon, const char *end)  // colon冒号
    {
        string field(start, colon);
        ++colon;
        while (colon < end && isspace(*colon)) { ++colon; }
        string value(colon, end);
        // 如果value的最后一个字符是空格，则去掉空格
        while (!value.empty() && isspace(value[value.size() - 1])) 
        {
            value.resize(value.size() - 1);
        }
        headers_[field] = value;
    }

    string getHeader(const string& field) const
    {
        string result;
        std::map<string, string>::const_iterator it = headers_.find(field);
        if (it != headers_.end())
        {
        result = it->second;
        }
        return result;
    }

    const std::map<string, string>& headers() const { return headers_; }

    void swap(HttpRequest& that)
    {
        std::swap(method_, that.method_);
        std::swap(version_, that.version_);
        path_.swap(that.path_);
        query_.swap(that.query_);
        receiveTime_.swap(that.receiveTime_);
        headers_.swap(that.headers_);
    }


private:
    Method method_;             // request method
    Version version_;           // http verion
    string path_;
    string query_;
    Timestamp receiveTime_;
    map<string, string> headers_;
};


