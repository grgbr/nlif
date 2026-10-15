struct nlif_srepo {
	sr_session_ctx_t *      sess;
	struct nlif_store       store;
	struct nlifd_notif_work notif;
	struct nlif_gate        gate;
	struct upoll            poll;
};

static int
nlif_srepo_process(const struct nlif_srepo * repo)
{
	return upoll_process(&repo->poll, -1);
}

static int
nlif_srepo_open(struct nlif_srepo * repo, sr_session_ctx_t * session)
{
	int err;

	err = upoll_open(&repo->poll, 2U);
	if (err) {
		nlif_err("cannot open poller: %s.", strerror(-err));
		return err;
	}

	err = nlif_gate_init(&repo->gate);
	if (err)
		goto close_poll;

	err = nlif_store_init(&repo->store);
	if (err)
		goto fini_gate;

	err = nlifd_enable_notif(&repo->notif, &repo->store, &repo->poll);
	if (err)
		goto fini_store;

	err = nlif_store_load(&repo->store, &repo->gate);
	if (err)
		goto disable_notif;

	repo->sess = session;

	return 0;

disable_notif:
	nlifd_disable_notif(&repo->notif, &repo->store, &repo->poll);
fini_store:
	nlif_store_fini(&repo->store);
fini_gate:
	nlif_gate_fini(&repo->gate);
close_poll:
	upoll_close(&repo->poll);

	return err;
}

static void
nlif_srepo_close(struct nlif_srepo * repo)
{
	nlifd_disable_notif(&repo->notif, &repo->store, &repo->poll);
	nlif_store_fini(&repo->store);
	nlif_gate_fini(&repo->gate);
	upoll_close(&repo->poll);
}

/******************************************************************************/

struct nlif_srplg_thr {
	volatile sig_atomic_t stop;
	struct nlif_srepo     repo;
	pthread_t             id;
};

static void *
nlif_srplg_process_thr(void * data)
{
	struct nlif_srplg_thr * thr = data;
	int                     ret;

	do {
		ret = nlif_srepo_process(&thr->repo);
	} while (!ret && !thr->stop);

	if (ret == -ESHUTDOWN)
		ret = 0;

	SRPLG_LOG_INF(PLUGIN_NAME, "thread exited with %d status", ret);

	uthr_exit(NULL);
}

static int
nlif_srplg_start_thr(struct nlif_srplg_thr * thread, sr_session_ctx_t * session)
{
	int            err;
	pthread_attr_t attr;
	sigset_t       msk = *usig_full_msk;

	err = nlif_srepo_open(&thread->repo, session);
	if (err)
		return err;

	thread->stop = 0;

	/* Make sure that spawned thread blocks all signals. */
	/*err = uthr_attr_init(&attr);*/
	err = pthread_attr_init(&attr);
	if (err) {
		nlif_assert(err = ENOMEM);
		abort();
	}
	/*err = uthr_attr_set_sigmask(attr, &msk);*/
	err = pthread_attr_setsigmask(&attr, &msk);
	if (err) {
		nlif_assert(err = ENOMEM);
		abort();
	}

	err = uthr_create(&thread->id, &attr, nlif_srplg_process_thr, repo);
	if (err) {
		SRPLG_LOG_ERR(PLUGIN_NAME,
		              "cannot spawn thread: %s",
		              strerror(-err));
		goto destroy;
	}

	uthr_attr_destroy(&attr);

	return 0;

destroy:
	uthr_attr_destroy(&attr);
close:
	nlif_srepo_close(&thread->repo);

	return err;
}

static void
nlif_srplg_stop_thr(struct nlif_srplg_thr * thread)
{
	int err __unused;

	thread->stop = 1;
#warning TODO: implement eventfd base wake up logic !!!

	err = uthr_join(thread->id, NULL);
	nlif_assert(!err);
}

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
