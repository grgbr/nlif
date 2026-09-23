#include "common.h"
#include <srepo/schema.h>
#include <srepo/data.h>

#if defined(CONFIG_SRPLUG_DAEMON)
#include "daemon.h"
#elif defined(CONFIG_SRPLUG_THREAD)
#include "thread.h"
#else
#error Invalid build configuration: no implementation found !
#endif

void *
srplug_malloc(size_t size)
{
	srplug_assert(size);

	void * data;

	data = malloc(size);
	if (!data)
		srplug_abort();

	return data;
}

/******************************************************************************
 * Sysrepo data changes handling.
 ******************************************************************************/

sr_error_t
srplug_handle_changes(sr_session_ctx_t *        session,
                      const char *              xpath,
                      srplug_handle_change_fn * handle,
                      void *                    data)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	srplug_assert(handle);

	sr_change_iter_t * iter;
	sr_error_t         ret;

	ret = sr_get_changes_iter(session, xpath, &iter);
	if (ret != SR_ERR_OK) {
		srplug_assert(ret != SR_ERR_INVAL_ARG);

		if (ret == SR_ERR_NO_MEMORY)
			srplug_abort();

		return ret;
	}

	do {
		sr_change_oper_t        oper;
		const struct lyd_node * node;
		const char *            old;

		ret = sr_get_change_tree_next(session,
		                              iter,
		                              &oper,
		                              &node,
		                              &old,
		                              NULL,
		                              NULL);
		if (ret != SR_ERR_OK) {
			srplug_assert(ret != SR_ERR_INVAL_ARG);

			if (ret == SR_ERR_NO_MEMORY)
				srplug_abort();

			break;
		}

		ret = handle(node, oper, old, data);
	} while (ret == SR_ERR_OK);

	sr_free_change_iter(iter);

	return (ret == SR_ERR_NOT_FOUND) ? SR_ERR_OK : ret;
}

/*
 * Sysrepo pluging data change dispatcher.
 */
struct srplug_change_dispatch {
	unsigned int                       nr;
	const struct srplug_change_hndlr * hndlrs;
	void *                             data;
};

#define srplug_assert_change_dispatch(_dispatch) \
	srplug_assert(_dispatch); \
	srplug_assert((_dispatch)->nr); \
	srplug_assert((_dispatch)->hndlrs)

#define srplug_assert_change_hndlr(_hndlr) \
	srplug_assert(_hndlr); \
	srplug_assert((_hndlr)->name); \
	srplug_assert((_hndlr)->name[0]); \
	srplug_assert((_hndlr)->handle)

static const char *
srplug_change_oper_str(sr_change_oper_t oper)
{
	switch (oper) {
	case SR_OP_CREATED:
		return "created";
	case SR_OP_MODIFIED:
		return "modified";
	case SR_OP_DELETED:
		return "deleted";
	case SR_OP_MOVED:
		return "moved";
	default:
		break;
	}

	return "unknown";
}

static sr_error_t
srplug_dispatch_child_change(const struct lyd_node * node,
                             sr_change_oper_t        oper,
                             const char *            old,
                             void *                  dispatch)
{
	srplug_assert(node);
	srplug_assert(node);
	srplug_assert_change_dispatch((const struct srplug_change_dispatch *)
	                              dispatch);

	const char *                          name;
	unsigned int                          h;
	const struct srplug_change_dispatch * disp = dispatch;

	name = srepo_dat_node_name(node);
	srplug_assert(name);
	srplug_assert(name[0]);

	for (h = 0; h < disp->nr; h++) {
		const struct srplug_change_hndlr * hndlr = &disp->hndlrs[h];

		srplug_assert_change_hndlr(hndlr);

		if (!strcmp(name, hndlr->name)) {
			srplug_node_debug(node,
			                  "handling '%s' change operation",
			                  srplug_change_oper_str(oper));
			return hndlr->handle(node, oper, old, disp->data);
		}
	}

	srplug_node_debug(node, "no change handler found: ignoring");

	return SR_ERR_OK;
}

sr_error_t
srplug_process_child_changes(sr_session_ctx_t *                 session,
                             const char *                       xpath,
                             const struct srplug_change_hndlr * handlers,
                             unsigned int                       nr,
                             void *                             data)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	srplug_assert(handlers);
	srplug_assert(nr);

	size_t                              len = strlen(xpath);
	char *                              pth;
	int                                 ret;
	const struct srplug_change_dispatch disp = {
		.nr     = nr,
		.hndlrs = handlers,
		.data   = data
	};

	pth = srplug_malloc(len + 2 + 1);
	memcpy(pth, xpath, len);
	memcpy(&pth[len], "/*", sizeof("/*"));

	ret = srplug_handle_changes(session,
	                            pth,
	                            srplug_dispatch_child_change,
	                            (void *)&disp);
	srplug_free(pth);

	return ret;
}

const struct lys_module *
srplug_find_module(const struct ly_ctx * context, const char * module)
{
	srplug_assert(context);
	srplug_assert(module);

	const struct lys_module * mod;

	mod = srepo_sch_find_module(context, module);
	if (mod)
		return mod;

	srplug_notice("'%s': missing YANG module", module);

	return NULL;
}

sr_error_t
srplug_acquire_context(sr_session_ctx_t *     session,
                       const struct ly_ctx ** context)
{
	srplug_assert(context);

	sr_error_t ret;

	ret = srepo_acquire_context(session, context);
	if (ret == SR_ERR_OK)
		return SR_ERR_OK;

	srplug_warn("cannot acquire context: %s", sr_strerror(ret));

	return ret;
}
