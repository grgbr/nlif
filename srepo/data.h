#ifndef _SREPO_DATA_H
#define _SREPO_DATA_H

#include <srepo/common.h>
#include <stdbool.h>

/******************************************************************************
 * Yang data node value manipulation.
 ******************************************************************************/

static inline const struct lyd_value *
srepo_dat_node_value(const struct lyd_node * node)
{
	srepo_assert(node);
	srepo_assert(node->schema);
	srepo_assert(node->schema->nodetype & LYD_NODE_TERM);

	return &((const struct lyd_node_term *)node)->value;
}

static inline const char *
srepo_dat_node_dflt(const struct lyd_node * node)
{
	srepo_assert(node);
	srepo_assert(node->schema);
	srepo_assert(node->schema->nodetype == LYS_LEAF);

	const struct lysc_node_leaf * leaf = (const struct lysc_node_leaf *)
	                                     node->schema;
	return leaf->dflt.str;
}

static inline LY_DATA_TYPE
srepo_dat_value_type(const struct lyd_value * value)
{
	srepo_assert(value);
	srepo_assert(value->realtype);

	return value->realtype->basetype;
}

static inline bool
srepo_dat_value_as_bool(const struct lyd_value * value)
{
	srepo_assert(srepo_dat_value_type(value) == LY_TYPE_BOOL);

	return (bool)value->boolean;
}

static inline bool
srepo_dat_node_as_bool(const struct lyd_node * node)
{
	return srepo_dat_value_as_bool(srepo_dat_node_value(node));
}

extern sr_error_t
srepo_dat_node_dflt_as_bool(const struct lyd_node * node, bool * value);

static inline const char *
srepo_dat_node_as_str(const struct lyd_node * node)
{
	srepo_assert(node);
	srepo_assert(node->schema);
	srepo_assert(node->schema->nodetype & LYD_NODE_TERM);

	return lyd_get_value(node);
}

static inline const char *
srepo_dat_node_dflt_as_str(const struct lyd_node * node)
{
	return srepo_dat_node_dflt(node);
}

/******************************************************************************
 * Yang data node manipulation.
 ******************************************************************************/

extern char *
srepo_dat_path(const struct lyd_node * node);

extern sr_error_t
srepo_dat_create_container(const struct ly_ctx * context,
                           struct lyd_node *     parent,
                           const char *          path,
                           struct lyd_node **    container);

extern sr_error_t
srepo_dat_create_list_ent(const struct ly_ctx * context,
                          struct lyd_node *     parent,
                          const char *          path,
                          struct lyd_node **    entry);

extern sr_error_t
srepo_dat_create_list_keyent(const struct ly_ctx * context,
                             struct lyd_node *     parent,
                             const char *          path,
                             const char *          key,
                             const char *          value,
                             struct lyd_node **    entry);

extern sr_error_t
srepo_dat_create_leaf(struct lyd_node *  parent,
                      const char *       path,
                      const char *       value,
                      struct lyd_node ** leaf);

/******************************************************************************
 * Yang data iteration logic.
 ******************************************************************************/

typedef sr_error_t srepo_dat_handle_change(const struct lyd_node *,
                                           sr_change_oper_t,
                                           const char *,
                                           void *);

extern sr_error_t
srepo_dat_foreach_change(sr_session_ctx_t *        session,
                         const char *              xpath,
                         srepo_dat_handle_change * handle,
                         void *                    data);

#endif /* _SREPO_DATA_H */
