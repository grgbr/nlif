#include "common.h"

sr_error_t
srepo_acquire_context(sr_session_ctx_t *     session,
                      const struct ly_ctx ** context)
{
	srepo_assert(session);
	srepo_assert(context);

	const struct ly_ctx * ctx;

	ctx = sr_session_acquire_context(session);
	if (!ctx) {
		const sr_error_info_t * einfo;
		int                     err __unused;

		err = sr_session_get_error(session, &einfo);
		srepo_assert(!err);
		srepo_assert(einfo->err->err_code != SR_ERR_OK);

		if (einfo->err->err_code != SR_ERR_NO_MEMORY)
			srepo_abort();

		return einfo->err->err_code;
	}

	*context = ctx;

	return SR_ERR_OK;
}

const char *
srepo_dstore_str(sr_datastore_t ds)
{
	switch (ds) {
	case SR_DS_RUNNING:
		return "running";

	case SR_DS_STARTUP:
		return "startup";

	case SR_DS_CANDIDATE:
		return "candidate";

	case SR_DS_OPERATIONAL:
		return "operational";

	case SR_DS_FACTORY_DEFAULT:
		return "factory-default";

	default:
		srepo_assert(0);
		return "??";
	}
}
