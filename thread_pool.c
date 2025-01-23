#include "thread_pool.h"

void handler(void *arg)
{
	printf("[%u] is ended.\n",
		(unsigned)pthread_self());

	pthread_mutex_unlock((pthread_mutex_t *)arg);
}

void *routine(void *arg)
{
	#ifdef DEBUG
	printf("[%u] is started.\n",
		(unsigned)pthread_self());
	#endif

	thread_pool *pool = (thread_pool *)arg;
	struct task *p;

	while(1)
	{
		/*
		** push a cleanup functon handler(), make sure that
		** the calling thread will release the mutex properly
		** even if it is cancelled during holding the mutex.
		**
		** NOTE:
		** pthread_cleanup_push() is a macro which includes a
		** loop in it, so if the specified field of codes that 
		** paired within pthread_cleanup_push() and pthread_
		** cleanup_pop() use 'break' may NOT break out of the
		** truely loop but break out of these two macros.
		** see line 61 below.
		*/
		//================================================//
		pthread_cleanup_push(handler, (void *)&pool->lock);
		pthread_mutex_lock(&pool->lock);
		//================================================//

		// 1, no task, and is NOT shutting down, then wait
		while(pool->waiting_tasks == 0 && !pool->shutdown)
		{
			//    进入睡眠时，会自动对m解锁
    		//    退出睡眠时，会自动对m加锁
			pthread_cond_wait(&pool->cond, &pool->lock);
		}

		// 2, no task, and is shutting down, then exit
		if(pool->waiting_tasks == 0 && pool->shutdown == true)
		{
			pthread_mutex_unlock(&pool->lock);
			pthread_exit(NULL); // CANNOT use 'break';
		}

		// 3, have some task, then consume it
		p = pool->task_list->next; //找到任务节点
		pool->task_list->next = p->next; //剩余的任务节点
		pool->waiting_tasks--; //任务量-1

		//================================================//
		pthread_mutex_unlock(&pool->lock);
		pthread_cleanup_pop(0);
		//================================================//

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); //禁用取消功能，以免这个线程被其他线程取消
		(p->do_task)(p->arg); //执行任务
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); //执行完之后重新启用取消功能

		free(p); //执行完任务之后释放对应节点的内存
	}

	pthread_exit(NULL);
}

//pool：线程池指针，threads_number：初始活跃线程个数（大于等于1）
bool init_pool(thread_pool *pool, unsigned int threads_number)
{
	/*****************************************************  初始化线程池 *****************************************************/
	pthread_mutex_init(&pool->lock, NULL); //初始化互斥锁
	pthread_cond_init(&pool->cond, NULL); //初始化条件量

	pool->shutdown = false; //表示线程池当前处于工作状态
	pool->task_list = malloc(sizeof(struct task));  //为任务链表分配内存。链表用于存储待处理的任务
	pool->tids = malloc(sizeof(pthread_t) * MAX_ACTIVE_THREADS); //为线程 ID 数组分配内存

	if(pool->task_list == NULL || pool->tids == NULL)
	{
		perror("allocate memory error");
		return false;
	}

	pool->task_list->next = NULL; //设置任务链表的起始节点为 NULL，表示当前任务队列为空。

	pool->max_waiting_tasks = MAX_WAITING_TASKS; //设置任务队列的最大容量为 MAX_WAITING_TASKS
	pool->waiting_tasks = 0; //设置当前等待处理的任务数量为 0
	pool->active_threads = threads_number; //记录线程池中工作线程的数量。

	/************************************************************************************************************************/

	/*****************************************************  创建活跃线程 *****************************************************/
	int i;
	for(i=0; i<pool->active_threads; i++)
	{
		if(pthread_create(&((pool->tids)[i]), NULL,
					routine, (void *)pool) != 0)
		{
			perror("create threads error");
			return false;
		}

		#ifdef DEBUG
		printf("[%u]:[%s] ==> tids[%d]: [%u] is created.\n",
			(unsigned)pthread_self(), __FUNCTION__,
			i, (unsigned)pool->tids[i]);
		#endif
	}
	/************************************************************************************************************************/

	return true;
}

//pool：线程池指针，do_task：投送至线程池的执行历程，arg：执行历程do_task的参数
bool add_task(thread_pool *pool,
	      void *(*do_task)(void *arg), void *arg)
{
	struct task *new_task = malloc(sizeof(struct task)); //创建新的任务节点
	if(new_task == NULL)
	{
		perror("allocate memory error");
		return false;
	}
	new_task->do_task = do_task;
	new_task->arg = arg;
	new_task->next = NULL;

	//============ LOCK =============//
	pthread_mutex_lock(&pool->lock); //加锁保护线程池的任务队列，防止多个线程同时操作任务队列导致竞争条件
	//===============================//

	if(pool->waiting_tasks >= MAX_WAITING_TASKS) //任务量达到MAX_WAITING_TASKS，不能再加任务
	{
		pthread_mutex_unlock(&pool->lock);

		fprintf(stderr, "too many tasks.\n");
		free(new_task);

		return false;
	}
	
	struct task *tmp = pool->task_list;
	while(tmp->next != NULL) //找到任务链表中最后一个任务节点
		tmp = tmp->next;

	tmp->next = new_task; //最后一个任务节点后添加新的任务节点
	pool->waiting_tasks++; //同时待处理任务数+1

	//=========== UNLOCK ============//
	pthread_mutex_unlock(&pool->lock);
	//===============================//

	#ifdef DEBUG
	printf("[%u][%s] ==> a new task has been added.\n",
		(unsigned)pthread_self(), __FUNCTION__);
	#endif

	pthread_cond_signal(&pool->cond); //唤醒第一个进入条件量睡眠的线程
	return true;
}

//pool：线程池指针，addtional_threads：添加线程的个数
int add_thread(thread_pool *pool, unsigned additional_threads)
{
	if(additional_threads == 0)
		return 0;

	unsigned total_threads =
			pool->active_threads + additional_threads;

	int i, actual_increment = 0;
	for(i = pool->active_threads;
	    i < total_threads && i < MAX_ACTIVE_THREADS;
	    i++)
	{
		if(pthread_create(&((pool->tids)[i]),
					NULL, routine, (void *)pool) != 0)
		{
			perror("add threads error");

			// no threads has been created, return fail
			if(actual_increment == 0)
				return -1;

			break;
		}
		actual_increment++; 

		#ifdef DEBUG
		printf("[%u]:[%s] ==> tids[%d]: [%u] is created.\n",
			(unsigned)pthread_self(), __FUNCTION__,
			i, (unsigned)pool->tids[i]);
		#endif
	}

	pool->active_threads += actual_increment;
	return actual_increment; //实际添加的线程个数
}

//pool：线程池指针，removing_threads：要删除的线程个数，返回线程池剩余的线程个数
int remove_thread(thread_pool *pool, unsigned int removing_threads)
{
	if(removing_threads == 0)
		return pool->active_threads;

	int remaining_threads = pool->active_threads - removing_threads;
	remaining_threads = remaining_threads > 0 ? remaining_threads : 1; //至少要保留一个线程

	int i;
	for(i=pool->active_threads-1; i>remaining_threads-1; i--)
	{
		errno = pthread_cancel(pool->tids[i]); //删除线程

		if(errno != 0)
			break;

		#ifdef DEBUG
		printf("[%u]:[%s] ==> cancelling tids[%d]: [%u]...\n",
			(unsigned)pthread_self(), __FUNCTION__,
			i, (unsigned)pool->tids[i]);
		#endif
	}

	if(i == pool->active_threads-1)
		return -1;
	else
	{
		pool->active_threads = i+1;
		return i+1;
	}
}

bool destroy_pool(thread_pool *pool)
{
	// 1, activate all threads
	pool->shutdown = true;
	pthread_cond_broadcast(&pool->cond);

	// 2, wait for their exiting
	int i;
	for(i=0; i<pool->active_threads; i++)
	{
		errno = pthread_join(pool->tids[i], NULL);
		if(errno != 0)
		{
			printf("join tids[%d] error: %s\n",
					i, strerror(errno));
		}
		else
			printf("[%u] is joined\n", (unsigned)pool->tids[i]);
		
	}

	// 3, free memories
	free(pool->task_list); //全部的任务节点在之后完之后，对应的内存就已经释放了
	free(pool->tids);
	free(pool);

	return true;
}