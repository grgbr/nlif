#include "repo.h"
#include "srplug.h"
#include <elog/elog.h>
#include <getopt.h>
#include <sysexits.h>
#include <string.h>

#if 0
static const struct elog_stdio_conf  conf = {
	.super.severity = ELOG_DEBUG_SEVERITY,
	.format         = ELOG_TAG_FMT
};

static struct elog_stdio logger;

static int
on_change(sr_session_ctx_t * session,
          uint32_t           sub_id,
          const char *       module_name,
          const char *       xpath,
          sr_event_t         event,
          uint32_t           request_id,
          void *             private_data)
{
	int        rc;
	sr_val_t * val;

	(void)sub_id;
	(void)module_name;
	(void)xpath;
	(void)event;
	(void)request_id;
	(void)private_data;

	/*
	 * Get the value from sysrepo, we do not care if the value did not
	 * change in our case.
	 */
	rc = sr_get_item(session, "/oven:oven/temperature", 0, &val);
	if (rc != SR_ERR_OK)
		goto sr_error;

	elog_debug((struct elog *)&logger,
	           "temperature: %hhu\n",
	           val->data.uint8_val);
	sr_free_val(val);

	rc = sr_get_item(session, "/oven:oven/turned-on", 0, &val);
	if (rc != SR_ERR_OK)
		goto sr_error;

	elog_debug((struct elog *)&logger,
	           "turned-on: %d\n",
	           (int)val->data.bool_val);
	sr_free_val(val);

	return SR_ERR_OK;

sr_error:
	elog_err((struct elog *)&logger,
	         "change callback failed: %s\n",
	         sr_strerror(rc));

	return rc;
}

static const struct srplug_change_sub chg_sub = {
	.module    = "oven",
	.xpath     = NULL,
	.on_change = on_change,
	.data      = NULL,
	.priority  = 0,
	.options   = SR_SUBSCR_ENABLED | SR_SUBSCR_DONE_ONLY
};

int
main(void)
{
	struct srplug_daemon dmn;
	int                  ret;

	elog_init_stdio(&logger, &conf);
	srplug_setup((struct elog *)&logger);

	ret = srplug_daemon_init(&dmn, 2U);
	if (ret) {
		fprintf(stderr, "init failed: %s\n", strerror(-ret));
		goto out;
	}

	ret = srplug_daemon_change_subscribe(&dmn, &chg_sub);
	if (ret)
		goto fini;

	ret = srplug_daemon_poll(&dmn);
	if (ret)
		fprintf(stderr, "daemon loop failed: %s\n", strerror(-ret));

fini:
	srplug_daemon_fini(&dmn);
out:
	elog_fini_stdio(&logger);

	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
#endif

#if 0

#include "store.h"
#include <utils/poll.h>
#include <utils/signal.h>
#include <stdlib.h>
#include <getopt.h>
#include <sysexits.h>

struct nlifd_notif_work {
	struct upoll_worker base;
	struct nlif_gate *  gate;
};

#define nlifd_assert_notif_work(_work) \
	nlif_assert(_work); \
	nlif_assert((_work)->base.dispatch); \
	nlif_gate_assert((_work)->gate)

#define NLIFD_INIT_NOTIF_WORK(_work, _gate) \
	{ \
		.base.dispatch = nlifd_dispatch_notif, \
		.gate          = _gate, \
	}

static
int
nlifd_dispatch_notif(struct upoll_worker * worker,
                     uint32_t              state __unused,
                     const struct upoll *  poller __unused)
{
	nlif_assert(worker);
	nlif_assert(state);
	nlif_assert(!(state & EPOLLOUT));
	nlif_assert(!(state & EPOLLRDHUP));
	nlif_assert(!(state & EPOLLPRI));
	nlif_assert(!(state & EPOLLHUP));
	nlif_assert(!(state & EPOLLERR));
	nlif_assert(state & EPOLLIN);
	nlif_assert(poller);

	struct nlifd_notif_work * notif = containerof(worker,
	                                              typeof(*notif),
	                                              base);

	nlifd_assert_notif_work(notif);
	nlif_gate_notify(notif->gate);

	return 0;
}

static int
nlifd_enable_notif(struct nlifd_notif_work * worker,
                   struct nlif_store *       store,
                   const struct upoll *      poller)
{
	nlifd_assert_notif_work(worker);
	nlif_store_assert(store);
	nlif_assert(poller);

	struct nlif_gate * gate = worker->gate;
	int                ret;
	const char *       msg __unused;

	ret = nlif_store_enable_notif(store, gate);
	if (ret) {
		msg = "cannot enable store notification";
		goto err;
	}

	ret = upoll_register(poller,
	                     nlif_gate_fd(gate),
	                     EPOLLIN,
	                     &worker->base);
	if (ret) {
		msg = "cannot enable polling";
		goto disable;
	}

	nlif_debug("notification worker enabled.");

	return 0;

disable:
	nlif_store_disable_notif(store, gate);
err:
	nlif_err("cannot enable notification worker: %s: %s.",
	         msg,
	         strerror(-ret));

	return ret;
}

static void
nlifd_disable_notif(struct nlifd_notif_work * worker,
                    struct nlif_store *       store,
                    const struct upoll *      poller)
{
	nlifd_assert_notif_work(worker);
	nlif_store_assert(store);
	nlif_assert(poller);

	struct nlif_gate * gate = worker->gate;

	upoll_unregister(poller, nlif_gate_fd(gate));
	nlif_store_disable_notif(store, gate);

	nlif_debug("notification worker disabled.");
}
#endif

struct nlifd_conf {
#if defined(CONFIG_NLIF_DAEMON_STDLOG)
	struct elog_stdio_conf  stdlog;
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */
#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
	struct elog_syslog_conf syslog;
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */
};

static struct nlifd_conf *
nlifd_alloc_conf(void)
{
	return nlif_malloc(sizeof(struct nlifd_conf));
}

static void
nlifd_free_conf(struct nlifd_conf * config)
{
	nlif_free(config);
}

#define nlifd_early_log(_format, ...) \
	fprintf(stderr, \
	        "%s: " _format, \
	        program_invocation_short_name, \
	        ## __VA_ARGS__)

#if defined(CONFIG_NLIF_DAEMON_STDLOG)

static int
nlifd_parse_stdlog_level(const char *             arg,
                         struct elog_parse *      parse,
                         struct elog_stdio_conf * config)
{
	if (arg[0] == '\0') {
		nlifd_early_log("console log level unspecified.\n");
		return -EPERM;
	}

	if (!strcmp(arg, "none")) {
		config->super.severity = -1;
		return 0;
	}

	if (elog_parse_stdio_severity(parse, config, arg)) {
		nlifd_early_log("invalid console log level: %s.\n",
		                parse->error);
		return -EPERM;
	}

#if !defined(CONFIG_NLIF_DEBUG)
	if (config->super.severity >= ELOG_DEBUG_SEVERITY) {
		nlifd_early_log("unexpected console log level.\n");
		return -EPERM;
	}
#endif /* !defined(CONFIG_NLIF_DEBUG) */

	return 0;
}

static struct elog *
nlifd_create_stdlog(const struct nlifd_conf * config)
{
	if (config->stdlog.super.severity >= 0) {
		struct elog * log;

		log = (struct elog *)elog_create_stdio(&config->stdlog);
		if (!log)
			abort();

		return log;
	}

	return NULL;
}

#define NLIFD_USAGE_STDLOG_OPTS \
"\n" \
"    --stdlog-level=SEVERITY    -- set console log verbosity level to SEVERITY\n" \
"                                  (defaults to " STROLL_STRING(CONFIG_NLIF_DAEMON_STDLOG_SEVERITY) ")"

#else  /* !defined(CONFIG_NLIF_DAEMON_STDLOG) */

static inline struct elog *
nlifd_create_stdlog(const struct nlifd_conf * config __unused)
{
	return NULL;
}

#define NLIFD_USAGE_STDLOG_OPTS

#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */

#if defined(CONFIG_NLIF_DAEMON_SYSLOG)

static int
nlifd_parse_syslog_level(const char *              arg,
                         struct elog_parse *       parse,
                         struct elog_syslog_conf * config)
{
	if (arg[0] == '\0') {
		nlifd_early_log("syslog level unspecified.\n");
		return -EPERM;
	}

	if (!strcmp(arg, "none")) {
		config->super.severity = -1;
		return 0;
	}

	if (elog_parse_syslog_severity(parse, config, arg)) {
		nlifd_early_log("invalid syslog level: %s.\n", parse->error);
		return -EPERM;
	}

#if !defined(CONFIG_NLIF_DEBUG)
	if (config->super.severity >= ELOG_DEBUG_SEVERITY) {
		nlifd_early_log("unexpected syslog level.\n");
		return -EPERM;
	}
#endif /* !defined(CONFIG_NLIF_DEBUG) */

	return 0;
}

static int
nlifd_parse_syslog_facility(const char *              arg,
                            struct elog_parse *       parse,
                            struct elog_syslog_conf * config)
{
	if (arg[0] == '\0') {
		nlifd_early_log("syslog facility unspecified.\n");
		return -EPERM;
	}

	if (elog_parse_syslog_facility(parse, config, arg)) {
		nlifd_early_log("invalid syslog facility: %s.\n", parse->error);
		return -EPERM;
	}

	return 0;
}

static inline struct elog *
nlifd_create_syslog(const struct nlifd_conf * config __unused)
{
	if (config->syslog.super.severity >= 0) {
		struct elog * log;

		log = (struct elog *)elog_create_syslog(&config->syslog);
		if (!log)
			abort();

		return log;
	}

	return NULL;
}

#warning Replace STROLL_STRING() usage with appropriate replacement for facilities

#define NLIFD_USAGE_SYSLOG_OPTS \
"\n" \
"    --syslog-level=SEVERITY    -- set syslog verbosity level to SEVERITY\n" \
"                                  (defaults to " STROLL_STRING(CONFIG_NLIF_DAEMON_SYSLOG_SEVERITY) ")\n" \
"    --syslog-facitily=FACILITY -- log messages to syslog using FACILITY\n" \
"                                  (defaults to `" CONFIG_NLIF_DAEMON_SYSLOG_FACILITY_STRING "')"

#define NLIFD_USAGE_FACILITY \
	"\n" \
	"    FACILITY := dflt|auth|authpriv|cron|daemon|ftp|lpr|mail|news|syslog|user|\n" \
	"                local0|local1|local2|local3|local4|local5|local6|local7"

#else  /* !defined(CONFIG_NLIF_DAEMON_SYSLOG) */

static inline struct elog *
nlifd_create_syslog(const struct nlifd_conf * config __unused)
{
	return NULL;
}

#define NLIFD_USAGE_SYSLOG_OPTS
#define NLIFD_USAGE_FACILITY

#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */

#if defined(CONFIG_NLIF_DAEMON_STDLOG) && defined(CONFIG_NLIF_DAEMON_SYSLOG)

static struct elog *
nlifd_create_multlog(const struct nlifd_conf * config)
{
	if ((config->stdlog.super.severity >= 0) &&
	    (config->syslog.super.severity >= 0)) {
		struct elog * logger;

#if defined(CONFIG_NLIF_DEBUG)
		logger = (struct elog *)elog_create_multi(elog_destroy);
#else  /* !defined(CONFIG_NLIF_DEBUG) */
		logger = (struct elog *)elog_create_multi(elog_fini);
#endif /* defined(CONFIG_NLIF_DEBUG) */
		if (!logger)
			abort();

		return logger;
	}

	return NULL;
}

#else  /* !(defined(CONFIG_NLIF_DAEMON_STDLOG) && defined(CONFIG_NLIF_DAEMON_SYSLOG)) */

static inline struct elog *
nlifd_create_multlog(const struct nlifd_conf * config __unused)
{
	return NULL;
}

#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) && defined(CONFIG_NLIF_DAEMON_SYSLOG) */

#if defined(CONFIG_NLIF_LOG)

static struct elog *
nlifd_create_log(const struct nlifd_conf * config)
{
	struct elog * mlog = NULL;
	struct elog * log;

	elog_setup(ELOG_DFLT_TAG, ELOG_DFLT_PID);

	mlog = nlifd_create_multlog(config);

	log = nlifd_create_stdlog(config);
	if (!mlog) {
		mlog = log;
		goto setup;
	}
	if (elog_register_multi_sublog((struct elog_multi *)mlog, log))
		abort();

	log = nlifd_create_syslog(config);
	if (!mlog) {
		mlog = log;
		goto setup;
	}
	if (elog_register_multi_sublog((struct elog_multi *)mlog, log))
		abort();

setup:
	nlif_log_setup(mlog);

	return mlog;
}

#if defined(CONFIG_NLIF_DEBUG)

static void
nlifd_destroy_log(struct elog * logger)
{
	if (logger)
		elog_destroy(logger);
}

#define NLIFD_USAGE_DEBUG_LEVEL "|debug"

#else  /* !defined(CONFIG_ELOGD_DEBUG) */

static void
nlifd_destroy_log(struct elog * logger)
{
	if (logger)
		elog_fini(logger);
}

#define NLIFD_USAGE_DEBUG_LEVEL

#endif /* defined(CONFIG_ELOGD_DEBUG) */

#define NLIFD_USAGE_LEVEL \
	"\n" \
	"Where:\n" \
	"    SEVERITY := none|dflt|emerg|alert|crit|err|warn|notice|info" \
	NLIFD_USAGE_DEBUG_LEVEL

#else  /* !defined(CONFIG_NLIF_LOG) */

static struct elog *
nlifd_create_log(const struct nlifd_conf * config __unused)
{
	return NULL;
}

static void
nlifd_destroy_log(struct elog * logger __unused)
{
}

#define NLIFD_USAGE_LEVEL
#define NLIFD_USAGE_FACILITY

#endif /* defined(CONFIG_NLIF_LOG) */

#define NLIFD_USAGE \
"Usage: %1$s [OPTIONS]\n" \
"Network interface management daemon.\n" \
"\n" \
"With OPTIONS:" \
NLIFD_USAGE_STDLOG_OPTS \
NLIFD_USAGE_SYSLOG_OPTS \
"\n" \
"    -h|--help                  -- this help message\n" \
NLIFD_USAGE_LEVEL \
NLIFD_USAGE_FACILITY

#include <sys/syslog.h>

static void
nlifd_show_usage(void)
{
	fprintf(stderr, NLIFD_USAGE "\n", program_invocation_short_name);
}

static int
nlifd_parse_cmdln(int argc, char * const argv[], struct nlifd_conf ** config)
{
#if defined(CONFIG_NLIF_DAEMON_STDLOG)
	struct elog_parse                    stdlog_parse;
	static const struct elog_stdio_conf  stdlog_dflt_conf = {
		.super.severity = CONFIG_NLIF_DAEMON_STDLOG_SEVERITY,
		.format         = ELOG_TAG_FMT
	};
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */

#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
	struct elog_parse                    syslog_parse;
	static const struct elog_syslog_conf syslog_dflt_conf = {
		.super.severity = CONFIG_NLIF_DAEMON_SYSLOG_SEVERITY,
		.format         = ELOG_TAG_FMT | ELOG_PID_FMT,
		.facility       = CONFIG_NLIF_DAEMON_SYSLOG_FACILITY_VALUE
	};
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */

	struct nlifd_conf * cfg;
	int                 ret = EXIT_FAILURE;

	cfg = nlifd_alloc_conf();

#if defined(CONFIG_NLIF_DAEMON_STDLOG)
	elog_init_stdio_parse(&stdlog_parse, &cfg->stdlog, &stdlog_dflt_conf);
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */
#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
	elog_init_syslog_parse(&syslog_parse, &cfg->syslog, &syslog_dflt_conf);
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */

	while (true) {
		enum {
#if defined(CONFIG_NLIF_DAEMON_STDLOG)
			STDLOG_LVL_OPT  = 1U << 0,
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */
#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
			SYSLOG_LVL_OPT  = 1U << 1,
			SYSLOG_FAC_OPT  = 1U << 2,
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */
			HELP_OPT        = 'h',
			MISSING_OPT     = ':',
			UNKNOWN_OPT     = '?'
		};

		int                        opt;
		static const struct option opts[] = {
#if defined(CONFIG_NLIF_DAEMON_STDLOG)
			{ "stdlog-level",    required_argument, NULL, STDLOG_LVL_OPT },
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */
#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
			{ "syslog-level",    required_argument, NULL, SYSLOG_LVL_OPT },
			{ "syslog-facility", required_argument, NULL, SYSLOG_FAC_OPT },
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */
			{ "help",            no_argument,       NULL, HELP_OPT },
			{ NULL,              0,                 NULL, -1 }
		};

		opt = getopt_long(argc, argv, ":h", opts, NULL);
		if (opt < 0)
			break;

		switch (opt) {
#if defined(CONFIG_NLIF_DAEMON_STDLOG)
		case STDLOG_LVL_OPT:
			if (nlifd_parse_stdlog_level(optarg,
			                             &stdlog_parse,
			                             &cfg->stdlog))
				goto out;
			break;
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */

#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
		case SYSLOG_LVL_OPT:
			if (nlifd_parse_syslog_level(optarg,
			                             &syslog_parse,
			                             &cfg->syslog))
				goto out;
			break;

		case SYSLOG_FAC_OPT:
			if (nlifd_parse_syslog_facility(optarg,
			                                &syslog_parse,
			                                &cfg->syslog))
				goto out;
			break;
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */

		case HELP_OPT:
			ret = EX_USAGE;
			goto usage;

		case MISSING_OPT:
			nlifd_early_log("option '%s' requires an argument.\n\n",
			                argv[optind - 1]);
			goto usage;

		case UNKNOWN_OPT:
			nlifd_early_log("unrecognized option '%s'.\n\n",
			                argv[optind - 1]);
			goto usage;

		default:
			nlifd_early_log("unexpected option parsing error.\n\n");
			goto usage;
		}
	}

	if (argc - optind) {
		nlifd_early_log("invalid number of arguments.\n\n");
		goto usage;
	}

#if defined(CONFIG_NLIF_DAEMON_STDLOG)
	elog_fini_parse(&stdlog_parse);
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */
#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
	elog_fini_parse(&syslog_parse);
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */

	*config = cfg;

	return EXIT_SUCCESS;

usage:
	nlifd_show_usage();

#if defined(CONFIG_NLIF_LOG)
/*
 * Prevent from -Wunused-label warning when logging build option is disabled,
 * where the `out' label below is not used (see the switch statement above).
 */
out:
#endif /* defined(CONFIG_NLIF_LOG) */

#if defined(CONFIG_NLIF_DEBUG)
#if defined(CONFIG_NLIF_DAEMON_STDLOG)
	elog_fini_parse(&stdlog_parse);
#endif /* defined(CONFIG_NLIF_DAEMON_STDLOG) */
#if defined(CONFIG_NLIF_DAEMON_SYSLOG)
	elog_fini_parse(&syslog_parse);
#endif /* defined(CONFIG_NLIF_DAEMON_SYSLOG) */

	nlifd_free_conf(cfg);
#endif /* defined(CONFIG_NLIF_DEBUG) */

	return ret;
}

int
main(int argc, char * const argv[])
{
	struct nlifd_conf *     cfg;
	int                     ret = EXIT_FAILURE;
	struct elog *           log;
	struct srplug_daemon    dmn;
	struct nlif_repo        repo;

	ret = nlifd_parse_cmdln(argc, argv, &cfg);
	if (ret)
		return EXIT_FAILURE;
	log = nlifd_create_log(cfg);
	nlifd_free_conf(cfg);

	srplug_setup(log);

	ret = srplug_daemon_open(&dmn, 1U);
	if (ret)
		goto fini_log;

	ret = nlif_repo_open(&repo, srplug_daemon_poller(&dmn));
	if (ret)
		goto close_dmn;

	ret = srplug_daemon_poll(&dmn);
	if (ret == -ESHUTDOWN)
		ret = 0;

	nlif_repo_close(&repo, srplug_daemon_poller(&dmn));

close_dmn:
	srplug_daemon_close(&dmn);
fini_log:
	nlifd_destroy_log(log);

	return !ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
