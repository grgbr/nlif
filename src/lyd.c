#include "lyd.h"
#include <sysrepo.h>

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

		char * ppath;

		ppath = srplug_lyd_path(parent);
		srplug_notice("'%s%s%s': cannot create container node: %s",
		              ppath ? ppath : "",
		              ppath ? "/" : "",
		              path,
		              ly_strerr(err));
		srplug_free(ppath);

		return SR_ERR_LY;
	}

	{
#warning FACTORIZE ME!!
		char * ppath;

		if (parent)
			ppath = srplug_lyd_path(parent);
		else
			ppath = NULL;
		srplug_debug("'%s%s%s': container node created",
		             ppath ? ppath : "",
		             ppath ? "/" : "",
		             path);
		srplug_free(ppath);
	}

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

		char * ppath;

		ppath = srplug_lyd_path(parent);
		srplug_notice("'%s%s%s': "
		              "cannot create unkeyed list entry node: %s",
		              ppath ? ppath : "",
		              ppath ? "/" : "",
		              path,
		              ly_strerr(err));
		srplug_free(ppath);

		return SR_ERR_LY;
	}

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

	if (asprintf(&kpath, "%s[%s=\"%s\"]", path, key, value) <= 0) {
		if (errno == ENOMEM)
			abort();

#warning FACTORIZE ME!!
		char * ppath;

		ppath = srplug_lyd_path(parent);
		srplug_notice("'%s%s%s[%s=\"%s\"]': "
		              "cannot create list node key path: %s",
		              ppath ? ppath : "",
		              ppath ? "/" : "",
		              path,
		              key,
		              value);
		srplug_free(ppath);

		return SR_ERR_LY;
	}

	ret = srplug_lyd_new_path(context, parent, kpath, value, 0, entry);
	if (ret != LY_SUCCESS) {
		srplug_assert(ret != LY_EEXIST);

		char * ppath;

		ppath = srplug_lyd_path(parent);
		srplug_notice("'%s%s%s': cannot create keyed list entry node: %s",
		              ppath ? ppath : "",
		              ppath ? "/" : "",
		              kpath,
		              ly_strerr(ret));
		srplug_free(ppath);

		ret = SR_ERR_LY;
	}

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

		char * ppath;

		ppath = srplug_lyd_path(parent);
		srplug_notice("'%s/%s': cannot create leaf node: %s",
		              ppath,
		              path,
		              ly_strerr(err));
		srplug_free(ppath);

		return SR_ERR_LY;
	}

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

		return einfo->err->err_code;
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
