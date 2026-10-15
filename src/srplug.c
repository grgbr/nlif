#include "srplug.h"
#include <utils/signal.h>

#if defined(CONFIG_SRPLUG_DAEMON)

#if defined(CONFIG_SRPLUG_LOG)

static struct elog * srplug_daemon_logger;

#define srplug_daemon_err(_format, ...) \
	srplug_daemon_log(ELOG_ERR_SEVERITY, _format ".", ## __VA_ARGS__)

#define srplug_daemon_warn(_format, ...) \
	srplug_daemon_log(ELOG_WARNING_SEVERITY, _format ".", ## __VA_ARGS__)

#define srplug_daemon_info(_format, ...) \
	srplug_daemon_log(ELOG_INFO_SEVERITY, _format ".", ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_daemon_debug(_format, ...) \
	srplug_daemon_log(ELOG_DEBUG_SEVERITY, _format ".", ## __VA_ARGS__)

#else  /* !defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_daemon_debug(_format, ...)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

void
srplug_daemon_log(enum elog_severity severity, const char * format, ...)
{
	if (srplug_daemon_logger) {
		va_list args;

		va_start(args, format);
		elog_vlog(srplug_daemon_logger, severity, format, args);
		va_end(args);
	}
}

#else  /* !defined(CONFIG_SRPLUG_LOG) */

#define srplug_daemon_err(_format, ...)
#define srplug_daemon_warn(_format, ...)
#define srplug_daemon_info(_format, ...)
#define srplug_daemon_debug(_format, ...)

#endif /* !defined(CONFIG_SRPLUG_LOG) */

/******************************************************************************
 * Signal handling.
 ******************************************************************************/

static
int
srplug_sigs_dispatch(struct upoll_worker * worker,
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

	const struct srplug_sigs_work * wk;
	struct signalfd_siginfo        info;
	int                            ret;

	wk = containerof(worker, struct srplug_sigs_work, base);
	srplug_assert(wk);
	srplug_assert(wk->fd > 0);

	ret = usig_read_fd(wk->fd, &info, 1);
	srplug_assert(ret);
	if (ret < 0)
		return (ret == -EAGAIN) ? 0 : ret;

	switch (info.ssi_signo) {
	case SIGHUP:
		/* TODO: implement reload ! */
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		/* Tell caller we were requested to terminate. */
		srplug_daemon_debug("interrupted by signal '%s'",
		                     strsignal((int)info.ssi_signo));
		return -ESHUTDOWN;

	case SIGUSR1:
	case SIGUSR2:
		/* Silently ignore these... */
		return 0;

	default:
		srplug_assert(0);
	}

	unreachable();
}

static int
srplug_sigs_init(struct srplug_sigs_work * worker, const struct upoll * poller)
{
	srplug_assert(worker);
	srplug_assert(poller);

	sigset_t     msk = *usig_empty_msk;
	sigset_t     blk = *usig_full_msk;
	int          ret;
	const char * msg __unused;

	/* REVIEW ME !!
	 * Do we need to ignore the following signals as performed by
	 * sysrepo-plugind ??
	 *
	 * - SIGPIPE
	 * - SIGTSTP
	 * - SIGTTIN
	 * - SIGTTOU
	 *
	 * Setup current working directory ??
	 */

	usig_addset(&msk, SIGHUP);
	usig_addset(&msk, SIGINT);
	usig_addset(&msk, SIGQUIT);
	usig_addset(&msk, SIGTERM);
	usig_addset(&msk, SIGUSR1);
	usig_addset(&msk, SIGUSR2);

	ret = usig_open_fd(&msk, SFD_NONBLOCK | SFD_CLOEXEC);
	if (ret < 0) {
		msg = "cannot open signal file";
		goto err;
	}

	worker->fd = ret;
	ret = upoll_register_dispatch(poller,
	                              ret,
	                              EPOLLIN,
	                              &worker->base,
	                              srplug_sigs_dispatch);
	if (ret) {
		if (ret == -ENOMEM)
			abort();

		msg = "cannot register worker";
		goto close;
	}

	usig_delset(&blk, SIGCONT);
	usig_delset(&blk, SIGTSTP);
	usig_delset(&blk, SIGTRAP);
	usig_delset(&blk, SIGTTIN);
	usig_delset(&blk, SIGTTOU);
#warning TODO: SIG_IGN signals instead !!
	usig_procmask(SIG_SETMASK, &blk, NULL);

	srplug_daemon_debug("signal handlers registered");

	return 0;

close:
	usig_close_fd(worker->fd);
err:
	srplug_daemon_err("cannot setup signal handlers: %s", msg);

	return ret;
}

static void
srplug_sigs_fini(const struct srplug_sigs_work * worker,
                 const struct upoll *            poller)
{
	srplug_assert(worker);
	srplug_assert(worker->fd > 0);
	srplug_assert(poller);

	upoll_unregister(poller, worker->fd);
	usig_close_fd(worker->fd);

	srplug_daemon_debug("signal handlers unregistered");
}

/******************************************************************************
 * Utilities
 ******************************************************************************/

static void
srplug_adjust_tmr(struct etux_timer * timer, const struct timespec * tmout)
{
	srplug_assert(timer);
	srplug_assert(tmout);

	if (tmout->tv_sec || tmout->tv_nsec) {
		struct timespec tm;

		/* Compute requested timeout absolute time. */
		utime_monotonic_now(&tm);
		utime_tspec_add_clamp(&tm, tmout);

		/*
		 * And arm the timer if necessary, i.e., if it is not armed or
		 * if its expiry date is later than the requested one.
		 */
		if (!etux_timer_is_armed(timer) ||
		    utime_tspec_after(etux_timer_expiry_tspec(timer), &tm))
			etux_timer_arm_tspec(timer, &tm);
	}
	else
		etux_timer_cancel(timer);
}

static
void
srplug_process_subs(sr_subscription_ctx_t * subscriptions,
                    sr_session_ctx_t *      session,
                    struct etux_timer *     timer)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert(timer);

	struct timespec tmout;
	int             err;

	/*
	 * No need to initialize `tmout' since sr_subscription_process_events()
	 * will zero it out.
	 */
	err = sr_subscription_process_events(subscriptions, session, &tmout);
	switch (err) {
	case SR_ERR_OK:
		break;

	case SR_ERR_TIME_OUT:
		/*
		 * Reschedule a call to sr_subscription_process_events() no
		 * later than 100 milliseconds in the future.
		 */
		tmout.tv_sec = 0;
		tmout.tv_nsec = 100000000;
		break;

	case SR_ERR_NO_MEMORY: /* Not enough memory. */
		abort();

	default:
		srplug_daemon_err("failed to process subscription events: %s",
		                  sr_strerror(err));
		return;
	}

	/* Adjust timer configuration. */
	srplug_adjust_tmr(timer, &tmout);
}

static void
srplug_clear_subs(sr_subscription_ctx_t * subscriptions)
{
	int err;

#warning REVIEW ME: What should we do here in case of failure ?! May this really happen ?!

	/* Free all subscribtions (worker->ctx may be NULL here). */
	err = sr_unsubscribe(subscriptions);
	if (err != SR_ERR_OK)
		srplug_daemon_warn("cannot clear subscriptions: %s",
		                   sr_strerror(err));
}

#define srplug_assert_change_sub(_sub) \
	srplug_assert(_sub); \
	srplug_assert((_sub)->module); \
	srplug_assert((_sub)->module[0]); \
	srplug_assert((_sub)->on_change); \
	srplug_assert(!((_sub)->options & \
	                (SR_SUBSCR_NO_THREAD | SR_SUBSCR_THREAD_SUSPEND)))

static int
srplug_register_change_sub(sr_subscription_ctx_t **         subscriptions,
                           sr_session_ctx_t *               session,
                           const struct srplug_change_sub * subscription,
                           const struct upoll *             poller)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert_change_sub(subscription);
	srplug_assert(poller);

	int err;

	err = sr_module_change_subscribe(
		session,
		subscription->module,
		subscription->xpath,
		subscription->on_change,
		subscription->data,
		subscription->priority,
		subscription->options | SR_SUBSCR_NO_THREAD,
		subscriptions);
	if (err == SR_ERR_OK)
		return 0;

	if (err == SR_ERR_NO_MEMORY)
		abort();

	srplug_daemon_info("'%s': "
	                   "cannot register configuration data handler: %s",
	                   subscription->xpath ? subscription->xpath : "",
	                   sr_strerror(err));

	return -EPERM;
}

#define srplug_assert_oper_sub(_sub) \
	srplug_assert(_sub); \
	srplug_assert((_sub)->module); \
	srplug_assert((_sub)->module[0]); \
	srplug_assert((_sub)->on_get); \
	srplug_assert(!((_sub)->options & \
	                (SR_SUBSCR_NO_THREAD | SR_SUBSCR_THREAD_SUSPEND)))

static int
srplug_register_oper_sub(sr_subscription_ctx_t **       subscriptions,
                         sr_session_ctx_t *             session,
                         const struct srplug_oper_sub * subscription,
                         const struct upoll *           poller)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert_oper_sub(subscription);
	srplug_assert(poller);

	int err;

	err = sr_oper_get_subscribe(
		session,
		subscription->module,
		subscription->xpath,
		subscription->on_get,
		subscription->data,
		subscription->options | SR_SUBSCR_NO_THREAD,
		subscriptions);
	if (err == SR_ERR_OK)
		return 0;

	if (err == SR_ERR_NO_MEMORY)
		abort();

	srplug_daemon_info("'%s': cannot register operational data handler: %s",
	                   subscription->xpath ? subscription->xpath : "",
	                   sr_strerror(err));

	return -EPERM;
}

#define srplug_assert_rpc_sub(_sub) \
	srplug_assert(_sub); \
	srplug_assert((_sub)->xpath); \
	srplug_assert((_sub)->xpath[0]); \
	srplug_assert((_sub)->on_rpc); \
	srplug_assert(!((_sub)->options & \
	                (SR_SUBSCR_NO_THREAD | SR_SUBSCR_THREAD_SUSPEND)))

static int
srplug_register_rpc_sub(sr_subscription_ctx_t **      subscriptions,
                        sr_session_ctx_t *            session,
                        const struct srplug_rpc_sub * subscription,
                        const struct upoll *          poller)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert_rpc_sub(subscription);
	srplug_assert(poller);

	int err;

	err = sr_rpc_subscribe(
		session,
		subscription->xpath,
		subscription->on_rpc,
		subscription->data,
		subscription->priority,
		subscription->options | SR_SUBSCR_NO_THREAD,
		subscriptions);
	if (err == SR_ERR_OK)
		return 0;

	if (err == SR_ERR_NO_MEMORY)
		abort();

	srplug_daemon_info("'%s': cannot register RPC / action handler: %s",
	                   subscription->xpath,
	                   sr_strerror(err));

	return -EPERM;
}

/******************************************************************************
 * Sysrepo plugin logic when instantiated as a daemon.
 ******************************************************************************/

static
int
srplug_daemon_dispatch_subs(struct upoll_worker * worker,
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

	struct srplug_daemon * dmn;

	dmn = containerof(worker, struct srplug_daemon, sub_tmr);
	srplug_daemon_assert(dmn);

	srplug_process_subs(dmn->sub_ctx, dmn->sess, &dmn->sub_tmr);

	return 0;
}

static int
srplug_daemon_enable_subs(struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	int err;

	if (!daemon->sub_cnt) {
		srplug_assert(daemon->sub_ctx);
		srplug_assert(!etux_timer_is_armed(&daemon->sub_tmr));

		int fd;

		err = sr_get_event_pipe(daemon->sub_ctx, &fd);
		srplug_assert(err == SR_ERR_OK);
		srplug_assert(fd >= 0);

		err = upoll_register_dispatch(&daemon->poll,
		                              fd,
		                              EPOLLIN,
		                              &daemon->sub_work,
		                              srplug_daemon_dispatch_subs);
		if (err) {
			if (err == -ENOMEM)
				abort();

			srplug_clear_subs(daemon->sub_ctx);
			daemon->sub_ctx = NULL;

			srplug_daemon_err("cannot enable subscription worker: "
			                  "%s",
			                  strerror(-err));

			return err;
		}

		srplug_daemon_debug("subscription worker enabled");
	}

	daemon->sub_cnt++;

	return 0;
}

static void
srplug_daemon_disable_subs(struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	if (daemon->sub_cnt) {
		srplug_assert(daemon->sub_work.dispatch);
		srplug_assert(daemon->sub_ctx);

		int fd;
		int err;

		etux_timer_cancel(&daemon->sub_tmr);

		err = sr_get_event_pipe(daemon->sub_ctx, &fd);
		srplug_assert(err == SR_ERR_OK);
		srplug_assert(fd >= 0);

		/* Unregister from asynchronous poller. */
		upoll_unregister(&daemon->poll, fd);

		daemon->sub_cnt = 0;

		srplug_daemon_debug("subscription worker disabled");
	}
}

int
srplug_daemon_change_subscribe(struct srplug_daemon *           daemon,
                               const struct srplug_change_sub * subscription)
{
	srplug_daemon_assert(daemon);
	srplug_assert_change_sub(subscription);

	int err;

	/* Register the configuration data subscription handler. */
	err = srplug_register_change_sub(&daemon->sub_ctx,
	                                 daemon->sess,
	                                 subscription,
	                                 &daemon->poll);
	if (err)
		return err;

	/* Enable asynchronous processing of subscriptions. */
	return srplug_daemon_enable_subs(daemon);
}

int
srplug_daemon_oper_subscribe(struct srplug_daemon *         daemon,
                             const struct srplug_oper_sub * subscription)
{
	srplug_daemon_assert(daemon);
	srplug_assert_oper_sub(subscription);

	int err;

	/* Register the operational data subscription handler. */
	err = srplug_register_oper_sub(&daemon->sub_ctx,
	                               daemon->sess,
	                               subscription,
	                               &daemon->poll);
	if (err)
		return err;

	/* Enable asynchronous processing of subscriptions. */
	return srplug_daemon_enable_subs(daemon);
}

int
srplug_daemon_rpc_subscribe(struct srplug_daemon *        daemon,
                            const struct srplug_rpc_sub * subscription)
{
	srplug_daemon_assert(daemon);
	srplug_assert_rpc_sub(subscription);

	int err;

	/* Register the RPC subscription handler. */
	err = srplug_register_rpc_sub(&daemon->sub_ctx,
	                              daemon->sess,
	                              subscription,
	                              &daemon->poll);
	if (err)
		return err;

	/* Enable asynchronous processing of subscriptions. */
	return srplug_daemon_enable_subs(daemon);
}

int
srplug_daemon_poll(const struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	int ret;

	do {
		ret = upoll_process(&daemon->poll, etux_timer_issue_msec());
		etux_timer_run();
	} while (!ret || (ret == -ETIME));

	return (ret == -ESHUTDOWN) ? 0 : ret;
}

static
void
srplug_daemon_expire_subs(struct etux_timer * timer)
{
	srplug_assert(timer);

	const struct srplug_daemon * dmn;

	dmn = containerof(timer, struct srplug_daemon, sub_tmr);
	srplug_daemon_assert(dmn);

	srplug_process_subs(dmn->sub_ctx, dmn->sess, timer);
}

int
srplug_daemon_open(struct srplug_daemon * daemon, unsigned int poll_nr)
{
	srplug_assert(daemon);
	srplug_assert(poll_nr <= (unsigned int)INT_MAX);

	int                err;
	sr_conn_ctx_t *    conn = NULL;
	sr_session_ctx_t * sess = NULL;

	/* connect to sysrepo */
	err = sr_connect(SR_CONN_DEFAULT, &conn);
	if (err != SR_ERR_OK) {
		srplug_daemon_err("cannot connect to datastore: %s",
		                  sr_strerror(err));
		return -EPERM;
	}

	err = sr_session_start(conn, SR_DS_RUNNING, &sess);
	if (err) {
		srplug_daemon_err("cannot start datastore session: %s",
		                  sr_strerror(err));
		goto disconnect;
	}

	/*
	 * Add 2 additional polling workers for subscription and signal handling
	 * workers.
	 */
	err = upoll_open(&daemon->poll, poll_nr + 2);
	if (err) {
		srplug_daemon_err("cannot open poller: %s", strerror(-err));
		goto disconnect;
	}

	err = srplug_sigs_init(&daemon->sigs, &daemon->poll);
	if (err)
		goto close;

	daemon->sess = sess;
	daemon->sub_ctx = NULL;
	etux_timer_init(&daemon->sub_tmr, srplug_daemon_expire_subs);
	daemon->sub_cnt = 0;

	srplug_daemon_debug("daemon initialized");

	return 0;

close:
	upoll_close(&daemon->poll);
disconnect:
	/* Also closes all sessions related to this connection. */
	sr_disconnect(conn);

	return -EPERM;
}

void
srplug_daemon_close(struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	sr_conn_ctx_t * conn = sr_session_get_connection(daemon->sess);

	/* Stop asynchronouns processing of subscriptions. */
	srplug_daemon_disable_subs(daemon);

	/* Release all subscriptions. */
	srplug_assert(!etux_timer_is_armed(&daemon->sub_tmr));
	srplug_clear_subs(daemon->sub_ctx);

	/* Stop and close signal handling as well as main poller. */
	srplug_sigs_fini(&daemon->sigs, &daemon->poll);
	upoll_close(&daemon->poll);

	/* Close all sessions and connection to Sysrepo datastores. */
	sr_disconnect(conn);

	srplug_daemon_debug("daemon finished");
}

#if defined(CONFIG_SRPLUG_LOG)

static void
srplug_log_cb(sr_log_level_t level, const char * message)
{
	srplug_assert(srplug_daemon_logger);
	srplug_assert(message);
	
	enum elog_severity svrt;

	switch (level) {
	case SR_LL_ERR:
		svrt = ELOG_ERR_SEVERITY;
		break;
	case SR_LL_WRN:
		svrt = ELOG_WARNING_SEVERITY;
		break;
	case SR_LL_INF:
		svrt = ELOG_NOTICE_SEVERITY;
		break;
	case SR_LL_VRB:
		svrt = ELOG_INFO_SEVERITY;
		break;
	case SR_LL_DBG:
		svrt = ELOG_DEBUG_SEVERITY;
		break;
	default:
		srplug_assert(0);
	}

	srplug_daemon_log(svrt, "%s", message);
}

void
srplug_setup(struct elog * logger)
{
	srplug_assert(!srplug_daemon_logger);

	srplug_daemon_logger = logger;

	/*
	 * Disable Sysrepo's syslog logic.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_syslog(NULL, SR_LL_NONE); */

	/*
	 * Also disable Sysrepo's logic that logs to standard error.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_stderr(SR_LL_NONE); */

	if (logger) {
		/*
		 * Install our own logger to delegate Sysrepo / libyang logging
		 * message processing to elog.
		 */
		sr_log_set_cb(srplug_log_cb);
	}
	else {
		/* Disable Sysrepo / libyang logging entirely. */
		sr_log_set_cb(NULL);
	}
}

#else  /* !defined(CONFIG_SRPLUG_LOG) */

void
srplug_setup(struct elog * logger)
{
	srplug_assert(!srplug_daemon_logger);

	srplug_daemon_logger = NULL;

	/*
	 * Disable Sysrepo's syslog logic.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_syslog(NULL, SR_LL_NONE); */

	/*
	 * Also disable Sysrepo's logic that logs to standard error.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_stderr(SR_LL_NONE); */

	/* Disable Sysrepo / libyang logging entirely. */
	sr_log_set_cb(NULL);
}

#endif /* defined(CONFIG_SRPLUG_LOG) */

#endif /* defined(CONFIG_SRPLUG_DAEMON) */

#if 0
#if defined(CONFIG_SRPLUG_THREAD)

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
			abort();

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
		abort();
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
			abort();

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
#endif
