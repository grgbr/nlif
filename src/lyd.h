#ifndef _SRPLUG_LYD_H
#define _SRPLUG_LYD_H

#include "srplug.h"
#include <sysrepo_types.h>

extern char *
srplug_lyd_path(const struct lyd_node * node);

extern int
srplug_lyd_create_container(const struct ly_ctx * context,
                            struct lyd_node *     parent,
                            const char *          path,
                            struct lyd_node **    container);

extern int
srplug_lyd_create_list_ent(const struct ly_ctx * context,
                           struct lyd_node *     parent,
                           const char *          path,
                           struct lyd_node **    entry);

extern int
srplug_lyd_create_list_keyent(const struct ly_ctx * context,
                              struct lyd_node *     parent,
                              const char *          path,
                              const char *          key,
                              const char *          value,
                              struct lyd_node **    entry);

extern int
srplug_lyd_create_leaf(struct lyd_node *  parent,
                       const char *       path,
                       const char *       value,
                       struct lyd_node ** leaf);

extern int
srplug_lyd_acquire_context(sr_session_ctx_t *     session,
                       const struct ly_ctx ** context);

extern void
srplug_lyd_release_context(sr_session_ctx_t * session);

#endif /* _SRPLUG_LYD_H */
