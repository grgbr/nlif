#ifndef _SRPLUG_H
#define _SRPLUG_H

#define CONFIG_SRPLUG_ASSERT 1
#define CONFIG_SRPLUG_THREAD 1
#define CONFIG_SRPLUG_PROCESS 1
#define CONFIG_SRPLUG_LOG 1
#define CONFIG_SRPLUG_DEBUG 1

#include <utils/poll.h>

#if defined(CONFIG_SRPLUG_ASSERT)

#include <stroll/assert.h>

#define srplug_assert(_cond) \
	stroll_assert("srplug", _cond)

#else  /* !defined(CONFIG_SRPLUG_ASSERT) */

#define srplug_assert(_cond)

#endif /* defined(CONFIG_SRPLUG_ASSERT) */

#if defined(CONFIG_SRPLUG_PROCESS)

struct srplug_sigs_work {
	struct upoll_worker base;
	int                 fd;
};

struct srplug_process {
	struct upoll            poll;
	struct srplug_sigs_work sigs;
};

#define srplug_process_assert(_process) \
	srplug_assert(_process)

static inline const struct upoll *
srplug_process_poller(const struct srplug_process * process)
{
	srplug_process_assert(thread);

	return (const struct upoll *)&process->poll;
}

extern int
srplug_process_poll(const struct srplug_process * process);

extern int
srplug_process_init(struct srplug_process * process, unsigned int poll_nr);

extern void
srplug_process_fini(struct srplug_process * process);

#endif /* defined(CONFIG_SRPLUG_PROCESS) */

#if defined(CONFIG_SRPLUG_THREAD)

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

#endif /* defined(CONFIG_SRPLUG_THREAD) */

#endif /* _SRPLUG_H */
