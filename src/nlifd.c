#include "repo.h"
#include "srplug.h"
#include <sysexits.h>

/******************************************************************************
 * Nlif sysrepo specific implementation.
 ******************************************************************************/

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"
#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces"

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

/******************************************************************************/

extern int
srplug_acquire_context(sr_session_ctx_t *     session,
                       const struct ly_ctx ** context);

extern void
srplug_release_context(sr_session_ctx_t * session);

extern int
srplug_lyd_create_container(const struct ly_ctx * context,
                            struct lyd_node *     parent,
                            const char *          path,
                            struct lyd_node **    container);

extern int
srplug_lyd_create_list_ent(const struct ly_ctx * context,
                           struct lyd_node *     parent,
                           const char *          path,
                           struct lyd_node **    entry);

extern int
srplug_lyd_create_list_keyent(const struct ly_ctx * context,
                              struct lyd_node *     parent,
                              const char *          path,
                              const char *          key,
                              const char *          value,
                              struct lyd_node **    entry);

extern int
srplug_lyd_create_leaf(struct lyd_node *  parent,
                       const char *       path,
                       const char *       value,
                       struct lyd_node ** leaf);

int
srplug_acquire_context(sr_session_ctx_t *     session,
                       const struct ly_ctx ** context)
{
	srplug_assert(session);
	srplug_assert(context);

	const struct ly_ctx * ctx;

	ctx = sr_session_acquire_context(session);
	if (!ctx) {
		const sr_error_info_t * einfo;
		int                     err;

		err = sr_session_get_error(session, &einfo);
		srplug_assert(!err);
		srplug_assert(einfo->err->err_code != SR_ERR_OK);

		return einfo->err->err_code;
	}

	*context = ctx;

	return SR_ERR_OK;
}

void
srplug_release_context(sr_session_ctx_t * session)
{
	srplug_assert(session);

	sr_session_release_context(session);
}

static char *
srplug_lyd_path(const struct lyd_node * node)
{
	srplug_assert(node);

	char * path;

	path = lyd_path(node, LYD_PATH_STD, NULL, 0);
	if (!path)
		abort();

	return path;
}

static
int
srplug_lyd_new_path(const struct ly_ctx * context,
                    struct lyd_node *     parent,
                    const char *          path,
                    const char *          value,
                    uint32_t              options,
                    struct lyd_node **    container)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);

	LY_ERR err;

	err = lyd_new_path(parent,
	                   context,
	                   path,
	                   value,
	                   options,
	                   container);
	if (err == LY_SUCCESS)
		return LY_SUCCESS;

	nlif_assert(err != LY_EINVAL);
	nlif_assert(err != LY_EVALID);

	if (err == LY_EMEM)
		abort();

	return err;
}

int
srplug_lyd_create_container(const struct ly_ctx * context,
                            struct lyd_node *     parent,
                            const char *          path,
                            struct lyd_node **    container)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);
	srplug_assert(parent || (path[0] == '/'));

	LY_ERR err;

	err = srplug_lyd_new_path(context, parent, path, NULL, 0, container);
	if (err != LY_SUCCESS) {
		nlif_assert(err != LY_EEXIST);

		char * ppath;

		ppath = srplug_lyd_path(parent);
		nlif_notice("'%s%s%s': cannot create container node: %s",
		            ppath ? ppath : "",
		            ppath ? "/" : "",
		            path,
		            ly_strerr(err));
		srplug_free(ppath);

		return SR_ERR_LY;
	}

	{
#warning FACTORIZE ME!!
		char * ppath;

		if (parent)
			ppath = srplug_lyd_path(parent);
		else
			ppath = NULL;
		nlif_debug("'%s%s%s': container node created",
		            ppath ? ppath : "",
		            ppath ? "/" : "",
		            path);
		srplug_free(ppath);
	}

	return SR_ERR_OK;
}

int
srplug_lyd_create_list_ent(const struct ly_ctx * context,
                           struct lyd_node *     parent,
                           const char *          path,
                           struct lyd_node **    entry)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);
	srplug_assert(parent || (path[0] == '/'));

	LY_ERR err;

	err = srplug_lyd_new_path(context, parent, path, NULL, 0, entry);
	if (err != LY_SUCCESS) {
		nlif_assert(err != LY_EEXIST);

		char * ppath;

		ppath = srplug_lyd_path(parent);
		nlif_notice("'%s%s%s': "
		            "cannot create unkeyed list entry node: %s",
		            ppath ? ppath : "",
		            ppath ? "/" : "",
		            path,
		            ly_strerr(err));
		srplug_free(ppath);

		return SR_ERR_LY;
	}

	return SR_ERR_OK;
}

int
srplug_lyd_create_list_keyent(const struct ly_ctx * context,
                              struct lyd_node *     parent,
                              const char *          path,
                              const char *          key,
                              const char *          value,
                              struct lyd_node **    entry)
{
	srplug_assert(context || parent);
	srplug_assert(path);
	srplug_assert(path[0]);
	srplug_assert(parent || (path[0] == '/'));
	srplug_assert(key);
	srplug_assert(key[0]);
	srplug_assert(value);
	srplug_assert(value[0]);

	int    ret;
	char * kpath;

	if (asprintf(&kpath, "%s[%s=\"%s\"]", path, key, value) <= 0) {
		if (errno == ENOMEM)
			abort();

#warning FACTORIZE ME!!
		char * ppath;

		ppath = srplug_lyd_path(parent);
		nlif_notice("'%s%s%s[%s=\"%s\"]': "
		            "cannot create list node key path: %s",
		            ppath ? ppath : "",
		            ppath ? "/" : "",
		            path,
		            key,
		            value);
		srplug_free(ppath);

		return SR_ERR_LY;
	}

	ret = srplug_lyd_new_path(context, parent, kpath, value, 0, entry);
	if (ret != LY_SUCCESS) {
		nlif_assert(ret != LY_EEXIST);

		char * ppath;

		ppath = srplug_lyd_path(parent);
		nlif_notice("'%s%s%s': cannot create keyed list entry node: %s",
		            ppath ? ppath : "",
		            ppath ? "/" : "",
		            kpath,
		            ly_strerr(ret));
		srplug_free(ppath);

		ret = SR_ERR_LY;
	}

	nlif_free(kpath);

	return SR_ERR_OK;
}

int
srplug_lyd_create_leaf(struct lyd_node *  parent,
                       const char *       path,
                       const char *       value,
                       struct lyd_node ** leaf)
{
	srplug_assert(parent);
	srplug_assert(path);
	srplug_assert(path[0]);

	LY_ERR err;

	err = srplug_lyd_new_path(NULL, parent, path, value, 0, leaf);
	if (err != LY_SUCCESS) {
		nlif_assert(err != LY_EEXIST);

		char * ppath;

		ppath = srplug_lyd_path(parent);
		nlif_notice("'%s/%s': cannot create leaf node: %s",
		            ppath,
		            path,
		            ly_strerr(err));
		srplug_free(ppath);

		return SR_ERR_LY;
	}

	return SR_ERR_OK;
}

/******************************************************************************/

#include "iface.h"
#include <sysrepo/xpath.h>

const char *
nlifd_iface_admstate_str(const struct nlif_iface * interface);
const char *
nlifd_iface_name_from_node(const struct lyd_node * node,
                           char                    name[IFNAMSIZ]);

const char *
nlifd_iface_admstate_str(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	if (nlif_iface_admstate(interface))
		return "up";
	else
		return "down";
}

const char *
nlifd_iface_name_from_node(const struct lyd_node * node,
                           char                    name[IFNAMSIZ])
{
	nlif_assert(node);
	nlif_assert(!strcmp(LYD_NAME(node), "interface"));
	nlif_assert(name);

	char *         path;
	sr_xpath_ctx_t ctx;
	char *         str;
	size_t         len;

	path = srplug_lyd_path(node);
	nlif_assert(path);

	str = sr_xpath_key_value(path, "interface", "name", &ctx);
	if (!str) {
		nlif_info("'%s': invalid interface node path", path);
		goto free;
	}

	len = strnlen(str, IFNAMSIZ);
	if (!len) {
		nlif_info("'%s': missing interface name", path);
		goto free;
	}

	if (len == IFNAMSIZ) {
		nlif_info("'%s': interface name too long", path);
		goto free;
	}

	memcpy(name, str, len);
	name[len] = '\0';

	nlif_free(path);

	return name;

free:
	nlif_free(path);

	return NULL;
}

static int
nlifd_iface_new_entry(const struct ly_ctx *     context,
                      struct lyd_node *         container,
                      const struct nlif_iface * interface,
                      struct lyd_node **        entry)
{
	nlif_assert(context);
	nlif_assert(container);
	nlif_assert(interface);

	struct lyd_node * ent;
	int               ret;

	ret = srplug_lyd_create_list_keyent(context,
	                                    container,
	                                    "interface",
	                                    "name",
	                                    nlif_iface_name(interface),
	                                    &ent);
	if (ret != SR_ERR_OK)
		return ret;

	ret = srplug_lyd_create_leaf(ent, "enabled", "false", NULL);
	if (ret != SR_ERR_OK)
		return ret;

	if (entry)
		*entry = ent;

	return SR_ERR_OK;
}

#if 0
static int
nlifd_load_ifaces(sr_session_ctx_t *       session,
                  const struct nlif_repo * repository)
{
	nlif_assert(session);
	nlif_assert(repository);

	const struct ly_ctx *          ctx;
	struct lyd_node *              top;
	const struct nlif_store_hndl * hndl;
	const struct nlif_iface      * iface;
	int                            ret;

	/* TODO: check ctx != NULL */
	ctx = sr_session_acquire_context(session);

	ret = srplug_lyd_create_container(ctx,
	                                  NULL,
	                                  NLIFD_IETF_IFACE_YANG_ROOT_PATH,
	                                  &top);
	if (ret != SR_ERR_OK)
		goto release;

	nlif_store_foreach_iface(nlif_repo_store(repository), hndl, iface) {
		ret = nlifd_iface_new_entry(ctx, top, iface, NULL);
		if (ret != SR_ERR_OK)
			break;
	}

release:
	sr_session_release_context(session);

	return ret;
}
#endif

static int
nlifd_on_iface_root_change(sr_session_ctx_t * session,
                           uint32_t           sub_id __unused,
                           const char *       module __unused,
                           const char *       xpath __unused,
                           sr_event_t         event,
                           uint32_t           request_id __unused,
                           void *             repository __unused)
{
	nlif_assert(session);
	nlif_assert(repository);
	nlif_assert(!strcmp(module, NLIFD_IETF_IFACE_YANG_MODULE));
	nlif_assert(!strcmp(xpath, NLIFD_IETF_IFACE_YANG_ROOT_PATH));

	switch (event) {
	case SR_EV_ENABLED:
		{
			const struct ly_ctx * ctx;
			int                   ret;

			ret = srplug_acquire_context(session, &ctx);
			if (ret != SR_ERR_OK)
				return ret;

			ret = srplug_lyd_create_container(
				ctx,
				NULL,
				NLIFD_IETF_IFACE_YANG_ROOT_PATH,
				NULL);

			srplug_release_context(session);

			return ret;
		}

	case SR_EV_DONE:
		return SR_ERR_OK;

	case SR_EV_ABORT:
	case SR_EV_CHANGE:
	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		nlif_assert(0);
		return SR_ERR_INTERNAL;
	}
}

static const struct srplug_sub nlifd_iface_root_change_sub = {
	.kind   = SRPLUG_CHANGE_SUB_KIND,
	.change = {
		.module    = NLIFD_IETF_IFACE_YANG_MODULE,
		.xpath     = NLIFD_IETF_IFACE_YANG_ROOT_PATH,
		.on_change = nlifd_on_iface_root_change,
		.priority  = 0,
		.options   = SR_SUBSCR_ENABLED
	}
};

#if 0
static int
nlifd_on_iface_change(sr_session_ctx_t * session __unused,
                      uint32_t           sub_id __unused,
                      const char *       module __unused,
                      const char *       xpath,
                      sr_event_t         event,
                      uint32_t           request_id __unused,
                      void *             repository __unused)
{
	switch (event) {
	case SR_EV_CHANGE:
		srplug_debug("%s: changes", xpath);
		return SR_ERR_CALLBACK_FAILED;

	case SR_EV_DONE:
		srplug_debug("%s: done", xpath);
		return SR_ERR_OK;

	case SR_EV_ABORT:
		srplug_info("'%s': changes aborted", xpath);
		return SR_ERR_OK;

	case SR_EV_UPDATE:
	case SR_EV_ENABLED:
	case SR_EV_RPC:
	default:
		nlif_assert(0);
	}
}

static const struct srplug_sub nlifd_iface_change_sub = {
	.kind   = SRPLUG_CHANGE_SUB_KIND,
	.change = {
		.module    = NLIFD_IETF_IFACE_YANG_MODULE,
		.xpath     = NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface",
		.on_change = nlifd_on_iface_change,
		.priority  = 0,
		.options   = SR_SUBSCR_DEFAULT /* SR_SUBSCR_ENABLED | SR_SUBSCR_DONE_ONLY*/
	}
};

static int
nlifd_on_iface_get_admin_status(sr_session_ctx_t * session,
                                uint32_t           sub_id __unused,
                                const char *       module __unused,
                                const char *       path __unused,
                                const char *       request_xpath,
                                uint32_t           op_id __unused,
                                struct lyd_node ** parent,
                                void *             repository)
{
	nlif_assert(*parent);
	nlif_assert(!strcmp(LYD_NAME(*parent), "interface"));

	char                      name[IFNAMSIZ];
	const struct nlif_iface * iface;
	const struct ly_ctx *     ctx;
	int                       ret;

	/*SR_ERR_CALLBACK_FAILED ?? */
	/* TODO: check that feature is enabled. */

	if (!nlifd_iface_name_from_node(*parent, name)) {
		nlif_info("'%s': invalid interface path", request_xpath);
		return SR_ERR_INVAL_ARG; /*SR_ERR_NOT_FOUND ??*/
	}

	iface = nlif_store_find_iface_byname(nlif_repo_store(repository), name);
	if (!iface) {
		nlif_info("'%s': no such interface", name);
		return SR_ERR_NOT_FOUND;
	}

	/* TODO: check ctx != NULL */
	ctx = sr_session_acquire_context(session);
	ret = srplug_lyd_create_leaf(*parent,
	                             "admin-status", /* path?? */
	                             nlifd_iface_admstate_str(iface),
	                             NULL);
	if (ret != SR_ERR_OK)
		goto release;

	ret = SR_ERR_OK;

release:
	sr_session_release_context(session);

	return ret;
}

static const struct srplug_sub nlifd_iface_admin_status_sub = {
	.kind = SRPLUG_OPER_SUB_KIND,
	.oper = {
		.module  = NLIFD_IETF_IFACE_YANG_MODULE,
		.xpath   = NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface/admin-status",
		.on_get  = nlifd_on_iface_get_admin_status,
		.options = SR_SUBSCR_DEFAULT /* | SR_SUBSCR_OPER_MERGE */
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

#if 0
	ret = srplug_daemon_change_subscribe(&dmn, &nlifd_change_sub, NULL);
	if (ret)
		goto close_repo;
	ret = srplug_daemon_subscribe(&dmn,
	                              &nlifd_iface_change_sub,
	                              &repo);
	if (ret)
		goto close_repo;

	ret = srplug_daemon_subscribe(&dmn,
	                              &nlifd_iface_admin_status_sub,
	                              &repo);
	if (ret)
		goto close_repo;
#endif
	ret = srplug_daemon_subscribe(&dmn,
	                              &nlifd_iface_root_change_sub,
	                              &repo);
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
