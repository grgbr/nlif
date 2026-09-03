#ifndef _NLIF_COMMON_H
#define _NLIF_COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include "config.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <elog/elog.h>
#include <ynl/ynl.h>

#if defined(CONFIG_NLIF_ASSERT)

#include <stroll/assert.h>

#define nlif_assert(_cond) \
	stroll_assert("nlif", _cond)

#else  /* !defined(CONFIG_NLIF_ASSERT) */

#define nlif_assert(_cond)

#endif /* defined(CONFIG_NLIF_ASSERT) */

#if defined(CONFIG_NLIF_LOG)

extern void
nlif_log(enum elog_severity severity, const char * format, ...);


#define nlif_err(_format, ...) \
	nlif_log(ELOG_ERR_SEVERITY, _format, ## __VA_ARGS__)

#define nlif_warn(_format, ...) \
	nlif_log(ELOG_WARNING_SEVERITY, _format, ## __VA_ARGS__)

#define nlif_info(_format, ...) \
	nlif_log(ELOG_INFO_SEVERITY, _format, ## __VA_ARGS__)

#if defined(CONFIG_NLIF_DEBUG)

#define nlif_dbg(_format, ...) \
	nlif_log(ELOG_DEBUG_SEVERITY, _format, ## __VA_ARGS__)

#else  /* !defined(CONFIG_NLIF_DEBUG) */

#define nlif_dbg(_format, ...)

#endif /* defined(CONFIG_NLIF_DEBUG) */

extern void
nlif_log_setup(struct elog * logger);

#else  /* !defined(CONFIG_NLIF_LOG) */

static inline void
nlif_log(enum elog_severity severity __unused,
         const char *       format __unused,
         ...)
{
}

#define nlif_err(_format, ...)
#define nlif_warn(_format, ...)
#define nlif_info(_format, ...)
#define nlif_dbg(_format, ...)

static inline void
nlif_log_setup(struct elog * logger __unused)
{
}

#endif /* defined(CONFIG_NLIF_LOG) */

#define nlif_abort(_format, ...) \
	({ \
		nlif_log(ELOG_CURRENT_SEVERITY, _format, ## __VA_ARGS__); \
		abort(); \
	 })

extern void *
nlif_malloc(size_t size);

static inline void
nlif_free(void * mem)
{
	free(mem);
}

static inline int
nlif_ynl_err(const struct ynl_error * error)
{
	nlif_assert(error);
	nlif_assert(error->code);

	return (error->code < __YNL_ERRNO_END) ? -(int)error->code : -EIO;
}

#if defined(CONFIG_NLIF_PRINT)

static inline const char *
nlif_maybe_empty_str(const char * string)
{
	return string ? string : "none";
}

#define NLIF_UINT_STRSZ (11U)

extern char *
nlif_nozero_str(unsigned int uint, char string[NLIF_UINT_STRSZ]);

#endif /* defined(CONFIG_NLIF_PRINT) */

#endif /* _NLIF_COMMON_H */
