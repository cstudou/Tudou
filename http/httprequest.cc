#include "httprequest.h"
using namespace tudou;

HttpMessage::HttpMessage()
: _method(INVAILD)
, _version(UNKNOW)
, _body("")
{}

void HttpMessage::setVersion(Version ver) { _version = ver; }
HttpMessage::Version HttpMessage::getVersion() const { return _version; }

bool HttpMessage::setMethod(const char *x, size_t n)
{
    const string &method = string(x, n);
    if(method == "GET") { _method = GET; }
    else if(method == "POST") { _method = POST; }
    else if(method == "HEAD") { _method = HEAD; }
    else if(method == "PUT") { _method = PUT; }
    else if(method == "DELETE") { _method == DELETE; }
    else { _method = INVAILD; }
    return _method != INVAILD;
}
HttpMessage::Method HttpMessage::getMethod() const { return _method; }

void HttpMessage::setFilenameCgi(const pair<string, string> &p) { _filename_cgiargs = p; }
const pair<string, string> &HttpMessage::getFilenameCgi() const { return _filename_cgiargs; }

void HttpMessage::addHeader(const string &key, const string &value)
{
    _head[key] = value;
}
string HttpMessage::getHeader(const string &key) 
{
    if(_head.find(key) == _head.end())
    {
        return "";
    }
    return _head[key];
}

const map<string, string> & HttpMessage::getHeader() const { return _head; }
void HttpMessage::setBody(const string &body) { _body = body; }
string &HttpMessage::getBody() { return _body; }
void HttpMessage::reset() //重置
{
    _method = INVAILD, _version = UNKNOW;
    _body = "";
    _filename_cgiargs = make_pair("", "");
    _head.clear();
}
inline
const string & HttpMessage::getFilename() { return _filename_cgiargs.first; }
void HttpMessage::print()
{
    std::cout << "method = " << _method 
            << "version = " << _version 
            << "filename = " << _filename_cgiargs.first <<" "<<_filename_cgiargs.second
            << std::endl;
        for(auto it : _head)
        {
            std::cout << it.first <<": " << it.second << std::endl;
        }
        std::cout << _body << std::endl;
}




void HttpParse::print() { _message.print();}
HttpMessage &HttpParse::getHttpMessage() { return _message; }
HttpParse::HttpParse()
: _state(PARSE_REQUESTLINE)
{}

bool HttpParse::parseUrl(const string &path)
{
    if(path.find("cgi-bin") == string::npos)
    {
        pair<string, string> url("." + path, "");
        if(path[path.size() - 1] == '/')
        {
            url.first += "home.html";
        }
        _message.setFilenameCgi(url);
        return true;
    }
    return false; //不支持动态
}

bool HttpParse::parseRequestLine(int len, Buffer &buffer)
{
    
    string _message_line = buffer.read(len - buffer.getReadPos() + 2); //读出请求行
    //std::cout << "_message: " << _message_line << std::endl; 
    auto pos1 = _message_line.find(' ');
    _message.setMethod(&_message_line[0], pos1); //请求方法
    ++pos1; //路径
    auto pos2 = _message_line.find(' ', pos1); //找下一个空格
    parseUrl(string(&_message_line[pos1], pos2-pos1));	//路径

    //开始解析http版本
    if(!equal(&_message_line[pos2+1], &_message_line[pos2 + 7], "HTTP/1."))
    {
        Debug << "HTTP version parse error" << std::endl;
        return false;
    }
    if(_message_line[pos2 + 8] == '1')
    {
        _message.setVersion(HttpMessage::HTTP11);
    }
    else if(_message_line[pos2 + 8] == '0')
    {
        _message.setVersion(HttpMessage::HTTP10);
    }
    else
    {
        Debug << "HTTP version parse error" << std::endl;
        return false;
    }
    return true;
}

bool HttpParse::parseRequestHead(int len, Buffer &buffer)
{
    string _message_line = buffer.read(len - buffer.getReadPos() + 2); //读出请求头
    auto pos = _message_line.find(':'); //找到：
    
    if(pos == string::npos)
    {
        Debug << "beginning to parse body" << std::endl;
        return false;
    }
    _message.addHeader(string(&_message_line[0], pos), 
                    string(&_message_line[pos+2], _message_line.size()-pos-4));
    return true;
}
//解析头部
bool HttpParse::parse(Buffer &buffer)
{
    bool flag;
    int findPos = -1;
    stringstream ss;

    while(true)
    {
        if(_state == PARSE_REQUESTLINE) //从请求行开始解析
        {
            findPos = buffer.find(); //找到"\r\n"
    
            if(findPos == -1)
            {
                return false; //解析失败
            }
            
            flag = parseRequestLine(findPos, buffer);
            if(flag)
            {
                _state = PARSE_HEAD; //读请求头
            }
            else
            {
                return false;
            }

        }
        else if(_state == PARSE_HEAD) //请求头处理
        {
            findPos = buffer.find();
            if(findPos == -1)
            {
                return false; //解析失败
            }
            flag = parseRequestHead(findPos, buffer);
            if(!flag)
            {
                //遇到空行，下面是请求体
                _state = PARSE_BODY;
            }
        }
        else if(_state == PARSE_BODY)
        {
            /*
                暂时只支持get
            */
            break;	
        }
    }
    
    return true;
}



void HttpResponse::getFileType()
{
    if(_message._filename_cgiargs.first.find(".html") != string::npos)
    {
        _filetype = "text/html";
    }
    else if(_message._filename_cgiargs.first.find(".gif") != string::npos)
    {
        _filetype = "image/gif";
    }
    else if(_message._filename_cgiargs.first.find(".png") != string::npos)
    {
        _filetype = "image/png";
    }
    else if(_message._filename_cgiargs.first.find(".jpg") != string::npos)
    {
        _filetype = "image/jpeg";
    }
    else if(_message._filename_cgiargs.first.find(".avi") != string::npos)
    {
        _filetype = "vedio/x-msvideo";
    }
    else
    {
        _filetype = "text/plain";
    }
}
void HttpResponse::clientError(const string &cause, const string &errnum, const string &shortmsg, const string &longmsg)
{
    stringstream ss, aa;

    //build http response body
    ss << "<html><title>Tudou Error</title>";
    ss << "<body bgcolor=""ffffff"">\r\n";
    ss << errnum << ": " << shortmsg << "\r\n";
    ss << "<p>" << longmsg << ": " << cause << "\r\n";
    ss << "<hr><em>The Tuduo Web server</em>\r\n";
    //打印信息 
    if(_message._version == 0)
    {
        aa << "HTTP/1.0" << " " << errnum << " " << shortmsg << "\r\n";
    }
    else
    {
        aa << "HTTP/1.1" << " " << errnum << " " << shortmsg << "\r\n";
    }
    aa << "Content-type: text/html\r\n";
    
    aa << "Content-length: " << ss.str().size() << "\r\n\r\n";
    _buffer.append(aa.str());
    _buffer.append(ss.str());
}

void HttpResponse::append()
{
    char buf[1024] = {};
    int nread = 0;
    if(_message._method != (HttpMessage::POST || HttpMessage::GET))
    {
        clientError(_message.getFilename(), "501", "Not Implemented", "Tudou cannot implement this method");
        return ;
    }
    if(stat(_message.getFilename().c_str(), &sbuf) < 0)
    {
        clientError(_message.getFilename(), "404", "Not Found", "Tudou cannot find this file");
        return ;
    }
    if(!(S_ISREG(sbuf.st_mode)) ||!(sbuf.st_mode & S_IRUSR))
    {
        clientError(_message.getFilename(), "403", "Not Found", "Tiny couldn't read this file");  
        return ;    
    }
    getFileType();
    stringstream ss;
    //
    //	such as:
    //		HTTP/1.1 200 OK
    //
    ss << "HTTP/1." << _message._version << " " << _state << " OK " << "\r\nServer: " << _message_return << "\r\n";
    if(_message._head.find("Connection") != _message._head.end()
        && (_message._head["Connection"] == "Keep-Alive"
        || _message._head["Connection"] == "keep-alive"))
    {
        ss << "Connection: keep-alive" << "\r\n";
    }
    else
    {
        ss << "Connection: close" << "\r\n";
    }
    ss << "Content-length: " << sbuf.st_size << "\r\n";
    ss << "Content-type: " << _filetype << "\r\n\r\n";
    
    _buffer.append(ss.str());
    int srcfd = open(_message.getFilename().c_str(), O_RDONLY, 0777); 
    auto len = sbuf.st_size;
    while(len)
    {
        nread = ::read(srcfd, buf, 1024);
        _buffer.append(buf, nread);
        len -= nread;
    }
    close(srcfd);
}



HttpResponse::HttpResponse(HttpMessage &message, const string &str, Buffer &buffer)
: _message(message)
, _message_return(str)
, _buffer(buffer)
, _state(HTTP200)
{}

