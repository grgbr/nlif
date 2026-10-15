#include "repo.h"
#include "lib/iface.h"
#include "srplug/daemon.h"
#include "srepo/schema.h"
#include "srepo/data.h"
#include "utils/string.h"
#include <sysrepo/xpath.h>
#include <sysexits.h>

/******************************************************************************
 * Nlif sysrepo specific implementation.
 ******************************************************************************/

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"

#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces"

static const char *
nlifd_iface_type_str(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	switch (nlif_iface_type(interface)) {
	case ARPHRD_ETHER:
		{
			const char * kind = nlif_iface_kind(interface);

			if (!kind)
				return "iana-if-type:ethernetCsmacd";
			else if (!strcmp(kind, "bridge"))
				return "iana-if-type:bridge";
			else if (!strcmp(kind, "dummy"))
				return "iana-if-type:other";
		}
		break;

	case ARPHRD_LOOPBACK:
		return "iana-if-type:softwareLoopback";

	default:
		break;
	}

	/* Unsupported. */
	return NULL;
}

static sr_error_t
nlifd_iface_new_entry(const struct ly_ctx * context,
                      struct lyd_node *     container,
                      struct nlif_iface *   interface,
                      struct lyd_node **    entry)
{
	srplug_assert(context);
	srplug_assert(container);
	srplug_assert(interface);
	srplug_assert(nlif_iface_state(interface) == NLIF_CLEAN_STAT);

	struct lyd_node * ent;
	const char *      str;
	int               ret;

	ret = srepo_dat_create_list_keyent(context,
	                                   container,
	                                   "interface",
	                                   "name",
	                                   nlif_iface_name(interface),
	                                   &ent);
	if (ret != SR_ERR_OK)
		return ret;

	/* TODO: replace "false" with default schema value if existing. */
	ret = srepo_dat_create_leaf(ent, "enabled", "false", NULL);
	if (ret != SR_ERR_OK)
		return ret;

	str = nlifd_iface_type_str(interface);
	if (!str) {
		srplug_node_info(ent, "unsupported interface type");
		return SR_ERR_UNSUPPORTED;
	}
	ret = srepo_dat_create_leaf(ent, "type", str, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	if (entry)
		*entry = ent;

	return SR_ERR_OK;
}

#if 0
static int
nlifd_iface_admstate_str(struct nlif_iface * interface, const char ** state)
{
	nlif_iface_assert(interface);
	nlif_assert(state);

	bool up;
	int  ret;

	ret = nlif_iface_admstate(interface, &up);
	if (ret)
		/* TODO: log a message. */
		return ret;

	*state = up ?  "up" : "down";

	return 0;
}

static const char *
nlifd_iface_name_from_xpath(char * xpath, char name[IFNAMSIZ])
{
	srplug_assert(xpath);
	srplug_assert(name);

	sr_xpath_ctx_t ctx;
	const char *   str;
	size_t         len;
	const char *   msg;

	str = sr_xpath_key_value(xpath, "interface", "name", &ctx);
	if (!str) {
		msg = "invalid interface node path";
		goto err;
	}

	len = strnlen(str, IFNAMSIZ);
	if (!len) {
		msg = "missing interface name";
		goto err;
	}

	if (len == IFNAMSIZ) {
		msg = "interface name too long";
		goto err;
	}

	memcpy(name, str, len);
	name[len] = '\0';

	return name;

err:
	sr_xpath_recover(&ctx);
	srplug_path_debug(xpath, "%s", msg);

	return NULL;
}

static struct nlif_iface *
nlifd_iface_from_xpath(char *                   xpath,
                       const struct nlif_repo * repository)
{
	srplug_assert(xpath);
	nlif_repo_assert(repository);

	char                      name[IFNAMSIZ];
	const struct nlif_store * store = nlif_repo_store(repository);
	struct nlif_iface *       iface;

	if (!nlifd_iface_name_from_xpath(xpath, name))
		return NULL;

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface)
		srplug_info("'%s': no such interface", name);

	return iface;
}

static const char *
nlifd_iface_name_from_node(const struct lyd_node * node,
                           char                    name[IFNAMSIZ])
{
	srplug_assert(node);
	srplug_assert(name);

	char *       path;
	const char * str;

	path = srepo_dat_path(node);
	str = nlifd_iface_name_from_xpath(path, name);
	nlif_free(path);

	return str;
}

static struct nlif_iface *
nlifd_iface_from_node(const struct lyd_node *  term,
                      const struct nlif_repo * repository)
{
	srplug_assert(term);
	srplug_assert(term->schema);
	srplug_assert(term->schema->nodetype & LYD_NODE_TERM);
	nlif_repo_assert(repository);

	char                      name[IFNAMSIZ];
	const struct nlif_store * store = nlif_repo_store(repository);
	struct nlif_iface *       iface;

	if (!nlifd_iface_name_from_node(term, name))
		return NULL;

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface)
		srplug_info("'%s': no such interface", name);

	return iface;
}

#if 0
static int
nlifd_on_iface_get_admin_status(sr_session_ctx_t * session,
                                uint32_t           sub_id __unused,
                                const char *       module __unused,
                                const char *       path __unused,
                                const char *       request_xpath,
                                uint32_t           op_id __unused,
                                struct lyd_node ** parent,
                                void *             repository)
{
	srplug_assert(*parent);
	srplug_assert(!strcmp(LYD_NAME(*parent), "interface"));
	nlif_repo_assert(repository);

	char                      name[IFNAMSIZ];
	const struct nlif_iface * iface;
	const struct ly_ctx *     ctx;
	int                       ret;

	/*SR_ERR_CALLBACK_FAILED ?? */
	/* TODO: check that feature is enabled. */

	if (!nlifd_iface_name_from_node(*parent, name)) {
		nlif_info("'%s': invalid interface path", request_xpath);
		return SR_ERR_INVAL_ARG; /*SR_ERR_NOT_FOUND ??*/
	}

	iface = nlif_store_find_iface_byname(nlif_repo_store(repository), name);
	if (!iface) {
		nlif_info("'%s': no such interface", name);
		return SR_ERR_NOT_FOUND;
	}

	/* TODO: check ctx != NULL */
	ctx = sr_session_acquire_context(session);
	ret = srepo_dat_create_leaf(*parent,
	                            "admin-status", /* path?? */
	                            nlifd_iface_admstate_str(iface),
	                            NULL);
	if (ret != SR_ERR_OK)
		goto release;

	ret = SR_ERR_OK;

release:
	sr_session_release_context(session);

	return ret;
}

static const struct srplug_sub nlifd_iface_admin_status_sub = {
	.kind = SRPLUG_OPER_SUB_KIND,
	.oper = {
		.module  = NLIFD_IETF_IFACE_YANG_MODULE,
		.xpath   = NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface/admin-status",
		.on_get  = nlifd_on_iface_get_admin_status,
		.options = SR_SUBSCR_DEFAULT /* | SR_SUBSCR_OPER_MERGE */
	}
};
#endif

static sr_error_t
nlifd_iface_change_type(const struct lyd_node * leaf,
                        sr_change_oper_t        oper,
                        const char *            old,
                        void *                  repository)
{
	srplug_assert(leaf);
	nlif_repo_assert((const struct nlif_repo *)repository);

	const char *        val = srepo_dat_node_as_str(leaf);
	struct nlif_iface * iface;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot change interface type: "
		                   "no such interface");
		return SR_ERR_NOT_FOUND;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		if (!strcmp(nlifd_iface_type_str(iface), val))
			return SR_ERR_OK;
		break;

	case SR_OP_MODIFIED:
		srplug_assert(old);
		if (!strcmp(nlifd_iface_type_str(iface), val))
			return SR_ERR_OK;
		break;

	case SR_OP_DELETED:
		srplug_assert(!old);
		return SR_ERR_OK;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
	}

	srplug_node_notice(leaf, "cannot change interface type");

	return SR_ERR_UNSUPPORTED;
}

static int
nlifd_on_iface_type_change(sr_session_ctx_t * session,
                           uint32_t           sub_id __unused,
                           const char *       module __unused,
                           const char *       xpath,
                           sr_event_t         event,
                           uint32_t           request_id __unused,
                           void *             repository)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_repo_assert((const struct nlif_repo *)repository);

	switch (event) {
	case SR_EV_ENABLED:
	case SR_EV_CHANGE:
		return srepo_dat_foreach_change(session,
		                                xpath,
		                                nlifd_iface_change_type,
		                                repository);

	case SR_EV_DONE:
	case SR_EV_ABORT:
		return SR_ERR_OK;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}
}

static sr_error_t
nlifd_iface_change_enabled(const struct lyd_node * leaf,
                           sr_change_oper_t        oper,
                           const char *            old,
                           void *                  repository)
{
	srplug_assert(leaf);
	nlif_repo_assert((const struct nlif_repo *)repository);

	bool                val;
	struct nlif_iface * iface;
	int                 ret;

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		val = srepo_dat_node_as_bool(leaf);
		break;

	case SR_OP_MODIFIED:
		srplug_assert(old);
		val = srepo_dat_node_as_bool(leaf);
		break;

	case SR_OP_DELETED:
		srplug_assert(!old);
		if (srepo_dat_node_dflt_as_bool(leaf, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
	}

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot %s interface: no such interface",
		                   val ? "enable" : "disable");
		return SR_ERR_NOT_FOUND;
	}

	nlif_iface_set_admstate(iface, val);
	ret = nlif_iface_apply(iface);
	if (!ret)
		return SR_ERR_OK;

	nlif_iface_reload(iface);

	srplug_node_notice(leaf,
	                   "cannot %s interface: %s",
	                   val ? "enable" : "disable",
	                   strerror(-ret));

	return SR_ERR_CALLBACK_FAILED;
}

static sr_error_t
nlifd_iface_abort_enabled(const struct lyd_node * leaf,
                          sr_change_oper_t        oper,
                          const char *            old,
                          void *                  repository)
{
	srplug_assert(leaf);
	nlif_repo_assert((const struct nlif_repo *)repository);

	bool                val;
	struct nlif_iface * iface;
	int                 ret;

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		if (srepo_dat_node_dflt_as_bool(leaf, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MODIFIED:
	case SR_OP_DELETED:
		srplug_assert(old);
		ret = ustr_parse_bool(old, &val);
		if (ret) {
			srplug_node_warn(leaf,
			                 "cannot abort interface configuration:"
			                 " '%s': invalid abort value",
			                 old);
			return SR_ERR_INVAL_ARG;
		}
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
	}

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_warn(leaf,
		                 "cannot abort interface configuration: "
		                 "no such interface");
		return SR_ERR_NOT_FOUND;
	}

	nlif_iface_set_admstate(iface, val);
	ret = nlif_iface_apply(iface);
	if (!ret)
		return SR_ERR_OK;

	nlif_iface_reload(iface);

	srplug_node_warn(leaf,
	                 "cannot abort interface configuration: %s",
	                 strerror(-ret));

	return SR_ERR_CALLBACK_FAILED;
}

static int
nlifd_on_iface_enabled_change(sr_session_ctx_t * session,
                              uint32_t           sub_id __unused,
                              const char *       module __unused,
                              const char *       xpath,
                              sr_event_t         event,
                              uint32_t           request_id __unused,
                              void *             repository)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_repo_assert((const struct nlif_repo *)repository);

	switch (event) {
	case SR_EV_ENABLED:
	case SR_EV_CHANGE:
		return srepo_dat_foreach_change(session,
		                                xpath,
		                                nlifd_iface_change_enabled,
		                                repository);

	case SR_EV_DONE:
		return SR_ERR_OK;

	case SR_EV_ABORT:
		return srepo_dat_foreach_change(session,
		                                xpath,
		                                nlifd_iface_abort_enabled,
		                                repository);

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}
}

#endif

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

static sr_error_t
nlifd_print_change(const struct lyd_node * node,
                   sr_change_oper_t        oper,
                   const char *            old,
                   void *                  data __unused)
{
	srplug_assert(node);

	const char * op;

	switch (oper) {
	case SR_OP_CREATED:
		op = "create";
		break;

	case SR_OP_MODIFIED:
		op = "modify";
		break;

	case SR_OP_DELETED:
		op = "delete";
		break;

	case SR_OP_MOVED:
		op = "move";
		break;

	default:
		srplug_assert(0);
	}

	srplug_node_debug(node, "change event: op=%s new='%s' old='%s'",
	                  op,
	                  srepo_dat_node_as_str(node),
	                  old);
	return SR_ERR_OK;
}

struct srplug_change_handler {
	const char *              name;
	srepo_dat_handle_change * handle;
};

static const struct srplug_change_handler nlifd_iface_change_handlers[] = {
	{ .name = "enabled", .handle = nlifd_iface_change_enabled },
	{ .name = "type",    .handle = nlifd_iface_change_type }
};

struct srplug_change_dispatch {
	unsigned int                         nr;
	const struct srplug_change_handler * hndlrs;
	void *                               data;
};

static sr_error_t
srplug_dispatch_change(const struct lyd_node *               node,
                       sr_change_oper_t                      oper,
                       const char *                          xpath,
                       const struct srplug_change_dispatch * dispatch)
{
	unsigned int   h;
	sr_xpath_ctx_t ctx;
	const char *   base;

	base = sr_xpath_node_name(xpath);
	srplug_assert(base);
	srplug_assert(base[0]);

	for (h = 0; h < dispatch->nr; h++) {
		const struct srplug_change_handler * hndl =
			&dispatch->hndlrs[h];

		if (!strcmp(base, hndl->name))
			return hndl->handle(node, oper, xpath, dispatch->data);
	}

	srplug_path_debug(xpath, "change handler not found: ignoring.");

	return SR_ERR_OK;
}

sr_error_t
srplug_handle_changes(sr_session_ctx_t *                   session,
                      const char *                         xpath,
                      const struct srplug_change_handler * handlers,
                      unsigned int                         nr,
                      void *                               data)
{
	const struct srplug_change_dispatch disp = {
		.nr     = nr,
		.hndlrs = handlers,
		.data   = data
	};

	return srepo_dat_foreach_change(session,
	                                xpath,
	                                srplug_dispatch_change,
	                                &disp);
}

static int
nlifd_on_iface_change(sr_session_ctx_t * session,
                      uint32_t           sub_id __unused,
                      const char *       module __unused,
                      const char *       xpath,
                      sr_event_t         event,
                      uint32_t           request_id __unused,
                      void *             repository)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_repo_assert((const struct nlif_repo *)repository);

	static const struct srplug_change_handler hndl[] = {

	};

	switch (event) {
	case SR_EV_ENABLED:
	case SR_EV_CHANGE:
		nlifd_iface_process_changes();
		evt = "change";
		break;

	case SR_EV_DONE:
		srplug_path_debug(xpath,
		                  "configuration changes processing completed");
		return SR_ERR_OK;

	case SR_EV_ABORT:
		evt = "abort";
		break;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}

	srplug_path_debug(xpath, "begin change: %s", evt);
	srepo_dat_foreach_change(session,
	                         xpath,
	                         nlifd_print_change,
	                         NULL);
	srplug_path_debug(xpath, "end event: %s", evt);

	return SR_ERR_CALLBACK_FAILED;
}

static int
nlifd_on_iface_top_change(sr_session_ctx_t * session,
                      uint32_t           sub_id __unused,
                      const char *       module __unused,
                      const char *       xpath,
                      sr_event_t         event,
                      uint32_t           request_id __unused,
                      void *             repository)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_repo_assert((const struct nlif_repo *)repository);

	const char * evt;

	switch (event) {
	case SR_EV_ENABLED:
		evt = "enable";
		break;

	case SR_EV_CHANGE:
		evt = "change";
		break;

	case SR_EV_DONE:
		evt = "done";
		break;

	case SR_EV_ABORT:
		evt = "abort";
		break;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}

	struct ly_out * out;
	sr_data_t * data = NULL;

	ly_out_new_fd(STDERR_FILENO, &out);

	sr_get_data(session, xpath, 0, 0, 0, &data);
	lyd_print_all(out, data->tree, LYD_JSON,  LYD_PRINT_EMPTY_LEAF_LIST);
	sr_release_data(data);

	srplug_path_debug(xpath, "begin child changes: %s", evt);
	srepo_dat_foreach_change(session,
	                         "/ietf-interfaces:interfaces/interface/*",
	                         nlifd_print_change,
	                         NULL);
	srplug_path_debug(xpath, "end child changes: %s", evt);

	srplug_path_debug(xpath, "begin top changes: %s", evt);
	srepo_dat_foreach_change(session,
	                         "/ietf-interfaces:interfaces/interface[name='dummy2']/*",
	                         nlifd_print_change,
	                         NULL);
	srplug_path_debug(xpath, "end top changes: %s", evt);

	ly_out_free(out, NULL, 0);

	return SR_ERR_CALLBACK_FAILED;
}

static sr_error_t
nlifd_iface_reset_dstore(sr_session_ctx_t *       session,
                         const struct ly_ctx *    context,
                         const struct nlif_repo * repository)
{
	srplug_assert(session);
	srplug_assert(context);
	nlif_repo_assert(repository);

	struct lyd_node *              top;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;
	int                            err;

	err = srepo_dat_create_container(context,
	                                 NULL,
	                                 NLIFD_IETF_IFACE_YANG_ROOT_PATH,
	                                 &top);
	if (err)
		return err;

	nlif_store_foreach_iface(nlif_repo_store(repository), hndl, iface) {
		err = nlifd_iface_new_entry(context, top, iface, NULL);
		if (err)
			goto free;
	}

	/*
	 * Here, `top' tree ownership is transfered to srepo_replace_dstore()
	 * so that it will be freed once it has returned thanks to a call to
	 * lyd_free_all().
	 * This means we are not requested to call lyd_free_tree() on it from
	 * now on.
	 */
	return srepo_replace_dstore(session, NLIFD_IETF_IFACE_YANG_MODULE, top);

free:
	lyd_free_tree(top);

	return err;
}

static const struct srplug_sub nlifd_subs[] = {
#if 0
	SRPLUG_CHANGE_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                  NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface/type",
	                  nlifd_on_iface_type_change,
	                  0,
	                  SR_SUBSCR_DEFAULT/*| SR_SUBSCR_ENABLED*/),
	SRPLUG_CHANGE_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                  NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface/enabled",
	                  nlifd_on_iface_enabled_change,
	                  0,
	                  SR_SUBSCR_DEFAULT/*| SR_SUBSCR_ENABLED*/)
#endif
	SRPLUG_CHANGE_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                  NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface",
	                  nlifd_on_iface_top_change,
	                  0,
	                  SR_SUBSCR_DEFAULT/*| SR_SUBSCR_ENABLED*/),
};

/******************************************************************************
 * Top-level logic.
 ******************************************************************************/

static int
nlifd_load(const struct srplug_daemon * daemon,
           const struct nlif_repo *     repository)
{
	sr_session_ctx_t *    sess = srplug_daemon_session(daemon);
	const struct ly_ctx * ctx;
	int                   ret;

	ret = srplug_acquire_context(sess, &ctx);
	if (ret != SR_ERR_OK)
		goto err;

	ret = SR_ERR_NOT_FOUND;
	if (!srplug_find_module(ctx, "ietf-interfaces"))
		goto release;
	if (!srplug_find_module(ctx, "iana-if-type"))
		goto release;

	ret = nlifd_iface_reset_dstore(sess, ctx, repository);
	if (ret != SR_ERR_OK)
		goto release;

	srepo_release_context(sess);

	return 0;

release:
	srepo_release_context(sess);
err:
	srplug_err("cannot load interfaces datastore: %s", sr_strerror(ret));

	return -EPERM;
}

static void
nlifd_setup(int argc, char * argv[], struct elog ** logger)
{
	static struct srplug_daemon_cmdln cmdln = {
		.brief = "Network interface management daemon.",
		.nr    = 0,
		.opts  = NULL
	};

	struct srplug_daemon_conf *       cfg;
	int                               ret;

	cfg = srplug_daemon_alloc_conf();

	ret = srplug_daemon_cmdln_parse(argc, argv, &cmdln, cfg);
	switch (ret) {
	case 0:
		/* Success: keep moving... */
		*logger = srplug_daemon_create_log(cfg);
		nlif_log_setup(*logger);
		nlif_free(cfg);
		return;

	case 1:
		/* Help informations have been requested on the command line. */
		ret = EXIT_SUCCESS;
		break;

	default:
		/* Command line syntax or parsing error. */
		ret = EX_USAGE;
	}

	nlif_free(cfg);

	exit(ret);
}

int
main(int argc, char * argv[])
{
	int                  ret;
	struct elog *        log;
	struct srplug_daemon dmn;
	struct nlif_repo     repo;

	nlifd_setup(argc, argv, &log);

	ret = srplug_daemon_open(&dmn, 1U);
	if (ret)
		goto fini_log;

	ret = nlif_repo_open(&repo, srplug_daemon_poller(&dmn));
	if (ret)
		goto close_dmn;

	ret = nlifd_load(&dmn, &repo);
	if (ret)
		goto close_repo;

	ret = srplug_daemon_subscribe_all(&dmn,
	                                  nlifd_subs,
	                                  stroll_array_nr(nlifd_subs),
	                                  &repo);
	if (ret)
		goto close_repo;

	ret = srplug_daemon_poll(&dmn);
	if (ret == -ESHUTDOWN)
		ret = 0;

close_repo:
	nlif_repo_close(&repo, srplug_daemon_poller(&dmn));
close_dmn:
	srplug_daemon_close(&dmn);
fini_log:
	srplug_daemon_destroy_log(log);

	return !ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
