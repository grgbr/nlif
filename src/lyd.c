#include "lyd.h"
#include <sysrepo.h>
#include <elog/elog.h>

#warning TODO: use srplg_log_errinfo() to push errors to clients.

char *
srplug_lyd_path(const struct lyd_node * node)
{
	srplug_assert(node);

	char * path;

	path = lyd_path(node, LYD_PATH_STD, NULL, 0);
	if (!path)
		abort();

	return path;
}

static
int
srplug_lyd_new_path(const struct ly_ctx * context,
                    struct lyd_node *     parent,
                    const char *          path,
                    const char *          value,
                    uint32_t              options,
                    struct lyd_node **    container)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);

	LY_ERR err;

	err = lyd_new_path(parent,
	                   context,
	                   path,
	                   value,
	                   options,
	                   container);
	if (err == LY_SUCCESS)
		return LY_SUCCESS;

	srplug_assert(err != LY_EINVAL);
	srplug_assert(err != LY_EVALID);

	if (err == LY_EMEM)
		abort();

	return err;
}

int
srplug_lyd_create_container(const struct ly_ctx * context,
                            struct lyd_node *     parent,
                            const char *          path,
                            struct lyd_node **    container)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);
	srplug_assert(parent || (path[0] == '/'));

	LY_ERR err;

	err = srplug_lyd_new_path(context, parent, path, NULL, 0, container);
	if (err != LY_SUCCESS) {
		srplug_assert(err != LY_EEXIST);
		srplug_pnode_notice(parent,
		                    path,
		                    "cannot create container node: %s",
		                    ly_strerr(err));
		return SR_ERR_LY;
	}

	srplug_pnode_debug(parent,
	                   path,
	                   "container node created");

	return SR_ERR_OK;
}

int
srplug_lyd_create_list_ent(const struct ly_ctx * context,
                           struct lyd_node *     parent,
                           const char *          path,
                           struct lyd_node **    entry)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);
	srplug_assert(parent || (path[0] == '/'));

	LY_ERR err;

	err = srplug_lyd_new_path(context, parent, path, NULL, 0, entry);
	if (err != LY_SUCCESS) {
		srplug_assert(err != LY_EEXIST);
		srplug_pnode_notice(parent,
		                    path,
		                    "cannot create unkeyed list entry node: %s",
		                    ly_strerr(err));
		return SR_ERR_LY;
	}

	srplug_pnode_debug(parent,
	                   path,
	                   "unkeyed list entry node created");

	return SR_ERR_OK;
}

int
srplug_lyd_create_list_keyent(const struct ly_ctx * context,
                              struct lyd_node *     parent,
                              const char *          path,
                              const char *          key,
                              const char *          value,
                              struct lyd_node **    entry)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);
	srplug_assert(parent || (path[0] == '/'));
	srplug_assert(key);
	srplug_assert(key[0]);
	srplug_assert(value);
	srplug_assert(value[0]);

	int    ret;
	char * kpath;

	ret = asprintf(&kpath, "%s[%s='%s']", path, key, value);
	srplug_assert(ret);
	if (ret < 0) {
		if (errno == ENOMEM)
			abort();

		srplug_pnode_notice(parent,
		                    path,
		                    "cannot create list node key path: %s",
		                    strerror(errno));
		return SR_ERR_LY;
	}

	ret = srplug_lyd_new_path(context, parent, kpath, value, 0, entry);
	if (ret != LY_SUCCESS) {
		srplug_assert(ret != LY_EEXIST);
		srplug_pnode_notice(parent,
		                    kpath,
		                    "cannot create keyed list entry node: %s",
		                    ly_strerr(ret));
		ret = SR_ERR_LY;
		goto free;
	}

	srplug_pnode_debug(parent,
	                   kpath,
	                   "keyed list entry node created");

free:
	srplug_free(kpath);

	return SR_ERR_OK;
}

int
srplug_lyd_create_leaf(struct lyd_node *  parent,
                       const char *       path,
                       const char *       value,
                       struct lyd_node ** leaf)
{
	srplug_assert(parent);
	srplug_assert(path);
	srplug_assert(path[0]);

	LY_ERR err;

	err = srplug_lyd_new_path(NULL, parent, path, value, 0, leaf);
	if (err != LY_SUCCESS) {
		srplug_assert(err != LY_EEXIST);
		srplug_pnode_notice(parent,
		                    path,
		                    "cannot create leaf node: %s",
		                    ly_strerr(err));
		return SR_ERR_LY;
	}

	srplug_pnode_debug(parent,
	                   path,
	                   "leaf node created");

	return SR_ERR_OK;
}

int
srplug_lyd_acquire_context(sr_session_ctx_t *     session,
                           const struct ly_ctx ** context)
{
	srplug_assert(session);
	srplug_assert(context);

	const struct ly_ctx * ctx;

	ctx = sr_session_acquire_context(session);
	if (!ctx) {
		const sr_error_info_t * einfo;
		int                     err;

		err = sr_session_get_error(session, &einfo);
		srplug_assert(!err);
		srplug_assert(einfo->err->err_code != SR_ERR_OK);

		srplug_warn("cannot acquire session context: %s",
		            einfo->err->message);

		return SR_ERR_LY;
	}

	*context = ctx;

	return SR_ERR_OK;
}

void
srplug_lyd_release_context(sr_session_ctx_t * session)
{
	srplug_assert(session);

	sr_session_release_context(session);
}

const char *
srplug_dstore_str(sr_datastore_t ds)
{
	switch (ds) {
	case SR_DS_RUNNING:
		return "running";

	case SR_DS_STARTUP:
		return "startup";

	case SR_DS_CANDIDATE:
		return "candidate";

	case SR_DS_OPERATIONAL:
		return "operational";

	case SR_DS_FACTORY_DEFAULT:
		return "factory-default";

	default:
		srplug_assert(0);
		return "??";
	}
}

int
srplug_replace_dstore(sr_session_ctx_t * session,
                      const char *       module,
                      struct lyd_node *  tree)
{
	srplug_assert(session);
	srplug_assert(module);
	srplug_assert(module[0]);
	srplug_assert(tree);

	int ret;

	ret = sr_replace_config(session, module, tree, 0);
	if (ret != SR_ERR_OK) {
		/*
		 * Session datastore MUST be either startup, running or
		 * candidate.
		 * In addition, the lyd_node tree given in argument MUST have
		 * been created using the YANG context related to the session
		 * given in argument.
		 */
		srplug_assert(ret != SR_ERR_INVAL_ARG);

		srplug_notice("'%s' datastore: cannot replace content: %s",
		              srplug_dstore_str(sr_session_get_ds(session)),
		              sr_strerror(ret));
	}

	return ret;
}
