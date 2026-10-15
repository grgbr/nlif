#include "schema.h"
#include <sysrepo.h>

sr_error_t
srepo_sch_feature_status(const struct lys_module * module,
                         const char *              feature,
                         bool *                    enabled)
{
	srepo_assert(module);
	srepo_assert(feature);
	srepo_assert(feature[0]);
	srepo_assert(enabled);

	LY_ERR err;

	err = lys_feature_value(module, feature);
	switch (err) {
	case LY_SUCCESS:
		*enabled = true;
		break;

	case LY_ENOT:
		*enabled = false;
		break;

	case LY_ENOTFOUND:
		return SR_ERR_NOT_FOUND;

	default:
		break;
	}

	srepo_assert(0);

	return SR_ERR_INTERNAL;
}

const struct lys_module *
srepo_sch_find_module(const struct ly_ctx * context, const char * module)
{
	srepo_assert(context);
	srepo_assert(module);
	srepo_assert(module[0]);

	const struct lys_module * mod;

	mod = ly_ctx_get_module_implemented(context, module);
	if (mod && !sr_is_module_internal(mod))
		return mod;

	return NULL;
}
