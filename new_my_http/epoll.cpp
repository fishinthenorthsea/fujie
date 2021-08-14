#include"epoll.h"


struct epoll_event* events;


int epoll_init()
{
    int epoll_fd = epoll_create(LISTENQ + 1);
    if(epoll_fd == -1)
        return -1;
    //events = (struct epoll_event*)malloc(sizeof(struct epoll_event) * MAXEVENTS);
    events = new epoll_event[MAXEVENTS];
    return epoll_fd;
}



int epoll_add(int epoll_fd, int fd, void *request, __uint32_t events)
{
    struct epoll_event event;
    event.data.ptr = request;
    event.events = events;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        perror("epoll_add error");
        return -1;
    }
    return 0;
}

