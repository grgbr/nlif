#include "srplug.h"
#include <stroll/array.h>
#include <utils/signal.h>
#include <sysexits.h>

void *
srplug_malloc(size_t size)
{
	srplug_assert(size);

	void * data;

	data = malloc(size);
	if (!data)
		abort();

	return data;
}

#if defined(CONFIG_SRPLUG_DAEMON)

/******************************************************************************
 * Logging handling.
 ******************************************************************************/

#define srplug_daemon_early_log(_format, ...) \
	fprintf(stderr, \
	        "%s: " _format ".\n", \
	        program_invocation_short_name, \
	        ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_LOG)

static struct elog * srplug_daemon_logger;

void
srplug_daemon_log(enum elog_severity severity, const char * format, ...)
{
	if (srplug_daemon_logger) {
		va_list args;

		va_start(args, format);
		elog_vlog(srplug_daemon_logger, severity, format, args);
		va_end(args);
	}
}

#define srplug_daemon_err(_format, ...) \
	srplug_daemon_log(ELOG_ERR_SEVERITY, _format ".", ## __VA_ARGS__)

#define srplug_daemon_warn(_format, ...) \
	srplug_daemon_log(ELOG_WARNING_SEVERITY, _format ".", ## __VA_ARGS__)

#define srplug_daemon_info(_format, ...) \
	srplug_daemon_log(ELOG_INFO_SEVERITY, _format ".", ## __VA_ARGS__)

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_daemon_debug(_format, ...) \
	srplug_daemon_log(ELOG_DEBUG_SEVERITY, _format ".", ## __VA_ARGS__)

#else  /* !defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_daemon_debug(_format, ...)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#else  /* !defined(CONFIG_SRPLUG_LOG) */

#define srplug_daemon_err(_format, ...) \
	do { } while (0)

#define srplug_daemon_warn(_format, ...) \
	do { } while (0)

#define srplug_daemon_info(_format, ...) \
	do { } while (0)

#define srplug_daemon_debug(_format, ...) \
	do { } while (0)

#endif /* defined(CONFIG_SRPLUG_LOG) */

#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)

static int
srplug_daemon_parse_stdlog_level(const char *             arg,
                                 struct elog_parse *      parse,
                                 struct elog_stdio_conf * config)
{
	if (arg[0] == '\0') {
		srplug_daemon_early_log("console log level unspecified");
		return -EINVAL;
	}

	if (!strcmp(arg, "none")) {
		config->super.severity = -1;
		return 0;
	}

	if (elog_parse_stdio_severity(parse, config, arg)) {
		srplug_daemon_early_log("invalid console log level: %s",
		                        parse->error);
		return -EINVAL;
	}

#if !defined(CONFIG_SRPLUG_DEBUG)
	if (config->super.severity >= ELOG_DEBUG_SEVERITY) {
		srplug_daemon_early_log("invalid console log level: "
		                        "invalid '%s' specifier",
		                        arg);
		return -EINVAL;
	}
#endif /* !defined(CONFIG_SRPLUG_DEBUG) */

	return 0;
}

static struct elog *
srplug_daemon_create_stdlog(const struct srplug_daemon_conf * config)
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

#else  /* !defined(CONFIG_SRPLUG_DAEMON_STDLOG) */

static inline struct elog *
srplug_daemon_create_stdlog(const struct srplug_daemon_conf * config __unused)
{
	return NULL;
}

#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) */

#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)

static int
srplug_daemon_parse_syslog_level(const char *              arg,
                                 struct elog_parse *       parse,
                                 struct elog_syslog_conf * config)
{
	if (arg[0] == '\0') {
		srplug_daemon_early_log("syslog level unspecified");
		return -EINVAL;
	}

	if (!strcmp(arg, "none")) {
		config->super.severity = -1;
		return 0;
	}

	if (elog_parse_syslog_severity(parse, config, arg)) {
		srplug_daemon_early_log("invalid syslog level: %s",
		                        parse->error);
		return -EINVAL;
	}

#if !defined(CONFIG_SRPLUG_DEBUG)
	if (config->super.severity >= ELOG_DEBUG_SEVERITY) {
		srplug_daemon_early_log("invalid syslog level: "
		                        "invalid '%s' specifier",
		                        arg);
		return -EINVAL;
	}
#endif /* !defined(CONFIG_SRPLUG_DEBUG) */

	return 0;
}

static int
srplug_daemon_parse_syslog_facility(const char *              arg,
                                    struct elog_parse *       parse,
                                    struct elog_syslog_conf * config)
{
	if (arg[0] == '\0') {
		srplug_daemon_early_log("syslog facility unspecified");
		return -EINVAL;
	}

	if (elog_parse_syslog_facility(parse, config, arg)) {
		srplug_daemon_early_log("invalid syslog facility: %s",
		                        parse->error);
		return -EINVAL;
	}

	return 0;
}

static struct elog *
srplug_daemon_create_syslog(const struct srplug_daemon_conf * config)
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

#else  /* !defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

static inline struct elog *
srplug_daemon_create_syslog(const struct srplug_daemon_conf * config __unused)
{
	return NULL;
}

#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

#if defined(CONFIG_SRPLUG_DAEMON_STDLOG) && defined(CONFIG_SRPLUG_DAEMON_SYSLOG)

static struct elog *
srplug_daemon_create_multlog(const struct srplug_daemon_conf * config)
{
	if ((config->stdlog.super.severity >= 0) &&
	    (config->syslog.super.severity >= 0)) {
		struct elog * logger;

#if defined(CONFIG_SRPLUG_DEBUG)
		logger = (struct elog *)elog_create_multi(elog_destroy);
#else  /* !defined(CONFIG_SRPLUG_DEBUG) */
		logger = (struct elog *)elog_create_multi(elog_fini);
#endif /* defined(CONFIG_SRPLUG_DEBUG) */
		if (!logger)
			abort();

		return logger;
	}

	return NULL;
}

#else  /* !(defined(CONFIG_SRPLUG_DAEMON_STDLOG) &&
            defined(CONFIG_SRPLUG_DAEMON_SYSLOG)) */

static inline struct elog *
srplug_daemon_create_multlog(const struct srplug_daemon_conf * config __unused)
{
	return NULL;
}

#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) &&
          defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

#if defined(CONFIG_SRPLUG_LOG)

static void
srplug_daemon_srlog_cb(sr_log_level_t level, const char * message)
{
	srplug_assert(srplug_daemon_logger);
	srplug_assert(message);
	
	enum elog_severity svrt;

	switch (level) {
	case SR_LL_ERR:
		svrt = ELOG_ERR_SEVERITY;
		break;
	case SR_LL_WRN:
		svrt = ELOG_WARNING_SEVERITY;
		break;
	case SR_LL_INF:
		svrt = ELOG_NOTICE_SEVERITY;
		break;
	case SR_LL_VRB:
		svrt = ELOG_INFO_SEVERITY;
		break;
	case SR_LL_DBG:
		svrt = ELOG_DEBUG_SEVERITY;
		break;
	default:
		srplug_assert(0);
	}

	srplug_daemon_log(svrt, "%s", message);
}

static void
srplug_daemon_setup_srlog(struct elog * logger)
{
	srplug_assert(!srplug_daemon_logger);

	srplug_daemon_logger = logger;

	/*
	 * Disable Sysrepo's syslog logic.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_syslog(NULL, SR_LL_NONE); */

	/*
	 * Also disable Sysrepo's logic that logs to standard error.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_stderr(SR_LL_NONE); */

	if (logger) {
		/*
		 * Install our own logger to delegate Sysrepo / libyang logging
		 * message processing to elog.
		 */
		sr_log_set_cb(srplug_daemon_srlog_cb);
	}
	else {
		/* Disable Sysrepo / libyang logging entirely. */
		sr_log_set_cb(NULL);
	}
}

struct elog *
srplug_daemon_create_log(const struct srplug_daemon_conf * config)
{
	struct elog * mlog = NULL;
	struct elog * log;

	elog_setup(ELOG_DFLT_TAG, ELOG_DFLT_PID);

	mlog = srplug_daemon_create_multlog(config);

	log = srplug_daemon_create_stdlog(config);
	if (log) {
		if (!mlog) {
			mlog = log;
			goto setup;
		}
		if (elog_register_multi_sublog((struct elog_multi *)mlog, log))
			abort();
	}

	log = srplug_daemon_create_syslog(config);
	if (log) {
		if (!mlog) {
			mlog = log;
			goto setup;
		}
		if (elog_register_multi_sublog((struct elog_multi *)mlog, log))
			abort();
	}

setup:
	srplug_daemon_setup_srlog(mlog);

	return mlog;
}

#if defined(CONFIG_SRPLUG_DEBUG)

void
srplug_daemon_destroy_log(struct elog * logger)
{
	if (logger)
		elog_destroy(logger);
}

#else  /* !defined(CONFIG_SRPLUG_DEBUG) */

void
srplug_daemon_destroy_log(struct elog * logger)
{
	if (logger)
		elog_fini(logger);
}

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#else  /* !defined(CONFIG_SRPLUG_LOG) */

struct elog *
srplug_daemon_create_log(const struct srplug_daemon_conf * config __unused)
{
	/*
	 * Disable Sysrepo's syslog logic.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_syslog(NULL, SR_LL_NONE); */

	/*
	 * Also disable Sysrepo's logic that logs to standard error.
	 *
	 * Comment this as it is disabled by default.
	 */
	/* sr_log_stderr(SR_LL_NONE); */

	/* Disable Sysrepo / libyang logging entirely. */
	sr_log_set_cb(NULL);

	return NULL;
}

#endif /* defined(CONFIG_SRPLUG_LOG) */

/******************************************************************************
 * Command line parsing.
 ******************************************************************************/

#include <argp.h>

struct srplug_daemon_cmdln_ctx {
	struct argp                        argp;
	const struct srplug_daemon_cmdln * cmdln;
#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)
	struct elog_parse                  stdlog_parse;
#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) */
#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)
	struct elog_parse                  syslog_parse;
#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */
	struct srplug_daemon_conf *        conf;
};

#if defined(CONFIG_SRPLUG_DEBUG)

#define SRPLUG_DAEMON_SEVERITY_DEBUG_HELP "|debug"

#else  /* !defined(CONFIG_SRPLUG_DEBUG) */

#define SRPLUG_DAEMON_SEVERITY_DEBUG_HELP

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#if defined(CONFIG_SRPLUG_DAEMON_STDLOG) || defined(CONFIG_SRPLUG_DAEMON_SYSLOG)

#define SRPLUG_DAEMON_SEVERITY_HELP \
	"\n" \
	"    SEVERITY := none|dflt|emerg|alert|crit|err|warn|notice|info" \
	SRPLUG_DAEMON_SEVERITY_DEBUG_HELP

#else  /* !(defined(CONFIG_SRPLUG_DAEMON_STDLOG) ||
            defined(CONFIG_SRPLUG_DAEMON_SYSLOG)) */

#define SRPLUG_DAEMON_SEVERITY_HELP ""

#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) ||
          defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)

#define SRPLUG_DAEMON_FACILITY_HELP \
	"\n" \
	"    FACILITY := dflt|auth|authpriv|cron|daemon|ftp|lpr|mail|news|syslog|user|\n" \
	"                local0|local1|local2|local3|local4|local5|local6|local7"

#else  /* !defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

#define SRPLUG_DAEMON_FACILITY_HELP ""

#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

#define SRPLUG_DAEMON_POST_HELP \
	(SRPLUG_DAEMON_SEVERITY_HELP SRPLUG_DAEMON_FACILITY_HELP)

#define SRPLUG_DAEMON_STDLOG_LEVEL_OPT    ('l')
#define SRPLUG_DAEMON_SYSLOG_LEVEL_OPT    ('s')
#define SRPLUG_DAEMON_SYSLOG_FACILITY_OPT ('f')
#define SRPLUG_DAEMON_HELP_OPT            ('h')
#define SRPLUG_DAEMON_USAGE_OPT           (SRPLUG_OPT_MAX - 1)

static const struct argp_option srplug_daemon_cmdln_intern_opts[] = {
#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)
	{
		.name  = "stdlog-level",
		.key   = SRPLUG_DAEMON_STDLOG_LEVEL_OPT,
		.arg   = "SEVERITY",
		.flags = 0,
		.doc   = "set console log verbosity level to SEVERITY\n"
                         "(defaults to `" CONFIG_SRPLUG_DAEMON_STDLOG_SEVERITY_STRING "')",
		.group = 0
	},
#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) */
#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)
	{
		.name  = "syslog-level",
		.key   = SRPLUG_DAEMON_SYSLOG_LEVEL_OPT,
		.arg   = "SEVERITY",
		.flags = 0,
		.doc   = "set syslog verbosity level to SEVERITY\n"
		         "(defaults to `" CONFIG_SRPLUG_DAEMON_SYSLOG_SEVERITY_STRING "')",
		.group = 0
	},
	{
		.name  = "syslog-facility",
		.key   = SRPLUG_DAEMON_SYSLOG_FACILITY_OPT,
		.arg   = "FACILITY",
		.flags = 0,
		.doc   = "log messages to syslog using FACILITY\n"
		         "(defaults to `" CONFIG_SRPLUG_DAEMON_SYSLOG_FACILITY_STRING "')",
		.group = 0
	},
#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */
	{
		.name  = "help",
		.key   = SRPLUG_DAEMON_HELP_OPT,
		.arg   = NULL,
		.flags = 0,
		.doc   = "display a full help message",
		.group = 0
	},
	{
		.name  = "usage",
		.key   = SRPLUG_DAEMON_USAGE_OPT,
		.arg   = NULL,
		.flags = 0,
		.doc   = "display a short help message",
		.group = 0
	},
	{ 0, }
};

#define _srplug_daemon_cmdln_assert_opt(_opt) \
	srplug_assert(_opt); \
	srplug_assert((_opt)->short_name >= 0); \
	srplug_assert((_opt)->short_name < SRPLUG_OPT_MAX); \
	srplug_assert(!(_opt)->long_name || (_opt)->long_name[0]); \
	srplug_assert(!(_opt)->required || (_opt)->arg_name); \
	srplug_assert((_opt)->parse)

static void
srplug_daemon_cmdln_assert_opt(const struct srplug_daemon_cmdln_opt * option)
{
	_srplug_daemon_cmdln_assert_opt(option);

	unsigned int o;

	for (o = 0; o < stroll_array_nr(srplug_daemon_cmdln_intern_opts); o++) {
		srplug_assert(option->short_name !=
		              srplug_daemon_cmdln_intern_opts[o].key);

		if (option->long_name && srplug_daemon_cmdln_intern_opts[o].name)
			srplug_assert(strcmp(
				option->long_name,
				srplug_daemon_cmdln_intern_opts[o].name));
	}
}

static int
srplug_daemon_cmdln_cmp_optkeys(const void * first,
                                const void * second,
                                void *       data __unused)
{
	const struct srplug_daemon_cmdln_opt * fst = first;
	const struct srplug_daemon_cmdln_opt * snd = second;

	return snd->short_name - fst->short_name;
}

static void
srplug_daemon_cmdln_assert_optkeys(const struct srplug_daemon_cmdln_opt * group,
                                   unsigned int                           nr)
{
	srplug_assert(group);
	srplug_assert(nr);

	unsigned int o;

	_srplug_daemon_cmdln_assert_opt(&group[0]);
	for (o = 1; o < nr; o++) {
		_srplug_daemon_cmdln_assert_opt(&group[o]);

		assert(group[o].short_name != group[o - 1].short_name);
	}
}

static void
srplug_daemon_cmdln_assert_optnames(struct srplug_daemon_cmdln_opt * group,
                                    unsigned int                     nr)
{
	assert(!nr || group);

	if (nr) {
		unsigned int o;

		int
		cmp(const void * first,
		    const void * second,
		    void *       data __unused)
		{
			const struct srplug_daemon_cmdln_opt * fst = first;
			const struct srplug_daemon_cmdln_opt * snd = second;

			if (fst->long_name && snd->long_name)
				return strcmp(fst->long_name, snd->long_name);
			else
				return (int)(snd->long_name - fst->long_name);
		}

		stroll_array_quick_sort(group, nr, sizeof(group[0]), cmp, NULL);

		_srplug_daemon_cmdln_assert_opt(&group[0]);
		for (o = 1; o < nr; o++) {
			_srplug_daemon_cmdln_assert_opt(&group[o]);

			assert(strcmp(group[o].long_name,
			              group[o - 1].long_name));
		}
	}
}

static error_t
srplug_daemon_cmdln_parse_opt(int                              key,
                              char *                           argument,
                              struct argp_state *              state,
                              struct srplug_daemon_cmdln_ctx * context)
{
	const struct srplug_daemon_cmdln * cmdln;
	int                                ret __unused;

	switch (key) {
#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)
	case SRPLUG_DAEMON_STDLOG_LEVEL_OPT:
		ret = srplug_daemon_parse_stdlog_level(argument,
		                                       &context->stdlog_parse,
		                                       &context->conf->stdlog);
		return (!ret) ? 0 : -ret;
#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) */

#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)
	case SRPLUG_DAEMON_SYSLOG_LEVEL_OPT:
		ret = srplug_daemon_parse_syslog_level(argument,
		                                       &context->syslog_parse,
		                                       &context->conf->syslog);
		return (!ret) ? 0 : -ret;

	case SRPLUG_DAEMON_SYSLOG_FACILITY_OPT:
		ret = srplug_daemon_parse_syslog_facility(
			argument,
			&context->syslog_parse,
			&context->conf->syslog);
		return (!ret) ? 0 : -ret;
#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

	case SRPLUG_DAEMON_HELP_OPT:
		argp_state_help(state, state->out_stream, ARGP_HELP_STD_HELP);
		return ESHUTDOWN;

	case SRPLUG_DAEMON_USAGE_OPT:
		argp_state_help(state, state->out_stream, ARGP_HELP_USAGE |
		                                          ARGP_HELP_SEE |
		                                          ARGP_HELP_EXIT_OK);
		return ESHUTDOWN;

	default:
		break;
	}

	cmdln = context->cmdln;
	if (cmdln->nr) {
		srplug_assert(cmdln->opts);

		const struct srplug_daemon_cmdln_opt * opt;

		opt = stroll_array_bisect_search(
			&key,
			cmdln->opts,
			cmdln->nr,
			sizeof(*opt),
			srplug_daemon_cmdln_cmp_optkeys,
			NULL);
		if (opt)
			return -opt->parse(opt, argument, state, context->conf);
	}

	return ARGP_ERR_UNKNOWN;
}

static error_t
srplug_daemon_cmdln_argp_parser(int                 key,
                                char *              argument,
                                struct argp_state * state)
{
	struct srplug_daemon_cmdln_ctx * ctx = state->input;

	switch(key) {
	case ARGP_KEY_INIT:
		/*
		 * This parser has just started: nothing particular to do
		 * here...
		 */
		return 0;

	case ARGP_KEY_ARG:
		/*
		 * `arg' is a non-option argument but we don't expect any.
		 * Inform the caller that maybe another parser may handle it.
		 */
		return ARGP_ERR_UNKNOWN;

	case ARGP_KEY_ARGS:
		/*
		 * Previous call to srplug_daemon_cmdln_argp_parser() with
		 * ARGP_KEY_ARG as `key' argument returned ARGP_ERR_UNKNOWN.
		 * Ignore this as we consumme no non-option arguments on the
		 * command line.
		 */
		return ARGP_ERR_UNKNOWN;

	case ARGP_KEY_NO_ARGS:
		/*
		 * No non-option argument seen. Called just before
		 * ARGP_KEY_END. As we expect no non-option arguments, just do
		 * nothing.
		 */
		return 0;

	case ARGP_KEY_END:
		/*
		 * No more argument at all for this parser. No particular clean
		 * up to do for parent parsers since we do allocate no
		 * resources.
		 */
		return 0;

	case ARGP_KEY_SUCCESS:
		/*
		 * This parser has completed successfully and there may be
		 * arguments left.
		 * Let other parsers (if any) handle these.
		 */
		return 0;

	case ARGP_KEY_ERROR:
		/*
		 * Parsing terminated with an error (and ARGP_KEY_SUCCESS will
		 * never be seen).
		 * Called after parsing of a proper `key' option returned an
		 * error.
		 * Nothing particular to do here as an error will be returned
		 * from argp_parse() anyway.
		 */
		return 0;

	case ARGP_KEY_FINI:
		/*
		 * End of parsing logic for this parser.
		 * As we did nothing specific at the ARGP_KEY_INIT stage, there
		 * is nothing particular to do here.
		 */
		return 0;

	default:
		return srplug_daemon_cmdln_parse_opt(key, argument, state, ctx);
	}
}

static size_t
srplug_daemon_cmdln_optdesc_col(const struct argp_option * option,
                                size_t                     column)
{
	srplug_assert(option);

	size_t len;

	len = 4 + 2 + strlen(option->name);
	if (option->arg)
		len += 1 + strlen(option->arg);
	len += 2;
	srplug_assert(len <= 40);

	return stroll_max(len, column);
}

static char *
srplug_daemon_cmdln_post_help(void)
{
	if (SRPLUG_DAEMON_POST_HELP[0]) {
		char * str;
		int    ret;

		ret = asprintf(&str, "Where:%s", SRPLUG_DAEMON_POST_HELP);
		if (ret > 0)
			return str;

		if (errno == ENOMEM)
			abort();
	}

	return NULL;
}

static char *
srplug_daemon_cmdln_help_filter(int key, const char * text, void * data)
{
	const struct srplug_daemon_cmdln_ctx * ctx = data;
	const struct srplug_daemon_cmdln *     cmdln = ctx->cmdln;

	switch (key) {
	case ARGP_KEY_HELP_PRE_DOC:
		return cmdln->brief ? strdup(cmdln->brief) : NULL;

	case ARGP_KEY_HELP_POST_DOC:
		return srplug_daemon_cmdln_post_help();

	case ARGP_KEY_HELP_HEADER:
		return NULL;

	case ARGP_KEY_HELP_EXTRA:
		return NULL;

	default:
		return (char *)text;
	}
}

int
srplug_daemon_cmdln_parse(int                                argc,
                          char *                             argv[],
                          const struct srplug_daemon_cmdln * cmdln,
                          struct srplug_daemon_conf *        config)

{
	srplug_assert(argc);
	srplug_assert(argv);
	srplug_assert(!cmdln->brief || cmdln->brief[0]);
	srplug_assert(!cmdln->nr || cmdln->opts);
	//srplug_assert(config);
	srplug_assert(stroll_array_nr(srplug_daemon_cmdln_intern_opts) > 1);

#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)
	static const struct elog_stdio_conf  stdlog_dflt = {
		.super.severity = CONFIG_SRPLUG_DAEMON_STDLOG_SEVERITY_VALUE,
		.format         = ELOG_TAG_FMT
	};
#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) */
#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)
	static const struct elog_syslog_conf syslog_dflt = {
		.super.severity = CONFIG_SRPLUG_DAEMON_SYSLOG_SEVERITY_VALUE,
		.format         = ELOG_TAG_FMT | ELOG_PID_FMT,
		.facility       = CONFIG_SRPLUG_DAEMON_SYSLOG_FACILITY_VALUE
	};
#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

	struct argp_option *           opts;
	unsigned int                   o;
	size_t                         col = 0;
	char *                         fmt;
	int                            ret;
	struct srplug_daemon_cmdln_ctx ctx = {
		.argp  = {
			.parser      = srplug_daemon_cmdln_argp_parser,
			/* No non-option arguments expected. */
			.args_doc    = NULL,
			.doc         = NULL,
			/* One single top-level parser. */
			.children    = NULL,
			.help_filter = srplug_daemon_cmdln_help_filter,
			/* no string translation support. */
			.argp_domain = NULL
		},
		.cmdln = cmdln,
		.conf  = config
	};

	opts = malloc((cmdln->nr +
	               stroll_array_nr(srplug_daemon_cmdln_intern_opts)) *
	              sizeof(opts[0]));
	if (!opts)
		abort();

	for (o = 0; o < cmdln->nr; o++) {
		srplug_daemon_cmdln_assert_opt(&cmdln->opts[o]);

		struct argp_option * opt = &opts[o];

		opt->name = cmdln->opts[o].long_name;
		opt->key = cmdln->opts[o].short_name;
		opt->arg = cmdln->opts[o].arg_name;
		opt->flags = cmdln->opts[o].required ? 0 : OPTION_ARG_OPTIONAL;
		opt->doc = cmdln->opts[o].help;
		opt->group = 0;

		col = srplug_daemon_cmdln_optdesc_col(opt, col);
	}

	srplug_daemon_cmdln_assert_optnames(cmdln->opts, cmdln->nr);

	if (cmdln->nr) {
		stroll_array_quick_sort(cmdln->opts,
		                        cmdln->nr,
		                        sizeof(cmdln->opts[0]),
		                        srplug_daemon_cmdln_cmp_optkeys,
		                        NULL);
		srplug_daemon_cmdln_assert_optkeys(cmdln->opts, cmdln->nr);
	}

	for (o = 0;
	     o < (stroll_array_nr(srplug_daemon_cmdln_intern_opts) - 1);
	     o++) {
		srplug_assert(srplug_daemon_cmdln_intern_opts[o].key >= 0);
		srplug_assert(srplug_daemon_cmdln_intern_opts[o].key <
		              SRPLUG_OPT_MAX);

		memcpy(&opts[cmdln->nr + o],
		       &srplug_daemon_cmdln_intern_opts[o],
		       sizeof(*opts));

		col = srplug_daemon_cmdln_optdesc_col(&opts[cmdln->nr + o],
		                                      col);
	}
	memset(&opts[cmdln->nr + o], 0, sizeof(*opts));

	ret = asprintf(
		&fmt,
		"short-opt-col=0,long-opt-col=4,doc-opt-col=0,opt-doc-col=%zu",
		col);
	if (ret < 0) {
		if (errno == ENOMEM)
			abort();
		ret = errno;
		goto free_opts;
	}
	srplug_assert(fmt);
	setenv("ARGP_HELP_FMT", fmt, 1);

#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)
	elog_init_stdio_parse(&ctx.stdlog_parse,
	                      &config->stdlog,
	                      &stdlog_dflt);
#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) */
#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)
	elog_init_syslog_parse(&ctx.syslog_parse,
	                       &config->syslog,
	                       &syslog_dflt);
#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

	ctx.argp.options = opts;
	ret = argp_parse(&ctx.argp,
	                 argc,
	                 argv,
	                 ARGP_NO_EXIT | ARGP_NO_HELP,
	                 NULL, /* No non-option argument support. */
	                 &ctx);
	if (ret == ENOMEM)
		abort();

#if defined(CONFIG_SRPLUG_DAEMON_STDLOG)
	elog_fini_parse(&ctx.stdlog_parse);
#endif /* defined(CONFIG_SRPLUG_DAEMON_STDLOG) */
#if defined(CONFIG_SRPLUG_DAEMON_SYSLOG)
	elog_fini_parse(&ctx.syslog_parse);
#endif /* defined(CONFIG_SRPLUG_DAEMON_SYSLOG) */

	unsetenv("ARGP_HELP_FMT");
	free(fmt);

free_opts:
	free(opts);

	return (ret != ESHUTDOWN) ? -ret : 1;
}

/******************************************************************************
 * Signal handling.
 ******************************************************************************/

static
int
srplug_sigs_dispatch(struct upoll_worker * worker,
                     uint32_t              state __unused,
                     const struct upoll *  poller __unused)
{
	srplug_assert(worker);
	srplug_assert(state);
	srplug_assert(!(state & EPOLLOUT));
	srplug_assert(!(state & EPOLLRDHUP));
	srplug_assert(!(state & EPOLLPRI));
	srplug_assert(!(state & EPOLLHUP));
	srplug_assert(!(state & EPOLLERR));
	srplug_assert(state & EPOLLIN);
	srplug_assert(poller);

	const struct srplug_sigs_work * wk;
	struct signalfd_siginfo        info;
	int                            ret;

	wk = containerof(worker, struct srplug_sigs_work, base);
	srplug_assert(wk);
	srplug_assert(wk->fd > 0);

	ret = usig_read_fd(wk->fd, &info, 1);
	srplug_assert(ret);
	if (ret < 0)
		return (ret == -EAGAIN) ? 0 : ret;

	switch (info.ssi_signo) {
	case SIGHUP:
		/* TODO: implement reload ! */
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		/* Tell caller we were requested to terminate. */
		srplug_daemon_debug("interrupted by signal '%s'",
		                     strsignal((int)info.ssi_signo));
		return -ESHUTDOWN;

	default:
		srplug_assert(0);
	}

	unreachable();
}

static int
srplug_sigs_init(struct srplug_sigs_work * worker, const struct upoll * poller)
{
	srplug_assert(worker);
	srplug_assert(poller);

	sigset_t     blk = *usig_empty_msk;
	sigset_t     ign = *usig_full_msk;
	int          ret;
	const char * msg __unused;

	/*
	 * Setup current working directory ??
	 */

	usig_addset(&blk, SIGHUP);
	usig_addset(&blk, SIGINT);
	usig_addset(&blk, SIGQUIT);
	usig_addset(&blk, SIGTERM);

	ret = usig_open_fd(&blk, SFD_NONBLOCK | SFD_CLOEXEC);
	if (ret < 0) {
		msg = "cannot open signal file";
		goto err;
	}

	worker->fd = ret;
	ret = upoll_register_dispatch(poller,
	                              ret,
	                              EPOLLIN,
	                              &worker->base,
	                              srplug_sigs_dispatch);
	if (ret) {
		if (ret == -ENOMEM)
			abort();

		msg = "cannot register worker";
		goto close;
	}

	/* Block signals we handle through asynchronous poller. */
	usig_procmask(SIG_SETMASK, &blk, NULL);

	/*
	 * Now make sure that we ignore all other signals we do not want, i.e.,
	 * make sure we still :
	 * - allow job control and controlling terminal interactions,
	 * - allow tracing and breakpoints processing (maybe hard and soft),
	 * - and ensure seccomp(2) failures and bad syscall(2) terminates the
	 *   daemon...
	 */
	usig_delset(&ign, SIGCONT);
	usig_delset(&ign, SIGTSTP);
	usig_delset(&ign, SIGTTIN);
	usig_delset(&ign, SIGTTOU);
	usig_delset(&ign, SIGTRAP);
	usig_delset(&ign, SIGSYS);
	/* ...and don't ignore signals that we handle asynchronously. */
	usig_notset(&blk, &blk);
	usig_andset(&ign, &ign, &blk);
	usig_ignore_set(&ign);

	srplug_daemon_debug("signal handlers registered");

	return 0;

close:
	usig_close_fd(worker->fd);
err:
	srplug_daemon_err("cannot setup signal handlers: %s", msg);

	return ret;
}

static void
srplug_sigs_fini(const struct srplug_sigs_work * worker,
                 const struct upoll *            poller)
{
	srplug_assert(worker);
	srplug_assert(worker->fd > 0);
	srplug_assert(poller);

	upoll_unregister(poller, worker->fd);
	usig_close_fd(worker->fd);

	srplug_daemon_debug("signal handlers unregistered");
}

/******************************************************************************
 * Utilities
 ******************************************************************************/

static void
srplug_adjust_tmr(struct etux_timer * timer, const struct timespec * tmout)
{
	srplug_assert(timer);
	srplug_assert(tmout);

	if (tmout->tv_sec || tmout->tv_nsec) {
		struct timespec tm;

		/* Compute requested timeout absolute time. */
		utime_monotonic_now(&tm);
		utime_tspec_add_clamp(&tm, tmout);

		/*
		 * And arm the timer if necessary, i.e., if it is not armed or
		 * if its expiry date is later than the requested one.
		 */
		if (!etux_timer_is_armed(timer) ||
		    utime_tspec_after(etux_timer_expiry_tspec(timer), &tm))
			etux_timer_arm_tspec(timer, &tm);
	}
	else
		etux_timer_cancel(timer);
}

static
void
srplug_process_subs(sr_subscription_ctx_t * subscriptions,
                    sr_session_ctx_t *      session,
                    struct etux_timer *     timer)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert(timer);

	struct timespec tmout;
	int             err;

	/*
	 * No need to initialize `tmout' since sr_subscription_process_events()
	 * will zero it out.
	 */
	err = sr_subscription_process_events(subscriptions, session, &tmout);
	switch (err) {
	case SR_ERR_OK:
		break;

	case SR_ERR_TIME_OUT:
		/*
		 * Reschedule a call to sr_subscription_process_events() no
		 * later than 100 milliseconds in the future.
		 */
		tmout.tv_sec = 0;
		tmout.tv_nsec = 100000000;
		break;

	case SR_ERR_NO_MEMORY: /* Not enough memory. */
		abort();

	default:
		srplug_daemon_err("failed to process subscription events: %s",
		                  sr_strerror(err));
		return;
	}

	/* Adjust timer configuration. */
	srplug_adjust_tmr(timer, &tmout);
}

static void
srplug_clear_subs(sr_subscription_ctx_t * subscriptions)
{
	int err;

#warning REVIEW ME: What should we do here in case of failure ?! May this really happen ?!

	/* Free all subscribtions (worker->ctx may be NULL here). */
	err = sr_unsubscribe(subscriptions);
	if (err != SR_ERR_OK)
		srplug_daemon_warn("cannot clear subscriptions: %s",
		                   sr_strerror(err));
}

#define srplug_assert_change_sub(_sub) \
	srplug_assert(_sub); \
	srplug_assert((_sub)->module); \
	srplug_assert((_sub)->module[0]); \
	srplug_assert((_sub)->on_change); \
	srplug_assert(!((_sub)->options & \
	                (SR_SUBSCR_NO_THREAD | SR_SUBSCR_THREAD_SUSPEND)))

static int
srplug_register_change_sub(sr_subscription_ctx_t **         subscriptions,
                           sr_session_ctx_t *               session,
                           const struct srplug_change_sub * subscription,
                           const struct upoll *             poller)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert_change_sub(subscription);
	srplug_assert(poller);

	int err;

	err = sr_module_change_subscribe(
		session,
		subscription->module,
		subscription->xpath,
		subscription->on_change,
		subscription->data,
		subscription->priority,
		subscription->options | SR_SUBSCR_NO_THREAD,
		subscriptions);
	if (err == SR_ERR_OK)
		return 0;

	if (err == SR_ERR_NO_MEMORY)
		abort();

	srplug_daemon_info("'%s': "
	                   "cannot register configuration data handler: %s",
	                   subscription->xpath ? subscription->xpath : "",
	                   sr_strerror(err));

	return -EPERM;
}

#define srplug_assert_oper_sub(_sub) \
	srplug_assert(_sub); \
	srplug_assert((_sub)->module); \
	srplug_assert((_sub)->module[0]); \
	srplug_assert((_sub)->on_get); \
	srplug_assert(!((_sub)->options & \
	                (SR_SUBSCR_NO_THREAD | SR_SUBSCR_THREAD_SUSPEND)))

static int
srplug_register_oper_sub(sr_subscription_ctx_t **       subscriptions,
                         sr_session_ctx_t *             session,
                         const struct srplug_oper_sub * subscription,
                         const struct upoll *           poller)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert_oper_sub(subscription);
	srplug_assert(poller);

	int err;

	err = sr_oper_get_subscribe(
		session,
		subscription->module,
		subscription->xpath,
		subscription->on_get,
		subscription->data,
		subscription->options | SR_SUBSCR_NO_THREAD,
		subscriptions);
	if (err == SR_ERR_OK)
		return 0;

	if (err == SR_ERR_NO_MEMORY)
		abort();

	srplug_daemon_info("'%s': cannot register operational data handler: %s",
	                   subscription->xpath ? subscription->xpath : "",
	                   sr_strerror(err));

	return -EPERM;
}

#define srplug_assert_rpc_sub(_sub) \
	srplug_assert(_sub); \
	srplug_assert((_sub)->xpath); \
	srplug_assert((_sub)->xpath[0]); \
	srplug_assert((_sub)->on_rpc); \
	srplug_assert(!((_sub)->options & \
	                (SR_SUBSCR_NO_THREAD | SR_SUBSCR_THREAD_SUSPEND)))

static int
srplug_register_rpc_sub(sr_subscription_ctx_t **      subscriptions,
                        sr_session_ctx_t *            session,
                        const struct srplug_rpc_sub * subscription,
                        const struct upoll *          poller)
{
	srplug_assert(subscriptions);
	srplug_assert(session);
	srplug_assert_rpc_sub(subscription);
	srplug_assert(poller);

	int err;

	err = sr_rpc_subscribe(
		session,
		subscription->xpath,
		subscription->on_rpc,
		subscription->data,
		subscription->priority,
		subscription->options | SR_SUBSCR_NO_THREAD,
		subscriptions);
	if (err == SR_ERR_OK)
		return 0;

	if (err == SR_ERR_NO_MEMORY)
		abort();

	srplug_daemon_info("'%s': cannot register RPC / action handler: %s",
	                   subscription->xpath,
	                   sr_strerror(err));

	return -EPERM;
}

/******************************************************************************
 * Sysrepo plugin logic when instantiated as a daemon.
 ******************************************************************************/

static
int
srplug_daemon_dispatch_subs(struct upoll_worker * worker,
                            uint32_t              state __unused,
                            const struct upoll *  poller __unused)
{
	srplug_assert(worker);
	srplug_assert(state);
	srplug_assert(!(state & EPOLLOUT));
	srplug_assert(!(state & EPOLLRDHUP));
	srplug_assert(!(state & EPOLLPRI));
	srplug_assert(!(state & EPOLLHUP));
	srplug_assert(!(state & EPOLLERR));
	srplug_assert(state & EPOLLIN);
	srplug_assert(poller);

	struct srplug_daemon * dmn;

	dmn = containerof(worker, struct srplug_daemon, sub_tmr);
	srplug_daemon_assert(dmn);

	srplug_process_subs(dmn->sub_ctx, dmn->sess, &dmn->sub_tmr);

	return 0;
}

static int
srplug_daemon_enable_subs(struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	int err;

	if (!daemon->sub_cnt) {
		srplug_assert(daemon->sub_ctx);
		srplug_assert(!etux_timer_is_armed(&daemon->sub_tmr));

		int fd;

		err = sr_get_event_pipe(daemon->sub_ctx, &fd);
		srplug_assert(err == SR_ERR_OK);
		srplug_assert(fd >= 0);

		err = upoll_register_dispatch(&daemon->poll,
		                              fd,
		                              EPOLLIN,
		                              &daemon->sub_work,
		                              srplug_daemon_dispatch_subs);
		if (err) {
			if (err == -ENOMEM)
				abort();

			srplug_clear_subs(daemon->sub_ctx);
			daemon->sub_ctx = NULL;

			srplug_daemon_err("cannot enable subscription worker: "
			                  "%s",
			                  strerror(-err));

			return err;
		}

		srplug_daemon_debug("subscription worker enabled");
	}

	daemon->sub_cnt++;

	return 0;
}

static void
srplug_daemon_disable_subs(struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	if (daemon->sub_cnt) {
		srplug_assert(daemon->sub_work.dispatch);
		srplug_assert(daemon->sub_ctx);

		int fd;
		int err;

		etux_timer_cancel(&daemon->sub_tmr);

		err = sr_get_event_pipe(daemon->sub_ctx, &fd);
		srplug_assert(err == SR_ERR_OK);
		srplug_assert(fd >= 0);

		/* Unregister from asynchronous poller. */
		upoll_unregister(&daemon->poll, fd);

		daemon->sub_cnt = 0;

		srplug_daemon_debug("subscription worker disabled");
	}
}

int
srplug_daemon_change_subscribe(struct srplug_daemon *           daemon,
                               const struct srplug_change_sub * subscription)
{
	srplug_daemon_assert(daemon);
	srplug_assert_change_sub(subscription);

	int err;

	/* Register the configuration data subscription handler. */
	err = srplug_register_change_sub(&daemon->sub_ctx,
	                                 daemon->sess,
	                                 subscription,
	                                 &daemon->poll);
	if (err)
		return err;

	/* Enable asynchronous processing of subscriptions. */
	return srplug_daemon_enable_subs(daemon);
}

int
srplug_daemon_oper_subscribe(struct srplug_daemon *         daemon,
                             const struct srplug_oper_sub * subscription)
{
	srplug_daemon_assert(daemon);
	srplug_assert_oper_sub(subscription);

	int err;

	/* Register the operational data subscription handler. */
	err = srplug_register_oper_sub(&daemon->sub_ctx,
	                               daemon->sess,
	                               subscription,
	                               &daemon->poll);
	if (err)
		return err;

	/* Enable asynchronous processing of subscriptions. */
	return srplug_daemon_enable_subs(daemon);
}

int
srplug_daemon_rpc_subscribe(struct srplug_daemon *        daemon,
                            const struct srplug_rpc_sub * subscription)
{
	srplug_daemon_assert(daemon);
	srplug_assert_rpc_sub(subscription);

	int err;

	/* Register the RPC subscription handler. */
	err = srplug_register_rpc_sub(&daemon->sub_ctx,
	                              daemon->sess,
	                              subscription,
	                              &daemon->poll);
	if (err)
		return err;

	/* Enable asynchronous processing of subscriptions. */
	return srplug_daemon_enable_subs(daemon);
}

int
srplug_daemon_poll(const struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	int ret;

	do {
		ret = upoll_process(&daemon->poll, etux_timer_issue_msec());
		etux_timer_run();
	} while (!ret || (ret == -ETIME));

	return (ret == -ESHUTDOWN) ? 0 : ret;
}

static
void
srplug_daemon_expire_subs(struct etux_timer * timer)
{
	srplug_assert(timer);

	const struct srplug_daemon * dmn;

	dmn = containerof(timer, struct srplug_daemon, sub_tmr);
	srplug_daemon_assert(dmn);

	srplug_process_subs(dmn->sub_ctx, dmn->sess, timer);
}

int
srplug_daemon_open(struct srplug_daemon * daemon, unsigned int poll_nr)
{
	srplug_assert(daemon);
	srplug_assert(poll_nr <= (unsigned int)INT_MAX);

	int                err;
	sr_conn_ctx_t *    conn = NULL;
	sr_session_ctx_t * sess = NULL;

	/* connect to sysrepo */
	err = sr_connect(SR_CONN_DEFAULT, &conn);
	if (err != SR_ERR_OK) {
		srplug_daemon_err("cannot connect to datastore: %s",
		                  sr_strerror(err));
		return -EPERM;
	}

	err = sr_session_start(conn, SR_DS_RUNNING, &sess);
	if (err) {
		srplug_daemon_err("cannot start datastore session: %s",
		                  sr_strerror(err));
		goto disconnect;
	}

	/*
	 * Add 2 additional polling workers for subscription and signal handling
	 * workers.
	 */
	err = upoll_open(&daemon->poll, poll_nr + 2);
	if (err) {
		srplug_daemon_err("cannot open poller: %s", strerror(-err));
		goto disconnect;
	}

	err = srplug_sigs_init(&daemon->sigs, &daemon->poll);
	if (err)
		goto close;

	daemon->sess = sess;
	daemon->sub_ctx = NULL;
	etux_timer_init(&daemon->sub_tmr, srplug_daemon_expire_subs);
	daemon->sub_cnt = 0;

	srplug_daemon_debug("daemon initialized");

	return 0;

close:
	upoll_close(&daemon->poll);
disconnect:
	/* Also closes all sessions related to this connection. */
	sr_disconnect(conn);

	return -EPERM;
}

void
srplug_daemon_close(struct srplug_daemon * daemon)
{
	srplug_daemon_assert(daemon);

	sr_conn_ctx_t * conn = sr_session_get_connection(daemon->sess);

	/* Stop asynchronouns processing of subscriptions. */
	srplug_daemon_disable_subs(daemon);

#if 0
	/*
	 * Release all subscriptions.
	 * Keep this code for reference purpose only since all subscriptions are
	 * free'd at sr_disconnect() time.
	 */
	srplug_assert(!etux_timer_is_armed(&daemon->sub_tmr));
	srplug_clear_subs(daemon->sub_ctx);
#endif

	/* Stop and close signal handling as well as main poller. */
	srplug_sigs_fini(&daemon->sigs, &daemon->poll);
	upoll_close(&daemon->poll);

#if 0
	/*
	 * Close Sysrepo datastore session.
	 * Keep this code for reference purpose only since all sessions are
	 * free'd at sr_disconnect() time.
	 */
	sr_session_stop(daemon->sess);
#endif

	/* Close all sessions and connection to Sysrepo datastores. */
	sr_disconnect(conn);

	srplug_daemon_debug("daemon finished");
}

#endif /* defined(CONFIG_SRPLUG_DAEMON) */

#if 0
#if defined(CONFIG_SRPLUG_THREAD)

#include <utils/time.h>
#include <stdbool.h>
#include <sysrepo.h>

#if defined(CONFIG_SRPLUG_LOG)

static const char * srplug_name;

#define srplug_thread_err(_format, ...) \
	srplg_log(srplug_name, SR_LL_ERR, _format ".", ## __VA_ARGS__)

#define srplug_thread_warn(_format, ...) \
	srplg_log(srplug_name, SR_LL_WRN, _format ".", ## __VA_ARGS__)

#define srplug_thread_info(_format, ...) \
	srplg_log(srplug_name, SR_LL_INF, _format ".", ## __VA_ARGS__)

#else  /* !defined(CONFIG_SRPLUG_LOG) */

#if defined(CONFIG_SRPLUG_DEBUG)

#define srplug_thread_debug(_format, ...) \
	srplg_log(srplug_name, SR_LL_DBG, _format ".", ## __VA_ARGS__)

#else  /* !defined(CONFIG_SRPLUG_DEBUG) */

#define srplug_thread_debug(_format, ...)

#endif /* defined(CONFIG_SRPLUG_DEBUG) */

#endif /* defined(CONFIG_SRPLUG_LOG) */

static
int
srplug_waker_dispatch(struct upoll_worker * worker,
                      uint32_t              state __unused,
                      const struct upoll *  poller __unused)
{
	srplug_assert(worker);
	srplug_assert(state);
	srplug_assert(!(state & EPOLLOUT));
	srplug_assert(!(state & EPOLLRDHUP));
	srplug_assert(!(state & EPOLLPRI));
	srplug_assert(!(state & EPOLLHUP));
	srplug_assert(!(state & EPOLLERR));
	srplug_assert(state & EPOLLIN);
	srplug_assert(poller);

	const struct srplug_waker_work * wk;
	eventfd_t                        cnt;
	int                              ret;

	wk = containerof(worker, struct srplug_waker_work, base);
	srplug_assert(wk);
	srplug_assert(wk->fd > 0);

	ret = uevt_read(wk->fd, &cnt);
	if (ret < 0) {
		/*
		 * Not possible since all signals are blocked when thread is
		 * spawned thanks to uthr_attr_set_sigmask().
		 */
		srplug_assert(ret != -EINTR);
		/*
		 * This would mean that we got awakened with no call to
		 * srplug_waker_trigger() which should not be possible either,
		 * i.e., a bug occured...
		 */
		srplug_assert(ret != -EAGAIN);

		return ret;
	}

	srplug_assert(cnt);

	return 0;
}

static
void
srplug_waker_trigger(const struct srplug_waker_work * waker)
{
	srplug_assert(waker);
	srplug_assert(waker->fd > 0);

	int err;

	err = uevt_write(waker->fd, 1U);
	srplug_assert(!err || (err == -EAGAIN));
}

static
int
srplug_waker_open(struct srplug_waker_work * waker, const struct upoll * poller)
{
	srplug_assert(waker);
	srplug_assert(poller);

	int          ret;
	const char * msg;

	ret = uevt_open(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (ret < 0) {
		msg = "cannot open file";
		goto err;
	}

	waker->fd = ret;
	ret = upoll_register_dispatch(poller,
	                              ret,
	                              EPOLLIN,
	                              &waker->base,
	                              srplug_waker_dispatch);
	if (ret) {
		if (ret == -ENOMEM)
			abort();

		msg = "cannot register";
		goto close;
	}

	return 0;

close:
	uevt_close(waker->fd);
err:
	srplug_thread_err("cannot open wakeup worker: %s", msg);

	return ret;
}

static void
srplug_waker_close(const struct srplug_waker_work * waker,
                   const struct upoll *             poller)
{
	srplug_assert(waker);
	srplug_assert(waker->fd > 0);
	srplug_assert(poller);

	upoll_unregister(poller, waker->fd);
	uevt_close(waker->fd);
}

static void
srplug_thread_switch_state(struct srplug_thread *   thread,
                           enum srplug_thread_state state)
{
	srplug_thread_assert(thread);
	srplug_assert(state >= 0);
	srplug_assert(state < SRPLUG_THR_STAT_NR);

	uthr_lock_mutex(&thread->lck);
	thread->state = state;
	uthr_signal_cond(&thread->cond);
	uthr_unlock_mutex(&thread->lck);
}

static bool
srplug_thread_running(const struct srplug_thread * thread)
{
	srplug_thread_assert(thread);

	return thread->state == SRPLUG_RUNNING_THR_STAT;
}

static void *
srplug_thread_process(void * data)
{
	srplug_thread_assert((struct srplug_thread *)data);

	struct srplug_thread * thr = data;
	int                   ret;

	uthr_set_name(thr->id, srplug_name);

	/* Switch to runnig state and signal waiter that we have started. */
	srplug_thread_switch_state(thr, SRPLUG_RUNNING_THR_STAT);

	srplug_thread_info("thread started");

	/* Run main loop. */
	do {
		ret = upoll_process(&thr->poll, -1);
	} while (!ret && srplug_thread_running(thr));
	if (ret == -ESHUTDOWN)
		ret = 0;

	/* Switch to exited state and signal waiter that we have exited. */
	srplug_thread_switch_state(thr, SRPLUG_EXITED_THR_STAT);

	srplug_thread_info("thread exited with status: '%s'", strerror(-ret));

	uthr_exit(NULL);
}

int
srplug_thread_start(struct srplug_thread * thread)
{
	srplug_thread_assert(thread);
	srplug_assert(thread->state == SRPLUG_THR_STAT_NR);

	pthread_attr_t           attr;
	const sigset_t           msk = *usig_full_msk;
	enum srplug_thread_state stat;
	int                      err;

	/*
	 * Make sure that the thread is spawned in detached state and that it
	 * blocks all signals.
	 */
	uthr_attr_init(&attr);
	uthr_attr_set_detachstate(&attr, PTHREAD_CREATE_DETACHED);
	err = uthr_attr_set_sigmask(&attr, &msk);
	if (err) {
		srplug_assert(err == -ENOMEM);
		abort();
	}

	/* Spawn thread. */
	thread->state = SRPLUG_STARTING_THR_STAT;
	err = uthr_create(&thread->id, &attr, srplug_thread_process, thread);
	uthr_attr_destroy(&attr);
	if (err) {
		srplug_thread_err("cannot spawn thread: %s", strerror(-err));
		goto err;
	}

	/* Wait for the thread routine to signal us a state switch. */
	uthr_lock_mutex(&thread->lck);
	stat = thread->state;
	while (stat == SRPLUG_STARTING_THR_STAT) {
		uthr_wait_cond(&thread->cond, &thread->lck);
		stat = thread->state;
	}
	uthr_unlock_mutex(&thread->lck);

	switch (stat) {
	case SRPLUG_RUNNING_THR_STAT:
		/* Thread start operation completed successfully. */
		break;

	case SRPLUG_EXITED_THR_STAT:
		/* Thread start operation failed to complete successfully. */
		err = -ESRCH;
		goto err;

	default:
		srplug_assert(0);
	}

	return 0;

err:
	thread->state = SRPLUG_THR_STAT_NR;

	return err;
}

void
srplug_thread_stop(struct srplug_thread * thread)
{
	srplug_thread_assert(thread);

	struct timespec tspec;
	int             ret = 0;

#define NLIF_SRPLUG_STOP_SECS (3)
	uthr_cond_now(&thread->cond, &tspec);
	utime_tspec_add_sec_clamp(&tspec, NLIF_SRPLUG_STOP_SECS);

	uthr_lock_mutex(&thread->lck);

	switch (thread->state) {
	case SRPLUG_RUNNING_THR_STAT:
		thread->state = SRPLUG_STOPPING_THR_STAT;
		srplug_waker_trigger(&thread->wake);

		while (thread->state == SRPLUG_STOPPING_THR_STAT) {
			ret = uthr_timed_wait_cond(&thread->cond,
			                           &thread->lck,
			                           &tspec);
			if (ret)
				break;
		}

		break;

	case SRPLUG_EXITED_THR_STAT:
		break;

	default:
		srplug_assert(0);
	}

	uthr_unlock_mutex(&thread->lck);

	if (ret) {
		/*
		 * Thread stop operation timed out: shoot it down (SIGKILL
		 * cannot be blocked) !
		 */
		srplug_assert(ret == -ETIMEDOUT);

		uthr_kill(thread->id, SIGKILL);
		srplug_thread_warn("timed out while stopping thread");
	}

	thread->state = SRPLUG_THR_STAT_NR;

	srplug_thread_debug("thread stopped");
}

int
srplug_thread_init(struct srplug_thread * thread, unsigned int poll_nr)
{
	srplug_assert(thread);
	srplug_assert(poll_nr);
	srplug_assert(poll_nr <= (unsigned int)INT_MAX);

	int err;

	err = upoll_open(&thread->poll, poll_nr);
	if (err) {
		srplug_thread_err("cannot open poller: %s", strerror(-err));
		return err;
	}

	err = srplug_waker_open(&thread->wake, &thread->poll);
	if (err)
		goto close_poll;

	uthr_init_mutex(&thread->lck);
	err = uthr_init_cond(&thread->cond, CLOCK_MONOTONIC);
	if (err) {
		if (err == -ENOMEM);
			abort();

		srplug_thread_err(
			"cannot initialize thread state condition: %s",
			strerror(-err));
		goto fini_lck;
	}

	thread->state = SRPLUG_THR_STAT_NR;

	srplug_thread_debug("thread initialized");

	return 0;

fini_lck:
	uthr_fini_mutex(&thread->lck);
	srplug_waker_close(&thread->wake, &thread->poll);
close_poll:
	upoll_close(&thread->poll);

	return err;
}

void
srplug_thread_fini(struct srplug_thread * thread)
{
	srplug_thread_assert(thread);
	srplug_assert(thread->state == SRPLUG_THR_STAT_NR);

	uthr_fini_cond(&thread->cond);
	uthr_fini_mutex(&thread->lck);
	srplug_waker_close(&thread->wake, &thread->poll);
	upoll_close(&thread->poll);

	srplug_thread_debug("thread finished");
}

void
srplug_setup(const char * name)
{
	srplug_assert(name);
	srplug_assert(name[0]);
	srplug_assert(strlen(name) < UTHR_NAME_MAX);

	srplug_name = name;
}

#endif /* defined(CONFIG_SRPLUG_THREAD) */
#endif
