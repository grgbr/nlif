#ifndef _NLIF_COMMON_H
#define _NLIF_COMMON_H

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ynl/ynl.h>

#define CONFIG_NLIF_ASSERT 1
#define CONFIG_NLIF_DEBUG 1
#define CONFIG_NLIF_PRINT 1
#define CONFIG_NLIF_OBSRV 1

#if defined(CONFIG_NLIF_ASSERT)

#include <stroll/assert.h>

#define nlif_assert(_cond) \
	stroll_assert("nlif", _cond)

#else  /* !defined(CONFIG_NLIF_ASSERT) */

#define nlif_assert(_cond)

#endif /* defined(CONFIG_NLIF_ASSERT) */

#define nlif_log(_format, ...) \
	fprintf(stderr, \
	        "%s: " _format "\n", \
	        program_invocation_short_name, \
	        ## __VA_ARGS__)

#define nlif_err(_format, ...) \
	nlif_log(_format, ## __VA_ARGS__)

#define nlif_warn(_format, ...) \
	nlif_log(_format, ## __VA_ARGS__)

#define nlif_info(_format, ...) \
	nlif_log(_format, ## __VA_ARGS__)

#if defined(CONFIG_NLIF_DEBUG)

#define nlif_dbg(_format, ...) \
	nlif_log(_format, ## __VA_ARGS__)

#else  /* !defined(CONFIG_NLIF_DEBUG) */

#define nlif_dbg(_format, ...)

#endif /* defined(CONFIG_NLIF_DEBUG) */

#define nlif_abort(_format, ...) \
	({ \
		nlif_log(_format, ## __VA_ARGS__); \
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
