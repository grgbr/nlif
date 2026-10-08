#include "repo.h"
#include "lib/iface.h"
#include <srutils/srplug/daemon.h>
#include <srutils/srplug/data.h>
#include <srutils/srepo/schema.h>
#include "utils/string.h"
#include <sysrepo/xpath.h>
#include <sysexits.h>

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"

#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces"

#define NLIFD_IETF_IFACE_YANG_LIST_PATH \
	NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface"
#define NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN \
	(sizeof(NLIFD_IETF_IFACE_YANG_LIST_PATH) - 1)

#define nlifd_assert_paths() \
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN, \
	              "empty ietf-interfaces interface list xpath length"); \
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN < SREPO_XPATH_SIZE, \
	              "ietf-interfaces interface list xpath length too long")

static struct srplug_feat
nlifd_arbitrary_names_feat = SRPLUG_FEAT_SETUP("arbitrary-names");

static struct srplug_feat
nlifd_pre_provisioning_feat = SRPLUG_FEAT_SETUP("pre-provisioning");

static struct srplug_feat
nlifd_if_mib_feat = SRPLUG_FEAT_SETUP("if-mib");

/******************************************************************************
 * Various helpers.
 ******************************************************************************/

#define nlifd_log(_svrt, _fmt, ...) \
	srepo_log(_svrt, "", _fmt, ## __VA_ARGS__)

#define nlifd_err(_fmt, ...) \
	nlifd_log(ELOG_ERR_SEVERITY, _fmt, ## __VA_ARGS__)

#define nlifd_warn(_fmt, ...) \
	nlifd_log(ELOG_WARNING_SEVERITY, _fmt, ## __VA_ARGS__)

#define nlifd_notice(_fmt, ...) \
	nlifd_log(ELOG_NOTICE_SEVERITY, _fmt, ## __VA_ARGS__)

#define nlifd_info(_fmt, ...) \
	nlifd_log(ELOG_INFO_SEVERITY, _fmt, ## __VA_ARGS__)

#define nlifd_debug(_fmt, ...) \
	nlifd_log(ELOG_DEBUG_SEVERITY, _fmt, ## __VA_ARGS__)


#define nlifd_log_xpath(_sess, _xpath, _svrt, _fmt, ...) \
	srepo_xpath_log_sess(_sess, \
	                     _xpath, \
	                     _svrt, \
	                     "", \
	                     _fmt, \
	                     ## __VA_ARGS__)

#define nlifd_xpath_err(_sess, _xpath, _fmt, ...) \
	nlifd_log_xpath(_sess, \
	                _xpath, \
	                ELOG_ERR_SEVERITY, \
	                _fmt, \
	                ## __VA_ARGS__)

#define nlifd_xpath_warn(_sess, _xpath, _fmt, ...) \
	nlifd_log_xpath(_sess, \
	                _xpath, \
	                ELOG_WARNING_SEVERITY, \
	                _fmt, \
	                ## __VA_ARGS__)

#define nlifd_xpath_notice(_sess, _xpath, _fmt, ...) \
	nlifd_log_xpath(_sess, \
	                _xpath, \
	                ELOG_NOTICE_SEVERITY, \
	                _fmt, \
	                ## __VA_ARGS__)

#define nlifd_xpath_info(_sess, _xpath, _fmt, ...) \
	nlifd_log_xpath(_sess, \
	                _xpath, \
	                ELOG_INFO_SEVERITY, \
	                _fmt, \
	                ## __VA_ARGS__)

#define nlifd_xpath_debug(_sess, _xpath, _fmt, ...) \
	nlifd_log_xpath(_sess, \
	                _xpath, \
	                ELOG_DEBUG_SEVERITY, \
	                _fmt, \
	                ## __VA_ARGS__)


#define nlifd_log_node(_sess, _node, _svrt, _fmt, ...) \
	srepo_dat_log_sess_node(_sess, \
	                        _node, \
	                        _svrt, \
	                        "", \
	                        _fmt, \
	                        ## __VA_ARGS__)

#define nlifd_node_err(_sess, _node, _fmt, ...) \
	nlifd_log_node(_sess, \
	               _node, \
	               ELOG_ERR_SEVERITY, \
	               _fmt, \
	               ## __VA_ARGS__)

#define nlifd_node_warn(_sess, _node, _fmt, ...) \
	nlifd_log_node(_sess, \
	               _node, \
	               ELOG_WARNING_SEVERITY, \
	               _fmt, \
	               ## __VA_ARGS__)

#define nlifd_node_notice(_sess, _node, _fmt, ...) \
	nlifd_log_node(_sess, \
	               _node, \
	               ELOG_NOTICE_SEVERITY, \
	               _fmt, \
	               ## __VA_ARGS__)

#define nlifd_node_info(_sess, _node, _fmt, ...) \
	nlifd_log_node(_sess, \
	               _node, \
	               ELOG_INFO_SEVERITY, \
	               _fmt, \
	               ## __VA_ARGS__)

#define nlifd_node_debug(_sess, _node, _fmt, ...) \
	nlifd_log_node(_sess, \
	               _node, \
	               ELOG_DEBUG_SEVERITY, \
	               _fmt, \
	               ## __VA_ARGS__)

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

/******************************************************************************
 * XPATH and features handling.
 ******************************************************************************/

static char *
nlifd_iface_create_xpath_base(void)
{
	nlifd_assert_paths();

	return srepo_xpath_create(NLIFD_IETF_IFACE_YANG_LIST_PATH,
	                          NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
}

static ssize_t
nlifd_iface_fill_path_name(char * path, const char * name)
{
	return srepo_xpath_printf(
		path, NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN, "[name='%s']", name);
}

static ssize_t
nlifd_iface_create_path(char ** path, struct nlif_iface * interface)
{
	nlifd_assert_paths();

	ssize_t      ret;
	const char * name = name; /* Avoid spurious warning. */

	ret = nlif_iface_coherent_name(interface, &name);
	if (ret)
		return ret;

	return srepo_xpath_createf(
		path, NLIFD_IETF_IFACE_YANG_LIST_PATH "[name='%s']", name);
}

static void
_nlifd_iface_concat_leaf_path(char *       path,
                              size_t       plen,
                              const char * string,
                              size_t       slen)
{
	nlifd_assert_paths();

	memcpy(&path[plen], string, slen);
	path[plen + slen] = '\0';
}

#define nlifd_iface_concat_leaf_path(_path, _plen, _str) \
	_nlifd_iface_concat_leaf_path( \
		_path, \
		_plen, \
		_str, \
		compile_eval(_stroll_is_array(_str), \
		             sizeof(_str) - 1, \
		             "array expected"))

static struct nlif_iface *
nlifd_iface_from_node(const sr_session_ctx_t *  session,
                      const struct lyd_node *   node,
                      const struct nlif_store * store)
{
	char *         path;
	sr_xpath_ctx_t ctx;
	const char *   name;
	const char *   msg;
	ssize_t        len;

	msg = "invalid interface node path";
	path = srepo_dat_node_path(node);
	if (!path)
		goto err;
	name = srepo_xpath_key_value(path, "interface", "name", &ctx);
	if (!name)
		goto err;

	len = nlif_iface_validate_name(name);
	if (len > 0) {
		struct nlif_iface * iface;

		iface = nlif_store_find_iface_byname(store, name);
		if (iface) {
			srepo_free(path);
			return iface;
		}

		msg = "no such interface";
		goto recover;
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

recover:
	srepo_xpath_recover(&ctx);
err:
	nlifd_xpath_info(session, path, "%s", msg);
	srepo_free(path);

	return NULL;
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

/******************************************************************************
 * Interfaces operational data handling.
 ******************************************************************************/

static sr_error_t
nlifd_iface_fill_admstate(sr_session_ctx_t *        session,
                          char *                    path,
                          size_t                    length,
                          const struct nlif_iface * interface)
{
	srplug_assert(session);
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	srplug_assert(length < SREPO_XPATH_SIZE);
	nlif_iface_assert(interface);

	nlifd_iface_concat_leaf_path(path, length, "/admin-status");

	return srepo_dat_change_bypath(session,
	                               path,
	                               nlif_iface_admstate(interface) ? "up"
	                                                              : "down",
	                               "ietf-origin:system",
	                               SR_EDIT_DEFAULT);
}

static sr_error_t
nlifd_iface_fill_operstate(sr_session_ctx_t *        session,
                           char *                    path,
                           size_t                    length,
                           const struct nlif_iface * interface)
{
	srplug_assert(session);
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	srplug_assert(length < SREPO_XPATH_SIZE);
	nlif_iface_assert(interface);

	const char * st =
		nlif_link_operstate_str(nlif_iface_operstate(interface));

	nlifd_iface_concat_leaf_path(path, length, "/oper-status");

	return srepo_dat_change_bypath(session,
	                               path,
	                               st,
	                               "ietf-origin:system",
	                               SR_EDIT_DEFAULT);
}

static sr_error_t
nlifd_iface_fill_index(sr_session_ctx_t *        session,
                       char *                    path,
                       size_t                    length,
                       const struct nlif_iface * interface)
{
	srplug_assert(session);
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	srplug_assert(length < SREPO_XPATH_SIZE);
	nlif_iface_assert(interface);

	nlifd_iface_concat_leaf_path(path, length, "/if-index");

	return srepo_dat_changef_bypath(session,
	                                path,
	                                "ietf-origin:system",
	                                SR_EDIT_DEFAULT,
	                                "%u",
	                                nlif_iface_index(interface));
}

static sr_error_t
nlifd_iface_fill_hwaddr(sr_session_ctx_t *        session,
                        char *                    path,
                        size_t                    length,
                        const struct nlif_iface * interface)
{
	srplug_assert(session);
	srplug_assert(path);
	srplug_assert(length > NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	srplug_assert(length < SREPO_XPATH_SIZE);
	nlif_iface_assert(interface);

	char str[NLIF_LINK_HWADDR_STRSZ];

	nlifd_iface_concat_leaf_path(path, length, "/phys-address");

	return srepo_dat_change_bypath(
		session,
		path,
		nlif_link_hwaddr_str(nlif_iface_hwaddr(interface), str),
		"ietf-origin:system",
		SR_EDIT_DEFAULT);
}

static sr_error_t
nlifd_iface_refresh_oper(sr_session_ctx_t *  session,
                         char *              path,
                         size_t              length,
                         struct nlif_iface * interface)
{
	srplug_assert(session);
	srplug_assert(path);
	srplug_assert(path[0]);
	srplug_assert(strnlen(path, SREPO_XPATH_SIZE) < SREPO_XPATH_SIZE);
	srplug_assert(strnlen(path, SREPO_XPATH_SIZE) >
	              NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	nlif_iface_assert(interface);

	sr_error_t err;

	if (nlifd_if_mib_feat.on) {
		err = nlifd_iface_fill_admstate(session,
		                                path,
		                                length,
		                                interface);
		if (err != SR_ERR_OK)
			return err;
	}

	err = nlifd_iface_fill_operstate(session, path, length, interface);
	if (err != SR_ERR_OK)
		return err;

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_get_stats(sr_session_ctx_t *        session,
                      struct lyd_node *         container,
                      const struct nlif_store * store)
{
	srplug_assert(session);
	srplug_assert(container);
	srplug_assert(!strcmp(srepo_dat_node_name(container), "interface"));
	srplug_assert(srepo_dat_node_type(container) == LYS_LIST);
	nlif_store_assert(store);

	struct nlif_iface *      iface;
	int                      ret;
	struct rtnl_link_stats64 stats;

	iface = nlifd_iface_from_node(session, container, store);
	if (!iface)
		return SR_ERR_NOT_FOUND;

	/*
	 * ip -s -s link show dev <interface>
	 * ethtool -S <interface>
	 */
	ret = nlif_iface_load_stats(iface, &stats);
	if (ret)
		return nlifd_iface_error(ret);

	/* TODO: discontinuity-time */

	ret = srepo_dat_create_leaf_printf(container,
	                                    "statistics/in-octets",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.rx_bytes);
	if (ret != SR_ERR_OK)
		return ret;

	/* TODO: in-unicast-pkts */
	/* TODO: in-broadcast-pkts */
	/* TODO: in-multicast-pkts */

	ret = srepo_dat_create_leaf_printf(container,
	                                    "statistics/in-discards",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.rx_dropped);
	if (ret != SR_ERR_OK)
		return ret;

	ret = srepo_dat_create_leaf_printf(container,
	                                    "statistics/in-errors",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.rx_errors);
	if (ret != SR_ERR_OK)
		return ret;

	/* TODO: in-unknown-protos */

	ret = srepo_dat_create_leaf_printf(container,
	                                    "statistics/out-octets",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.tx_bytes);
	if (ret != SR_ERR_OK)
		return ret;

	/* TODO: out-unicast-pkts */
	/* TODO: out-broadcast-pkts */
	/* TODO: out-multicast-pkts */

	ret = srepo_dat_create_leaf_printf(container,
	                                    "statistics/out-discards",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.tx_dropped);
	if (ret != SR_ERR_OK)
		return ret;

	ret = srepo_dat_create_leaf_printf(container,
	                                    "statistics/out-errors",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.tx_errors);
	if (ret != SR_ERR_OK)
		return ret;

	return SR_ERR_OK;
}

static int
nlifd_on_iface_get_stats(sr_session_ctx_t * session,
                         uint32_t           sub_id __unused,
                         const char *       module __unused,
                         const char *       path __unused,
                         const char *       request_xpath __unused,
                         uint32_t           op_id __unused,
                         struct lyd_node ** parent,
                         void *             store)
{
	srplug_assert(parent);
	nlif_store_assert((const struct nlif_store *)store);

	sr_error_t err;

	err = nlifd_iface_get_stats(session, *parent, store);
	if (err != SR_ERR_OK) {
		nlifd_node_warn(session,
		                *parent,
		                "cannot fill in interface statistics: %s",
		                srepo_errstr(err));
		return err;
	}

	nlifd_node_debug(session, *parent, "interface statistics filled in");

	return SR_ERR_OK;
}

/* Operational state subscription. */
static const struct srplug_oper_sub nlifd_oper_sub = {
	.module  = NLIFD_IETF_IFACE_YANG_MODULE,
	.xpath   =       NLIFD_IETF_IFACE_YANG_LIST_PATH "/statistics",
	.feature =      NULL,
	.on_get  =      nlifd_on_iface_get_stats,
	.options =      0
};

/******************************************************************************
 * Interfaces configuration handling.
 ******************************************************************************/

static sr_error_t
nlifd_iface_new_entry(const sr_session_ctx_t * session,
                      const struct ly_ctx *    context,
                      struct lyd_node *        container,
                      struct nlif_iface *      interface,
                      struct lyd_node **       entry)
{
	srplug_assert(session);
	srplug_assert(context);
	srplug_assert(container);
	srplug_assert(interface);
	srplug_assert(nlif_iface_state(interface) == NLIF_CLEAN_STAT);

	struct lyd_node * ent;
	const char *      str;
	int               ret;

	ret = srepo_dat_create_list_keyent(context,
	                                   container,
	                                   "interface",
	                                   "name",
	                                   nlif_iface_name(interface),
	                                   &ent);
	if (ret != SR_ERR_OK)
		return ret;

	/* Create default nodes as defined by the interface YANG model. */
	ret = srepo_dat_new_dflt_nodes(ent, LYD_IMPLICIT_NO_STATE, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	str = nlifd_iface_type_str(nlif_iface_type(interface),
	                           nlif_iface_kind(interface));
	if (!str)
		return SR_ERR_UNSUPPORTED;

	ret = srepo_dat_create_leaf(ent, "type", str, NULL);
	if (ret != SR_ERR_OK)
		return ret;

	if (entry)
		*entry = ent;

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_change_enabled(sr_session_ctx_t *      session,
                           const struct lyd_node * leaf,
                           sr_change_oper_t        oper,
                           const char *            old,
                           void *                  store)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(session, leaf, store);
	if (!iface) {
		nlifd_node_notice(session,
		                  leaf,
		                  "cannot change interface: no such interface");
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
		nlifd_node_notice(session,
		                  leaf,
		                  "cannot change interface: %s",
		                  strerror(-ret));
		return nlifd_iface_error(ret);
	}

	nlifd_node_debug(session, leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_enabled(sr_session_ctx_t *      session,
                            const struct lyd_node * leaf,
                            sr_change_oper_t        oper,
                            const char *            old,
                            void *                  store)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(session, leaf, store);
	if (!iface) {
		nlifd_node_err(session,
		               leaf,
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
			nlifd_node_err(session,
			               leaf,
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
		nlifd_node_err(session,
		               leaf,
		               "cannot restore interface: %s", strerror(-ret));
	else
		nlifd_node_debug(session, leaf, "interface restored");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_change_type(sr_session_ctx_t *      session,
                        const struct lyd_node * leaf,
                        sr_change_oper_t        oper,
                        const char *            old,
                        void *                  store)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

	struct nlif_iface * iface;
	const char *        val = srepo_dat_node_as_str(leaf);
	const char *        type;
	int                 ret;

	iface = nlifd_iface_from_node(session, leaf, store);
	if (!iface) {
		nlifd_node_notice(session,
		                  leaf,
		                  "cannot change interface: no such interface");
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
		nlifd_node_notice(session,
		                  leaf,
		                  "cannot change interface: "
		                  "failed to retrieve type: %s",
		                  strerror(-ret));
		return nlifd_iface_error(ret);
	}

	if (strcmp(type, val)) {
		nlifd_node_notice(session,
		                  leaf,
		                  "cannot change interface: %s",
		                  srepo_errstr(SR_ERR_UNSUPPORTED));
		return SR_ERR_UNSUPPORTED;
	}

	nlifd_node_debug(session, leaf, "interface changed");

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_restore_type(sr_session_ctx_t *      session,
                         const struct lyd_node * leaf,
                         sr_change_oper_t        oper __unused,
                         const char *            old __unused,
                         void *                  store __unused)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

	nlifd_node_debug(session, leaf, "interface restored");

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
nlifd_iface_process_aborts(sr_session_ctx_t *  session,
                           const char *        xpath,
                           struct nlif_store * store)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_store_assert(store);

	int                            ret;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;

	/* Since restore handlers MUST not fail, this should never fail... */
	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_restore_hndlrs,
		stroll_array_nr(nlifd_iface_restore_hndlrs),
		store);

	/*
	 *  ...unless Sysrepo internals fail to retrieve subscription events.
	 * Keep trying to restore as many interfaces as we can however.
	 */
	nlif_store_foreach_iface(store, hndl, iface) {
		int err;

		err = nlif_iface_apply(iface);
		if (err < 0) {
			nlifd_xpath_err(
				session,
				xpath,
				"[%u]: cannot abort interface changes: "
				"%s",
				nlif_iface_index(iface),
				strerror(-err));
			if (ret == SR_ERR_OK)
				ret = nlifd_iface_error(err);
		}
		else if (err)
			nlifd_xpath_debug(session,
			                  xpath,
			                  "interface changes aborted");
	}

	return ret;
}

static sr_error_t
nlifd_iface_process_changes(sr_session_ctx_t *  session,
                            const char *        xpath,
                            struct nlif_store * store)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_store_assert(store);

	int ret;

	ret = srplug_process_child_changes(
		session,
		xpath,
		nlifd_iface_change_hndlrs,
		stroll_array_nr(nlifd_iface_change_hndlrs),
		store);
	if (ret == SR_ERR_OK) {
		const struct nlif_store_hndl * hndl;
		struct nlif_iface *            iface;

		nlif_store_foreach_iface(store, hndl, iface) {
			ret = nlif_iface_apply(iface);
			if (ret < 0) {
				nlifd_xpath_notice(
					session,
					xpath,
					"[%u]: cannot apply interface changes: "
					"%s",
					nlif_iface_index(iface),
					strerror(-ret));
				nlifd_iface_process_aborts(session,
				                           xpath,
				                           store);
				return nlifd_iface_error(ret);
			}

			if (ret) {
				nlifd_xpath_debug(session,
				                  xpath,
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
                      void *             store)
{
	srplug_assert(session);
	srplug_assert(xpath);
	srplug_assert(xpath[0]);
	nlif_store_assert((const struct nlif_store *)store);

	sr_error_t   ret;
	const char * act;

	switch (event) {
	case SR_EV_ENABLED:
		ret = nlifd_iface_process_changes(session, xpath, store);
		act = "load";
		break;

	case SR_EV_CHANGE:
		ret = nlifd_iface_process_changes(session, xpath, store);
		act = "apply";
		break;

	case SR_EV_DONE:
		nlifd_xpath_debug(session, xpath, "interfaces configured");
		return SR_ERR_OK;

	case SR_EV_ABORT:
		ret = nlifd_iface_process_aborts(session, xpath, store);
		act = "abort";
		break;

	case SR_EV_UPDATE:
	case SR_EV_RPC:
	default:
		srplug_assert(0);
	}

	if (ret != SR_ERR_OK)
		nlifd_xpath_warn(session,
		                 xpath,
		                 "failed to %s interfaces configuration: %s",
		                 act,
		                 srepo_errstr(ret));

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

#define nlifd_assert_daemon(_nlifd) \
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
	struct nlif_iface * iface = data;
	char *              path;
	ssize_t             len;
	sr_session_ctx_t *  sess = srplug_daemon_session(&dmn->super);
	sr_error_t          ret;

	nlifd_assert_daemon(dmn);
	nlif_iface_assert(iface);

	len = nlifd_iface_create_path(&path, iface);
	if (len < 0) {
		nlifd_warn("[%u]: cannot refresh interface status: %s",
		           nlif_iface_index(iface),
		           strerror(-len));
		return;
	}

	ret = nlifd_iface_refresh_oper(sess, path, len, iface);
	if (ret != SR_ERR_OK)
		goto err;

	ret = srepo_apply_changes(sess);
	if (ret == SR_ERR_OK) {
#if defined(CONFIG_NLIF_DEBUG)
		path[len] = '\0';
		nlifd_xpath_debug(sess, path, "interface status refreshed");
#endif /* defined(CONFIG_NLIF_DEBUG) */
		srepo_free(path);
		return;
	}

err:
	srepo_discard_oper_changes(sess, NLIFD_IETF_IFACE_YANG_MODULE);

	/*
	 * Fix path since it may have been modified by calls to
	 * nlifd_iface_fill_admstate() or nlifd_iface_fill_operstate().
	 */
	path[len] = '\0';
	nlifd_xpath_warn(sess,
	                 path,
	                 "cannot refresh interface status: %s",
	                 srepo_errstr(ret));
	srepo_free(path);
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

	nlifd_assert_daemon(dmn);
	nlif_gate_notify(&dmn->gate);

	return 0;
}

static int
nlifd_enable_notif(struct nlifd * daemon)
{
	nlifd_assert_daemon(daemon);

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

	nlifd_debug("notification worker enabled");

	return 0;

disable:
	nlif_store_disable_notif(&daemon->store, &daemon->gate);
err:
	nlif_store_unsubscribe(&daemon->store, &daemon->oper_sub);
	nlifd_err("cannot enable notification worker: %s: %s",
	           msg,
	           strerror(-ret));

	return ret;
}

static void
nlifd_disable_notif(struct nlifd * daemon)
{
	nlifd_assert_daemon(daemon);

	upoll_unregister(srplug_daemon_poller(&daemon->super),
	                 nlif_gate_fd(&daemon->gate));
	nlif_store_disable_notif(&daemon->store, &daemon->gate);
	nlif_store_unsubscribe(&daemon->store, &daemon->oper_sub);

	nlifd_debug("notification worker disabled");
}

static int
nlifd_iface_dstore_empty(sr_session_ctx_t * session, bool * empty)
{
	sr_data_t * data;
	sr_error_t  ret;
	
	ret = srepo_dat_load_data(session,
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

	srepo_dat_release_data(data);

	return SR_ERR_OK;
}

static sr_error_t
nlifd_iface_reset_config_dstore(sr_session_ctx_t *    session,
                                const struct ly_ctx * context,
                                const struct nlifd *  daemon)
{
	srplug_assert(session);
	srplug_assert(context);
	nlifd_assert_daemon(daemon);

	struct lyd_node *              top;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;
	int                            err;

	err = srepo_dat_create_container(context,
	                                 NULL,
	                                 NLIFD_IETF_IFACE_YANG_ROOT_PATH,
	                                 &top);
	if (err != SR_ERR_OK)
		goto err;

	nlif_store_foreach_iface(&daemon->store, hndl, iface) {
		err = nlifd_iface_new_entry(session, context, top, iface, NULL);
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
	err = srepo_replace_config(session, NLIFD_IETF_IFACE_YANG_MODULE, top);
	if (err == SR_ERR_OK)
		goto free;

	return SR_ERR_OK;

free:
	srepo_dat_free_tree(top);
err:
	nlifd_warn("cannot reset interfaces configuration: %s",
	           srepo_errstr(err));

	return err;
}

static sr_error_t
nlifd_iface_setup_oper_dstore(sr_session_ctx_t *   session,
                              const struct nlifd * daemon)
{
	srplug_assert(session);
	nlifd_assert_daemon(daemon);

	char *                         path;
	const struct nlif_store_hndl * hndl;
	struct nlif_iface *            iface;
	sr_error_t                     ret;

	srepo_switch_dstore(session, SR_DS_OPERATIONAL);

	path = nlifd_iface_create_xpath_base();
	nlif_store_foreach_iface(&daemon->store, hndl, iface) {
		ssize_t len;

		len = nlifd_iface_fill_path_name(path, nlif_iface_name(iface));
		if (len < 0) {
			ret = nlifd_iface_error(len);
			goto err;
		}

		ret = nlifd_iface_refresh_oper(session, path, len, iface);
		if (ret != SR_ERR_OK)
			goto err;

		if (nlifd_if_mib_feat.on) {
			ret = nlifd_iface_fill_index(session, path, len, iface);
			if (ret != SR_ERR_OK)
				goto err;
		}

		ret = nlifd_iface_fill_hwaddr(session, path, len, iface);
		if (ret != SR_ERR_OK)
			goto err;
	}

	ret = srepo_apply_changes(session);
	if (ret == SR_ERR_OK) {
		srepo_free(path);
		return ret;
	}

err:
	srepo_free(path);
	nlifd_warn("cannot setup interfaces status: %s", srepo_errstr(ret));

	return ret;
}

static int
nlifd_load(struct nlifd * daemon)
{
	nlifd_assert_daemon(daemon);

	sr_session_ctx_t *    sess = srplug_daemon_session(&daemon->super);
	const struct ly_ctx * ctx;
	bool                  empty = false;
	int                   ret;

	ret = srepo_acquire_context(sess, &ctx);
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
	                                     &daemon->store);
	if (ret) {
		ret = SR_ERR_SYS;
		goto release;
	}

	ret = nlifd_iface_setup_oper_dstore(sess, daemon);
	if (ret != SR_ERR_OK)
		goto release;

	ret = srplug_daemon_oper_subscribe(&daemon->super,
	                                   &nlifd_oper_sub,
	                                   &daemon->store);
	if (ret) {
		ret = SR_ERR_SYS;
		goto release;
	}

	srepo_release_context(sess);

	return 0;

release:
	srepo_release_context(sess);
err:
	nlifd_err("cannot load interfaces data: %s", srepo_errstr(ret));

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

	nlifd_notice("daemon started");

	return 0;

disable_notif:
	nlifd_disable_notif(daemon);
fini_store:
	nlif_store_fini(&daemon->store);
	nlif_gate_fini(&daemon->gate);
close_dmn:
	srplug_daemon_close(&daemon->super);
err:
	nlifd_err("cannot start daemon: %s", strerror(-err));

	return err;
}

static void
nlifd_close(struct nlifd * daemon)
{
	nlifd_assert_daemon(daemon);

	nlifd_disable_notif(daemon);
	nlif_store_fini(&daemon->store);
	nlif_gate_fini(&daemon->gate);

	srplug_daemon_close(&daemon->super);

	nlifd_notice("daemon stopped");
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
