#ifndef _SRPLUG_LYD_H
#define _SRPLUG_LYD_H

#include "srplug.h"
#include <sysrepo_types.h>

#if defined(CONFIG_SRPLUG_LOG)

#define srplug_path_log(_svrt, _path, _fmt, ...) \
	srplug_log(_svrt, "%s: " _fmt, _path, ## __VA_ARGS__)

#define srplug_path_err(_path, _fmt, ...) \
	srplug_path_log(ELOG_ERR_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#define srplug_path_warn(_path, _fmt, ...) \
	srplug_path_log(ELOG_WARN_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#define srplug_path_notice(_path, _fmt, ...) \
	srplug_path_log(ELOG_NOTICE_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#define srplug_path_info(_path, _fmt, ...) \
	srplug_path_log(ELOG_INFO_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_path_debug(_path, _fmt, ...) \
	srplug_path_log(ELOG_DEBUG_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_node_log(_svrt, _node, _fmt, ...) \
	srplug_path_log(_svrt, srplug_lyd_path(_node), _fmt, ## __VA_ARGS__)

#define srplug_node_err(_node, _fmt, ...) \
	srplug_node_log(ELOG_ERR_SEVERITY, _node, _fmt, ## __VA_ARGS__)

#define srplug_node_warn(_node, _fmt, ...) \
	srplug_node_log(ELOG_WARN_SEVERITY, _node, _fmt, ## __VA_ARGS__)

#define srplug_node_notice(_node, _fmt, ...) \
	srplug_node_log(ELOG_NOTICE_SEVERITY, _node, _fmt, ## __VA_ARGS__)

#define srplug_node_info(_node, _fmt, ...) \
	srplug_node_log(ELOG_INFO_SEVERITY, _node, _fmt, ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_node_debug(_node, _fmt, ...) \
	srplug_node_log(ELOG_DEBUG_SEVERITY, _node, _fmt, ## __VA_ARGS__)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_pnode_log(_svrt, _node, _path, _fmt, ...) \
	({ \
		enum elog_severity      __svrt = _svrt; \
		const struct lyd_node * __node = _node; \
		const char *            __path = _path; \
		\
		__node ? srplug_log(__svrt, \
		                    "%s/%s: " _fmt, \
		                    srplug_lyd_path(__node), \
		                    __path, \
		                    ## __VA_ARGS__) \
		       : srplug_path_log(__svrt, \
		                         __path, \
		                         _fmt, \
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

#define srplug_path_log(_svrt, _path, _fmt, ...) \
	do { } while (0)

#define srplug_path_err(_path, _fmt, ...) \
	do { } while (0)

#define srplug_path_warn(_path, _fmt, ...) \
	do { } while (0)

#define srplug_path_notice(_path, _fmt, ...) \
	do { } while (0)

#define srplug_path_info(_path, _fmt, ...) \
	do { } while (0)

#define srplug_path_debug(_path, _fmt, ...) \
	do { } while (0)

#define srplug_node_log(_svrt, _node, _fmt, ...) \
	do { } while (0)

#define srplug_node_err(_node, _fmt, ...) \
	do { } while (0)

#define srplug_node_warn(_node, _fmt, ...) \
	do { } while (0)

#define srplug_node_notice(_node, _fmt, ...) \
	do { } while (0)

#define srplug_node_info(_node, _fmt, ...) \
	do { } while (0)

#define srplug_node_debug(_node, _fmt, ...) \
	do { } while (0)

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

extern const char *
srplug_dstore_str(sr_datastore_t ds);

extern int
srplug_replace_dstore(sr_session_ctx_t * session,
                      const char *       module,
                      struct lyd_node *  tree);

#endif /* _SRPLUG_LYD_H */
