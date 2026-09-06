#include "store.h"
#include <utils/poll.h>

struct nlif_srepo_notif_work {
	struct upoll_worker base;
	struct nlif_gate *  gate;
};

#define nlif_srepo_assert_notif_work(_work) \
	nlif_assert(_work); \
	nlif_assert((_work)->base.dispatch); \
	nlif_gate_assert((_work)->gate)

#define NLIF_SREPO_INIT_NOTIF_WORK(_work, _gate) \
	{ \
		.base.dispatch = nlif_srepo_dispatch_notif, \
		.gate          = _gate, \
	}

struct nlif_srepo {
	sr_session_ctx_t *           sess;
	struct nlif_store            store;
	struct nlif_srepo_notif_work notif;
	struct nlif_gate             gate;
};

static
int
nlif_srepo_dispatch_notif(struct upoll_worker * worker,
                          uint32_t              state __unused,
                          const struct upoll *  poller __unused)
{
	nlif_assert(worker);
	nlif_assert(state);
	nlif_assert(!(state & EPOLLOUT));
	nlif_assert(!(state & EPOLLRDHUP));
	nlif_assert(!(state & EPOLLPRI));
	nlif_assert(!(state & EPOLLHUP));
	nlif_assert(!(state & EPOLLERR));
	nlif_assert(state & EPOLLIN);
	nlif_assert(poller);

	struct nlif_srepo_notif_work * notif = containerof(worker,
	                                                   typeof(*notif),
	                                                   base);

	nlif_srepo_assert_notif_work(notif);
	nlif_gate_notify(notif->gate);

	return 0;
}

static int
nlif_srepo_enable_notif(struct nlif_srepo_notif_work * worker,
                        struct nlif_store *            store,
                        const struct upoll *           poller)
{
	nlif_srepo_assert_notif_work(worker);
	nlif_store_assert(store);
	nlif_assert(poller);

	struct nlif_gate * gate = worker->gate;
	int                ret;
	const char *       msg __unused;

	ret = nlif_store_enable_notif(store, gate);
	if (ret) {
		msg = "cannot enable store notification";
		goto err;
	}

	ret = upoll_register(poller,
	                     nlif_gate_fd(gate),
	                     EPOLLIN,
	                     &worker->base);
	if (ret) {
		msg = "cannot enable polling";
		goto disable;
	}

	nlif_debug("notification worker enabled.");

	return 0;

disable:
	nlif_store_disable_notif(store, gate);
err:
	nlif_err("cannot enable notification worker: %s: %s.",
	         msg,
	         strerror(-ret));

	return ret;
}

static void
nlif_srepo_disable_notif(struct nlif_srepo_notif_work * worker,
                         struct nlif_store *            store,
                         const struct upoll *           poller)
{
	nlif_srepo_assert_notif_work(worker);
	nlif_store_assert(store);
	nlif_assert(poller);

	struct nlif_gate * gate = worker->gate;

	upoll_unregister(poller, nlif_gate_fd(gate));
	nlif_store_disable_notif(store, gate);

	nlif_debug("notification worker disabled.");
}

static int
nlif_srepo_open(struct nlif_srepo *  repo,
                sr_session_ctx_t *   session,
                const struct upoll * poller)
{
	nlif_assert(repo);
	nlif_assert(session);
	nlif_assert(poller);

	int err;

	err = nlif_gate_init(&repo->gate);
	if (err)
		goto close_poll;

	err = nlif_store_init(&repo->store);
	if (err)
		goto fini_gate;

	err = nlif_srepo_enable_notif(&repo->notif, &repo->store, poller);
	if (err)
		goto fini_store;

	err = nlif_store_load(&repo->store, &repo->gate);
	if (err)
		goto disable_notif;

	repo->sess = session;

	return 0;

disable_notif:
	nlif_srepo_disable_notif(&repo->notif, &repo->store, poller);
fini_store:
	nlif_store_fini(&repo->store);
fini_gate:
	nlif_gate_fini(&repo->gate);

	return err;
}

static void
nlif_srepo_close(struct nlif_srepo * repo, const struct upoll * poller)
{
	nlif_srepo_disable_notif(&repo->notif, &repo->store, poller);
	nlif_store_fini(&repo->store);
	nlif_gate_fini(&repo->gate);
}






FINISH ME!!








int
sr_plugin_init_cb(sr_session_ctx_t * session, void ** private)
{
	struct nlif_srplg_thr * thr;

	thr = nlif_malloc(sizeof(*thr));
	nlif_assert(thr);

	if (nlif_srplg_start_thr(&thr, session))
		goto free;

	*private = thr;

	SRPLG_LOG_INF(PLUGIN_NAME, "plugin initialized");

	return SR_ERR_OK;

free:
	nlif_free(thr);

	SRPLG_LOG_ERR(PLUGIN_NAME, "plugin initialization failed");

	return SR_ERR_INTERNAL;
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t * session __unused, void * private)
{
	struct nlif_srplg_thr * thr = private;

	nlif_srplg_stop_thr(thr);
	nlif_free(thr);

	SRPLG_LOG_INF(PLUGIN_NAME, "plugin cleaned up");
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

static int
nlif_srepo_init_plugin(sr_session_ctx_t * session, void ** private)
{
	struct nlif_srplg * plg;

	SRPLG_LOG_ERR(PLUGIN_NAME, "Implement me !!");

	return SR_ERR_UNSUPPORTED;

	plg = nlif_malloc(sizeof(*plg));

	*private = plg;

	SRPLG_LOG_DBG(PLUGIN_NAME, "plugin initialized.");
}

static void
nlif_srepo_cleanup_plugin(sr_session_ctx_t * session, void * private)
{
	struct nlif_srplg * plg = private;

	/* Finally, free memory allocated at plugin initialization. */
	nlif_free(plg);

	SRPLG_LOG_DBG(PLUGIN_NAME, "plugin cleaned up.");
}

#if defined(SRPLG_INSTANTIATE_AS_DAEMON)

struct srplg_repo {
	/* The Sysrepo running datastore session. */
	sr_session_ctx_t * run_sess;
	void *             priv;
};

#define srplg_repo_assert(_repo) \
	srplg_assert(_repo); \
	srplg_assert((_repo)->run_sess)

    sr_log_syslog("sysrepo-plugind", log_level); ??
	sr_log_stderr(SR_LL_DBG);

int
nlif_srepo_open(struct srplg_repo * srepo)
{
	sr_conn_ctx_t *    conn;
	sr_error_t         err;
	sr_session_ctx_t * sess = NULL;

	sr_log_stderr(SR_LL_DBG);

	/* Connect to sysrepo */
	err = sr_connect(SR_CONN_DEFAULT, &conn);
	if (err != SR_ERR_OK) {
		SRPLG_LOG_ERR(PLUGIN_NAME,
		              "cannot open datastore connection: %s",
		              sr_strerror(err));
		return -EPERM;
	}

	err = sr_session_start(conn, SR_DS_RUNNING, &sess);
	if (err != SR_ERR_OK) {
		SRPLG_LOG_ERR(PLUGIN_NAME,
		              "cannot open session to running datastore: %s",
		              sr_strerror(err));
		goto disconn;
	}

	err = nlif_srepo_init_plugin(sess, &srepo->priv);
	if (err != SR_ERR_OK) {
		SRPLG_LOG_ERR(PLUGIN_NAME,
		              "plugin intitialization failed: %s",
		              sr_strerror(err));
		goto disconnect;
	}

	return 0;

disconn:
	/* This also closes all sessions related to this connection. */
	sr_disconnect(conn);

	return -EPERM;
}

void
nlif_srepo_close(struct srplg_repo * srepo)
{
	srplg_repo_assert(repo);

	sr_conn_ctx_t * conn = sr_session_get_connection(srepo->run_sess);

	/* Cleanup plugin state. */
	nlif_srepo_cleanup_plugin(srepo->run_sess, srepo->priv);

	/* Close all sessions and connection to Sysrepo datastores. */
	sr_disconnect(conn);
}

#else  /* !defined(SRPLG_INSTANTIATE_AS_DAEMON) */

int
sr_plugin_init_cb(sr_session_ctx_t * session, void ** private)
{
	return nlif_srepo_init_plugin(session, private);
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t * session, void * private)
{
	nlif_srepo_cleanup_plugin(session, private);
}

#endif /* defined(SRPLG_INSTANTIATE_AS_DAEMON) */
