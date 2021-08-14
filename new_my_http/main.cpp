#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include"thread.h"
#include"epoll.h"
#include"request.h"
#include"r_and_w.h"
#include <memory>
#include<queue>
#include <unistd.h>
#include"mysql.h"

const int TIMER_TIME_OUT = 500;
extern pthread_mutex_t qlock;
extern priority_queue<mytimer*, deque<mytimer*>, timerCmp> myTimerQueue;
connection_pool * m_connPool;

void handle_expired_event()
{
    pthread_mutex_lock(&qlock);
    while (!myTimerQueue.empty())
    {
        mytimer *ptimer_now = myTimerQueue.top();
        if (ptimer_now->isDeleted())
        {
            myTimerQueue.pop();
            delete ptimer_now;
        }
        else if (ptimer_now->isvalid() == false)
        {
            myTimerQueue.pop();
            delete ptimer_now;
        }
        else
        {
            break;
        }
    }
    pthread_mutex_unlock(&qlock);
}

void myHandler(void *args)
{
	requestData *req_data = (requestData*)args;
	req_data->handleRequest();
}


void acceptConnection(int listen_fd, int epoll_fd,string path)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    socklen_t client_addr_len = 0;
    int accept_fd = 0;
    while((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0)
    {
        /*
        // TCP的保活机制默认是关闭的
        int optval = 0;
        socklen_t len_optval = 4;
        getsockopt(accept_fd, SOL_SOCKET,  SO_KEEPALIVE, &optval, &len_optval);
        cout << "optval ==" << optval << endl;
        */


        int ret = setSocketNonBlocking(accept_fd);
        if (ret < 0)
        {
            perror("Set non block failed!");
            return;
        }


		

		requestData *req_info = new requestData(epoll_fd, accept_fd, path);


        // 文件描述符可以读，边缘触发(Edge Triggered)模式，保证一个socket连接在任一时刻只被一个线程处理

        __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;

        epoll_add(epoll_fd, accept_fd, static_cast<void*>(req_info), _epo_event);



        // 新增时间信息


        mytimer *mtimer = new mytimer(req_info, TIMER_TIME_OUT);
        req_info->addTimer(mtimer);
		
        pthread_mutex_lock(&qlock);
        myTimerQueue.push(mtimer);
        pthread_mutex_unlock(&qlock);
		
		
	
    }
}



int init_socket(int port) {

	int listenfd;
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {    //创建套接字
			   //perror("wudu!");
		ERR_EXIT("socket");
	}


	//activate_nonblock(listenfd);

	int one = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
		close(listenfd);
		return -1;
	}


	struct sockaddr_in servaddr;          //本机地址
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);


	if (bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {  //绑定端口
		ERR_EXIT("bind");
	}
	if (listen(listenfd, SOMAXCONN) < 0) {     //监听
		ERR_EXIT("listen");
	}
	return listenfd;
}







int main(int argc, char *argv[])
{

	//handle_for_sigpipe();
	signal(SIGPIPE,SIG_IGN);

	//const string path = "/home/fujie/Desktop/dir";
	
	string PATH = "/";


	if (argc < 2)
	{
		printf("./server port path\n");
	}

	int port = atoi(argv[1]);


	int ret = chdir("/home/fujie/Desktop/dir");
	//改变当前进程工程目录
	if (ret != 0) {
		perror("chdir error");
		exit(1);
	}



	int listenfd = init_socket(port);
	if (listenfd < 0)
	{
		perror("socket bind failed");
		return 1;
	}

	if (setSocketNonBlocking(listenfd) < 0)
    {
        perror("set socket non block failed");
     
	    return 1;
	}



	int epollfd = epoll_init();
	if (epollfd < 0)
	{
		perror("epoll init failed");
		return 1;
	}


	m_connPool = connection_pool::GetInstance();
    m_connPool->init("192.168.1.135","root","","web", 3306, 10, 1);

  




	threadpool_t *tp = pthreadpool_create(4, 100, 65535);

	struct epoll_event* events;
	events = new epoll_event[MAXEVENTS];


    __uint32_t event = EPOLLIN | EPOLLET;

    requestData *req = new requestData();
    req->setFd(listenfd);
    epoll_add(epollfd, listenfd, static_cast<void*>(req), event);

	

	while (true)
	{
	//	printf("\n\n\n\nwait!!!!!!!!\n");

		int events_num = epoll_wait(epollfd, events, MAXEVENTS, -1);

		

		if (events_num < 0) {
			perror("epoll wait error");
			break;
			
		}
		if (events_num == 0)
            continue;
		

		for (int i = 0; i < events_num; i++)
		{
			// 获取有事件产生的描述符
			requestData* request = (requestData*)(events[i].data.ptr);

			int fd = request->getFd();

			// 有事件发生的描述符为监听描述符
			if (fd == listenfd)
			{
				//cout << "This is listen_fd" << endl;
				acceptConnection(listenfd, epollfd, PATH);
			}
			else
			{
				// 排除错误事件
				if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)
					|| (!(events[i].events & EPOLLIN)))
				{
					printf("error event\n");
					disconnect(fd,epollfd);
					continue;
				}

			 
				// 将请求任务加入到线程池中
				// 加入线程池之前将Timer和request分离

		    	request->seperateTimer();  //request , timer  分离

				int ret=threadpool_add(tp, myHandler, events[i].data.ptr);
			}
		}
		handle_expired_event();
	}

	return 0;
}