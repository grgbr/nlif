#include "data.h"
#include <errno.h>

#warning TODO: use srplg_log_errinfo() to push errors to clients.

/******************************************************************************
 * Yang data node value manipulation.
 ******************************************************************************/

sr_error_t
srepo_dat_node_dflt_as_bool(const struct lyd_node * node, bool * value)
{
	srepo_assert(srepo_dat_value_type(srepo_dat_node_value(node)) ==
	             LY_TYPE_BOOL);

	const char * dflt = srepo_dat_node_dflt(node);

	if (dflt) {
		*value = (!strcmp(dflt, "true")) ? true : false;
		return SR_ERR_OK;
	}

	return SR_ERR_NOT_FOUND;
}

/******************************************************************************
 * Yang data node manipulation.
 ******************************************************************************/

char *
srepo_dat_path(const struct lyd_node * node)
{
	srepo_assert(node);

	char * path;

	path = lyd_path(node, LYD_PATH_STD, NULL, 0);
	if (!path)
		srepo_abort();

	return path;
}

static LY_ERR
srepo_dat_new_path(const struct ly_ctx * context,
                   struct lyd_node *     parent,
                   const char *          path,
                   const char *          value,
                   uint32_t              options,
                   struct lyd_node **    container)
{
	srepo_assert(context || parent);
	srepo_assert(path);
	srepo_assert(path[0]);

	LY_ERR err;

	err = lyd_new_path(parent, context, path, value, options, container);
	if (err == LY_SUCCESS)
		return LY_SUCCESS;

	srepo_assert(err != LY_EINVAL);
	srepo_assert(err != LY_EVALID);

	if (err == LY_EMEM)
		srepo_abort();

	return err;
}

sr_error_t
srepo_dat_create_container(const struct ly_ctx * context,
                           struct lyd_node *     parent,
                           const char *          path,
                           struct lyd_node **    container)
{
	srepo_assert(context || parent);
	srepo_assert(path);
	srepo_assert(path[0]);
	srepo_assert(parent || (path[0] == '/'));

	LY_ERR err;

	err = srepo_dat_new_path(context, parent, path, NULL, 0, container);
	if (err == LY_SUCCESS)
		return SR_ERR_OK;

	srepo_assert(err != LY_EEXIST);

	return SR_ERR_LY;
}

sr_error_t
srepo_dat_create_list_ent(const struct ly_ctx * context,
                          struct lyd_node *     parent,
                          const char *          path,
                          struct lyd_node **    entry)
{
	srepo_assert(context || parent);
	srepo_assert(path);
	srepo_assert(path[0]);
	srepo_assert(parent || (path[0] == '/'));

	LY_ERR err;

	err = srepo_dat_new_path(context, parent, path, NULL, 0, entry);
	if (err == LY_SUCCESS)
		return SR_ERR_OK;

	srepo_assert(err != LY_EEXIST);

	return SR_ERR_LY;
}

sr_error_t
srepo_dat_create_list_keyent(const struct ly_ctx * context,
                             struct lyd_node *     parent,
                             const char *          path,
                             const char *          key,
                             const char *          value,
                             struct lyd_node **    entry)
{
	srepo_assert(context || parent);
	srepo_assert(path);
	srepo_assert(path[0]);
	srepo_assert(parent || (path[0] == '/'));
	srepo_assert(key);
	srepo_assert(key[0]);
	srepo_assert(value);
	srepo_assert(value[0]);

	int    ret;
	char * kpath;

	ret = asprintf(&kpath, "%s[%s='%s']", path, key, value);
	srepo_assert(ret);
	if (ret < 0) {
		if (errno == ENOMEM)
			srepo_abort();
		return SR_ERR_LY;
	}

	ret = srepo_dat_new_path(context, parent, kpath, value, 0, entry);
	if (ret != LY_SUCCESS) {
		srepo_assert(ret != LY_EEXIST);

		ret = SR_ERR_LY;
		goto free;
	}

	ret = SR_ERR_OK;

free:
	free(kpath);

	return SR_ERR_OK;
}

sr_error_t
srepo_dat_create_leaf(struct lyd_node *  parent,
                      const char *       path,
                      const char *       value,
                      struct lyd_node ** leaf)
{
	srepo_assert(parent);
	srepo_assert(path);
	srepo_assert(path[0]);

	LY_ERR err;

	err = srepo_dat_new_path(NULL, parent, path, value, 0, leaf);
	if (err == LY_SUCCESS)
		return SR_ERR_OK;

	srepo_assert(err != LY_EEXIST);

	return SR_ERR_LY;
}
