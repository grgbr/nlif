#include "common.h"
#include <srepo/schema.h>

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
