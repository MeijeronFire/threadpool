// SPDX-License-Identifier: MIT

// in large part using https://nachtimwald.com/2019/04/12/thread-pool-in-c/#object-data

#include <stdlib.h>
#include "tpool.h"

// creates a work object
static tpool_work *tpool_work_create(thread_func func, void *arg)
{
	tpool_work *work;

	if (func == NULL)
		return NULL;

	work		= malloc(sizeof(*work));
	work->func	= func;
	work->arg	= arg;
	work->next	= NULL;
	return work;
}

// frees a work object
static void tpool_work_destroy(tpool_work *work)
{
	if (work == NULL)
		return;
	free(work);
}

// gets element from work queue, => get
static tpool_work *tpool_work_get(tpool *tm)
{
	tpool_work *work;

	if (tm == NULL) // no proper threadpool passed
		return NULL;

	work = tm->work_first;
	if (work == NULL) // so if the list is empty
		return NULL;

	if (work->next == NULL) { // => one element left in list
		tm->work_first = NULL;
		tm->work_last  = NULL;
	} else { // => >1 elements in linked list
		tm->work_first = work->next;
	}

	return work;
}

// so this executes everything in the threadpool
static void *tpool_worker(void *arg)
{
	tpool		*tm = arg;
	tpool_work	*work;

	while (1) {
		// lock the mutex, so nothing changes the members of the pool
		pthread_mutex_lock(&(tm->work_mutex));

		// when there is no work and we are not stopped, enter loop
		while (tm->work_first == NULL && !tm->stop) {
			// block a thread until we are signalled to work again
			// releases the mutex whilst waiting and locks it once
			// done waiting
			pthread_cond_wait(&(tm->work_cond), &(tm->work_mutex));
		}

		// if we have to stop, exit the loop
		if (tm->stop)
			break;

		// once we are here, we know that there is work to be done
		// and we do not have to stop. So, we get the work:
		work = tpool_work_get(tm); // get the function to be ran into work
		tm->working_cnt++; // increment the amount of work being done
		pthread_mutex_unlock(&(tm->work_mutex)); // unlock the mutex (for some reason)

		// so if we were succesfull in getting this work
		// because, if 4 threads all pull, only one will get the work so the next third
		// don't need to do anything
		if (work != NULL) {
			work->func(work->arg); // run the function with args
			tpool_work_destroy(work); // remove it from the list as it no longer needs to be started
		}

		// now once work has been processed
		pthread_mutex_lock(&(tm->work_mutex)); // relock
		tm->working_cnt--; // decrease count by 1

		// if there are no threads working AND we are not stopped AND no items in queue
		if (!tm->stop && tm->working_cnt == 0 && tm->work_first == NULL)
			pthread_cond_signal(&(tm->working_cond)); // inform other threads of this state
		pthread_mutex_unlock(&(tm->work_mutex)); // and unlock mutex
	}

	tm->thread_cnt--; // since this thread is stopped, decrement
	pthread_cond_signal(&(tm->working_cond)); // signal thread has exited
	pthread_mutex_unlock(&(tm->work_mutex)); // unlock mutex
	return NULL;
}

// creates n amount of threads that execute the tpool_worker function
tpool *tpool_create(size_t num)
{
	tpool		*tm; // the threadpool
	pthread_t	thread; // just using this to create the threads, not read at all.
	size_t		i;

	if (num == 0)
		num = 2; // default / minimum amount of threads

	tm             = calloc(1, sizeof(*tm)); // initialise as empty
	tm->thread_cnt = num; // write the amount of threads to the function

	pthread_mutex_init(&(tm->work_mutex), NULL); // initialize the mutex
	pthread_cond_init(&(tm->work_cond), NULL); // initialize the signal work
	pthread_cond_init(&(tm->working_cond), NULL); // initialize the signal working

	tm->work_first = NULL; // allocate with 0
	tm->work_last  = NULL; // allocate with 0

	// make all of the "anonymous" threads
	// "There is no need to store the thread ids because they will never be accessed directly"
	for (i = 0; i < num; i++) {
		pthread_create(&thread, NULL, tpool_worker, tm);
		pthread_detach(thread);
	}

	return tm;
}

// the destructor of the threadpool object
// KANKER KANKER KANKER KANKER KANKER
void tpool_destroy(tpool *tm)
{
	tpool_work *work;
	tpool_work *work2;

	if (tm == NULL) // if already destroyed BEGONE
		return;

	pthread_mutex_lock(&(tm->work_mutex)); // lock it

	/*
	 * works by:
	 *	work2 = next
	 *	delete work
	 *	assign work = work2
	 *	repeat untill empty
	 */
	work = tm->work_first;
	while (work != NULL) {
		work2 = work->next;
		tpool_work_destroy(work);
		work = work2;
	}
	// then just set all values back to correct, empty
	tm->work_first = NULL;
	tm->stop = true;
	// broadcast signal "work"
	pthread_cond_broadcast(&(tm->work_cond));
	// unlock as no one uses it anymore
	pthread_mutex_unlock(&(tm->work_mutex));

	// wait for everything to finish processing before deleting
	tpool_wait(tm);

	// AAAH IK MAAK ALLES HELEMAAL FUCKING KAPOT AISDUSAPI FASDOFA
	pthread_mutex_destroy(&(tm->work_mutex));
	pthread_cond_destroy(&(tm->work_cond));
	pthread_cond_destroy(&(tm->working_cond));

	free(tm);
}

// push to linked list
bool tpool_add_work(tpool *tm, thread_func func, void *arg)
{
	// where work object is stored
	tpool_work *work;

	if (tm == NULL)
		return false;

	// create work object
	work = tpool_work_create(func, arg);
	if (work == NULL)
		return false;

	// lock mutex
	pthread_mutex_lock(&(tm->work_mutex));

	// if empty list, set first and last to object created above
	if (tm->work_first == NULL) {
		tm->work_first = work;
		tm->work_last  = tm->work_first;
	} else { // if nonempty, set it to last
		tm->work_last->next = work;
		tm->work_last       = work;
	}

	// say "IT IS TIME TO WORK!"
	pthread_cond_broadcast(&(tm->work_cond));
	pthread_mutex_unlock(&(tm->work_mutex));

	// upon success.
	return true;
}

// wait for pool to be empty
void tpool_wait(tpool *tm)
{
	if (tm == NULL)
		return;

	// lock mutex
	pthread_mutex_lock(&(tm->work_mutex));
	while (1) {
		if (tm->work_first != NULL || // non empty list OR
				(!tm->stop && tm->working_cnt != 0) || // not stopped and at least one thread working OR
				(tm->stop && tm->thread_cnt != 0)) { // stopped and more than one thread in total
			pthread_cond_wait(&(tm->working_cond), &(tm->work_mutex)); // wait for thread to be done
		} else { // once we are done
			break;
		}
	}
	pthread_mutex_unlock(&(tm->work_mutex));
}
