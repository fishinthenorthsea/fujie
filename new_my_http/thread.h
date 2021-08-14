#ifndef _THREAD_H_
#define _THREAD_H_





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
#include<ctype.h>
#include<vector>
#include<algorithm>
#include<pthread.h>
const int THREADPOOL_INVALID = -1;
const int THREADPOOL_LOCK_FAILURE = -2;
const int THREADPOOL_QUEUE_FULL = -3;
const int THREADPOOL_SHUTDOWN = -4;
const int THREADPOOL_THREAD_FAILURE = -5;
const int THREADPOOL_GRACEFUL = 1;


typedef struct {
    void (*function)(void *);
    void *arg;
} threadpool_task_t;



#define MIN_WAIT_TASK_NUM 10       /*当任务数超过了它，就该添加新线程了*/
#define DEFAULT_THREAD_NUM 10      /*每次创建或销毁的线程个数*/



struct threadpool_t
{
	pthread_mutex_t lock;            //锁住本结构体
	pthread_mutex_t thread_counter;  //记录忙状态线程个数的锁

	//条件变量   类似于信号
	pthread_cond_t queue_not_full;    //当任务队列满时，添加任务的线程阻塞，等待此条件变量

	pthread_cond_t queue_not_empty;   //任务队列不为空时，通知等待任务的线程


	pthread_t *threads;               //存在线程池中每个线程的id

	pthread_t adjust_tid;             //管理者线程tid

	threadpool_task_t *queue;    //任务队列首地址


	int min_thr_num;                   //线程池最小线程数
	int max_thr_num;                   //线程池最大线程数

	int live_thr_num;                  //当前存活线程个数



	int busy_thr_num;                  //忙状态线程个数
	int wait_exit_thr_num;             //要销毁的线程个数



	int queue_head;                  //task_queue队头下标
	int queue_tail;                    //task_queue队尾下标


	int queue_size;                   //task_queue实际任务数
	int queue_max_size;               //task_queue可容纳任务数上限  

	int shutdown;                        //标志位
};



//创造线程池
threadpool_t * pthreadpool_create(int min_thr_num, int  max_thr_num, int queue_max_size);  


int threadpool_add(threadpool_t *pool, void (*function)(void *), void *arg);




#endif
