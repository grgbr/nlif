#include "repo.h"
#include "srplug.h"
#include <sysexits.h>

/******************************************************************************
 * Nlif sysrepo specific implementation.
 ******************************************************************************/

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"
#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces",

static int
on_change(sr_session_ctx_t * session,
          uint32_t           sub_id __unused,
          const char *       module __unused,
          const char *       xpath __unused,
          sr_event_t         event __unused,
          uint32_t           request_id __unused,
          void *             data __unused)
{
	nlif_assert(!strcmp(module, "oven"));
	nlif_assert(!xpath);
	nlif_assert(!data);

	sr_val_t * val;
	int        err;

	/*
	 * Get the value from sysrepo, we do not care if the value did not
	 * change in our case.
	 */
	err = sr_get_item(session, "/oven:oven/temperature", 0, &val);
	if (err != SR_ERR_OK)
		goto sr_error;

	srplug_debug("temperature: %hhu", val->data.uint8_val);
	sr_free_val(val);

	err = sr_get_item(session, "/oven:oven/turned-on", 0, &val);
	if (err != SR_ERR_OK)
		goto sr_error;

	srplug_debug("turned-on: %d", (int)val->data.bool_val);
	sr_free_val(val);

	return SR_ERR_OK;

sr_error:
	srplug_err("change callback failed: %s", sr_strerror(err));

	return err;
}

static const struct srplug_change_sub nlifd_change_sub = {
	.module    = "oven",
	.xpath     = NULL,
	.on_change = on_change,
	.priority  = 0,
	.options   = SR_SUBSCR_ENABLED | SR_SUBSCR_DONE_ONLY
};

#if 0
static const struct srplug_sub nlifd_iface_sub = {
	.kind   = SRPLUG_CHANGE_SUB_KIND,
	.change = {
		.module    = NLIFD_IETF_IFACE_YANG_MODULE,
		.xpath     = NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface",
		.on_change = nlifd_on_iface_change,
		.priority  = 0,
		.options   = SR_SUBSCR_ENABLED | SR_SUBSCR_DONE_ONLY
	}
};
#endif

/******************************************************************************
 * Main entry point.
 ******************************************************************************/

static void
nlifd_setup(int argc, char * argv[], struct elog ** logger)
{
	static struct srplug_daemon_cmdln cmdln = {
		.brief = "Network interface management daemon.",
		.nr    = 0,
		.opts  = NULL
	};

	struct srplug_daemon_conf *       cfg;
	int                               ret;

	cfg = srplug_daemon_alloc_conf();

	ret = srplug_daemon_cmdln_parse(argc, argv, &cmdln, cfg);
	switch (ret) {
	case 0:
		/* Success: keep moving... */
		*logger = srplug_daemon_create_log(cfg);
		nlif_log_setup(*logger);
		nlif_free(cfg);
		return;

	case 1:
		/* Help informations have been requested on the command line. */
		ret = EXIT_SUCCESS;
		break;

	default:
		/* Command line syntax or parsing error. */
		ret = EX_USAGE;
	}

	nlif_free(cfg);

	exit(ret);
}

int
main(int argc, char * argv[])
{
	int                  ret;
	struct elog *        log;
	struct srplug_daemon dmn;
	struct nlif_repo     repo;

	nlifd_setup(argc, argv, &log);

	ret = srplug_daemon_open(&dmn, 1U);
	if (ret)
		goto fini_log;

	ret = nlif_repo_open(&repo, srplug_daemon_poller(&dmn));
	if (ret)
		goto close_dmn;

	ret = srplug_daemon_change_subscribe(&dmn, &nlifd_change_sub, NULL);
	if (ret)
		goto close_repo;

	ret = srplug_daemon_poll(&dmn);
	if (ret == -ESHUTDOWN)
		ret = 0;

close_repo:
	nlif_repo_close(&repo, srplug_daemon_poller(&dmn));
close_dmn:
	srplug_daemon_close(&dmn);
fini_log:
	srplug_daemon_destroy_log(log);

	return !ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
