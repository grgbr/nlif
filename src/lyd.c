#include "lyd.h"
#include <sysrepo.h>
#include <elog/elog.h>

#warning TODO: use srplg_log_errinfo() to push errors to clients.

#if defined(CONFIG_SRPLUG_LOG)

#define srplug_pnode_log(_svrt, _node, _path, _fmt, ...) \
	({ \
		enum elog_severity      __svrt = _svrt; \
		const struct lyd_node * __node = _node; \
		const char *            __path = _path; \
		\
		__node ? srplug_log(__svrt, \
		                    "'%s/%s': " _fmt, \
		                    srplug_lyd_path(__node), \
		                    __path, \
		                    ## __VA_ARGS__) \
		       : srplug_log(__svrt, \
		                    "'%s': " _fmt, \
		                    __path, \
		                    ## __VA_ARGS__); \
	 })

#define srplug_pnode_err(_node, _path, _fmt, ...) \
	srplug_pnode_log(ELOG_ERR_SEVERITY, \
	                 _node, \
	                 _path, \
	                 _fmt, \
	                 ## __VA_ARGS__)

#define srplug_pnode_warn(_node, _path, _fmt, ...) \
	srplug_pnode_log(ELOG_WARNING_SEVERITY, \
	                 _node, \
	                 _path, \
	                 _fmt, \
	                 ## __VA_ARGS__)

#define srplug_pnode_notice(_node, _path, _fmt, ...) \
	srplug_pnode_log(ELOG_NOTICE_SEVERITY, \
	                 _node, \
	                 _path, \
	                 _fmt, \
	                 ## __VA_ARGS__)

#define srplug_pnode_info(_node, _path, _fmt, ...) \
	srplug_pnode_log(ELOG_INFO_SEVERITY, \
	                 _node, \
	                 _path, \
	                 _fmt, \
	                 ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_pnode_debug(_node, _path, _fmt, ...) \
	srplug_pnode_log(ELOG_DEBUG_SEVERITY, \
	                 _node, \
	                 _path, \
	                 _fmt, \
	                 ## __VA_ARGS__)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#else  /* !defined(CONFIG_SRPLUG_LOG) */

#define srplug_pnode_log(_svrt, _node, _path, _fmt, ...) \
	do { } while (0)

#define srplug_pnode_err(_node, _path, _fmt, ...) \
	do { } while (0)

#define srplug_pnode_warn(_node, _path, _fmt, ...) \
	do { } while (0)

#define srplug_pnode_notice(_node, _path, _fmt, ...) \
	do { } while (0)

#define srplug_pnode_info(_node, _path, _fmt, ...) \
	do { } while (0)

#define srplug_pnode_debug(_node, _path, _fmt, ...) \
	do { } while (0)

#endif /* defined(CONFIG_SRPLUG_LOG) */

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

	if (asprintf(&kpath, "%s[%s=\"%s\"]", path, key, value) < 0) {
		if (errno == ENOMEM)
			abort();

		srplug_pnode_notice(parent,
		                    path,
		                    "cannot create list node key path: %s",
		                    strerror(errno));
		return SR_ERR_LY;
	}

	srplug_assert(ret > 0);
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
