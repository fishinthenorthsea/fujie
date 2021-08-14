#include"thread.h"

int is_thread_alive(pthread_t tid)
{
	int kill_rc = pthread_kill(tid, 0);     //发送0号信号，测试是否存活
	if (kill_rc == ESRCH)  //线程不存在
	{
		return false;
	}
	return true;
}



//每个线程需要干的任务
static void *threadpool_thread(void *threadpool) {
	threadpool_t * pool = (threadpool_t *)threadpool;
	threadpool_task_t task;

	while (1) {
		//刚创建的线程，需要等待任务队列有任务，否则阻塞在等待队列里有任务后再唤醒

		
		pthread_mutex_lock(&(pool->lock));   //上锁

		//pool->count == 0意味着没有任务，那么就阻塞在wait中

		//printf("即将进入等待\n");
		while ((pool->queue_size == 0) && (!pool->shutdown))
		{
			//printf("等待\n");
			pthread_cond_wait(&(pool->queue_not_empty), &(pool->lock));
			//清除
			if (pool->wait_exit_thr_num > 0) {
				pool->wait_exit_thr_num--;

				//如果活着的线程数 大于最小值  ---结束该线程
				//就是扩容过后，多的线程空闲出来了
				if (pool->live_thr_num > pool->min_thr_num) {
					pool->live_thr_num--;


					pthread_mutex_unlock(&(pool->lock));
					pthread_exit(NULL);
				}

			}
		}
		//printf("任务来临\n");
		
		if (pool->shutdown) {
			pthread_mutex_unlock(&(pool->lock));
			printf("thread 0x%x is exiting \n", (unsigned int)pthread_self());
			pthread_exit(NULL); //线程自己结束自己
		}


		//任务取出

		task.function = pool->queue[pool->queue_head].function;
		task.arg = pool->queue[pool->queue_head].arg;

		//任务取出后销毁
		pool->queue_head = (pool->queue_head + 1) % pool->queue_max_size;
		pool->queue_size--;

		//通知可以添加新的任务

		pthread_cond_broadcast(&(pool->queue_not_full));


		//结束解锁
		pthread_mutex_unlock(&(pool->lock));


		/*执行任务*/

		pthread_mutex_lock(&(pool->thread_counter));
		pool->busy_thr_num++;
		pthread_mutex_unlock(&(pool->thread_counter));

		(*(task.function))(task.arg);

		

		pthread_mutex_lock(&(pool->thread_counter));
		pool->busy_thr_num--;
		pthread_mutex_unlock(&(pool->thread_counter));
	}

	pthread_exit(NULL);
}

//管理者线程的释放
int threadpool_free(threadpool_t *pool)
{
	if (pool == NULL)
	{
		return -1;
	}
	if (pool->queue)
		free(pool->queue);


	if (pool->threads)
	{
		free(pool->threads);

		pthread_mutex_lock(&(pool->lock));
		pthread_mutex_destroy(&(pool->lock));


		pthread_mutex_lock(&(pool->thread_counter));
		pthread_mutex_destroy(&(pool->thread_counter));


		pthread_cond_destroy(&(pool->queue_not_full));
		pthread_cond_destroy(&(pool->queue_not_empty));


	}
	free(pool);
	pool = NULL;
	return 0;
}

//销毁线程
int threadpool_destroy(threadpool_t *pool)
{
	printf("Thread pool destroy !\n");
	int i, err = 0;

	if (pool == NULL)
	{
		return -1;
	}
	pool->shutdown = true;

	//销毁管理线程
	pthread_join(pool->adjust_tid, NULL);
	//通知所有空闲线程
	for (i = 0; i < pool->live_thr_num; i++) {
		pthread_cond_broadcast(&(pool->queue_not_empty));
	}

	for (i = 0; i < pool->live_thr_num; i++) {
		pthread_join(pool->threads[i], NULL);
	}

	threadpool_free(pool);

	return 0;
}





void *adjust_thread(void *threadpool) {
	int i;
	threadpool_t * pool = (threadpool_t *)threadpool;

	while (!pool->shutdown) {
		sleep(5);

		pthread_mutex_lock(&(pool->lock));

		int queue_size = pool->queue_size; //关注存活 任务数
		int live_tre_num = pool->live_thr_num; // 关注存活 线程数

		pthread_mutex_unlock(&(pool->lock));

		pthread_mutex_lock(&(pool->thread_counter));

		int busy_thr_num = pool->busy_thr_num;

		pthread_mutex_unlock(&(pool->thread_counter));


		printf("忙进程-----%d  存活-----%d\n",busy_thr_num,pool->live_thr_num);
	    //如果任务数大于最小线程池个数，且存活的线程小于最大线程个数
		printf("%d %d %d %d\n",queue_size,MIN_WAIT_TASK_NUM,pool->live_thr_num ,pool->max_thr_num);
		if (queue_size >= MIN_WAIT_TASK_NUM && pool->live_thr_num <= pool->max_thr_num)
		{
		//	printf("admin add-----------\n");
			pthread_mutex_lock(&(pool->lock));
			int add = 0;

			/*一次增加 DEFAULT_THREAD_NUM 个线程*/
			for (i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_NUM
				&& pool->live_thr_num < pool->max_thr_num; i++)
			{
				if (pool->threads[i] == 0 || !is_thread_alive(pool->threads[i]))
				{
					pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool);
					add++;
					pool->live_thr_num++;
				//	printf("new thread -----------------------\n");
				}
			}
			pthread_mutex_unlock(&(pool->lock));
			continue;
		}

		/*销毁多余的线程 忙线程x2 都小于 存活线程，并且存活的大于最小线程数*/
		if ((busy_thr_num * 2) < pool->live_thr_num  &&  pool->live_thr_num > pool->min_thr_num)
		{
			// printf("admin busy --%d--%d----\n", busy_thr_num, live_thr_num);
			/*一次销毁DEFAULT_THREAD_NUM个线程*/
			pthread_mutex_lock(&(pool->lock));
			pool->wait_exit_thr_num = DEFAULT_THREAD_NUM;
			pthread_mutex_unlock(&(pool->lock));

			for (i = 0; i < DEFAULT_THREAD_NUM&&pool->live_thr_num>3; i++)
			{
				//通知正在处于空闲的线程，自杀
				pthread_cond_signal(&(pool->queue_not_empty));
				pool->live_thr_num--;
			//	printf("admin cler --\n");
			}
		}
	}
}



int threadpool_add(threadpool_t *pool, void (*function)(void *), void *arg)
{
	
	int err = 0;
	if (pool == NULL || function == NULL)
	{
		return -1;
	}

	if (pthread_mutex_lock(&(pool->lock)) != 0)
	{
		return -2;
	}
	do
	{
		while (pool->queue_size == pool->queue_max_size && (!pool->shutdown)) {
			pthread_cond_wait(&(pool->queue_not_full), &(pool->lock));
		}
	

 		/* Are we full ? */
        if(pool->queue_max_size== pool->queue_size) {
            err = THREADPOOL_QUEUE_FULL;
            break;
        }
		

		/* Are we shutting down ? */
		if (pool->shutdown) {
			err = -1;
			break;
		}

		//清空工作线程的回调函数的参数arg
		if (pool->queue[pool->queue_tail].arg != NULL)
		{
		//	free(pool->queue[pool->queue_tail].arg);
			pool->queue[pool->queue_tail].arg = NULL;
		}


		/* Add task to queue
		加一个任务----
		首先肯定是把队列的尾部打上这个事件的序号arg和事件function
		然后把尾巴向后移
		然后任务数++
		*/

		pool->queue[pool->queue_tail].function = function;
		
		pool->queue[pool->queue_tail].arg = arg;

		pool->queue_tail = (pool->queue_tail + 1) % pool->queue_max_size;

		
		pool->queue_size++;
		// pthread_cond_broadcast 
		
		if (pthread_cond_signal(&(pool->queue_not_empty)) != 0) {
			err = -2;
			break;
		}
	} while (false);


	if (pthread_mutex_unlock(&pool->lock) != 0) {
		err = -2;
	}
	return err;	
}




//创建线程池
threadpool_t * pthreadpool_create(int min_thr_num, int  max_thr_num, int queue_max_size) {

	threadpool_t * pool = NULL;
	int i;
	do
	{
		/*
		if (min_thr_num <= 0 || min_thr_num > MAX_THREADS || queue_max_size <= 0 || queue_max_size > MAX_QUEUE) {
			return NULL;
		}
		*/
		if ((pool = (threadpool_t *)malloc(sizeof(threadpool_t))) == NULL)
		{
			break;
		}

		/* Initialize */
		pool->min_thr_num = min_thr_num;
		pool->max_thr_num = max_thr_num;

		pool->busy_thr_num = 0;
		pool->live_thr_num = min_thr_num;
		pool->wait_exit_thr_num = 0;
		pool->queue_size = 0;

		pool->queue_max_size = queue_max_size;


		pool->queue_head = pool->queue_tail = pool->queue_size = 0;

		pool->shutdown = false;    /*不关注线程池*/

		/*根据最大线程上限数，给工作数组开辟空间*/
		pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * max_thr_num);
		if (pool->threads == NULL) {
			printf("malloc threads fail\n");
			break;
		}

		memset(pool->threads, 0, sizeof(pthread_t) * max_thr_num);

		/*任务队列开辟空间*/
		pool->queue = (threadpool_task_t *)malloc(sizeof(threadpool_task_t) * queue_max_size);
		if (pool->queue == NULL) {
			printf("malloc queue fail\n");
			break;
		}
		/* 初始化条件锁和条件变量 */
		if ((pthread_mutex_init(&(pool->lock), NULL) != 0) ||
			(pthread_mutex_init(&(pool->thread_counter), NULL) != 0) ||

			(pthread_cond_init(&(pool->queue_not_empty), NULL) != 0) ||
			(pthread_cond_init(&(pool->queue_not_full), NULL) != 0)
			)
		{
			break;
		}



		//创建最小线程数的 线程
		for (i = 0; i < min_thr_num; i++) {
			printf("开启工作线程\n");
			if (pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void*)pool) != 0) {
				return NULL;
			}
			
			printf("start thread %x\n", (unsigned int)pool->threads[i]);
		}
		
		//printf("开启管理者线程\n");
		//pthread_create(&(pool->adjust_tid), NULL, adjust_thread, (void*)pool);

		return pool;

	} while (false);


	printf("线程池创建完毕\n");


	threadpool_free(pool);
	return NULL;
}

