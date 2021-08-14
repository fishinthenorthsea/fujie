#pragma once
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
#include<string>
#include<iostream>
#include<vector>
#include<algorithm>
#include<string>
using namespace std;

ssize_t readn(int fd, void *buff, size_t n);
ssize_t readn(int fd, std::string &inBuffer);
ssize_t writen(int fd, void *buff, size_t n);
ssize_t writen(int fd, std::string &sbuff);
void handle_for_sigpipe();
int setSocketNonBlocking(int fd);

void do_accept(int listenfd, int epollfd);


int init_epoll(int listenfd);
int init_socket(int port);
void activate_nonblock(int fd);
void send_error(int cfd, int status, char *title, char *text);
void disconnect(int cfd, int epfd);

