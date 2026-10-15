#include "common.h"
#include <stdio.h>

void *
nlif_malloc(size_t size)
{
	nlif_assert(size);

	void * mem;

	mem = malloc(size);
	if (!mem)
		abort();

	return mem;
}

#if defined(CONFIG_NLIF_PRINT)

char *
nlif_nozero_str(unsigned int uint, char string[NLIF_UINT_STRSZ])
{
	nlif_assert(string);

	if (uint)
		sprintf(string, "%u", uint);
	else
		memcpy(string, "none", sizeof("none"));

	return string;
}

#endif /* defined(CONFIG_NLIF_PRINT) */

#if defined(CONFIG_NLIF_LOG)

static struct elog * nlif_logger = NULL;

void
nlif_log(enum elog_severity severity, const char * format, ...)
{
	if (nlif_logger) {
		va_list args;

		va_start(args, format);
		elog_vlog(nlif_logger, severity, format, args);
		va_end(args);
	}
}

void
nlif_log_init(struct elog * logger)
{
	nlif_assert(!nlif_logger);

	nlif_logger = logger;
}

#if defined(CONFIG_NLIF_DEBUG)

void
nlif_log_fini(void)
{
	if (nlif_logger)
		elog_destroy(nlif_logger);
}

#else  /* !defined(CONFIG_NLIF_DEBUG) */

static void
nlif_log_fini(void)
{
	if (nlif_logger)
		elog_fini(nlif_logger);
}

#endif /* defined(CONFIG_NLIF_DEBUG) */

#endif /* defined(CONFIG_NLIF_LOG) */
