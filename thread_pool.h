#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <errno.h>
#include <pthread.h>

#define MAX_WAITING_TASKS	1000
#define MAX_ACTIVE_THREADS	20

//任务节点
struct task
{
	void *(*do_task)(void *arg);
	void *arg;

	struct task *next;
};

//线程池结构体
typedef struct thread_pool
{
	pthread_mutex_t lock; //线程池中包含多个线程，可能会同时对共享资源（如任务队列）进行操作。lock 是一个互斥锁，用于防止多个线程同时修改共享资源，保证线程安全。
	pthread_cond_t  cond; //当任务队列中没有任务时，工作线程需要进入等待状态。通过条件变量，主线程可以通知工作线程有新的任务到达。

	bool shutdown; //线程池销毁标记

	struct task *task_list; //任务链表

	pthread_t *tids; //线程池ID

	unsigned max_waiting_tasks; //表示任务队列的最大容量
	unsigned waiting_tasks; //任务链队列中等待的任务个数
	unsigned active_threads; //当前活跃线程的个数
}thread_pool;


bool init_pool(thread_pool *pool, unsigned int threads_number);
bool add_task(thread_pool *pool, void *(*do_task)(void *arg), void *task);
int  add_thread(thread_pool *pool, unsigned int additional_threads_number);
int  remove_thread(thread_pool *pool, unsigned int removing_threads_number);
bool destroy_pool(thread_pool *pool);

void *routine(void *arg);


#endif
