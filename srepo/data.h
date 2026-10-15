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

#endif /* _SREPO_DATA_H */
