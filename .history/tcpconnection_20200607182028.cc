#include "tcpconnection.h"

using namespace tudou;



#ifndef __TUDOU_TCPCONNECTION_H__
#define __TUDOU_TCPCONNECTION_H__

#include "event.h"
#include "error.h"
#include "util.h"
#include "socket.h"
#include "buffer.h"
#include "threadpool.h"
#include "eventlooppool.h"
#include <string.h>
#include "timer.h"
#include <iostream>
#include <memory>
#include <set>
#include <poll.h>
#include <functional>
using std::set;
using std::cout;
using std::endl;
using std::weak_ptr;
using std::shared_ptr;

namespace tudou
{

#define _MAX_READ_SIZE_ 1024
#define _CLOSE_TIME_ 8000


	TcpConnection::TcpConnection(EventLoop::Ptr loop, int fd)
	: _loop(loop)
	, _sockfd(fd)
	, _timeout(10)
	, _input()
	, _output()
	{
		//_read_cb = _write_cb = _error_cb = _close_cb = nullptr;
		_channel = make_shared<Channel>(_loop.get(), _sockfd); //初始化一个channel
	}
	
	
	//connect封装
	Ptr TcpConnection::connection(EventLoop::Ptr loop, const string &host, uint16_t port, int timeout = 10, const char *localIp = "0.0.0.0")
	{
		Ptr _conn(new TcpConnection(loop, 0));
		_address.set(host, port);
		_timeout = timeout;
		auto nowTime = getMsec(); //当前时间
		auto ret = SocketTool::connectTcp(host.c_str(), port); //socket - bind - connect
		switch(ret.first.getStatus())
		{
		case Error::OK:
			{
				_state = HANDSHAKING;
				set(_loop, ret.second);
				break;
			}
		default:
			break;
		}
		
		if(ret.second < 0)
		{
			return nullptr;
		}
		
		handleTimeout(shared_from_this(), timeout);
		return _conn; //返回一个Tcpconnection
	}

	//设置回调函数
	void TcpConnection::setReadCb(const MessageCallback &cb)
	{
		_read_cb = cb;
	}
	void TcpConnection::setWriteCb(const MessageCallback &cb)
	{
		_write_cb = cb;
	}
	void TcpConnection::setCloseCb(const CloseCallback &cb)
	{
		_close_cb = cb;
	}
	void TcpConnection::setErrorCb(const ErrorCallback &cb)
	{
		_error_cb = cb;
	}
	
	//发送消息，首先把消息存到缓冲区
	inline
	int TcpConnection::send(const string &msg) { send(msg.c_str(), msg.size()); }
	
	/*
		1. 缓冲区没有数据尝试直接::write
		2. 若有数据就append到缓冲区
		3. 没全部发完把剩下的append到缓冲区
	*/
	int TcpConnection::send(const char *msg, size_t len)
	{
		if(_channel)
		{
			if(!_output.empty()) //输出缓冲区有数据还没有发送
			{
				_output.append(msg, len);
				return 0;
			}
			//尝试直接发送
			pair<Error, int>
			ret = SocketTool::sendPart(_channel->getFd(), msg, len);
			auto &_err = ret.first;
			if(ret.first.getStatus() != Error::OK)
			{
				return -1; //发送不成功
			}
			if(ret.second == len)
			{
				return len; //全部发送成功
			}
			_output.append(msg+ret.second, len-ret.second); //剩下的加入缓冲区
			return 1;
		}
		return -1;
	}

	EventLoop::Ptr TcpConnection::getEventLoop() { return _loop; }
	Channel::Ptr TcpConnection::getChannel() { return _channel; }
	TcpState TcpConnection::getState() { return _state; }
		
	Buffer* TcpConnection::getInput() { return &_input; }
	Buffer* TcpConnection::getOutput() { return &_output; }
	SocketAddress TcpConnection::getAddress() { return _address; }
	
	void TcpConnection::setState(TcpState s) { _state = s; }
	
	//主动断开链接
	void TcpConnection::shutdown()
	{
		//缓冲区还有数据没有发完
		if(!_output.empty())
		{
			//把缓冲区所有信息发出去
			handleWrite(shared_from_this());
		}
		handleClose(shared_from_this());
		
	}

	//处理读事件,如果使用lt模式，有数据就会一直触发
	void TcpConnection::handleRead(const Ptr &ptr)
	{	
		if(ptr == nullptr) 
		{
			return ;
		}
		if(_state == TcpConnection::HANDSHAKING && !handleHandshake(ptr)) //判断是否链接
		{
			return ;
		}
		if(_state == CONNECTED && _channel->getFd() > 0)
		{
			auto ret = SocketTool::readPart(_channel->getFd(), _input);	
			
			if(ret.second == -1) //断开连接
			{
				handleClose(ptr);
				return ;
			}
			if(ret.second == 0) //读完
			{
				//更新timer
				_timer.lock()->reset();
				if(_read_cb && ret.second)
				{	
					_read_cb(ptr);
				}
				return ;
			}
		}
		
		//std::cout << "zuile" << std::endl;
	}
	void TcpConnection::handleWrite(const Ptr &ptr)
	{
		if(ptr == nullptr)
		{
			return ;
		}
		if(ptr->getState() == TcpConnection::HANDSHAKING)
		{
			handleHandshake(ptr);
		}
	
		if(_state == CONNECTED)
		{
			//没有数据可以发送
			//std::cout << "this is " <<std::endl;
			if(_output.empty() && _write_cb)
			{
				_write_cb(ptr);
				return ;
			}
			auto ret = SocketTool::sendPart(_channel->getFd(), _output);
			if(ret.first.getStatus() != Error::OK) //发送失败
			{
				handleClose(shared_from_this());
				return ;
			}
			if(_write_cb)
			{
				_write_cb(ptr);
			}

			
		}
		
	
	}
	void TcpConnection::handleError(const Ptr &ptr)
	{
		Error _error;
		_channel->enableRead(false);
		_channel->enableWrite(false);
		_state = FAILED;
		if(_error_cb)
		{
			_error_cb(ptr, _error);
		}
	}
	int TcpConnection::handleHandshake(const Ptr &ptr) //握手,connect函数在第二次握手返回,缓冲区是可写的
	{ 										//判断是否链接成功，成功之后触发pollout
		struct pollfd pf;
		memset(&pf, 0, sizeof(pf));
		pf.fd = _channel->getFd();
		pf.events = POLLOUT | POLLERR;
		int ret = poll(&pf, 1, 0);
		if(ret == 1)
		{
			if(pf.events & POLLOUT)
			{
				//监听到读事件连接成功
				_state = CONNECTED;
				return 0;
			}
		}
		
		return -1;
	}
	//处理关闭,被动断开链接
	void TcpConnection::handleClose(const Ptr &ptr)
	{

		_state = CLOSED;  //设置状态为close
		_channel->enableWrite(false);
		_channel->enableRead(false);
		//把channel从eventloop中删除
		_loop->delChannel(_channel.get());
		if(_close_cb)
		{
			_close_cb(ptr);
		}
	}
	
	//超时还没连上就断开
	void TcpConnection::handleTimeout(const Ptr &ptr, int timeout)
	{
		if(timeout > 0)
		{
			auto conn = shared_from_this();
			auto _time = make_shared<Timer>(timeout, [conn]()
								{
									if(conn->getState() == HANDSHAKING)
									{
										conn->setState(TcpConnection::CLOSED);
										auto ch = conn->getChannel();
										ch->close(); //关闭channel
										ch = nullptr; 
									}
									return false;
								}
						, ptr);	
			TimerManager::getInstance().addTimer(_time);
		}
	}

	void TcpConnection::set(EventLoop::Ptr loop, int _fd)
	{
		
		Ptr _point = shared_from_this(); //获取一个指针,设置channel相关回调函数
		_point->getChannel()->setReadCb( [=](){ _point->handleRead(_point); } );
		_point->getChannel()->setWriteCb( [=](){ _point->handleWrite(_point); } );
		//////////////////////
		_point->getChannel()->setErrorCb( [=](){ _point->handleError(_point); } );
		_point->getChannel()->setCloseCb( [=](){ _point->handleClose(_point); } );
	}
	weak_ptr<Timer>& TcpConnection::getTimer() { return _timer; }
	void TcpConnection::setTimer(Timer::Ptr &timer)
	{
		_timer = _timer; //每个connection都有一个定时器
	}

