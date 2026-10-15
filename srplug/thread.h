#ifndef _SRPLUG_THREAD_H
#define _SRPLUG_THREAD_H

#if defined(_SRPLUG_DAEMON_H)
#error daemon.h and thread.h header files are mutually exclusive !
#endif /* defined(_SRPLUG_DAEMON_H) */

#include <srplug/common.h>
#include <utils/thread.h>
#include <utils/event.h>

enum srplug_thread_state {
	SRPLUG_STARTING_THR_STAT = 0,
	SRPLUG_RUNNING_THR_STAT,
	SRPLUG_STOPPING_THR_STAT,
	SRPLUG_EXITED_THR_STAT,
	SRPLUG_THR_STAT_NR,
};

struct srplug_waker_work {
	struct upoll_worker base;
	int                 fd;
};

struct srplug_thread {
	volatile enum srplug_thread_state state;
	struct upoll                      poll;
	struct srplug_waker_work          wake;
	struct uthr_mutex                 lck;
	struct uthr_cond                  cond;
	pthread_t                         id;
};

#define srplug_thread_assert(_thr) \
	srplug_assert(_thr); \
	srplug_assert((_thr)->state >= 0); \
	srplug_assert((_thr)->state <= SRPLUG_THR_STAT_NR)

static inline const struct upoll *
srplug_thread_poller(const struct srplug_thread * thread)
{
	srplug_thread_assert(thread);

	return (const struct upoll *)&thread->poll;
}

extern int
srplug_thread_start(struct srplug_thread * thread);

extern void
srplug_thread_stop(struct srplug_thread * thread);

extern int
srplug_thread_init(struct srplug_thread * thread, unsigned int poll_nr);

extern void
srplug_thread_fini(struct srplug_thread * thread);

extern void
srplug_setup(const char * name);

#endif /* _SRPLUG_THREAD_H */
