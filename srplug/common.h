#ifndef _SRPLUG_COMMON_H
#define _SRPLUG_COMMON_H

#include "config.h"
#include <srepo/common.h>

#if defined(CONFIG_SRPLUG_ASSERT)

#include <stroll/assert.h>

#define srplug_assert(_cond) \
	stroll_assert("srplug", _cond)

#else  /* !defined(CONFIG_SRPLUG_ASSERT) */

#define srplug_assert(_cond)

#endif /* defined(CONFIG_SRPLUG_ASSERT) */

#if defined(CONFIG_SRPLUG_LOG)

#define srplug_path_log(_svrt, _path, _fmt, ...) \
	srplug_log(_svrt, "%s: " _fmt, _path, ## __VA_ARGS__)

#define srplug_path_err(_path, _fmt, ...) \
	srplug_path_log(ELOG_ERR_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#define srplug_path_warn(_path, _fmt, ...) \
	srplug_path_log(ELOG_WARNING_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#define srplug_path_notice(_path, _fmt, ...) \
	srplug_path_log(ELOG_NOTICE_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#define srplug_path_info(_path, _fmt, ...) \
	srplug_path_log(ELOG_INFO_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_path_debug(_path, _fmt, ...) \
	srplug_path_log(ELOG_DEBUG_SEVERITY, _path, _fmt, ## __VA_ARGS__)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_node_log(_svrt, _node, _fmt, ...) \
	srplug_path_log(_svrt, srepo_dat_path(_node), _fmt, ## __VA_ARGS__)

#define srplug_node_err(_node, _fmt, ...) \
	srplug_node_log(ELOG_ERR_SEVERITY, _node, _fmt, ## __VA_ARGS__)

#define srplug_node_warn(_node, _fmt, ...) \
	srplug_node_log(ELOG_WARNING_SEVERITY, _node, _fmt, ## __VA_ARGS__)

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
		                    srepo_dat_path(__node), \
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

/**
 * Configuration data change handler function signature.
 */
typedef sr_error_t srplug_handle_change_fn(const struct lyd_node *,
                                           sr_change_oper_t,
                                           const char *,
                                           void *);

/**
 * Iterate over configuration data changes related to the XPATH given in
 * argument and handle them.
 */
extern sr_error_t
srplug_handle_changes(sr_session_ctx_t *        session,
                      const char *              xpath,
                      srplug_handle_change_fn * handle,
                      void *                    data);

/**
 * Configuration data change HaNDLeR.
 */
struct srplug_change_hndlr {
	const char *              name;
	srplug_handle_change_fn * handle;
};

/**
 * Process configuration data changes related to XPATH direct children according
 * to handlers given in argument.
 */
extern sr_error_t
srplug_process_child_changes(sr_session_ctx_t *                 session,
                             const char *                       xpath,
                             const struct srplug_change_hndlr * handlers,
                             unsigned int                       nr,
                             void *                             data);

struct srplug_change_sub {
	const char *        module;
	const char *        xpath;
	sr_module_change_cb on_change;
	uint32_t            priority;
	uint32_t            options;
};

struct srplug_oper_sub {
	const char *         module;
	const char *         xpath;
	sr_oper_get_items_cb on_get;
	uint32_t             options;
};

struct srplug_rpc_sub {
	const char * xpath;
	sr_rpc_cb    on_rpc;
	uint32_t     priority;
	uint32_t     options;
};

enum srplug_sub_kind {
	SRPLUG_CHANGE_SUB_KIND = 0,
	SRPLUG_OPER_SUB_KIND,
	SRPLUG_RPC_SUB_KIND,
	SRPLUG_SUB_KIND_NR
};

struct srplug_sub {
	enum srplug_sub_kind             kind;
	union {
		struct srplug_change_sub change;
		struct srplug_oper_sub   oper;
		struct srplug_rpc_sub    rpc;
	};
};

#define SRPLUG_CHANGE_SUB(_mod, _xpath, _on_change, _prio, _opts) \
	{ \
		.kind   = SRPLUG_CHANGE_SUB_KIND, \
		.change = { \
			.module    = _mod, \
			.xpath     = _xpath, \
			.on_change = _on_change, \
			.priority  = _prio, \
			.options   = _opts \
		} \
	}

#define SRPLUG_OPER_SUB(_mod, _xpath, _on_get, _prio, _opts) \
	{ \
		.kind = SRPLUG_OPER_SUB_KIND, \
		.oper = { \
			.module  = _mod, \
			.xpath   = _xpath, \
			.on_get  = _on_get, \
			.options = _opts \
		} \
	}

#define SRPLUG_RPC_SUB(_mod, _xpath, _on_change, _prio, _opts) \
	{ \
		.kind = SRPLUG_RPC_SUB_KIND, \
		.rpc  = { \
			.xpath     = _xpath, \
			.on_rpc    = _on_rpc, \
			.priority  = _prio, \
			.options   = _opts \
		} \
	}

extern const struct lys_module *
srplug_find_module(const struct ly_ctx * context, const char * module);

extern sr_error_t
srplug_acquire_context(sr_session_ctx_t *     session,
                       const struct ly_ctx ** context);

extern void *
srplug_malloc(size_t size);

static inline void
srplug_free(void * data)
{
	free(data);
}

static inline void __noreturn
srplug_abort(void)
{
	srepo_abort();
}

#endif /* _SRPLUG_COMMON_H */
