#include "repo.h"
#include "lib/iface.h"
#include <srutils/srplug/daemon.h>
#include <srutils/srplug/data.h>
#include <srutils/srepo/schema.h>
#include "utils/string.h"
#include <sysrepo/xpath.h>
#include <sysexits.h>

/******************************************************************************
 * Srepo wrappers.
 ******************************************************************************/

static sr_error_t
srepo_ly_error(LY_ERR error)
{
	switch (error) {
	case LY_SUCCESS:
		return SR_ERR_OK;
	case LY_EMEM:
		return SR_ERR_NO_MEMORY;
	case LY_ESYS:
		return SR_ERR_SYS;
	case LY_EINVAL:
		return SR_ERR_INVAL_ARG;
	case LY_EEXIST:
		return SR_ERR_EXISTS;
	case LY_ENOTFOUND:
		return SR_ERR_NOT_FOUND;
	case LY_EVALID:
		return SR_ERR_VALIDATION_FAILED;
	case LY_EDENIED:
		return SR_ERR_OPERATION_FAILED;
	case LY_EINT:
	case LY_EINCOMPLETE:
	case LY_ERECOMPILE:
	case LY_ENOT:
	case LY_EOTHER:
	case LY_EPLUGIN:
		return SR_ERR_LY;
	default:
		srepo_assert(0);
		return SR_ERR_LY;
	}
}

static sr_error_t
srepo_dat_set(sr_session_ctx_t * session,
              const char *       path,
              const char *       value,
              const char *       origin,
              uint32_t           flags)
{
	srepo_assert(session);
	srepo_assert(srepo_xpath_validate(path) > 0);
	srepo_assert(value);
	srepo_assert(!(flags & ~(SR_EDIT_DEFAULT |
	                         SR_EDIT_NON_RECURSIVE |
	                         SR_EDIT_STRICT |
	                         SR_EDIT_ISOLATE)));

	sr_error_t ret;

	ret = sr_set_item_str(session, path, value, origin, flags);
	if (ret == SR_ERR_OK)
		return SR_ERR_OK;

	srepo_assert(ret != SR_ERR_INVAL_ARG);
	if (ret == SR_ERR_NO_MEMORY)
		srepo_abort();

	return ret;
}

static sr_error_t
srepo_dat_vsetf(sr_session_ctx_t * session,
                const char *       path,
                const char *       origin,
                uint32_t           flags,
                const char *       format,
                va_list            args)
{
	srepo_assert(session);
	srepo_assert(srepo_xpath_validate(path) > 0);
	srepo_assert(!(flags & ~(SR_EDIT_DEFAULT |
	                         SR_EDIT_NON_RECURSIVE |
	                         SR_EDIT_STRICT |
	                         SR_EDIT_ISOLATE)));
	srepo_assert(format);

	char * str;
	int    ret;

	ret = srepo_vasprintf(&str, format, args);
	if (ret < 0)
		return srepo_ly_error(ret);

	ret = srepo_dat_set(session, path, str, origin, flags);

	srepo_free(str);

	return ret;
}

static inline sr_error_t
srepo_dat_setf(sr_session_ctx_t * session,
               const char *       path,
               const char *       origin,
               uint32_t           flags,
               const char *       format,
               ...)
{
	srepo_assert(session);
	srepo_assert(srepo_xpath_validate(path) > 0);
	srepo_assert(!(flags & ~(SR_EDIT_DEFAULT |
	                         SR_EDIT_NON_RECURSIVE |
	                         SR_EDIT_STRICT |
	                         SR_EDIT_ISOLATE)));
	srepo_assert(format);

	sr_error_t ret;
	va_list    args;

	va_start(args, format);
	ret = srepo_dat_vsetf(session, path, origin, flags, format, args);
	va_end(args);

	return ret;
}

static sr_error_t
srepo_discard_oper_changes(sr_session_ctx_t * session, const char * module)
{
	srepo_assert(session);
	srepo_assert(!module || module[0]);

	sr_error_t ret;

	ret = sr_discard_oper_changes(session, module, 0);
	if (ret == SR_ERR_OK)
		return SR_ERR_OK;

	if (ret == SR_ERR_NO_MEMORY)
		srepo_abort();

	return ret;
}

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

/******************************************************************************
 * XPATH utilities.
 ******************************************************************************/

#define NLIFD_IETF_IFACE_YANG_MODULE \
	"ietf-interfaces"

#define NLIFD_IETF_IFACE_YANG_ROOT_PATH \
	"/" NLIFD_IETF_IFACE_YANG_MODULE ":interfaces"

#define NLIFD_IETF_IFACE_YANG_LIST_PATH \
	NLIFD_IETF_IFACE_YANG_ROOT_PATH "/interface"
#define NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN \
	(sizeof(NLIFD_IETF_IFACE_YANG_LIST_PATH) - 1)

static char *
nlifd_iface_create_xpath_base(void)
{
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN,
	              "empty ietf-interfaces interface list xpath length");
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN < SREPO_XPATH_SIZE,
	              "ietf-interfaces interface list xpath length too long");

	return srepo_xpath_create(NLIFD_IETF_IFACE_YANG_LIST_PATH,
	                          NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
}

static ssize_t
nlifd_iface_fill_path_name(char * path, const char * name)
{
	srplug_assert(path);
	srplug_assert(nlif_iface_validate_name(name) > 0);

	ssize_t len;

	len = srepo_xpath_printf(path,
	                         NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN,
	                         "[name='%s']",
	                         name);
	/* Interface name is no longer than IFNAMSIZ, i.e. 16 characters... */
	srplug_assert((size_t)len < SREPO_XPATH_SIZE);

	return len;
}

static ssize_t
nlifd_iface_create_path(char ** path, struct nlif_iface * interface)
{
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN,
	              "empty ietf-interfaces interface list xpath length");
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN < SREPO_XPATH_SIZE,
	              "ietf-interfaces interface list xpath length too long");
	srplug_assert(path);
	nlif_iface_assert(interface);

	ssize_t      ret;
	const char * name;

	ret = nlif_iface_coherent_name(interface, &name);
	if (ret)
		return ret;

	ret = srepo_xpath_createf(path,
	                          NLIFD_IETF_IFACE_YANG_LIST_PATH "[name='%s']",
	                          name);
	/* Interface name is no longer than IFNAMSIZ, i.e. 16 characters... */
	srplug_assert((size_t)ret < SREPO_XPATH_SIZE);

	return ret;
}

static void
_nlifd_iface_concat_leaf_path(char *       path,
                              size_t       plen,
                              const char * string,
                              size_t       slen)
{
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN,
	              "empty ietf-interfaces interface list xpath length");
	static_assert(NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN < SREPO_XPATH_SIZE,
	              "ietf-interfaces interface list xpath length too long");
	srepo_assert(path);
	srepo_assert(plen > NLIFD_IETF_IFACE_YANG_LIST_PATH_LEN);
	srepo_assert(plen < SREPO_XPATH_SIZE);
	srepo_assert(string);
	srepo_assert(slen);
	srepo_assert(slen < SREPO_XPATH_SIZE);
	srepo_assert(strlen(string) == slen);
	srepo_assert((plen + slen) < SREPO_XPATH_SIZE);

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

static char *
nlifd_iface_name_from_xpath(char * xpath, char name[IFNAMSIZ])
{
	srplug_assert(xpath);
	srplug_assert(name);

	sr_xpath_ctx_t ctx;
	const char *   str;
	size_t         len;
	const char *   msg;

	/*
	 * TODO: remove call to srepo_xpath_validate() since xpath should come
	 * from returned value of srepo_dat_path().
	 */
	if (srepo_xpath_validate(xpath) < 0) {
		msg = "invalid interface path";
		goto err;
	}
	str = srepo_xpath_key_value(xpath, "interface", "name", &ctx);
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
	srepo_xpath_recover(&ctx);
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

/******************************************************************************
 * Features handling.
 ******************************************************************************/

static struct srplug_feat
nlifd_arbitrary_names_feat = SRPLUG_FEAT_SETUP("arbitrary-names");

static struct srplug_feat
nlifd_pre_provisioning_feat = SRPLUG_FEAT_SETUP("pre-provisioning");

static struct srplug_feat
nlifd_if_mib_feat = SRPLUG_FEAT_SETUP("if-mib");

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

	return srepo_dat_set(session,
	                     path,
	                     nlif_iface_admstate(interface) ? "up" : "down",
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

	return srepo_dat_set(session,
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

	return srepo_dat_setf(session,
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

	return srepo_dat_set(session,
	                     path,
	                     nlif_link_hwaddr_str(nlif_iface_hwaddr(interface),
	                                          str),
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

	iface = nlifd_iface_from_node(container, store);
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

	ret = srplug_dat_create_leaf_printf(container,
	                                    "statistics/in-octets",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.rx_bytes);
	if (ret != SR_ERR_OK)
		return ret;

	/* TODO: in-unicast-pkts */
	/* TODO: in-broadcast-pkts */
	/* TODO: in-multicast-pkts */

	ret = srplug_dat_create_leaf_printf(container,
	                                    "statistics/in-discards",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.rx_dropped);
	if (ret != SR_ERR_OK)
		return ret;

	ret = srplug_dat_create_leaf_printf(container,
	                                    "statistics/in-errors",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.rx_errors);
	if (ret != SR_ERR_OK)
		return ret;

	/* TODO: in-unknown-protos */

	ret = srplug_dat_create_leaf_printf(container,
	                                    "statistics/out-octets",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.tx_bytes);
	if (ret != SR_ERR_OK)
		return ret;

	/* TODO: out-unicast-pkts */
	/* TODO: out-broadcast-pkts */
	/* TODO: out-multicast-pkts */

	ret = srplug_dat_create_leaf_printf(container,
	                                    "statistics/out-discards",
	                                    NULL,
	                                    "%" PRIu64,
	                                    stats.tx_dropped);
	if (ret != SR_ERR_OK)
		return ret;

	ret = srplug_dat_create_leaf_printf(container,
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
		srplug_node_warn(*parent,
		                 "cannot fill in interface statistics: %s",
		                 sr_strerror(err));
		return err;
	}

	srplug_node_debug(*parent, "interface statistics filled in");

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
nlifd_iface_change_enabled(const struct lyd_node * leaf,
                           sr_change_oper_t        oper,
                           const char *            old,
                           void *                  store)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, store);
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
                            void *                  store)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

	struct nlif_iface * iface;
	bool                val;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, store);
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
                        void *                  store)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

	struct nlif_iface * iface;
	const char *        val = srepo_dat_node_as_str(leaf);
	const char *        type;
	int                 ret;

	iface = nlifd_iface_from_node(leaf, store);
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
                         void *                  store __unused)
{
	srplug_assert(srepo_dat_node_type(leaf) & LYD_NODE_TERM);
	nlif_store_assert((const struct nlif_store *)store);

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
				srplug_path_notice(
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
		srplug_path_debug(xpath, "interfaces configured");
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
	struct nlif_iface * iface = data;
	char *              path;
	ssize_t             len;
	sr_session_ctx_t *  sess = srplug_daemon_session(&dmn->super);
	sr_error_t          ret;

	nlifd_assert(dmn);
	nlif_iface_assert(iface);

	len = nlifd_iface_create_path(&path, iface);
	if (len < 0) {
		srplug_warn("[%u]: cannot refresh interface status: %s",
		            nlif_iface_index(iface),
		            strerror(-len));
		return;
	}

	ret = nlifd_iface_refresh_oper(sess, path, len, iface);
	if (ret != SR_ERR_OK)
		goto discard;

	ret = srepo_apply_changes(sess);
	if (ret == SR_ERR_OK) {
#if defined(CONFIG_NLIF_DEBUG)
		path[len] = '\0';
		srplug_path_debug(path, "interface status refreshed");
#endif /* defined(CONFIG_NLIF_DEBUG) */
		srepo_free(path);
		return;
	}

discard:
	srepo_discard_oper_changes(sess, NLIFD_IETF_IFACE_YANG_MODULE);

	/*
	 * Fix path since it may have been modified by calls to
	 * nlifd_iface_fill_admstate() or nlifd_iface_fill_operstate().
	 */
	path[len] = '\0';
	srplug_path_warn(path,
	                 "cannot refresh interface status: %s",
	                 sr_strerror(ret));
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

static sr_error_t
nlifd_iface_setup_oper_dstore(sr_session_ctx_t *   session,
                              const struct nlifd * daemon)
{
	srplug_assert(session);
	nlifd_assert(daemon);

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
			goto discard;
		}

		ret = nlifd_iface_refresh_oper(session, path, len, iface);
		if (ret != SR_ERR_OK)
			goto discard;

		if (nlifd_if_mib_feat.on) {
			ret = nlifd_iface_fill_index(session, path, len, iface);
			if (ret != SR_ERR_OK)
				goto discard;
		}

		ret = nlifd_iface_fill_hwaddr(session, path, len, iface);
		if (ret != SR_ERR_OK)
			goto discard;
	}

	ret = srepo_apply_changes(session);
	if (ret == SR_ERR_OK) {
		srepo_free(path);
		return ret;
	}

discard:
	srepo_discard_oper_changes(session, NLIFD_IETF_IFACE_YANG_MODULE);
	srepo_free(path);

	srplug_warn("cannot setup interfaces status: %s", sr_strerror(ret));

	return ret;
}

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
	                                     &daemon->store);
	if (ret != SR_ERR_OK)
		goto release;

	ret = nlifd_iface_setup_oper_dstore(sess, daemon);
	if (ret != SR_ERR_OK)
		goto release;

	ret = srplug_daemon_oper_subscribe(&daemon->super,
	                                   &nlifd_oper_sub,
	                                   &daemon->store);
	if (ret)
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
