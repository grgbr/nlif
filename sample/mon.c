#include "store.h"
#include "iface.h"
#include <unistd.h>

int
main(int argc, char * const argv[])
{
	struct nlif_gate    gate;
	struct nlif_iface * iface;
	int                 ret;

	ret = nlif_gate_init(&gate);
	if (ret)
		return EXIT_FAILURE;

	if (argc == 1) {
		struct nlif_store              store = NLIF_STORE_INIT(store);
		const struct nlif_store_hndl * hndl;

		ret = nlif_store_load(&store, &gate);
		if (ret)
			goto fini;

		nlif_store_foreach_iface(&store, hndl, iface)
			nlif_iface_print(iface, stdout);

		nlif_store_enable_notif(&store, &gate);

		while (true) {
			nlif_gate_process_notif(&gate);
			usleep(500000);
		}

		nlif_store_disable_notif(&store, &gate);

		nlif_store_fini(&store);
	}
	else {
		nlif_err("invalid number of arguments.");
		ret = EINVAL;
	}

fini:
	nlif_gate_fini(&gate);

	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
