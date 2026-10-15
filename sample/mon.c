#include "store.h"
#include "iface.h"
#include <unistd.h>

int
main(int argc, const char * const argv[] __unused)
{
	static const struct elog_stdio_conf conf = {
		.super.severity = ELOG_DEBUG_SEVERITY,
		.format         = ELOG_TAG_FMT
	};

	struct elog_stdio                   log;
	struct nlif_gate                    gate;
	struct nlif_iface *                 iface;
	int                                 ret = EXIT_FAILURE;

	elog_init_stdio(&log, &conf);
	nlif_log_setup((struct elog *)&log);

	ret = nlif_gate_init(&gate);
	if (ret)
		goto fini_log;

	if (argc == 1) {
		struct nlif_store              store = NLIF_STORE_INIT(store);
		const struct nlif_store_hndl * hndl;

		ret = nlif_store_enable_notif(&store, &gate);
		if (ret)
			goto fini_store;

		ret = nlif_store_load(&store, &gate);
		if (ret)
			goto disable;

		nlif_store_foreach_iface(&store, hndl, iface)
			nlif_iface_print(iface, stdout);

		while (true) {
			nlif_gate_notify(&gate);
			usleep(500000);
		}

disable:
		nlif_store_disable_notif(&store, &gate);
fini_store:
		nlif_store_fini(&store);
	}
	else {
		nlif_err("invalid number of arguments.");
		ret = EINVAL;
	}

	nlif_gate_fini(&gate);

fini_log:
	elog_fini_stdio(&log);

	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
