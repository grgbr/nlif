struct nlif_srplg {
	/* Holds all Sysrepo subscriptions. */
	sr_subscription_ctx_t * subs;
};

static int
nlif_srepo_init_plugin(sr_session_ctx_t * session, void ** private)
{
	SRPLG_LOG_ERR(PLUGIN_NAME, "Implement me !!");

	return SR_ERR_UNSUPPORTED;

	SRPLG_LOG_DBG(PLUGIN_NAME, "plugin initialized.");
}

void
nlif_srepo_cleanup_plugin(sr_session_ctx_t * session, void * private)
{
	struct nlif_srplg * plg = private;

	/* Free all datastore subscriptions. */
	sr_unsubscribe(plg->subs);

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

int
nlif_srepo_open(struct srplg_repo * srepo)
{
	sr_conn_ctx_t * conn;
	sr_error_t      err;

	sr_log_stderr(SR_LL_DBG);

	/* Connect to sysrepo */
	err = sr_connect(SR_CONN_DEFAULT, &conn);
	if (err != SR_ERR_OK) {
		SRPLG_LOG_ERR(PLUGIN_NAME,
		              "cannot open datastore connection: %s",
		              sr_strerror(err));
		return -EPERM;
	}

	err = sr_session_start(conn, SR_DS_RUNNING, &srepo->run_sess);
	if (err != SR_ERR_OK) {
		SRPLG_LOG_ERR(PLUGIN_NAME,
		              "cannot open session to running datastore: %s",
		              sr_strerror(err));
		goto disconn;
	}

	err = nlif_srepo_init_plugin(srepo->run_sess, &srepo->priv);
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
