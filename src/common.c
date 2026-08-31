#include "common.h"

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
