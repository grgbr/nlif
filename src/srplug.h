#ifndef _SRPLUG_H
#define _SRPLUG_H

#include "config.h"
#include <utils/poll.h>
#include <utils/timer.h>
#include <elog/elog.h>
#include <sysrepo.h>

#if defined(CONFIG_SRPLUG_ASSERT)

#include <stroll/assert.h>

#define srplug_assert(_cond) \
	stroll_assert("srplug", _cond)

#else  /* !defined(CONFIG_SRPLUG_ASSERT) */

#define srplug_assert(_cond)

#endif /* defined(CONFIG_SRPLUG_ASSERT) */

extern void *
srplug_malloc(size_t size);

static inline void
srplug_free(void * data)
{
	free(data);
}

#if defined(CONFIG_SRPLUG_DAEMON)

/******************************************************************************
 * Command line parsing.
 ******************************************************************************/

struct srplug_daemon_conf {
#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)
	struct elog_stdio_conf  stdlog;
#endif /* defined(CONFIG_SRPLUG_STDLOG) */
#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)
	struct elog_syslog_conf syslog;
#endif /* defined(CONFIG_SRPLUG_SYSLOG) */
};

#if defined(CONFIG_SRPLUG_DAEMON_CONFIG)

static inline struct srplug_daemon_conf *
srplug_daemon_alloc_conf(void)
{
	return srplug_malloc(sizeof(struct srplug_daemon_conf));
}

static inline void
srplug_daemon_free_conf(struct srplug_daemon_conf * config)
{
	srplug_free(config);
}

#else  /* !defined(CONFIG_SRPLUG_DAEMON_CONFIG) */

static inline struct srplug_daemon_conf *
srplug_daemon_alloc_conf(void)
{
	return NULL;
}

static inline void
srplug_daemon_free_conf(struct srplug_daemon_conf * config __unused)
{
}

#endif /* defined(CONFIG_SRPLUG_DAEMON_CONFIG) */

struct argp_state;
struct srplug_daemon_cmdln_opt;

typedef int
        srplug_cmdln_parse_fn(const struct srplug_daemon_cmdln_opt * option,
                              const char *                           argument,
                              struct argp_state *                    state,
                              struct srplug_daemon_conf *            config);

struct srplug_daemon_cmdln_opt {
	int                     short_name;
	const char *            long_name;
	const char *            arg_name;
	bool                    required;
	const char *            help;
	srplug_cmdln_parse_fn * parse;
};

#define SRPLUG_OPT_MAX (1 << 23)

struct srplug_daemon_cmdln {
	const char *                     brief;
	unsigned int                     nr;
	struct srplug_daemon_cmdln_opt * opts;
};

extern int
srplug_daemon_cmdln_parse(int                                argc,
                          char *                             argv[],
                          const struct srplug_daemon_cmdln * cmdln,
                          struct srplug_daemon_conf *        config);

/******************************************************************************
 * Logging handling.
 ******************************************************************************/

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

#define srplug_debug(_format, ...) \
	do { } while (0)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#else  /* !defined(CONFIG_SRPLUG_LOG) */

#define srplug_err(_format, ...) \
	do { } while (0)

#define srplug_warn(_format, ...) \
	do { } while (0)

#define srplug_info(_format, ...) \
	do { } while (0)

#define srplug_debug(_format, ...) \
	do { } while (0)

#endif /* defined(CONFIG_SRPLUG_LOG) */

extern struct elog *
srplug_daemon_create_log(const struct srplug_daemon_conf * config);

#if defined(CONFIG_SRPLUG_LOG)

extern void
srplug_daemon_destroy_log(struct elog * logger);

#else  /* !defined(CONFIG_SRPLUG_LOG) */

static inline void
srplug_daemon_destroy_log(struct elog * logger __unused)
{
}

#endif /* defined(CONFIG_SRPLUG_LOG) */

/******************************************************************************
 * Main daemon handling.
 ******************************************************************************/

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
	uint32_t            priority;
	uint32_t            options;
};

extern int
srplug_daemon_change_subscribe(struct srplug_daemon *           daemon,
                               const struct srplug_change_sub * subscription,
                               void *                           data);

struct srplug_oper_sub {
	const char *         module;
	const char *         xpath;
	sr_oper_get_items_cb on_get;
	uint32_t             options;
};

extern int
srplug_daemon_oper_subscribe(struct srplug_daemon *         daemon,
                             const struct srplug_oper_sub * subscription,
                             void *                         data);

struct srplug_rpc_sub {
	const char * xpath;
	sr_rpc_cb    on_rpc;
	uint32_t     priority;
	uint32_t     options;
};

extern int
srplug_daemon_rpc_subscribe(struct srplug_daemon *        daemon,
                            const struct srplug_rpc_sub * subscription,
                            void *                        data);

enum srplug_sub_kind {
	SRPLUG_CHANGE_SUB_KIND = 0,
	SRPLUG_OPER_SUB_KIND,
	SRPLUG_RPC_SUB_KIND,
	SRPLUG_SUB_KIND_NR
};

struct srplug_sub {
	enum srplug_sub_kind             kind;
	union {
		struct srplug_change_sub change;
		struct srplug_oper_sub   oper;
		struct srplug_rpc_sub    rpc;
	};
};

extern int
srplug_daemon_subscribe(struct srplug_daemon *    daemon,
                        const struct srplug_sub * subscription,
                        void *                    data);

extern int
srplug_daemon_subscribe_all(struct srplug_daemon *    daemon,
                            const struct srplug_sub * subscriptions,
                            unsigned int              nr,
                            void *                    data);

extern int
srplug_daemon_poll(const struct srplug_daemon * daemon);

extern int
srplug_daemon_open(struct srplug_daemon * daemon, unsigned int poll_nr);

extern void
srplug_daemon_close(struct srplug_daemon * daemon);

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
