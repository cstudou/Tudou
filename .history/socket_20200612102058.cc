#include "socket.h"
using namespace tudou;


template <typename Type>
static SocketTool::Type hton(Type v)//转网络序
{
    return typeHton(v);
}


template <typename Type>
static SocketTool::Type ntoh(Type v) //转字节序
{
    return typeNtoh(v);
}

static void SocketTool::inetPton(const char *s, struct sockaddr_in &serv)
{
    inet_pton(AF_INET, s, &serv.sin_addr.s_addr);
    //std::cout << ref << std::endl;
}
static string SocketTool::inetNtop(struct sockaddr_in &serv)
{
    char buf[1024]={};
    inet_ntop(AF_INET, &serv.sin_addr.s_addr,buf,static_cast<socklen_t>(sizeof(buf)));
    return buf;
}

//设置接受缓冲区大小
static int SocketTool::setRecvBuf(int sockfd, int size) 
{
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char *)&size, static_cast<socklen_t>(sizeof(size)));
    return ret;
}
//设置发送缓冲区大小
static int SocketTool::setSendBuf(int sockfd, int size) 
{
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char *)&size, static_cast<socklen_t>(sizeof(size)));
    return ret;
}

static bool SocketTool::checkIp(struct sockaddr_in &s)
{
    return s.sin_addr.s_addr != INADDR_NONE; //判断ip地址合法性
}

static int SocketTool::reuseAddr(int sockfd, bool flag)
{
    int opt = flag;
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, static_cast<socklen_t>(sizeof(opt)));
    return ret;
}

static int SocketTool::reusePort(int sockfd, bool flag)
{
    int opt = flag;
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (const char *)&opt, static_cast<socklen_t>(sizeof(opt)));
    return ret;
}
static int SocketTool::noDelay(int sockfd, bool flag) //禁用Nagle算法
{
    int opt = flag;
    int ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,(const char *)&opt,static_cast<socklen_t>(sizeof(opt)));
    return ret;
}

static int SocketTool::keepAlive(int sockFd, bool flag) 
{
    int opt = flag;
    int ret = setsockopt(sockFd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt,static_cast<socklen_t>(sizeof(opt)));
    return ret;	
}

static int SocketTool::setCloseOnExec(int sockfd)
{
        // close-on-exec
    auto flag = ::fcntl(sockfd, F_GETFD, 0);
    flag |= FD_CLOEXEC;
    return ::fcntl(sockfd, F_SETFD, flag);


}
static int SocketTool::setNonBlock(int sockfd, bool flag)
{
    int old_opt = ::fcntl(sockfd, F_GETFL, 0);
    if(flag)
    {
        old_opt |= O_NONBLOCK;
        return ::fcntl(sockfd, F_SETFL, old_opt);
    }
    else
    {
        old_opt &= ~O_NONBLOCK;
        return ::fcntl(sockfd, F_SETFL, old_opt);
    }
}

static int SocketTool::setCloseWait(int sockFd, int second) 
{
    struct linger sockLinger;
    //在调用closesocket()时还有数据未发送完，允许等待
    // 若m_sLinger.l_onoff=0;则调用closesocket()后强制关闭
    sockLinger.l_onoff = (second > 0);
    sockLinger.l_linger = second; //设置等待时间为x秒
    int ret = setsockopt(sockFd, SOL_SOCKET, SO_LINGER, (char*) &sockLinger, sizeof(linger));
    return ret;
}
static int SocketTool::socketTcp()
{
    int ret = socket(AF_INET, SOCK_STREAM, 0); 
    return ret;
}

static pair<Error, int> 
SocketTool::connectTcp(const char *ip, uint16_t port, bool isasync = false) //客户端使用
{
    struct sockaddr_in cli;
    Error _error(Error::OK, "connect ok");
    memset(&cli, 0, sizeof(cli));
    cli.sin_family = AF_INET;
    cli.sin_port = hton(port);
    inetPton(ip, cli); //存入服务器ip
    
    int fd = socketTcp();
    if(fd < 0)
    {
        _error.setType(Error::SOCKET_ERROR);
        return make_pair(_error, -1);
    }
    reuseAddr(fd, true);
    reusePort(fd, true);
    
    setNonBlock(fd, isasync); //设置是否非阻塞链接
    
    setCloseOnExec(fd);
    noDelay(fd, true);
    int opt = ::connect(fd, (struct sockaddr*)&cli, sizeof(cli));
    if(opt > 0)
    {
        return make_pair(_error, fd);
    }
    else if(isasync && errno == EINPROGRESS) //非阻塞链接
    {
        return make_pair(_error, fd);
    }
    _error.setType(Error::CONNECT_ERROR);
    return make_pair(_error, -1);
}


static pair<Error, int> 
SocketTool::listenTcp(const char *ip, uint16_t port)
{
    auto fd = socketTcp();
    if(fd < 0)
    {
        std::cout << "socket err" << std::endl;
        return make_pair(Error(Error::FAILED, "socket get error"), -1);
    }
    
    reuseAddr(fd, true);
    reusePort(fd, true);
    setNonBlock(fd, true);
    //std::cout << std::endl<<strerror(errno) << "0---2---1--" << "+++++"<<std::endl;
    setCloseOnExec(fd);
    setCloseWait(fd, 1); 
    auto flag = bindTcp(fd, ip, port);
    if(flag < 0)
    {
        //std::cout << strerror(errno) << std::endl;
        std::cout << "bind err" << std::endl;
        return make_pair(Error(Error::FAILED, "bind error"), -1);
    }
    flag = ::listen(fd, 20);
    //std::cout << strerror(errno) << std::endl;;
    if(flag)
    {
        std::cout << "listen err" << std::endl;
        return make_pair(Error(Error::FAILED, "bind error"), -1);
    }
    else
    {
        return make_pair(Error(Error::OK, "listen ok"), fd);
    }

}	
static int SocketTool::bindTcp(int &sockfd, const char *ip, uint16_t port)
{
    //std::cout << sockfd << std::endl;
    struct sockaddr_in serv;
    memset(&serv, 0 , sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = hton(port);
    serv.sin_addr.s_addr = INADDR_ANY;
    inetPton(ip, serv);
    
    int ret = ::bind(sockfd, (struct sockaddr*)&serv, sizeof(serv));
    return ret;
}

static int SocketTool::acceptTcp(const char *ip, uint16_t port)
{
    auto ret = listenTcp(ip, port);
    if(ret.second < 0)
    {
        return -1;
    }
    struct sockaddr_in serv;
    memset(&serv, 0 , sizeof(serv));
    socklen_t len = sizeof(serv);
    return ::connect(ret.second, (struct sockaddr*)&serv, len);
}
static pair<Error, int>
SocketTool::sendPart(int sockfd, const char *msg, size_t len)
{
    int ret = 0, part = 0;
    Error _error(Error::OK, "send ok");
    while(1)
    {
        ret = ::write(sockfd, msg + part, len - part);
        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            else if(errno == EPIPE)
            { 
                _error.setErrno(errno);
                return make_pair(_error, -2);
            }
            else if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                _error.setErrno(errno);
                return make_pair(_error, part);  //缓冲区满
            }
            else
            {		
                return make_pair(_error, part);
            }
            
        }
        else if(ret == 0)
        {
            _error.setErrno(errno);
            return make_pair(_error, -1);
        }
        else
        {
            part += ret;
            if(part == len) {return make_pair(_error, part);} 

        }
        
    }
}


static pair<Error, int>
SocketTool::sendPart(int sockfd, Buffer &_buffer)
{
    //尽可能全部发送
    return sendPart(sockfd, _buffer.begin(), _buffer.size());
    
}

static pair<Error, int>
SocketTool::readPart(int sockfd, Buffer &_buffer)
{
    
    int ret = 0;
    int part = 0;
    char _temp_buffer[65536];
    Error _error(Error::OK, "read Error::OK");
    
    while(1)
    {
        ret = ::read(sockfd, _temp_buffer, sizeof(_temp_buffer));
            
        if(ret <= 0)
        {
            if(errno == EAGAIN) 
            {
                return make_pair(_error, part);
            }
            else if(errno == EINTR)
            {
                continue;
            }
            else
            {
                _error.setType(Error::CONNECT_ERROR);
                if(ret == 0) return make_pair(_error, -1);
                if(ret == -1) return make_pair(_error, -2);
                
            }
        }
        else
        {
            _buffer.append(_temp_buffer, ret); //存到缓冲区
            part += ret;
            if(ret < 65536) return make_pair(_error, part);
        }
    }
        
}






SocketAddress::SocketAddress(const std::string& ip, uint16_t port) 
: _ip(ip)
, _port(port) 
{}

SocketAddress::SocketAddress() 
: _ip()
, _port(0) 
{}
void SocketAddress::set(const std::string& ip, uint64_t port)
{
    _ip = ip;
    _port = port;
}

