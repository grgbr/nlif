#include "repo.h"
#include "lib/iface.h"
#include <srutils/srplug/daemon.h>
#include <srutils/srplug/data.h>
#include <srutils/srepo/schema.h>
#include "utils/string.h"
#include <sysrepo/xpath.h>
#include <sysexits.h>

/******************************************************************************
 * Various helpers.
 ******************************************************************************/

static sr_error_t
nlifd_iface_error(int error)
{
	nlif_assert(error <= 0);

	switch (error) {
	case 0:
		return SR_ERR_OK;
	case -ENODEV:
	case -ENOENT:
		return SR_ERR_NOT_FOUND;
	case -EEXIST:
		return SR_ERR_EXISTS;
	case -EINVAL:
		return SR_ERR_INVAL_ARG;
	case -ENOTSUP:
		return SR_ERR_UNSUPPORTED;
	case -EPERM:
		return SR_ERR_OPERATION_FAILED;
	case -EACCES:
		return SR_ERR_UNAUTHORIZED;
	case -ETIME:
	case -ETIMEDOUT:
		return SR_ERR_TIME_OUT;
	case -ENOLCK:
	case -EDEADLOCK:
		return SR_ERR_LOCKED;
	case -EAGAIN:
		return SR_ERR_CALLBACK_SHELVE;
	case -ENOMEM:
		return SR_ERR_NO_MEMORY;
	case -EIO:
	default:
		return SR_ERR_SYS;
	}
}

/******************************************************************************
 * XPATH utilities.
 ******************************************************************************/

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

#if 0
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
#endif

static const char *
nlifd_iface_name_from_node(const struct lyd_node * node,
                           char                    name[IFNAMSIZ])
{
	srplug_assert(node);
	srplug_assert(name);

	char *       path;
	const char * str;

	path = srplug_dat_path(node);
	str = nlifd_iface_name_from_xpath(path, name);
	srplug_free(path);

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

/******************************************************************************
 * Netlink interface fields adapters.
 ******************************************************************************/

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"

#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces"

static const char *
nlifd_iface_type_str(unsigned short type, const char * kind)
{
	switch (type) {
	case ARPHRD_ETHER:
		{
			if (!kind)
				return "iana-if-type:ethernetCsmacd";
			else if (!strcmp(kind, "bridge"))
				return "iana-if-type:bridge";
			else if (!strcmp(kind, "dummy"))
				return "iana-if-type:other";
			break;
		}

	case ARPHRD_LOOPBACK:
		return "iana-if-type:softwareLoopback";

	default:
		break;
	}

	/* Unsupported. */
	return NULL;
}

static int
nlifd_iface_coherent_type_str(struct nlif_iface * interface, const char ** type)
{
	unsigned short tp;
	int            ret;
	const char *   str;

	ret = nlif_iface_coherent_type(interface, &tp);
	if (ret)
		return ret;

	str = nlifd_iface_type_str(tp, nlif_iface_kind(interface));
	if (!str)
		return -ENOTSUP;

	*type = str;

	return 0;
}

/******************************************************************************
 * Change subscription events handling.
 ******************************************************************************/

static sr_error_t
nlifd_iface_change_enabled(const struct lyd_node * leaf,
                           sr_change_oper_t        oper,
                           const char *            old,
                           void *                  repository)
{
	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "no such interface");
		return SR_ERR_NOT_FOUND;
	}

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
		srplug_assert(old);
		if (srepo_dat_node_dflt_as_bool(leaf, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_UNSUPPORTED;
	}

	ret = nlif_iface_set_admstate(iface, val);
	if (ret) {
		srplug_node_notice(leaf,
		                   "cannot change interface: %s",
		                   strerror(-ret));
		return nlifd_iface_error(ret);
	}

	srplug_node_debug(leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_enabled(const struct lyd_node * leaf,
                            sr_change_oper_t        oper,
                            const char *            old,
                            void *                  repository)
{
	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_err(leaf,
		                "cannot restore interface: no such interface");
		return SR_ERR_OK;
	}

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
			srplug_node_err(leaf,
			                "cannot restore interface: "
			                "'%s': invalid value",
			                old);
			return SR_ERR_OK;
		}
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_OK;
	}

	ret = nlif_iface_set_admstate(iface, val);
	if (ret)
		srplug_node_err(leaf,
		                "cannot restore interface: %s",
		                strerror(-ret));
	else
		srplug_node_debug(leaf, "interface restored");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_change_type(const struct lyd_node * leaf,
                        sr_change_oper_t        oper,
                        const char *            old,
                        void *                  repository)
{
	struct nlif_iface * iface;
	const char *        val = srepo_dat_node_as_str(leaf);
	const char *        type;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "no such interface");
		return SR_ERR_NOT_FOUND;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		break;

	case SR_OP_MODIFIED:
		srplug_assert(old);
		break;

	case SR_OP_DELETED:
		srplug_assert(old);
		return SR_ERR_OK;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_UNSUPPORTED;
	}

	ret = nlifd_iface_coherent_type_str(iface, &type);
	if (ret) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "failed to retrieve type: %s",
		                   strerror(-ret));
		return nlifd_iface_error(ret);
	}

	if (strcmp(type, val)) {
		srplug_node_notice(leaf,
		                   "cannot change interface: %s",
		                   sr_strerror(SR_ERR_UNSUPPORTED));
		return SR_ERR_UNSUPPORTED;
	}

	srplug_node_debug(leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_type(const struct lyd_node * leaf,
                         sr_change_oper_t        oper __unused,
                         const char *            old __unused,
                         void *                  repository __unused)
{
	srplug_node_debug(leaf, "interface restored");

	return SR_ERR_OK;
}

static const struct srplug_change_hndlr nlifd_iface_change_hndlrs[] = {
	{ .name = "enabled", .handle = nlifd_iface_change_enabled },
	{ .name = "type",    .handle = nlifd_iface_change_type }
};

static const struct srplug_change_hndlr nlifd_iface_restore_hndlrs[] = {
	{ .name = "enabled", .handle = nlifd_iface_restore_enabled },
	{ .name = "type",    .handle = nlifd_iface_restore_type }
};

static sr_error_t
nlifd_iface_process_aborts(sr_session_ctx_t * session,
                           const char *       xpath,
                           struct nlif_repo * repository)
{
	int                            ret;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;

	/* Since restore handlers MUST not fail, this should never fail... */
	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_restore_hndlrs,
		stroll_array_nr(nlifd_iface_restore_hndlrs),
		repository);

	/*
	 *  ...unless Sysrepo internals fail to retrieve subscription events.
	 * Keep trying to restore as many interfaces as we can however.
	 */
	nlif_store_foreach_iface(nlif_repo_store(repository), hndl, iface) {
		int err;

		err = nlif_iface_apply(iface);
		if (err < 0) {
			srplug_path_err(
				xpath,
				"[%u]: cannot abort interface changes: "
				"%s",
				nlif_iface_index(iface),
				strerror(-err));
			if (ret == SR_ERR_OK)
				ret = nlifd_iface_error(err);
		}
		else if (err)
			srplug_path_debug(xpath,
			                  "%s[%u]: "
			                  "interface changes aborted",
			                  nlif_iface_index(iface),
			                  nlif_iface_name(iface));
	}

	return ret;
}

static sr_error_t
nlifd_iface_process_changes(sr_session_ctx_t * session,
                            const char *       xpath,
                            struct nlif_repo * repository)
{
	int ret;

	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_change_hndlrs,
		stroll_array_nr(nlifd_iface_change_hndlrs),
		repository);
	if (ret == SR_ERR_OK) {
		const struct nlif_store_hndl * hndl;
		struct nlif_iface *            iface;

		nlif_store_foreach_iface(nlif_repo_store(repository),
		                         hndl,
		                         iface) {
			ret = nlif_iface_apply(iface);
			if (ret < 0) {
				srplug_path_notice(
					xpath,
					"[%u]: cannot apply interface changes: "
					"%s",
					nlif_iface_index(iface),
					strerror(-ret));
				nlifd_iface_process_aborts(session,
				                           xpath,
				                           repository);
				return nlifd_iface_error(ret);
			}

			if (ret) {
				srplug_path_debug(xpath,
				                  "%s[%u]: "
				                  "interface changes applied",
				                  nlif_iface_name(iface),
				                  nlif_iface_index(iface));
				ret = SR_ERR_OK;
			}
		}
	}

	return ret;
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

	sr_error_t   ret;
	const char * act;

	switch (event) {
	case SR_EV_ENABLED:
		ret = nlifd_iface_process_changes(session, xpath, repository);
		act = "load";
		break;

	case SR_EV_CHANGE:
		ret = nlifd_iface_process_changes(session, xpath, repository);
		act = "apply";
		break;

	case SR_EV_DONE:
		srplug_path_debug(xpath, "interfaces configured");
		return SR_ERR_OK;

	case SR_EV_ABORT:
		ret = nlifd_iface_process_aborts(session, xpath, repository);
		act = "abort";
		break;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}

	if (ret != SR_ERR_OK)
		srplug_path_warn(xpath,
		                 "failed to %s interfaces configuration: %s",
		                 act,
		                 sr_strerror(ret));

	return ret;
}

static const struct srplug_sub nlifd_subs[] = {
	SRPLUG_CHANGE_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                  NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface",
	                  nlifd_on_iface_change,
	                  0,
	                  SR_SUBSCR_DEFAULT/*| SR_SUBSCR_ENABLED*/),
};

/******************************************************************************
 * Interfaces configuration data store handling.
 ******************************************************************************/

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

	ret = srplug_dat_create_list_keyent(context,
	                                    container,
	                                    "interface",
	                                    "name",
	                                    nlif_iface_name(interface),
	                                    &ent);
	if (ret != SR_ERR_OK)
		return ret;

	/* Create default nodes as defined by the interface YANG model. */
	ret = srplug_dat_populate_defaults(ent, LYD_IMPLICIT_NO_STATE, NULL);
 	if (ret != SR_ERR_OK)
		return ret;

	str = nlifd_iface_type_str(nlif_iface_type(interface),
	                           nlif_iface_kind(interface));
	if (!str) {
		srplug_node_info(ent, "unsupported interface type");
		return SR_ERR_UNSUPPORTED;
	}
	ret = srplug_dat_create_leaf(ent, "type", str, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	if (entry)
		*entry = ent;

	return SR_ERR_OK;
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

	err = srplug_dat_create_container(context,
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
	 * Here, `top' tree ownership is transfered to srplug_replace_dstore()
	 * so that it will be freed once it has returned thanks to a call to
	 * lyd_free_all().
	 * This means we are not requested to call lyd_free_tree() on it from
	 * now on.
	 * Also note that any missing implicit (default) nodes will be added to
	 * the data tree.
	 */
	return srplug_replace_config(session,
	                             NLIFD_IETF_IFACE_YANG_MODULE,
	                             top);

free:
	srplug_dat_free_tree(top);

	return err;
}

/******************************************************************************
 * Top-level logic.
 ******************************************************************************/

static struct srplug_feat
nlifd_arbitrary_names_feat = SRPLUG_FEAT_SETUP("arbitrary-names");

static struct srplug_feat
nlifd_pre_provisioning_feat = SRPLUG_FEAT_SETUP("pre-provisioning");

static struct srplug_feat
nlifd_if_mib_feat = SRPLUG_FEAT_SETUP("if-mib");

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
	if (!srplug_find_module(ctx, "iana-if-type"))
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_arbitrary_names_feat) != SR_ERR_OK)
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_pre_provisioning_feat) != SR_ERR_OK)
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_if_mib_feat) != SR_ERR_OK)
		goto release;
	if (!srplug_find_module(ctx, "netlink-interfaces"))
		goto release;

	ret = nlifd_iface_reset_dstore(sess, ctx, repository);
	if (ret != SR_ERR_OK)
		goto release;

	srplug_release_context(sess);

	return 0;

release:
	srplug_release_context(sess);
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
	ret = srplug_dat_create_leaf(*parent,
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














