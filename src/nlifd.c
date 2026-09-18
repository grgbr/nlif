#include "repo.h"
#include "lib/iface.h"
#include "srplug/daemon.h"
#include "srepo/schema.h"
#include "srepo/data.h"
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
nlifd_iface_admstate_str(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	if (nlif_iface_admstate(interface))
		return "up";
	else
		return "down";
}

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

static const char *
nlifd_iface_name_from_xpath(char * xpath, char name[IFNAMSIZ])
{
	nlif_assert(xpath);
	nlif_assert(name);

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
	srplug_path_info(xpath, "%s", msg);

	return NULL;
}

static struct nlif_iface *
nlifd_iface_from_xpath(char *                   xpath,
                       const struct nlif_repo * repository)
{
	nlif_assert(xpath);
	nlif_repo_assert(repository);

	char                      name[IFNAMSIZ];
	const struct nlif_store * store = nlif_repo_store(repository);
	struct nlif_iface *       iface;

	if (!nlifd_iface_name_from_xpath(xpath, name)) {
		srplug_path_notice(xpath, "invalid interface path");
		return NULL;
	}

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface) {
		srplug_notice("'%s': no such interface", name);
		return NULL;
	}

	return iface;
}

static const char *
nlifd_iface_name_from_node(const struct lyd_node * node,
                           char                    name[IFNAMSIZ])
{
	nlif_assert(node);
	nlif_assert(name);

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
	nlif_assert(term);
	nlif_assert(term->schema);
	nlif_assert(term->schema->nodetype & LYD_NODE_TERM);
	nlif_repo_assert(repository);

	char                      name[IFNAMSIZ];
	const struct nlif_store * store = nlif_repo_store(repository);
	struct nlif_iface *       iface;

	if (!nlifd_iface_name_from_node(term, name)) {
		srplug_node_notice(term, "invalid interface path");
		return NULL;
	}

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface) {
		srplug_notice("'%s': no such interface", name);
		return NULL;
	}

	return iface;
}

static sr_error_t
nlifd_iface_new_entry(const struct ly_ctx *     context,
                      struct lyd_node *         container,
                      const struct nlif_iface * interface,
                      struct lyd_node **        entry)
{
	nlif_assert(context);
	nlif_assert(container);
	nlif_assert(interface);

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
nlifd_on_iface_get_admin_status(sr_session_ctx_t * session,
                                uint32_t           sub_id __unused,
                                const char *       module __unused,
                                const char *       path __unused,
                                const char *       request_xpath,
                                uint32_t           op_id __unused,
                                struct lyd_node ** parent,
                                void *             repository)
{
	nlif_assert(*parent);
	nlif_assert(!strcmp(LYD_NAME(*parent), "interface"));
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

static int
nlifd_on_iface_type_change(sr_session_ctx_t * session __unused,
                           uint32_t           sub_id __unused,
                           const char *       module __unused,
                           const char *       xpath,
                           sr_event_t         event,
                           uint32_t           request_id __unused,
                           void *             repository __unused)
{
	switch (event) {
	case SR_EV_ENABLED:
		srplug_debug("%s: SR_EV_ENABLED", xpath);
		return SR_ERR_OK;

	case SR_EV_CHANGE:
		srplug_debug("%s: SR_EV_CHANGE", xpath);
		return SR_ERR_OK;

	case SR_EV_DONE:
		srplug_debug("%s: SR_EV_DONE", xpath);
		return SR_ERR_OK;

	case SR_EV_ABORT:
		srplug_info("'%s': SR_EV_ABORT", xpath);
		return SR_ERR_OK;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		nlif_assert(0);
	}
}

static sr_error_t
nlifd_iface_change_enabled(struct nlif_iface *     interface,
                           sr_change_oper_t        oper,
                           const struct lyd_node * node,
                           const char *            old)
{
	nlif_iface_assert(interface);
	nlif_assert(node);

	bool val;
	int  ret;

	switch (oper) {
	case SR_OP_CREATED:
		nlif_assert(!old);
		val = srepo_dat_node_as_bool(node);
		break;

	case SR_OP_MODIFIED:
		nlif_assert(old);
		val = srepo_dat_node_as_bool(node);
		break;

	case SR_OP_DELETED:
		nlif_assert(!old);
		if (srepo_dat_node_dflt_as_bool(node, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
	}

	nlif_iface_set_admstate(interface, val);
	ret = nlif_iface_save(interface);
	if (!ret) {
		srplug_debug("'%s': interface set %s",
		             nlif_iface_name(interface),
		             val ? "up" : "down");
		return SR_ERR_OK;
	}

	srplug_notice("'%s': cannot set interface %s: %s",
	              nlif_iface_name(interface),
	              val ? "up" : "down",
	              strerror(-ret));

	return SR_ERR_CALLBACK_FAILED;
}

typedef sr_error_t nlifd_iface_handle_change_fn(struct nlif_iface *,
                                                sr_change_oper_t,
                                                const struct lyd_node *,
                                                const char *);

static sr_error_t
nlifd_iface_handle_changes(sr_session_ctx_t *             session,
                           const char *                   xpath,
                           struct nlif_repo *             repository,
                           nlifd_iface_handle_change_fn * handle)
{
	nlif_assert(session);
	nlif_assert(xpath);
	nlif_assert(xpath[0]);
	nlif_repo_assert(repository);

	sr_change_iter_t *      iter;
	sr_change_oper_t        oper;
	const struct lyd_node * node;
	const char *            old;
	sr_error_t              ret;

	ret = sr_get_changes_iter(session, xpath, &iter);
	if (ret != SR_ERR_OK) {
		srplug_assert(ret != SR_ERR_INVAL_ARG);

		if (ret == SR_ERR_NO_MEMORY)
			abort();

		return ret;
	}

	ret = sr_get_change_tree_next(session,
	                              iter,
	                              &oper,
	                              &node,
	                              &old,
	                              NULL,
	                              NULL);
	while (ret == SR_ERR_OK) {
		struct nlif_iface * iface;

		iface = nlifd_iface_from_node(node, repository);
		if (!iface) {
			ret = SR_ERR_NOT_FOUND;
			goto err;
		}

		ret = handle(iface, oper, node, old);
		if (ret != SR_ERR_OK)
			goto err;

		ret = sr_get_change_tree_next(session,
		                              iter,
		                              &oper,
		                              &node,
		                              &old,
		                              NULL,
		                              NULL);
	}

	sr_free_change_iter(iter);

	return SR_ERR_OK;

err:
	sr_free_change_iter(iter);

	return ret;
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
	switch (event) {
	case SR_EV_CHANGE:
		return nlifd_iface_handle_changes(session,
		                                  xpath,
		                                  repository,
		                                  nlifd_iface_change_enabled);

	case SR_EV_DONE:
		srplug_debug("%s: SR_EV_DONE", xpath);
		return SR_ERR_OK;

	case SR_EV_ABORT:
		srplug_info("'%s': SR_EV_ABORT", xpath);
		return SR_ERR_OK;

	case SR_EV_ENABLED:
	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		nlif_assert(0);
	}
}

static sr_error_t
nlifd_iface_reset_dstore(sr_session_ctx_t *       session,
                         const struct ly_ctx *    context,
                         const struct nlif_repo * repository)
{
	nlif_assert(session);
	nlif_assert(context);
	nlif_repo_assert(repository);

	struct lyd_node *              top;
	const struct nlif_store_hndl * hndl;
	const struct nlif_iface *      iface;
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
	                  SR_SUBSCR_DEFAULT/* SR_SUBSCR_ENABLED*/),
#endif
	SRPLUG_CHANGE_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                  NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface/enabled",
	                  nlifd_on_iface_enabled_change,
	                  0,
	                  SR_SUBSCR_DEFAULT)
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
