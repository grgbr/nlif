#include "repo.h"
#include "lib/iface.h"
#include <srutils/srplug/daemon.h>
#include <srutils/srplug/data.h>
#include <srutils/srepo/schema.h>
#include "utils/string.h"
#include <sysrepo/xpath.h>
#include <sysexits.h>

/******************************************************************************
 * Various helpers.
 ******************************************************************************/

static sr_error_t
nlifd_iface_error(int error)
{
	nlif_assert(error <= 0);

	switch (error) {
	case 0:
		return SR_ERR_OK;
	case -ENODEV:
	case -ENOENT:
		return SR_ERR_NOT_FOUND;
	case -EEXIST:
		return SR_ERR_EXISTS;
	case -EINVAL:
		return SR_ERR_INVAL_ARG;
	case -ENOTSUP:
		return SR_ERR_UNSUPPORTED;
	case -EPERM:
		return SR_ERR_OPERATION_FAILED;
	case -EACCES:
		return SR_ERR_UNAUTHORIZED;
	case -ETIME:
	case -ETIMEDOUT:
		return SR_ERR_TIME_OUT;
	case -ENOLCK:
	case -EDEADLOCK:
		return SR_ERR_LOCKED;
	case -EAGAIN:
		return SR_ERR_CALLBACK_SHELVE;
	case -ENOMEM:
		return SR_ERR_NO_MEMORY;
	case -EIO:
	default:
		return SR_ERR_SYS;
	}
}

#if 0

/******************************************************************************
 * XPATH utilities.
 ******************************************************************************/

static char *
nlifd_iface_name_from_xpath(char * xpath, char name[IFNAMSIZ])
{
	srplug_assert(xpath);
	srplug_assert(name);

	sr_xpath_ctx_t ctx;
	const char *   str;
	size_t         len;
	const char *   msg;

	str = sr_xpath_key_value(xpath, "interface", "name", &ctx);
	if (!str) {
		msg = "invalid interface node path";
		goto err;
	}

	len = nlif_iface_validate_name(str);
	if (len > 0) {
		srplug_assert(len < IFNAMSIZ);

		memcpy(name, str, len);
		name[len] = '\0';

		return name;
	}

	switch (len) {
	case -ENODATA:
		msg = "missing interface name";
		break;
	case -ENAMETOOLONG:
		msg = "interface name too long";
		break;
	default:
		srplug_assert(0);
		msg = "unknown error";
	}

err:
	sr_xpath_recover(&ctx);
	srplug_path_debug(xpath, "%s", msg);

	return NULL;
}

/* Keep this for now. Just in case... */
#if 0
static struct nlif_iface *
nlifd_iface_from_xpath(const char *             xpath,
                       const struct nlif_repo * repository)
{
	srplug_assert(xpath);
	nlif_repo_assert(repository);

	char *                    pth;
	char                      name[IFNAMSIZ];
	char *                    str;
	const struct nlif_store * store = nlif_repo_store(repository);
	struct nlif_iface *       iface;

	pth = srplug_strdup(xpath);
	str = nlifd_iface_name_from_xpath(pth, name);
	srplug_free(pth);
	if (!str)
		return NULL;

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface)
		srplug_info("'%s': no such interface", name);

	return iface;
}
#endif

static const char *
nlifd_iface_name_from_node(const struct lyd_node * node,
                           char                    name[IFNAMSIZ])
{
	srplug_assert(node);
	srplug_assert(name);

	char *       path;
	const char * str;

	path = srplug_dat_path(node);
	str = nlifd_iface_name_from_xpath(path, name);
	srplug_free(path);

	return str;
}

static struct nlif_iface *
nlifd_iface_from_node(const struct lyd_node *  node,
                      const struct nlif_repo * repository)
{
	srplug_assert(node);
	srplug_assert((srepo_dat_node_type(node) & LYD_NODE_TERM) ||
	              (srepo_dat_node_type(node) == LYS_LIST));
	nlif_repo_assert(repository);

	char                      name[IFNAMSIZ];
	const struct nlif_store * store = nlif_repo_store(repository);
	struct nlif_iface *       iface;

	if (!nlifd_iface_name_from_node(node, name))
		return NULL;

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface)
		srplug_info("'%s': no such interface", name);

	return iface;
}

/******************************************************************************
 * Netlink interface fields adapters.
 ******************************************************************************/

static const char *
nlifd_iface_type_str(unsigned short type, const char * kind)
{
	switch (type) {
	case ARPHRD_ETHER:
		{
			if (!kind)
				return "iana-if-type:ethernetCsmacd";
			else if (!strcmp(kind, "bridge"))
				return "iana-if-type:bridge";
			else if (!strcmp(kind, "dummy"))
				return "iana-if-type:other";
			break;
		}

	case ARPHRD_LOOPBACK:
		return "iana-if-type:softwareLoopback";

	default:
		break;
	}

	/* Unsupported. */
	return NULL;
}

static int
nlifd_iface_coherent_type_str(struct nlif_iface * interface, const char ** type)
{
	unsigned short tp;
	int            ret;
	const char *   str;

	ret = nlif_iface_coherent_type(interface, &tp);
	if (ret)
		return ret;

	str = nlifd_iface_type_str(tp, nlif_iface_kind(interface));
	if (!str)
		return -ENOTSUP;

	*type = str;

	return 0;
}

static sr_error_t
nlifd_iface_fill_admstate(struct lyd_node *   entry,
                          struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	bool up;
	int  ret;

	ret = nlif_iface_coherent_admstate(interface, &up);
	if (ret)
		return nlifd_iface_error(ret);

	return srplug_dat_create_leaf(entry,
	                              "admin-status",
	                              up ? "up" : "down",
	                              NULL);
}

static sr_error_t
nlifd_iface_fill_operstate(struct lyd_node *   entry,
                           struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	unsigned char st;
	int           ret;

	ret = nlif_iface_coherent_operstate(interface, &st);
	if (ret)
		return nlifd_iface_error(ret);

	return srplug_dat_create_leaf(entry,
	                              "oper-status",
	                              nlif_link_operstate_str(st),
	                              NULL);
}

static sr_error_t
nlifd_iface_fill_index(struct lyd_node * entry, struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	unsigned int idx = nlif_iface_index(interface);

	return srplug_dat_create_leaf_printf(entry,
	                                     "if-index",
	                                     NULL,
	                                     "%u",
	                                     idx);
}

static sr_error_t
nlifd_iface_fill_hwaddr(struct lyd_node * entry, struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	const struct ether_addr * addr;
	int                       ret;
	char                      str[NLIF_LINK_HWADDR_STRSZ];

	ret = nlif_iface_coherent_hwaddr(interface, &addr);
	if (ret)
		return nlifd_iface_error(ret);

	return srplug_dat_create_leaf(entry,
	                              "phys-address",
	                              nlif_link_hwaddr_str(addr, str),
	                              NULL);
}

/******************************************************************************
 * Subscription events handling.
 ******************************************************************************/

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"

#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces"

#define NLIFD_IETF_IFACE_YANG_LIST_PATH \
	NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface"

static struct srplug_feat
nlifd_arbitrary_names_feat = SRPLUG_FEAT_SETUP("arbitrary-names");

static struct srplug_feat
nlifd_pre_provisioning_feat = SRPLUG_FEAT_SETUP("pre-provisioning");

static struct srplug_feat
nlifd_if_mib_feat = SRPLUG_FEAT_SETUP("if-mib");

static sr_error_t
nlifd_iface_change_enabled(const struct lyd_node * leaf,
                           sr_change_oper_t        oper,
                           const char *            old,
                           void *                  repository)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "no such interface");
		return SR_ERR_NOT_FOUND;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		val = srepo_dat_node_as_bool(leaf);
		break;

	case SR_OP_MODIFIED:
		srplug_assert(old);
		val = srepo_dat_node_as_bool(leaf);
		break;

	case SR_OP_DELETED:
		srplug_assert(old);
		if (srepo_dat_node_dflt_as_bool(leaf, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_UNSUPPORTED;
	}

	ret = nlif_iface_set_admstate(iface, val);
	if (ret) {
		srplug_node_notice(leaf,
		                   "cannot change interface: %s",
		                   strerror(-ret));
		return nlifd_iface_error(ret);
	}

	srplug_node_debug(leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_enabled(const struct lyd_node * leaf,
                            sr_change_oper_t        oper,
                            const char *            old,
                            void *                  repository)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_err(leaf,
		                "cannot restore interface: no such interface");
		return SR_ERR_OK;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		if (srepo_dat_node_dflt_as_bool(leaf, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MODIFIED:
	case SR_OP_DELETED:
		srplug_assert(old);
		ret = ustr_parse_bool(old, &val);
		if (ret) {
			srplug_node_err(leaf,
			                "cannot restore interface: "
			                "'%s': invalid value",
			                old);
			return SR_ERR_OK;
		}
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_OK;
	}

	ret = nlif_iface_set_admstate(iface, val);
	if (ret)
		srplug_node_err(leaf,
		                "cannot restore interface: %s",
		                strerror(-ret));
	else
		srplug_node_debug(leaf, "interface restored");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_change_type(const struct lyd_node * leaf,
                        sr_change_oper_t        oper,
                        const char *            old,
                        void *                  repository)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	struct nlif_iface * iface;
	const char *        val = srepo_dat_node_as_str(leaf);
	const char *        type;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "no such interface");
		return SR_ERR_NOT_FOUND;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		break;

	case SR_OP_MODIFIED:
		srplug_assert(old);
		break;

	case SR_OP_DELETED:
		srplug_assert(old);
		return SR_ERR_OK;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_UNSUPPORTED;
	}

	ret = nlifd_iface_coherent_type_str(iface, &type);
	if (ret) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "failed to retrieve type: %s",
		                   strerror(-ret));
		return nlifd_iface_error(ret);
	}

	if (strcmp(type, val)) {
		srplug_node_notice(leaf,
		                   "cannot change interface: %s",
		                   sr_strerror(SR_ERR_UNSUPPORTED));
		return SR_ERR_UNSUPPORTED;
	}

	srplug_node_debug(leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_type(const struct lyd_node * leaf,
                         sr_change_oper_t        oper __unused,
                         const char *            old __unused,
                         void *                  repository __unused)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	srplug_node_debug(leaf, "interface restored");

	return SR_ERR_OK;
}

static const struct srplug_change_hndlr nlifd_iface_change_hndlrs[] = {
	SRPLUG_CHANGE_HNDLR("enabled", NULL, nlifd_iface_change_enabled),
	SRPLUG_CHANGE_HNDLR("type",    NULL, nlifd_iface_change_type)
};

static const struct srplug_change_hndlr nlifd_iface_restore_hndlrs[] = {
	SRPLUG_CHANGE_HNDLR("enabled", NULL, nlifd_iface_restore_enabled),
	SRPLUG_CHANGE_HNDLR("type",    NULL, nlifd_iface_restore_type)
};

static sr_error_t
nlifd_iface_process_aborts(sr_session_ctx_t * session,
                           const char *       xpath,
                           struct nlif_repo * repository)
{
	int                            ret;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;

	/* Since restore handlers MUST not fail, this should never fail... */
	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_restore_hndlrs,
		stroll_array_nr(nlifd_iface_restore_hndlrs),
		repository);

	/*
	 *  ...unless Sysrepo internals fail to retrieve subscription events.
	 * Keep trying to restore as many interfaces as we can however.
	 */
	nlif_store_foreach_iface(nlif_repo_store(repository), hndl, iface) {
		int err;

		err = nlif_iface_apply(iface);
		if (err < 0) {
			srplug_path_err(
				xpath,
				"[%u]: cannot abort interface changes: "
				"%s",
				nlif_iface_index(iface),
				strerror(-err));
			if (ret == SR_ERR_OK)
				ret = nlifd_iface_error(err);
		}
		else if (err)
			srplug_path_debug(xpath,
			                  "%s[%u]: "
			                  "interface changes aborted",
			                  nlif_iface_index(iface),
			                  nlif_iface_name(iface));
	}

	return ret;
}

static sr_error_t
nlifd_iface_process_changes(sr_session_ctx_t * session,
                            const char *       xpath,
                            struct nlif_repo * repository)
{
	int ret;

	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_change_hndlrs,
		stroll_array_nr(nlifd_iface_change_hndlrs),
		repository);
	if (ret == SR_ERR_OK) {
		const struct nlif_store_hndl * hndl;
		struct nlif_iface *            iface;

		nlif_store_foreach_iface(nlif_repo_store(repository),
		                         hndl,
		                         iface) {
			ret = nlif_iface_apply(iface);
			if (ret < 0) {
				srplug_path_notice(
					xpath,
					"[%u]: cannot apply interface changes: "
					"%s",
					nlif_iface_index(iface),
					strerror(-ret));
				nlifd_iface_process_aborts(session,
				                           xpath,
				                           repository);
				return nlifd_iface_error(ret);
			}

			if (ret) {
				srplug_path_debug(xpath,
				                  "%s[%u]: "
				                  "interface changes applied",
				                  nlif_iface_name(iface),
				                  nlif_iface_index(iface));
				ret = SR_ERR_OK;
			}
		}
	}

	return ret;
}

static int
nlifd_on_iface_change(sr_session_ctx_t * session,
                      uint32_t           sub_id __unused,
                      const char *       module __unused,
                      const char *       xpath,
                      sr_event_t         event,
                      uint32_t           request_id __unused,
                      void *             repository)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_repo_assert((const struct nlif_repo *)repository);

	sr_error_t   ret;
	const char * act;

	switch (event) {
	case SR_EV_ENABLED:
		ret = nlifd_iface_process_changes(session, xpath, repository);
		act = "load";
		break;

	case SR_EV_CHANGE:
		ret = nlifd_iface_process_changes(session, xpath, repository);
		act = "apply";
		break;

	case SR_EV_DONE:
		srplug_path_debug(xpath, "interfaces configured");
		return SR_ERR_OK;

	case SR_EV_ABORT:
		ret = nlifd_iface_process_aborts(session, xpath, repository);
		act = "abort";
		break;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}

	if (ret != SR_ERR_OK)
		srplug_path_warn(xpath,
		                 "failed to %s interfaces configuration: %s",
		                 act,
		                 sr_strerror(ret));

	return ret;
}

typedef sr_error_t
        nlifd_iface_fill_entry_leaf_fn(struct lyd_node *, struct nlif_iface *);

static sr_error_t
nlifd_on_iface_get_entry_leaf(sr_session_ctx_t *               session,
                              struct lyd_node *                entry,
                              const char *                     leaf __unused,
                              nlifd_iface_fill_entry_leaf_fn * fill,
                              struct nlif_repo *               repository)
{
	srplug_assert(session);
	srplug_assert(entry);
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	srplug_assert(leaf);
	srplug_assert(leaf[0]);
	srplug_assert(fill);
	nlif_repo_assert(repository);

	struct nlif_iface *   iface;
	int                   ret;
	const char *          msg;

	iface = nlifd_iface_from_node(entry, repository);
	if (!iface) {
		msg = "no such interface";
		ret = SR_ERR_NOT_FOUND;
		goto err;
	}

	ret = fill(entry, iface);
	if (ret) {
		msg = sr_strerror(ret);
		goto err;
	}

	srplug_node_debug(entry, "interface %s filled in", leaf);

	return SR_ERR_OK;

err:
	srplug_node_notice(entry,
	                   "cannot fill in interface %s: %s",
	                   leaf,
	                   msg);

	return ret;
}

static int
nlifd_on_iface_get_admin_status(sr_session_ctx_t * session,
                                uint32_t           sub_id __unused,
                                const char *       module __unused,
                                const char *       path __unused,
                                const char *       request_xpath __unused,
                                uint32_t           op_id __unused,
                                struct lyd_node ** parent,
                                void *             repository)
{
	srplug_assert(parent);
	srplug_assert(repository);

	return nlifd_on_iface_get_entry_leaf(session,
	                                     *parent,
	                                     "admin-status",
	                                     nlifd_iface_fill_admstate,
	                                     repository);
}

static int
nlifd_on_iface_get_oper_status(sr_session_ctx_t * session,
                               uint32_t           sub_id __unused,
                               const char *       module __unused,
                               const char *       path __unused,
                               const char *       request_xpath __unused,
                               uint32_t           op_id __unused,
                               struct lyd_node ** parent,
                               void *             repository)
{
	srplug_assert(parent);
	srplug_assert(repository);

	return nlifd_on_iface_get_entry_leaf(session,
	                                     *parent,
	                                     "oper-status",
	                                     nlifd_iface_fill_operstate,
	                                     repository);
}

static int
nlifd_on_iface_get_if_index(sr_session_ctx_t * session,
                            uint32_t           sub_id __unused,
                            const char *       module __unused,
                            const char *       path __unused,
                            const char *       request_xpath __unused,
                            uint32_t           op_id __unused,
                            struct lyd_node ** parent,
                            void *             repository)
{
	srplug_assert(parent);
	srplug_assert(repository);

	return nlifd_on_iface_get_entry_leaf(session,
	                                     *parent,
	                                     "if-index",
	                                     nlifd_iface_fill_index,
	                                     repository);
}

static int
nlifd_on_iface_get_phy_address(sr_session_ctx_t * session,
                               uint32_t           sub_id __unused,
                               const char *       module __unused,
                               const char *       path __unused,
                               const char *       request_xpath __unused,
                               uint32_t           op_id __unused,
                               struct lyd_node ** parent,
                               void *             repository)
{
	srplug_assert(parent);
	srplug_assert(repository);

	return nlifd_on_iface_get_entry_leaf(session,
	                                     *parent,
	                                     "phys-address",
	                                     nlifd_iface_fill_hwaddr,
	                                     repository);
}

/* Configuration change subscription. */
static const struct srplug_change_sub nlifd_change_sub = {
	.module    = NLIFD_IETF_IFACE_YANG_MODULE,
	.xpath     = NLIFD_IETF_IFACE_YANG_LIST_PATH,
	.feature   = NULL,
	.on_change = nlifd_on_iface_change,
	.priority  = 0,
	.options   = SR_SUBSCR_DEFAULT/*| SR_SUBSCR_ENABLED*/
};

/* Operational state subscription. */
static const struct srplug_sub nlifd_oper_subs[] = {
	SRPLUG_OPER_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                NLIFD_IETF_IFACE_YANG_LIST_PATH "/admin-status",
	                &nlifd_if_mib_feat,
	                nlifd_on_iface_get_admin_status,
	                0,
	                SR_SUBSCR_DEFAULT),
	SRPLUG_OPER_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                NLIFD_IETF_IFACE_YANG_LIST_PATH "/oper-status",
	                NULL,
	                nlifd_on_iface_get_oper_status,
	                0,
	                SR_SUBSCR_DEFAULT),
	SRPLUG_OPER_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                NLIFD_IETF_IFACE_YANG_LIST_PATH "/if-index",
	                &nlifd_if_mib_feat,
	                nlifd_on_iface_get_if_index,
	                0,
	                SR_SUBSCR_DEFAULT),
	SRPLUG_OPER_SUB(NLIFD_IETF_IFACE_YANG_MODULE,
	                NLIFD_IETF_IFACE_YANG_LIST_PATH "/phys-address",
	                NULL,
	                nlifd_on_iface_get_phy_address,
	                0,
	                SR_SUBSCR_DEFAULT),
};

/******************************************************************************
 * Interfaces configuration data store handling.
 ******************************************************************************/

#if 0
static struct nlif_obsrv_subscriber nlifd_iface_store_sub;

static sr_error_t
nlifd_iface_refresh_open_entry(sr_session_ctx_t * session,
                               struct nlif_iface * interface)
{
	sr_data_t * data;
	sr_error_t  ret;

	ret = sr_get_oper_changes(session, NLIFD_IETF_IFACE_YANG_MODULE, &data);
	ret = lyd_find_path(data->tree, )
}
#endif

static sr_error_t
nlifd_iface_new_entry(const struct ly_ctx * context,
                      struct lyd_node *     container,
                      struct nlif_iface *   interface,
                      struct lyd_node **    entry)
{
	srplug_assert(context);
	srplug_assert(container);
	srplug_assert(interface);
	srplug_assert(nlif_iface_state(interface) == NLIF_CLEAN_STAT);

	struct lyd_node * ent;
	const char *      str;
	int               ret;

	ret = srplug_dat_create_list_keyent(context,
	                                    container,
	                                    "interface",
	                                    "name",
	                                    nlif_iface_name(interface),
	                                    &ent);
	if (ret != SR_ERR_OK)
		return ret;

	/* Create default nodes as defined by the interface YANG model. */
	ret = srplug_dat_populate_defaults(ent, LYD_IMPLICIT_NO_STATE, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	str = nlifd_iface_type_str(nlif_iface_type(interface),
	                           nlif_iface_kind(interface));
	if (!str) {
		srplug_node_info(ent, "unsupported interface type");
		return SR_ERR_UNSUPPORTED;
	}
	ret = srplug_dat_create_leaf(ent, "type", str, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	if (entry)
		*entry = ent;

	return SR_ERR_OK;
}

static int
nlifd_iface_dstore_empty(sr_session_ctx_t * session, bool * empty)
{
	sr_data_t * data;
	sr_error_t  ret;
	
	ret = srplug_dat_load_data(session,
	                           NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface",
	                           1,
	                           0,
	                           &data);
	switch (ret) {
	case SR_ERR_OK:
		*empty = false;
		break;

	case SR_ERR_NOT_FOUND:
		*empty = true;
		break;

	default:
		return ret;
	}

	srplug_dat_release_data(data);

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_reset_config_dstore(sr_session_ctx_t *       session,
                                const struct ly_ctx *    context,
                                const struct nlif_repo * repository)
{
	srplug_assert(session);
	srplug_assert(context);
	nlif_repo_assert(repository);

	struct lyd_node *              top;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;
	int                            err;

	err = srplug_dat_create_container(context,
	                                  NULL,
	                                  NLIFD_IETF_IFACE_YANG_ROOT_PATH,
	                                  &top);
	if (err != SR_ERR_OK)
		return err;

	nlif_store_foreach_iface(nlif_repo_store(repository), hndl, iface) {
		err = nlifd_iface_new_entry(context, top, iface, NULL);
		if (err != SR_ERR_OK)
			goto free;
	}

	/*
	 * Here, `top' tree ownership is transfered to srplug_replace_dstore()
	 * so that it will be freed once it has returned thanks to a call to
	 * lyd_free_all().
	 * This means we are not requested to call lyd_free_tree() on it from
	 * now on.
	 * Also note that any missing implicit (default) nodes will be added to
	 * the data tree.
	 */
	return srplug_replace_config(session,
	                             NLIFD_IETF_IFACE_YANG_MODULE,
	                             top);

free:
	srplug_dat_free_tree(top);

	return err;
}

static sr_error_t
nlifd_iface_fill_oper_entry(struct lyd_node *   entry,
                            struct nlif_iface * interface)
{
	srplug_assert(entry);
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	srplug_assert(interface);
	srplug_assert(nlif_iface_state(interface) == NLIF_CLEAN_STAT);

	int ret;

	if (nlifd_if_mib_feat.on) {
		ret = nlifd_iface_fill_admstate(entry, interface);
		if (ret != SR_ERR_OK)
			return ret;
	}

	ret = nlifd_iface_fill_operstate(entry, interface);
	if (ret != SR_ERR_OK)
		return ret;

	if (nlifd_if_mib_feat.on) {
		ret = nlifd_iface_fill_index(entry, interface);
		if (ret != SR_ERR_OK)
			return ret;
	}

	ret = nlifd_iface_fill_hwaddr(entry, interface);
	if (ret != SR_ERR_OK)
		return ret;

	srplug_node_debug(entry, "interface operational status filled in");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_setup_oper_dstore(sr_session_ctx_t *       session,
                              const struct nlif_repo * repository)
{
	srplug_assert(session);
	nlif_repo_assert(repository);

	sr_data_t *       data;
	sr_error_t        ret;
	struct lyd_node * node;

	/*
	 * Get data from the top-level container as
	 * srplug_dat_merge_data_batch() requires top-level data trees to
	 * prepare its batch of data changes.
	 */
	ret = srplug_dat_load_data(session,
	                           NLIFD_IETF_IFACE_YANG_ROOT_PATH,
	                           2,
	                           SR_OPER_NO_CONFIG | SR_OPER_NO_SUBS,
	                           &data);
	if (ret)
		return ret;

	srepo_dat_foreach_child(data->tree, node) {
		struct nlif_iface * iface;

		iface = nlifd_iface_from_node(node, repository);
		srplug_assert(iface);

		ret = nlifd_iface_fill_oper_entry(node, iface);
		if (ret != SR_ERR_OK)
			goto release;
	}

	ret = srplug_dat_merge_data_batch(session, data);
	if (ret != SR_ERR_OK)
		goto release;

	ret = srplug_apply_changes(session);

release:
	srplug_dat_release_data(data);

	return ret;
}

/******************************************************************************
 * Top-level logic.
 ******************************************************************************/

static int
nlifd_load_config(struct srplug_daemon * daemon,
                  struct nlif_repo *     repository)
{
	sr_session_ctx_t *    sess = srplug_daemon_session(daemon);
	const struct ly_ctx * ctx;
	bool                  empty;
	int                   ret;

	ret = srplug_acquire_context(sess, &ctx);
	if (ret != SR_ERR_OK)
		goto err;

	ret = SR_ERR_NOT_FOUND;
	if (!srplug_find_module(ctx, "iana-if-type"))
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_arbitrary_names_feat) != SR_ERR_OK)
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_pre_provisioning_feat) != SR_ERR_OK)
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_if_mib_feat) != SR_ERR_OK)
		goto release;
	if (!srplug_find_module(ctx, "netlink-interfaces"))
		goto release;

	ret = nlifd_iface_dstore_empty(sess, &empty);
	if (ret != SR_ERR_OK)
		goto release;
	if (empty) {
		ret = nlifd_iface_reset_config_dstore(sess, ctx, repository);
		if (ret != SR_ERR_OK)
			goto release;
	}

	ret = srplug_daemon_change_subscribe(daemon,
	                                     &nlifd_change_sub,
	                                     repository);
	if (ret != SR_ERR_OK)
		goto release;

	srplug_release_context(sess);

	return 0;

release:
	srplug_release_context(sess);
err:
	srplug_err("cannot load interfaces configuration: %s",
	           sr_strerror(ret));

	return -EPERM;
}

static int
nlifd_load_oper(const struct srplug_daemon * daemon,
                const struct nlif_repo *     repository)
{
	sr_session_ctx_t * sess = srplug_daemon_session(daemon);
	sr_error_t         ret;

	ret = sr_session_switch_ds(sess, SR_DS_OPERATIONAL);
	if (ret)
		goto err;

	ret = nlifd_iface_setup_oper_dstore(sess, repository);
	if (ret != SR_ERR_OK)
		goto err;

	return 0;

err:
	srplug_err("cannot load interfaces operational status: %s",
	           sr_strerror(ret));

	return -EPERM;
}

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
		srplug_daemon_free_conf(cfg);
		return;

	case 1:
		/* Help informations have been requested on the command line. */
		ret = EXIT_SUCCESS;
		break;

	default:
		/* Command line syntax or parsing error. */
		ret = EX_USAGE;
	}

	srplug_daemon_free_conf(cfg);

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

	/*
	 * Setup running data store, i.e., load interfaces configuration data.
	 */
	ret = nlifd_load_config(&dmn, &repo);
	if (ret)
		goto close_repo;

	/*
	 * Setup operational data store, i.e., load interfaces status data.
	 * Note: current session datastore has switched to SR_DS_OPERATIONAL
	 * once returned from nlifd_load_oper().
	 */
	ret = nlifd_load_oper(&dmn, &repo);
	if (ret)
		goto close_repo;

	ret = srplug_daemon_subscribe_all(&dmn,
	                                  nlifd_oper_subs,
	                                  stroll_array_nr(nlifd_oper_subs),
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
#endif

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"

#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces"

#define NLIFD_IETF_IFACE_YANG_LIST_PATH \
	NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface"

/******************************************************************************
 * XPATH utilities.
 ******************************************************************************/

static char *
nlifd_iface_name_from_xpath(char * xpath, char name[IFNAMSIZ])
{
	srplug_assert(xpath);
	srplug_assert(name);

	sr_xpath_ctx_t ctx;
	const char *   str;
	size_t         len;
	const char *   msg;

	str = sr_xpath_key_value(xpath, "interface", "name", &ctx);
	if (!str) {
		msg = "invalid interface node path";
		goto err;
	}

	len = nlif_iface_validate_name(str);
	if (len > 0) {
		srplug_assert(len < IFNAMSIZ);

		memcpy(name, str, len);
		name[len] = '\0';

		return name;
	}

	switch (len) {
	case -ENODATA:
		msg = "missing interface name";
		break;
	case -ENAMETOOLONG:
		msg = "interface name too long";
		break;
	default:
		srplug_assert(0);
		msg = "unknown error";
	}

err:
	sr_xpath_recover(&ctx);
	srplug_path_debug(xpath, "%s", msg);

	return NULL;
}

/* Keep this for now. Just in case... */
#if 0
static struct nlif_iface *
nlifd_iface_from_xpath(const char * xpath, const struct nlif_store * store)
{
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_store_assert(store);

	char *              pth;
	char                name[IFNAMSIZ];
	char *              str;
	struct nlif_iface * iface;

	pth = srplug_strdup(xpath);
	str = nlifd_iface_name_from_xpath(pth, name);
	srplug_free(pth);
	if (!str)
		return NULL;

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface)
		srplug_path_info(xpath, "'%s': no such interface", name);

	return iface;
}
#endif

static const char *
nlifd_iface_name_from_node(const struct lyd_node * node,
                           char                    name[IFNAMSIZ])
{
	srplug_assert(node);
	srplug_assert(name);

	char *       path;
	const char * str;

	path = srplug_dat_path(node);
	str = nlifd_iface_name_from_xpath(path, name);
	srplug_free(path);

	return str;
}

static struct nlif_iface *
nlifd_iface_from_node(const struct lyd_node *   node,
                      const struct nlif_store * store)
{
	srplug_assert(node);
	srplug_assert((srepo_dat_node_type(node) & LYD_NODE_TERM) ||
	              (srepo_dat_node_type(node) == LYS_LIST));
	nlif_store_assert(store);

	char                name[IFNAMSIZ];
	struct nlif_iface * iface;

	if (!nlifd_iface_name_from_node(node, name))
		return NULL;

	iface = nlif_store_find_iface_byname(store, name);
	if (!iface)
		srplug_node_info(node, "'%s': no such interface", name);

	return iface;
}

/******************************************************************************
 * Netlink interface fields adapters.
 ******************************************************************************/

static const char *
nlifd_iface_type_str(unsigned short type, const char * kind)
{
	switch (type) {
	case ARPHRD_ETHER:
		{
			if (!kind)
				return "iana-if-type:ethernetCsmacd";
			else if (!strcmp(kind, "bridge"))
				return "iana-if-type:bridge";
			else if (!strcmp(kind, "dummy"))
				return "iana-if-type:other";
			break;
		}

	case ARPHRD_LOOPBACK:
		return "iana-if-type:softwareLoopback";

	default:
		break;
	}

	/* Unsupported. */
	return NULL;
}

static int
nlifd_iface_coherent_type_str(struct nlif_iface * interface, const char ** type)
{
	unsigned short tp;
	int            ret;
	const char *   str;

	ret = nlif_iface_coherent_type(interface, &tp);
	if (ret)
		return ret;

	str = nlifd_iface_type_str(tp, nlif_iface_kind(interface));
	if (!str)
		return -ENOTSUP;

	*type = str;

	return 0;
}

static sr_error_t
nlifd_iface_fill_admstate(struct lyd_node *   entry,
                          struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	bool up;
	int  ret;

	ret = nlif_iface_coherent_admstate(interface, &up);
	if (ret)
		return nlifd_iface_error(ret);

	return srplug_dat_create_leaf(entry,
	                              "admin-status",
	                              up ? "up" : "down",
	                              NULL);
}

static sr_error_t
nlifd_iface_fill_operstate(struct lyd_node *   entry,
                           struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	unsigned char st;
	int           ret;

	ret = nlif_iface_coherent_operstate(interface, &st);
	if (ret)
		return nlifd_iface_error(ret);

	return srplug_dat_create_leaf(entry,
	                              "oper-status",
	                              nlif_link_operstate_str(st),
	                              NULL);
}

static sr_error_t
nlifd_iface_fill_index(struct lyd_node * entry, struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	unsigned int idx = nlif_iface_index(interface);

	return srplug_dat_create_leaf_printf(entry,
	                                     "if-index",
	                                     NULL,
	                                     "%u",
	                                     idx);
}

static sr_error_t
nlifd_iface_fill_hwaddr(struct lyd_node * entry, struct nlif_iface * interface)
{
	srplug_assert(!strcmp(srepo_dat_node_name(entry), "interface"));
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	nlif_iface_assert(interface);

	const struct ether_addr * addr;
	int                       ret;
	char                      str[NLIF_LINK_HWADDR_STRSZ];

	ret = nlif_iface_coherent_hwaddr(interface, &addr);
	if (ret)
		return nlifd_iface_error(ret);

	return srplug_dat_create_leaf(entry,
	                              "phys-address",
	                              nlif_link_hwaddr_str(addr, str),
	                              NULL);
}

/******************************************************************************
 * Interfaces datastore contente handling.
 ******************************************************************************/

static struct srplug_feat
nlifd_arbitrary_names_feat = SRPLUG_FEAT_SETUP("arbitrary-names");

static struct srplug_feat
nlifd_pre_provisioning_feat = SRPLUG_FEAT_SETUP("pre-provisioning");

static struct srplug_feat
nlifd_if_mib_feat = SRPLUG_FEAT_SETUP("if-mib");

static sr_error_t
nlifd_iface_new_entry(const struct ly_ctx * context,
                      struct lyd_node *     container,
                      struct nlif_iface *   interface,
                      struct lyd_node **    entry)
{
	srplug_assert(context);
	srplug_assert(container);
	srplug_assert(interface);
	srplug_assert(nlif_iface_state(interface) == NLIF_CLEAN_STAT);

	struct lyd_node * ent;
	const char *      str;
	int               ret;

	ret = srplug_dat_create_list_keyent(context,
	                                    container,
	                                    "interface",
	                                    "name",
	                                    nlif_iface_name(interface),
	                                    &ent);
	if (ret != SR_ERR_OK)
		return ret;

	/* Create default nodes as defined by the interface YANG model. */
	ret = srplug_dat_populate_defaults(ent, LYD_IMPLICIT_NO_STATE, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	str = nlifd_iface_type_str(nlif_iface_type(interface),
	                           nlif_iface_kind(interface));
	if (!str) {
		srplug_node_info(ent, "unsupported interface type");
		return SR_ERR_UNSUPPORTED;
	}
	ret = srplug_dat_create_leaf(ent, "type", str, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	if (entry)
		*entry = ent;

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_fill_oper_entry(struct lyd_node *   entry,
                            struct nlif_iface * interface)
{
	srplug_assert(entry);
	srplug_assert(srepo_dat_node_type(entry) == LYS_LIST);
	srplug_assert(interface);
	srplug_assert(nlif_iface_state(interface) == NLIF_CLEAN_STAT);

	int ret;

	if (nlifd_if_mib_feat.on) {
		ret = nlifd_iface_fill_admstate(entry, interface);
		if (ret != SR_ERR_OK)
			return ret;
	}

	ret = nlifd_iface_fill_operstate(entry, interface);
	if (ret != SR_ERR_OK)
		return ret;

	if (nlifd_if_mib_feat.on) {
		ret = nlifd_iface_fill_index(entry, interface);
		if (ret != SR_ERR_OK)
			return ret;
	}

	ret = nlifd_iface_fill_hwaddr(entry, interface);
	if (ret != SR_ERR_OK)
		return ret;

	srplug_node_debug(entry, "interface operational status filled in");

	return SR_ERR_OK;
}

/******************************************************************************
 * Subscription events handling.
 ******************************************************************************/

static sr_error_t
nlifd_iface_change_enabled(const struct lyd_node * leaf,
                           sr_change_oper_t        oper,
                           const char *            old,
                           void *                  repository)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "no such interface");
		return SR_ERR_NOT_FOUND;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		val = srepo_dat_node_as_bool(leaf);
		break;

	case SR_OP_MODIFIED:
		srplug_assert(old);
		val = srepo_dat_node_as_bool(leaf);
		break;

	case SR_OP_DELETED:
		srplug_assert(old);
		if (srepo_dat_node_dflt_as_bool(leaf, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_UNSUPPORTED;
	}

	ret = nlif_iface_set_admstate(iface, val);
	if (ret) {
		srplug_node_notice(leaf,
		                   "cannot change interface: %s",
		                   strerror(-ret));
		return nlifd_iface_error(ret);
	}

	srplug_node_debug(leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_enabled(const struct lyd_node * leaf,
                            sr_change_oper_t        oper,
                            const char *            old,
                            void *                  repository)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_err(leaf,
		                "cannot restore interface: no such interface");
		return SR_ERR_OK;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		if (srepo_dat_node_dflt_as_bool(leaf, &val) != SR_ERR_OK)
			val = false;
		break;

	case SR_OP_MODIFIED:
	case SR_OP_DELETED:
		srplug_assert(old);
		ret = ustr_parse_bool(old, &val);
		if (ret) {
			srplug_node_err(leaf,
			                "cannot restore interface: "
			                "'%s': invalid value",
			                old);
			return SR_ERR_OK;
		}
		break;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_OK;
	}

	ret = nlif_iface_set_admstate(iface, val);
	if (ret)
		srplug_node_err(leaf,
		                "cannot restore interface: %s",
		                strerror(-ret));
	else
		srplug_node_debug(leaf, "interface restored");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_change_type(const struct lyd_node * leaf,
                        sr_change_oper_t        oper,
                        const char *            old,
                        void *                  repository)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	struct nlif_iface * iface;
	const char *        val = srepo_dat_node_as_str(leaf);
	const char *        type;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, repository);
	if (!iface) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "no such interface");
		return SR_ERR_NOT_FOUND;
	}

	switch (oper) {
	case SR_OP_CREATED:
		srplug_assert(!old);
		break;

	case SR_OP_MODIFIED:
		srplug_assert(old);
		break;

	case SR_OP_DELETED:
		srplug_assert(old);
		return SR_ERR_OK;

	case SR_OP_MOVED:
	default:
		srplug_assert(0);
		return SR_ERR_UNSUPPORTED;
	}

	ret = nlifd_iface_coherent_type_str(iface, &type);
	if (ret) {
		srplug_node_notice(leaf,
		                   "cannot change interface: "
		                   "failed to retrieve type: %s",
		                   strerror(-ret));
		return nlifd_iface_error(ret);
	}

	if (strcmp(type, val)) {
		srplug_node_notice(leaf,
		                   "cannot change interface: %s",
		                   sr_strerror(SR_ERR_UNSUPPORTED));
		return SR_ERR_UNSUPPORTED;
	}

	srplug_node_debug(leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_type(const struct lyd_node * leaf,
                         sr_change_oper_t        oper __unused,
                         const char *            old __unused,
                         void *                  repository __unused)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_repo_assert((const struct nlif_repo *)repository);

	srplug_node_debug(leaf, "interface restored");

	return SR_ERR_OK;
}

static const struct srplug_change_hndlr nlifd_iface_change_hndlrs[] = {
	SRPLUG_CHANGE_HNDLR("enabled", NULL, nlifd_iface_change_enabled),
	SRPLUG_CHANGE_HNDLR("type",    NULL, nlifd_iface_change_type)
};

static const struct srplug_change_hndlr nlifd_iface_restore_hndlrs[] = {
	SRPLUG_CHANGE_HNDLR("enabled", NULL, nlifd_iface_restore_enabled),
	SRPLUG_CHANGE_HNDLR("type",    NULL, nlifd_iface_restore_type)
};

static sr_error_t
nlifd_iface_process_aborts(sr_session_ctx_t * session,
                           const char *       xpath,
                           struct nlif_repo * repository)
{
	int                            ret;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;

	/* Since restore handlers MUST not fail, this should never fail... */
	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_restore_hndlrs,
		stroll_array_nr(nlifd_iface_restore_hndlrs),
		repository);

	/*
	 *  ...unless Sysrepo internals fail to retrieve subscription events.
	 * Keep trying to restore as many interfaces as we can however.
	 */
	nlif_store_foreach_iface(nlif_repo_store(repository), hndl, iface) {
		int err;

		err = nlif_iface_apply(iface);
		if (err < 0) {
			srplug_path_err(
				xpath,
				"[%u]: cannot abort interface changes: "
				"%s",
				nlif_iface_index(iface),
				strerror(-err));
			if (ret == SR_ERR_OK)
				ret = nlifd_iface_error(err);
		}
		else if (err)
			srplug_path_debug(xpath,
			                  "%s[%u]: "
			                  "interface changes aborted",
			                  nlif_iface_index(iface),
			                  nlif_iface_name(iface));
	}

	return ret;
}

static sr_error_t
nlifd_iface_process_changes(sr_session_ctx_t * session,
                            const char *       xpath,
                            struct nlif_repo * repository)
{
	int ret;

	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_change_hndlrs,
		stroll_array_nr(nlifd_iface_change_hndlrs),
		repository);
	if (ret == SR_ERR_OK) {
		const struct nlif_store_hndl * hndl;
		struct nlif_iface *            iface;

		nlif_store_foreach_iface(nlif_repo_store(repository),
		                         hndl,
		                         iface) {
			ret = nlif_iface_apply(iface);
			if (ret < 0) {
				srplug_path_notice(
					xpath,
					"[%u]: cannot apply interface changes: "
					"%s",
					nlif_iface_index(iface),
					strerror(-ret));
				nlifd_iface_process_aborts(session,
				                           xpath,
				                           repository);
				return nlifd_iface_error(ret);
			}

			if (ret) {
				srplug_path_debug(xpath,
				                  "%s[%u]: "
				                  "interface changes applied",
				                  nlif_iface_name(iface),
				                  nlif_iface_index(iface));
				ret = SR_ERR_OK;
			}
		}
	}

	return ret;
}

static int
nlifd_on_iface_change(sr_session_ctx_t * session,
                      uint32_t           sub_id __unused,
                      const char *       module __unused,
                      const char *       xpath,
                      sr_event_t         event,
                      uint32_t           request_id __unused,
                      void *             repository)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_repo_assert((const struct nlif_repo *)repository);

	sr_error_t   ret;
	const char * act;

	switch (event) {
	case SR_EV_ENABLED:
		ret = nlifd_iface_process_changes(session, xpath, repository);
		act = "load";
		break;

	case SR_EV_CHANGE:
		ret = nlifd_iface_process_changes(session, xpath, repository);
		act = "apply";
		break;

	case SR_EV_DONE:
		srplug_path_debug(xpath, "interfaces configured");
		return SR_ERR_OK;

	case SR_EV_ABORT:
		ret = nlifd_iface_process_aborts(session, xpath, repository);
		act = "abort";
		break;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}

	if (ret != SR_ERR_OK)
		srplug_path_warn(xpath,
		                 "failed to %s interfaces configuration: %s",
		                 act,
		                 sr_strerror(ret));

	return ret;
}

/* Configuration change subscription. */
static const struct srplug_change_sub nlifd_change_sub = {
	.module    = NLIFD_IETF_IFACE_YANG_MODULE,
	.xpath     = NLIFD_IETF_IFACE_YANG_LIST_PATH,
	.feature   = NULL,
	.on_change = nlifd_on_iface_change,
	.priority  = 0,
	.options   = SR_SUBSCR_DEFAULT/*| SR_SUBSCR_ENABLED*/
};

/******************************************************************************
 * Top-level interfaces management daemon logic.
 ******************************************************************************/

struct nlifd {
	struct srplug_daemon         super;
	struct nlif_store            store;
	struct upoll_worker          work;
	struct nlif_obsrv_subscriber oper_sub;
	struct nlif_gate             gate;
};

#define nlifd_assert(_nlifd) \
	srplug_assert(_nlifd); \
	srplug_daemon_assert(&(_nlifd)->super); \
	nlif_store_assert(&(_nlifd)->store); \
	nlif_gate_assert(&(_nlifd)->gate)

static void
nlifd_push_oper(struct nlif_obsrv_subscriber * subscriber,
                unsigned int                   event,
                void *                         data,
                struct nlif_obsrv_notifier *   notifier __unused)
{
	srplug_assert(event == NLIF_STORE_IFACE_CHANGE_EVT);
	srplug_assert(data);

	struct nlifd *      dmn = containerof(subscriber,
	                                      typeof(*dmn),
	                                      oper_sub);
	sr_data_t *         changes;
	int                 ret;
	struct nlif_iface * iface = data;
	const char *        name;
	struct lyd_node *   entry;

	nlifd_assert(dmn);
	nlif_iface_assert(iface);

	ret = nlif_iface_coherent_name(iface, &name);
	if (ret) {
		srplug_warn("[%u]: cannot refresh interface status: %s",
		            nlif_iface_index(iface),
		            strerror(-ret));
		return;
	}

	ret = sr_get_oper_changes(srplug_daemon_session(&dmn->super),
	                          NLIFD_IETF_IFACE_YANG_MODULE,
	                          &changes);
	if (ret != SR_ERR_OK)
		goto err;

	ret = srplug_dat_find_pathf(
		changes->tree,
		&entry,
		NLIFD_IETF_IFACE_YANG_LIST_PATH "[name='%s']",
		name);
	if (ret != SR_ERR_OK) {
		srplug_dat_release_data(changes);
		goto err;
	}

	if (nlifd_if_mib_feat.on) {
		ret = nlifd_iface_fill_admstate(entry, iface);
		if (ret != SR_ERR_OK)
			goto release;
	}

	ret = nlifd_iface_fill_operstate(entry, iface);
	if (ret != SR_ERR_OK)
		goto release;

	ret = srplug_dat_merge_data_batch(srplug_daemon_session(&dmn->super),
	                                  data);
	if (ret != SR_ERR_OK)
		goto release;

	ret = srplug_apply_changes(srplug_daemon_session(&dmn->super));
	if (ret != SR_ERR_OK)
		goto release;

	srplug_dat_release_data(changes);

	return;

release:
	srplug_dat_release_data(changes);
	srplug_node_warn(entry,
	                 "cannot refresh interface status: %s",
	                 sr_strerror(ret));
	return;

err:
	srplug_warn("%s[%u]: cannot refresh interface status: %s",
	            name,
	            nlif_iface_index(iface),
	            sr_strerror(ret));
}

static int
nlifd_dispatch_notif(struct upoll_worker * worker,
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

	struct nlifd * dmn = containerof(worker, typeof(*dmn), work);

	nlifd_assert(dmn);
	nlif_gate_notify(&dmn->gate);

	return 0;
}

static int
nlifd_enable_notif(struct nlifd * daemon)
{
	nlifd_assert(daemon);

	int          ret;
	const char * msg __unused;

	nlif_obsrv_setup_subscriber(&daemon->oper_sub, nlifd_push_oper);
	nlif_store_subscribe(&daemon->store, &daemon->oper_sub);

	ret = nlif_store_enable_notif(&daemon->store, &daemon->gate);
	if (ret) {
		msg = "cannot enable store notification";
		goto err;
	}

	ret = upoll_register_dispatch(srplug_daemon_poller(&daemon->super),
	                              nlif_gate_fd(&daemon->gate),
	                              EPOLLIN,
	                              &daemon->work,
	                              nlifd_dispatch_notif);
	if (ret) {
		msg = "cannot enable polling";
		goto disable;
	}

	srplug_debug("notification worker enabled");

	return 0;

disable:
	nlif_store_disable_notif(&daemon->store, &daemon->gate);
err:
	nlif_store_unsubscribe(&daemon->store, &daemon->oper_sub);
	srplug_err("cannot enable notification worker: %s: %s",
	           msg,
	           strerror(-ret));

	return ret;
}

static void
nlifd_disable_notif(struct nlifd * daemon)
{
	nlifd_assert(daemon);

	upoll_unregister(srplug_daemon_poller(&daemon->super),
	                 nlif_gate_fd(&daemon->gate));
	nlif_store_disable_notif(&daemon->store, &daemon->gate);
	nlif_store_unsubscribe(&daemon->store, &daemon->oper_sub);

	srplug_debug("notification worker disabled");
}

static int
nlifd_iface_dstore_empty(sr_session_ctx_t * session, bool * empty)
{
	sr_data_t * data;
	sr_error_t  ret;
	
	ret = srplug_dat_load_data(session,
	                           NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface",
	                           1,
	                           0,
	                           &data);
	switch (ret) {
	case SR_ERR_OK:
		*empty = false;
		break;

	case SR_ERR_NOT_FOUND:
		*empty = true;
		return SR_ERR_OK;

	default:
		return ret;
	}

	srplug_dat_release_data(data);

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_reset_config_dstore(sr_session_ctx_t *    session,
                                const struct ly_ctx * context,
                                const struct nlifd *  daemon)
{
	srplug_assert(session);
	srplug_assert(context);
	nlifd_assert(daemon);

	struct lyd_node *              top;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;
	int                            err;

	err = srplug_dat_create_container(context,
	                                  NULL,
	                                  NLIFD_IETF_IFACE_YANG_ROOT_PATH,
	                                  &top);
	if (err != SR_ERR_OK)
		return err;

	nlif_store_foreach_iface(&daemon->store, hndl, iface) {
		err = nlifd_iface_new_entry(context, top, iface, NULL);
		if (err != SR_ERR_OK)
			goto free;
	}

	/*
	 * Here, `top' tree ownership is transfered to srplug_replace_dstore()
	 * so that it will be freed once it has returned thanks to a call to
	 * lyd_free_all().
	 * This means we are not requested to call lyd_free_tree() on it from
	 * now on.
	 * Also note that any missing implicit (default) nodes will be added to
	 * the data tree.
	 */
	return srplug_replace_config(session,
	                             NLIFD_IETF_IFACE_YANG_MODULE,
	                             top);

free:
	srplug_dat_free_tree(top);

	return err;
}

#if 0
static sr_error_t
nlifd_iface_setup_oper_dstore(sr_session_ctx_t *   session,
                              const struct nlifd * daemon)
{
	srplug_assert(session);
	nlifd_assert(daemon);

	sr_data_t *       data;
	sr_error_t        ret;
	struct lyd_node * node;

	/*
	 * Get data from the top-level container as
	 * srplug_dat_merge_data_batch() requires top-level data trees to
	 * prepare its batch of data changes.
	 */
	ret = srplug_dat_load_data(session,
	                           NLIFD_IETF_IFACE_YANG_ROOT_PATH,
	                           2,
	                           SR_OPER_NO_CONFIG | SR_OPER_NO_SUBS,
	                           &data);
	if (ret)
		return ret;

	srepo_dat_foreach_child(data->tree, node) {
		struct nlif_iface * iface;

		iface = nlifd_iface_from_node(node, &daemon->store);
		srplug_assert(iface);

		ret = nlifd_iface_fill_oper_entry(node, iface);
		if (ret != SR_ERR_OK)
			goto release;
	}

	ret = srplug_dat_replace_data_batch(session, data);
	if (ret != SR_ERR_OK)
		goto release;

	ret = srplug_apply_changes(session);

release:
	srplug_dat_release_data(data);

	return ret;
}
#else

static char *
nlifd_iface_create_path_base(void)
{
	srplug_assert(path);
	srplug_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	srplug_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN < SREPO_PATH_SIZE);

	char * pth;

	pth = srepo_alloc_path();
	memcpy(pth,
	       NLIFD_IETF_IFACE_YANG_LIST_PATH,
	       NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN + 1);

	return pth;
}

static ssize_t
nlifd_iface_fill_path_name(char * path, const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	srplug_assert(path);

	int len;

	len = snprintf(&path[NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN],
	               SREPO_PATH_SIZE - NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN,
	               "[name='%s']",
	               nlif_iface_name(interface));
	srplug_assert(len);
	if (len < 0)
		return -errno;

	/* Interface name is no longer than IFNAMSIZ, i.e. 16 characters... */
	srplug_assert((NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN + len) <
	              SREPO_PATH_SIZE);

	return (ssize_t)(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN + len);
}

static void
_nlifd_iface_concat_path_leaf(char *       path,
                              size_t       plen,
                              const char * string,
                              size_t       slen)
{
	srplug_assert(path);
	srplug_assert(plen > NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	srplug_assert(plen < SREPO_PATH_SIZE);
	srplug_assert(strlen(path) == plen);
	srplug_assert(string);
	srplug_assert(slen);
	srplug_assert(slen < SREPO_PATH_SIZE);
	srplug_assert(strlen(string) == slen);
	srplug_assert((plen + slen) < SREPO_PATH_SIZE);

	memcpy(&path[plen], string, slen);
	path[length + slen] = '\0';
}

#define nlifd_iface_concat_path(_path, _plen, _str) \
	_nlifd_iface_concat_path_leaf(_path, _plen, _str, sizeof(_str) - 1)

static sr_error_t
nlifd_iface_fill_admstate(char *                    path,
                          size_t                    length,
                          const struct nlif_iface * interface)
{
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH);
	srplug_assert(length < SREPO_PATH_SIZE);
	srplug_assert(strlen(path) == length);
	nlif_iface_assert(interface);

	nlifd_iface_concat_path(path, length, "/admin-status");

	return srepo_dat_set(session,
	                     path,
	                     nlif_iface_admstate(iface) ? "up" : "down",
	                     "ietf-origin:system",
	                     SR_EDIT_DEFAULT);
}

static sr_error_t
nlifd_iface_fill_operstate(char *                    path,
                           size_t                    length,
                           const struct nlif_iface * interface)
{
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH);
	srplug_assert(length < SREPO_PATH_SIZE);
	srplug_assert(strlen(path) == length);
	nlif_iface_assert(interface);

	const char * st =
		nlif_link_operstate_str(nlif_iface_operstate(interface));

	nlifd_iface_concat_path(path, length, "/oper-status");

	return srepo_dat_set(session,
	                     path,
	                     st,
	                     "ietf-origin:system",
	                     SR_EDIT_DEFAULT);
}

static sr_error_t
nlifd_iface_fill_index(char *                    path,
                       size_t                    length,
                       const struct nlif_iface * interface)
{
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH);
	srplug_assert(length < SREPO_PATH_SIZE);
	srplug_assert(strlen(path) == length);
	nlif_iface_assert(interface);

	nlifd_iface_concat_path(path, length, "/if-index");

	return srepo_dat_setf(session,
	                      path,
	                      "ietf-origin:system",
	                      SR_EDIT_DEFAULT,
	                      "%u",
	                      nlif_iface_index(interface));
}

static sr_error_t
nlifd_iface_fill_hwaddr(char *                    path,
                        size_t                    length,
                        const struct nlif_iface * interface)
{
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH);
	srplug_assert(length < SREPO_PATH_SIZE);
	srplug_assert(strlen(path) == length);
	nlif_iface_assert(interface);

	char str[NLIF_LINK_HWADDR_STRSZ];

	nlifd_iface_concat_path(path, length, "/phys-address");

	return srepo_dat_set(session,
	                     path,
	                     nlif_link_hwaddr_str(nlif_iface_hwaddr(interface),
	                                          str);
	                     "ietf-origin:system",
	                     SR_EDIT_DEFAULT);
}

static sr_error_t
nlifd_iface_setup_oper_dstore(sr_session_ctx_t *   session,
                              const struct nlifd * daemon)
{
	srplug_assert(session);
	nlifd_assert(daemon);

	char *                         path;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;

	srepo_switch_dstore(session, SR_DS_OPERATIONAL);

	path = nlifd_iface_create_path_base();

	nlif_store_foreach_iface(&daemon->store, hndl, iface) {
		ssize_t len;

		len = nlifd_iface_fill_path_name(path, iface);
		if (len < 0) {
			ret = SR_ERR_SYS;
			goto free;
		}

		ret = nlifd_iface_fill_admstate(path, len, iface);
		if (ret != SR_ERR_OK)
			goto free;

		ret = nlifd_iface_fill_operstate(path, len, iface);
		if (ret != SR_ERR_OK)
			goto free;

		ret = nlifd_iface_fill_index(path, len, iface);
		if (ret != SR_ERR_OK)
			goto free;

		ret = nlifd_iface_fill_hwaddr(path, len, iface);
		if (ret != SR_ERR_OK)
			goto free;
	}

	ret = srplug_apply_changes(session);

free:
	srplug_discard_changes(session);
	srplug_free(path);

	return ret;
}
#endif
static int
nlifd_load(struct nlifd * daemon)
{
	nlifd_assert(daemon);

	sr_session_ctx_t *    sess = srplug_daemon_session(&daemon->super);
	const struct ly_ctx * ctx;
	bool                  empty;
	int                   ret;

	ret = srplug_acquire_context(sess, &ctx);
	if (ret != SR_ERR_OK)
		goto err;

	ret = SR_ERR_NOT_FOUND;
	if (!srplug_find_module(ctx, "iana-if-type"))
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_arbitrary_names_feat) != SR_ERR_OK)
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_pre_provisioning_feat) != SR_ERR_OK)
		goto release;
	if (srplug_probe_feature(ctx,
	                         "ietf-interfaces",
	                         &nlifd_if_mib_feat) != SR_ERR_OK)
		goto release;
	if (!srplug_find_module(ctx, "netlink-interfaces"))
		goto release;

	ret = nlifd_iface_dstore_empty(sess, &empty);
	if (ret != SR_ERR_OK)
		goto release;
	if (empty) {
		ret = nlifd_iface_reset_config_dstore(sess, ctx, daemon);
		if (ret != SR_ERR_OK)
			goto release;
	}

	ret = srplug_daemon_change_subscribe(&daemon->super,
	                                     &nlifd_change_sub,
	                                     daemon);
	if (ret != SR_ERR_OK)
		goto release;

	ret = nlifd_iface_setup_oper_dstore(sess, daemon);
	if (ret != SR_ERR_OK)
		goto release;

	srplug_release_context(sess);

	return 0;

release:
	srplug_release_context(sess);
err:
	srplug_err("cannot load interfaces data: %s", sr_strerror(ret));

	return -EPERM;
}

static int
nlifd_poll(const struct nlifd * daemon)
{
	int ret;

	ret = srplug_daemon_poll(&daemon->super);
	if (ret == -ESHUTDOWN)
		ret = 0;

	return ret;
}

static int
nlifd_open(struct nlifd * daemon)
{
	srplug_assert(daemon);

	int err;

	err = srplug_daemon_open(&daemon->super, 1U);
	if (err)
		goto err;

	err = nlif_gate_init(&daemon->gate);
	if (err)
		goto close_dmn;

	nlif_store_init(&daemon->store);

	err = nlifd_enable_notif(daemon);
	if (err)
		goto fini_store;

	err = nlif_store_load(&daemon->store, &daemon->gate);
	if (err)
		goto disable_notif;

	srplug_debug("daemon started");

	return 0;

disable_notif:
	nlifd_disable_notif(daemon);
fini_store:
	nlif_store_fini(&daemon->store);
	nlif_gate_fini(&daemon->gate);
close_dmn:
	srplug_daemon_close(&daemon->super);
err:
	srplug_err("cannot start daemon: %s", strerror(-err));

	return err;
}

static void
nlifd_close(struct nlifd * daemon)
{
	nlifd_assert(daemon);

	nlifd_disable_notif(daemon);
	nlif_store_fini(&daemon->store);
	nlif_gate_fini(&daemon->gate);

	srplug_debug("daemon stopped");
}

/******************************************************************************
 * Main and command line parsing logic.
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
		srplug_daemon_free_conf(cfg);
		return;

	case 1:
		/* Help informations have been requested on the command line. */
		ret = EXIT_SUCCESS;
		break;

	default:
		/* Command line syntax or parsing error. */
		ret = EX_USAGE;
	}

	srplug_daemon_free_conf(cfg);

	exit(ret);
}

int
main(int argc, char * argv[])
{
	struct elog * log;
	int           ret;
	struct nlifd  dmn;

	nlifd_setup(argc, argv, &log);

	ret = nlifd_open(&dmn);
	if (ret)
		goto fini_log;

	ret = nlifd_load(&dmn);
	if (ret)
		goto close_dmn;

	ret = nlifd_poll(&dmn);

close_dmn:
	nlifd_close(&dmn);
fini_log:
	srplug_daemon_destroy_log(log);

	return !ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
