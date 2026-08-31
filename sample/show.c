#include "store.h"
#include "iface.h"

int
main(int argc, char * const argv[])
{
	struct nlif_gate    gate;
	struct nlif_iface * iface;
	int                 ret;

	ret = nlif_gate_init(&gate);
	if (ret)
		return EXIT_FAILURE;

	if (argc == 2) {
		const char * arg = argv[1];

		if (arg[0] != '\0') {
			unsigned long indx;
			char *        err;

			indx = strtoul(argv[1], &err, 0);
			if (*err == '\0') {
				if ((indx > (unsigned long)UINT_MAX) ||
				    nlif_iface_validate_index((unsigned int)
				                              indx)) {
					nlif_err("'%lu': "
					         "invalid index specified.",
					         indx);
					goto fini;
				}

				ret = nlif_iface_create_byidx(
					(unsigned int)indx,
					&gate,
					&iface);
			}
			else {
				if (nlif_iface_validate_strid(arg) < 0) {
					nlif_err("'%s': "
					         "invalid name specified.",
					         arg);
					goto fini;
				}

				ret = nlif_iface_create_byname(arg,
				                               &gate,
				                               &iface);
			}

			if (ret) {
				nlif_err("'%s': cannot load interface: %s.",
				         arg,
				         strerror(ret));
				goto fini;
			}

			nlif_iface_print(iface, stdout);

			nlif_iface_destroy(iface);

			goto fini;
		}
	}
	else if (argc == 1) {
		struct nlif_store              store = NLIF_STORE_INIT(store);
		const struct nlif_store_hndl * hndl;

		ret = nlif_store_load(&store, &gate);
		if (ret)
			goto fini;

		nlif_store_foreach_iface(&store, hndl, iface)
			nlif_iface_print(iface, stdout);

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
