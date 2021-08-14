#ifndef _EPOLL_SERVER_H
#define _EPOLL_SERVER_H


#include<sys/types.h>
#include<sys/socket.h>
#include<unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include<signal.h>
#include <sys/wait.h>
#include<poll.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<stdlib.h>
#include<stdio.h>
#include<errno.h>
#include<strings.h>
#include<string.h>
#include<dirent.h>
#include <ctype.h>

const int MAXEVENTS = 5000;
const int LISTENQ = 1024;





#define ERR_EXIT(m) \
     do \
	 {\
		perror(m);\
		exit(EXIT_FAILURE);\
	 }while(0)


int epoll_init();
int epoll_add(int epoll_fd, int fd, void *request, __uint32_t events);




#endif
