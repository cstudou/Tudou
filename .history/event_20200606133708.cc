#include "event.h"
#include "channel.h"
#include "pipe.h"
#include "util.h"
#include <algorithm>

//#include "channel.h"
using namespace tudou;
//unique_ptr<EventLoop> EventLoop::_pInstance = nullptr;
EventLoop::EventLoop(int taskSize)
: _pipe()
, _loop_run(false)
, _is_exit(false)
, _task_list(taskSize)
, _thread_id(this_thread::get_id()) //主线程
{
	_epoll = make_shared<Epoll>();
	shared_ptr<Channel> ch(new Channel(this, _pipe.getReadFd())); //挂载到epoll树
	ch->enableRead(true);
	ch->setReadCb(
		[](){}
	);
}

	void EventLoop::pipeEvent()
	{
		std::cout << "pipr" << std::endl;
		char buf[1024] = {};
		
		while(true)
		{
			int ret = _pipe.read(buf, sizeof(buf));
			if(ret < 0 && (errno == EAGAIN || errno == EINTR))
			{
				continue;
			}
			else
			{
				break;
			}
		}
		list<DefaultFunction> temp;
		temp.swap(_task_list);

		for_each(begin(temp), end(temp), [&](const DefaultFunction &fun){ fun(); });
		
	}
	
	void EventLoop::shutdown()
	{
		_is_exit = true;
		if(_loop_thread)
		{
			_loop_thread->join();
			delete _loop_thread;
			_loop_thread = nullptr;
		}
	}
	int EventLoop::debu()
	{
		std::cout << "this is debug_ "<< _epoll->size() <<  std::endl;
	}
	void EventLoop::update(Channel* ch) { _epoll->updateChannel(ch); }
	void EventLoop::addChannel(Channel* ch) { _epoll->addChannel(ch); }
	void EventLoop::modChannel(Channel* ch) { _epoll->updateChannel(ch); }
	void EventLoop::delChannel(Channel* ch) { _epoll->removeChannel(ch); }

	void EventLoop::loop() 
	{
		if(!_looprun_mutex.try_lock())
		{
			return ;
		}
		struct epoll_event _epoll_array[DefaultArraySize];
		// 一直阻塞，有异步任务来的时候会解开阻塞执行异步任务
		while(1)
		{
		
			int _active_pos = epoll_wait(_epoll->_epoll_root, _epoll_array, DefaultArraySize, -1);
			
			
			for(int i=0; i<_active_pos; ++i)
			{
				Channel* ch = (Channel *)_epoll_array[i].data.ptr;
				int event = ch->getEvent();
				int fd = ch->getFd();
				 std::cout << ch->getEvent() << fd << " "  << << _pipe.getReadFd() << std::endl;
				if(fd == _pipe.getReadFd())
				{
					std::cout << "madas" << std::endl;
					pipeEvent();
					continue;
				}
				if(ch)
				{
					if(_epoll->_active_channel.find(ch) == _epoll->_active_channel.end())
					{
						std::cout << "???" << std::endl;
						continue; //remove了
					}
				
					if(ch->getEvent() & (EPOLLIN | EPOLLPRI))
					{
						//std::cout << "htp" << std::endl;
						ch->runRead();
						
					}
					if(ch->getEvent() & EPOLLOUT)
					{
						//std::cout << "ht" << std::endl;
						ch->runWrite();
					}
					if(ch->getEvent() & EPOLLERR)
					{
						std::cout << "ht12p" << std::endl;
						ch->runError();
					}
				}
			}
		}
		_looprun_mutex.unlock();
	}