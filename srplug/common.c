#include "common.h"
#include <srepo/schema.h>

#if defined(CONFIG_SRPLUG_DAEMON)
#include "daemon.h"
#elif defined(CONFIG_SRPLUG_THREAD)
#include "thread.h"
#else
#error Invalid build configuration: no implementation found !
#endif

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
