#ifndef _SREPO_SCHEMA_H
#define _SREPO_SCHEMA_H

#include <srepo/common.h>
#include <stdbool.h>

extern sr_error_t
srepo_sch_feature_status(const struct lys_module * module,
                         const char *              feature,
                         bool *                    enabled);

extern const struct lys_module *
srepo_sch_find_module(const struct ly_ctx * context, const char * module);

#endif /* _SREPO_SCHEMA_H */
