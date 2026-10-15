#ifndef _SREPO_COMMON_H
#define _SREPO_COMMON_H

#include "config.h"
#include <sysrepo.h>
#include <stroll/cdefs.h>
#include <stdlib.h>


#if defined(CONFIG_SREPO_ASSERT)

#include <stroll/assert.h>

#define srepo_assert(_cond) \
	stroll_assert("srepo", _cond)

#else  /* !defined(CONFIG_SRPLUG_ASSERT) */

#define srepo_assert(_cond)

#endif /* defined(CONFIG_SREPO_ASSERT) */

static inline void __noreturn
srepo_abort(void)
{
	abort();
}

extern sr_error_t
srepo_acquire_context(sr_session_ctx_t *     session,
                      const struct ly_ctx ** context);

static inline void
srepo_release_context(sr_session_ctx_t * session)
{
	srepo_assert(session);

	sr_session_release_context(session);
}

extern const char *
srepo_dstore_str(sr_datastore_t ds);

/*
 * Replace an entire sysrepo datastore with the data tree given in argument.
 *
 * Note that `tree' data tree will be freed once this function call has
 * returned.
 */
static inline sr_error_t
srepo_replace_dstore(sr_session_ctx_t * session,
                     const char *       module,
                     struct lyd_node *  tree)
{
	srepo_assert(session);
	srepo_assert(module);
	srepo_assert(module[0]);
	srepo_assert(tree);

	int ret;

	ret = sr_replace_config(session, module, tree, 0);
	if (ret == SR_ERR_OK)
		return SR_ERR_OK;

	/*
	 * Session datastore MUST be either startup, running or
	 * candidate.
	 * In addition, the lyd_node tree given in argument MUST have
	 * been created using the YANG context related to the session
	 * given in argument.
	 */
	srepo_assert(ret != SR_ERR_INVAL_ARG);

	return ret;
}

#endif /* _SREPO_COMMON_H */
