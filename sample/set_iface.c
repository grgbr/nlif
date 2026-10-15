#include "store.h"
#include "iface.h"

int
main(int argc, char * const argv[])
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

	if (argc == 2) {
		const char * arg = argv[1];

		if (arg[0] != '\0') {
			unsigned long     indx;
			char *            err;
			struct ether_addr addr;

			indx = strtoul(argv[1], &err, 0);
			if (*err == '\0') {
				if ((indx > (unsigned long)UINT_MAX) ||
				    nlif_iface_validate_index((unsigned int)
				                              indx)) {
					nlif_err("'%lu': "
					         "invalid index specified.",
					         indx);
					goto fini_gate;
				}

				ret = nlif_iface_create_byidx(
					(unsigned int)indx,
					&gate,
					&iface);
			}
			else {
				if (nlif_iface_validate_name(arg) < 0) {
					nlif_err("'%s': "
					         "invalid name specified.",
					         arg);
					goto fini_gate;
				}

				ret = nlif_iface_create_byname(arg,
				                               &gate,
				                               &iface);
			}

			if (ret) {
				nlif_err("'%s': cannot load interface: %s.",
				         arg,
				         strerror(-ret));
				goto fini_gate;
			}

			nlif_iface_set_admstate(iface, false);

			ret = nlif_iface_set_mtu(iface, 1500);
			if (ret) {
				nlif_err("'%s': "
				         "cannot configure interface MTU: "
				         "%s.",
				         arg,
				         strerror(-ret));
				goto destroy;
			}

			ret = nlif_iface_set_alias(iface, "atestalias");
			if (ret) {
				nlif_err("'%s': "
				         "cannot configure interface alias: "
				         "%s.",
				         arg,
				         strerror(-ret));
				goto destroy;
			}

			ret = nlif_iface_set_hwaddr(
				iface,
				ether_aton_r("1a:41:41:59:a7:e0", &addr));
			if (ret) {
				nlif_err("'%s': "
				         "cannot configure interface kddress: "
				         "%s.",
				         arg,
				         strerror(-ret));
				goto destroy;
			}

			ret = nlif_iface_apply(iface, &gate);
			if (ret) {
				nlif_err("'%s': "
				         "cannot apply interface configuration: "
				         "%s.",
				         arg,
				         strerror(-ret));
				goto destroy;
			}

			nlif_iface_print(iface, stdout);

destroy:
			nlif_iface_destroy(iface);

		}
		else {
			nlif_err("invalid empty argument.");
			ret = -EINVAL;
		}
	}
	else {
		nlif_err("invalid number of arguments.");
		ret = -EINVAL;
	}

fini_gate:
	nlif_gate_fini(&gate);
fini_log:
	elog_fini_stdio(&log);

	return (!ret) ? EXIT_SUCCESS : EXIT_FAILURE;
}
