#ifndef _SRPLUG_H
#define _SRPLUG_H

#define CONFIG_SRPLUG_ASSERT 1
#define CONFIG_SRPLUG_DAEMON 1
#define CONFIG_SRPLUG_LOG 1
#define CONFIG_SRPLUG_DEBUG 1

#include <utils/poll.h>
#include <utils/timer.h>
#include <sysrepo.h>
#include <elog/elog.h>

#if defined(CONFIG_SRPLUG_ASSERT)

#include <stroll/assert.h>

#define srplug_assert(_cond) \
	stroll_assert("srplug", _cond)

#else  /* !defined(CONFIG_SRPLUG_ASSERT) */

#define srplug_assert(_cond)

#endif /* defined(CONFIG_SRPLUG_ASSERT) */

#if !defined(CONFIG_SRPLUG_LOG)

#define srplug_err(_format, ...)
#define srplug_warn(_format, ...)
#define srplug_info(_format, ...)
#define srplug_debug(_format, ...)

#endif /* !defined(CONFIG_SRPLUG_LOG) */

#if defined(CONFIG_SRPLUG_DAEMON)

#if defined(CONFIG_SRPLUG_LOG)

extern void
srplug_daemon_log(enum elog_severity severity, const char * format, ...);

#define srplug_err(_format, ...) \
	srplug_daemon_log(ELOG_ERR_SEVERITY, _format ".", ## __VA_ARGS__)

#define srplug_warn(_format, ...) \
	srplug_daemon_log(ELOG_WARNING_SEVERITY, _format ".", ## __VA_ARGS__)

#define srplug_info(_format, ...) \
	srplug_daemon_log(ELOG_INFO_SEVERITY, _format ".", ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_debug(_format, ...) \
	srplug_daemon_log(ELOG_DEBUG_SEVERITY, _format ".", ## __VA_ARGS__)

#else  /* !defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_daemon_debug(_format, ...)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#endif /* defined(CONFIG_SRPLUG_LOG) */

struct srplug_sigs_work {
	struct upoll_worker base;
	int                 fd;
};

struct srplug_daemon {
        sr_session_ctx_t *      sess;
	struct upoll            poll;
	struct upoll_worker     sub_work;
	sr_subscription_ctx_t * sub_ctx;
	struct etux_timer       sub_tmr;
	unsigned int            sub_cnt;
	struct srplug_sigs_work sigs;
};

#define srplug_daemon_assert(_daemon) \
	srplug_assert(_daemon); \
	srplug_assert((_daemon)->sess)

static inline const struct upoll *
srplug_daemon_poller(const struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	return (const struct upoll *)&daemon->poll;
}

struct srplug_change_sub {
	const char *        module;
	const char *        xpath;
	sr_module_change_cb on_change;
	void *              data;
	uint32_t            priority;
	uint32_t            options;
};

extern int
srplug_daemon_change_subscribe(struct srplug_daemon *           daemon,
                               const struct srplug_change_sub * subscription);

struct srplug_oper_sub {
	const char *         module;
	const char *         xpath;
	sr_oper_get_items_cb on_get;
	void *               data;
	uint32_t             options;
};

extern int
srplug_daemon_oper_subscribe(struct srplug_daemon *         daemon,
                             const struct srplug_oper_sub * subscription);

struct srplug_rpc_sub {
	const char * xpath;
	sr_rpc_cb    on_rpc;
	void *       data;
	uint32_t     priority;
	uint32_t     options;
};

extern int
srplug_daemon_rpc_subscribe(struct srplug_daemon *        daemon,
                            const struct srplug_rpc_sub * subscription);

extern int
srplug_daemon_poll(const struct srplug_daemon * daemon);

extern int
srplug_daemon_open(struct srplug_daemon * daemon, unsigned int poll_nr);

extern void
srplug_daemon_close(struct srplug_daemon * daemon);

struct elog;

extern void
srplug_setup(struct elog * logger);

#endif /* defined(CONFIG_SRPLUG_DAEMON) */

#if 0
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
#endif

#endif /* _SRPLUG_H */
