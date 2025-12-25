// SPDX-License-Identifier: MIT

// in large part using https://nachtimwald.com/2019/04/12/thread-pool-in-c/#object-data
#ifndef __TPOOL_H__
#define __TPOOL_H__

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

struct tpool_struct;
struct tpool_work_struct;
typedef struct tpool_struct tpool;
typedef struct tpool_work_struct tpool_work;

typedef void (*thread_func)(void *arg);

struct tpool_struct {
	tpool_work		*work_first;
	tpool_work		*work_last;
	pthread_mutex_t  work_mutex;
	pthread_cond_t   work_cond;
	pthread_cond_t   working_cond;
	size_t           working_cnt;
	size_t           thread_cnt;
	bool             stop;
};
struct tpool_work_struct {
	thread_func					func;
	void						*arg;
	struct tpool_work_struct	*next;
};

tpool *tpool_create(size_t num);
void tpool_destroy(tpool *tm);

bool tpool_add_work(tpool *tm, thread_func func, void *arg);
void tpool_wait(tpool *tm);

#endif /* __TPOOL_H__ */
