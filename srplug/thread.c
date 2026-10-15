#include "thread.h"
#include <utils/time.h>
#include <stdbool.h>
#include <sysrepo.h>

#if defined(CONFIG_SRPLUG_LOG)

static const char * srplug_name;

#define srplug_thread_err(_format, ...) \
	srplg_log(srplug_name, SR_LL_ERR, _format ".", ## __VA_ARGS__)

#define srplug_thread_warn(_format, ...) \
	srplg_log(srplug_name, SR_LL_WRN, _format ".", ## __VA_ARGS__)

#define srplug_thread_info(_format, ...) \
	srplg_log(srplug_name, SR_LL_INF, _format ".", ## __VA_ARGS__)

#else  /* !defined(CONFIG_SRPLUG_LOG) */

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_thread_debug(_format, ...) \
	srplg_log(srplug_name, SR_LL_DBG, _format ".", ## __VA_ARGS__)

#else  /* !defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_thread_debug(_format, ...)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#endif /* defined(CONFIG_SRPLUG_LOG) */

static
int
srplug_waker_dispatch(struct upoll_worker * worker,
                      uint32_t              state __unused,
                      const struct upoll *  poller __unused)
{
	srplug_assert(worker);
	srplug_assert(state);
	srplug_assert(!(state & EPOLLOUT));
	srplug_assert(!(state & EPOLLRDHUP));
	srplug_assert(!(state & EPOLLPRI));
	srplug_assert(!(state & EPOLLHUP));
	srplug_assert(!(state & EPOLLERR));
	srplug_assert(state & EPOLLIN);
	srplug_assert(poller);

	const struct srplug_waker_work * wk;
	eventfd_t                        cnt;
	int                              ret;

	wk = containerof(worker, struct srplug_waker_work, base);
	srplug_assert(wk);
	srplug_assert(wk->fd > 0);

	ret = uevt_read(wk->fd, &cnt);
	if (ret < 0) {
		/*
		 * Not possible since all signals are blocked when thread is
		 * spawned thanks to uthr_attr_set_sigmask().
		 */
		srplug_assert(ret != -EINTR);
		/*
		 * This would mean that we got awakened with no call to
		 * srplug_waker_trigger() which should not be possible either,
		 * i.e., a bug occured...
		 */
		srplug_assert(ret != -EAGAIN);

		return ret;
	}

	srplug_assert(cnt);

	return 0;
}

static
void
srplug_waker_trigger(const struct srplug_waker_work * waker)
{
	srplug_assert(waker);
	srplug_assert(waker->fd > 0);

	int err;

	err = uevt_write(waker->fd, 1U);
	srplug_assert(!err || (err == -EAGAIN));
}

static
int
srplug_waker_open(struct srplug_waker_work * waker, const struct upoll * poller)
{
	srplug_assert(waker);
	srplug_assert(poller);

	int          ret;
	const char * msg;

	ret = uevt_open(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (ret < 0) {
		msg = "cannot open file";
		goto err;
	}

	waker->fd = ret;
	ret = upoll_register_dispatch(poller,
	                              ret,
	                              EPOLLIN,
	                              &waker->base,
	                              srplug_waker_dispatch);
	if (ret) {
		if (ret == -ENOMEM)
			srplug_abort();

		msg = "cannot register";
		goto close;
	}

	return 0;

close:
	uevt_close(waker->fd);
err:
	srplug_thread_err("cannot open wakeup worker: %s", msg);

	return ret;
}

static void
srplug_waker_close(const struct srplug_waker_work * waker,
                   const struct upoll *             poller)
{
	srplug_assert(waker);
	srplug_assert(waker->fd > 0);
	srplug_assert(poller);

	upoll_unregister(poller, waker->fd);
	uevt_close(waker->fd);
}

static void
srplug_thread_switch_state(struct srplug_thread *   thread,
                           enum srplug_thread_state state)
{
	srplug_thread_assert(thread);
	srplug_assert(state >= 0);
	srplug_assert(state < SRPLUG_THR_STAT_NR);

	uthr_lock_mutex(&thread->lck);
	thread->state = state;
	uthr_signal_cond(&thread->cond);
	uthr_unlock_mutex(&thread->lck);
}

static bool
srplug_thread_running(const struct srplug_thread * thread)
{
	srplug_thread_assert(thread);

	return thread->state == SRPLUG_RUNNING_THR_STAT;
}

static void *
srplug_thread_process(void * data)
{
	srplug_thread_assert((struct srplug_thread *)data);

	struct srplug_thread * thr = data;
	int                   ret;

	uthr_set_name(thr->id, srplug_name);

	/* Switch to runnig state and signal waiter that we have started. */
	srplug_thread_switch_state(thr, SRPLUG_RUNNING_THR_STAT);

	srplug_thread_info("thread started");

	/* Run main loop. */
	do {
		ret = upoll_process(&thr->poll, -1);
	} while (!ret && srplug_thread_running(thr));
	if (ret == -ESHUTDOWN)
		ret = 0;

	/* Switch to exited state and signal waiter that we have exited. */
	srplug_thread_switch_state(thr, SRPLUG_EXITED_THR_STAT);

	srplug_thread_info("thread exited with status: '%s'", strerror(-ret));

	uthr_exit(NULL);
}

int
srplug_thread_start(struct srplug_thread * thread)
{
	srplug_thread_assert(thread);
	srplug_assert(thread->state == SRPLUG_THR_STAT_NR);

	pthread_attr_t           attr;
	const sigset_t           msk = *usig_full_msk;
	enum srplug_thread_state stat;
	int                      err;

	/*
	 * Make sure that the thread is spawned in detached state and that it
	 * blocks all signals.
	 */
	uthr_attr_init(&attr);
	uthr_attr_set_detachstate(&attr, PTHREAD_CREATE_DETACHED);
	err = uthr_attr_set_sigmask(&attr, &msk);
	if (err) {
		srplug_assert(err == -ENOMEM);
		srplug_abort();
	}

	/* Spawn thread. */
	thread->state = SRPLUG_STARTING_THR_STAT;
	err = uthr_create(&thread->id, &attr, srplug_thread_process, thread);
	uthr_attr_destroy(&attr);
	if (err) {
		srplug_thread_err("cannot spawn thread: %s", strerror(-err));
		goto err;
	}

	/* Wait for the thread routine to signal us a state switch. */
	uthr_lock_mutex(&thread->lck);
	stat = thread->state;
	while (stat == SRPLUG_STARTING_THR_STAT) {
		uthr_wait_cond(&thread->cond, &thread->lck);
		stat = thread->state;
	}
	uthr_unlock_mutex(&thread->lck);

	switch (stat) {
	case SRPLUG_RUNNING_THR_STAT:
		/* Thread start operation completed successfully. */
		break;

	case SRPLUG_EXITED_THR_STAT:
		/* Thread start operation failed to complete successfully. */
		err = -ESRCH;
		goto err;

	default:
		srplug_assert(0);
	}

	return 0;

err:
	thread->state = SRPLUG_THR_STAT_NR;

	return err;
}

void
srplug_thread_stop(struct srplug_thread * thread)
{
	srplug_thread_assert(thread);

	struct timespec tspec;
	int             ret = 0;

#define NLIF_SRPLUG_STOP_SECS (3)
	uthr_cond_now(&thread->cond, &tspec);
	utime_tspec_add_sec_clamp(&tspec, NLIF_SRPLUG_STOP_SECS);

	uthr_lock_mutex(&thread->lck);

	switch (thread->state) {
	case SRPLUG_RUNNING_THR_STAT:
		thread->state = SRPLUG_STOPPING_THR_STAT;
		srplug_waker_trigger(&thread->wake);

		while (thread->state == SRPLUG_STOPPING_THR_STAT) {
			ret = uthr_timed_wait_cond(&thread->cond,
			                           &thread->lck,
			                           &tspec);
			if (ret)
				break;
		}

		break;

	case SRPLUG_EXITED_THR_STAT:
		break;

	default:
		srplug_assert(0);
	}

	uthr_unlock_mutex(&thread->lck);

	if (ret) {
		/*
		 * Thread stop operation timed out: shoot it down (SIGKILL
		 * cannot be blocked) !
		 */
		srplug_assert(ret == -ETIMEDOUT);

		uthr_kill(thread->id, SIGKILL);
		srplug_thread_warn("timed out while stopping thread");
	}

	thread->state = SRPLUG_THR_STAT_NR;

	srplug_thread_debug("thread stopped");
}

int
srplug_thread_init(struct srplug_thread * thread, unsigned int poll_nr)
{
	srplug_assert(thread);
	srplug_assert(poll_nr);
	srplug_assert(poll_nr <= (unsigned int)INT_MAX);

	int err;

	err = upoll_open(&thread->poll, poll_nr);
	if (err) {
		srplug_thread_err("cannot open poller: %s", strerror(-err));
		return err;
	}

	err = srplug_waker_open(&thread->wake, &thread->poll);
	if (err)
		goto close_poll;

	uthr_init_mutex(&thread->lck);
	err = uthr_init_cond(&thread->cond, CLOCK_MONOTONIC);
	if (err) {
		if (err == -ENOMEM);
			srplug_abort();

		srplug_thread_err(
			"cannot initialize thread state condition: %s",
			strerror(-err));
		goto fini_lck;
	}

	thread->state = SRPLUG_THR_STAT_NR;

	srplug_thread_debug("thread initialized");

	return 0;

fini_lck:
	uthr_fini_mutex(&thread->lck);
	srplug_waker_close(&thread->wake, &thread->poll);
close_poll:
	upoll_close(&thread->poll);

	return err;
}

void
srplug_thread_fini(struct srplug_thread * thread)
{
	srplug_thread_assert(thread);
	srplug_assert(thread->state == SRPLUG_THR_STAT_NR);

	uthr_fini_cond(&thread->cond);
	uthr_fini_mutex(&thread->lck);
	srplug_waker_close(&thread->wake, &thread->poll);
	upoll_close(&thread->poll);

	srplug_thread_debug("thread finished");
}

void
srplug_setup(const char * name)
{
	srplug_assert(name);
	srplug_assert(name[0]);
	srplug_assert(strlen(name) < UTHR_NAME_MAX);

	srplug_name = name;
}

#endif /* defined(CONFIG_SRPLUG_THREAD) */
