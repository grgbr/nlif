#include "iface.h"
#include "gate.h"
#include "link.h"
#include <stroll/bmap.h>

static void
nlif_iface_fill(struct nlif_iface *                interface,
                const struct rt_link_getlink_rsp * link)
{
	nlif_link_assert(link);

	interface->state = NLIF_CLEAN_STAT;
	stroll_bmap_clear_all(&interface->dirty_fields);
	interface->idx = link->_hdr.ifi_index;
	interface->group = link->group;
	interface->flags = link->_hdr.ifi_flags;
	stroll_bmap_clear_all(&interface->dirty_flags);
	interface->opstat = link->operstate;
	interface->lnkmod = link->linkmode;
	interface->type = link->_hdr.ifi_type;
	memcpy(interface->name, link->ifname, link->_len.ifname);
	interface->name[link->_len.ifname] = '\0';

	if (link->_len.ifalias) {
		interface->alias = nlif_malloc(link->_len.ifalias + 1);
		memcpy(interface->alias,
		       link->ifalias,
		       link->_len.ifalias);
		interface->alias[link->_len.ifalias] = '\0';
	}
	else
		interface->alias = NULL;

	if (link->_present.linkinfo && link->linkinfo._len.kind) {
		interface->kind = nlif_malloc(link->linkinfo._len.kind + 1);
		memcpy(interface->kind,
		       link->linkinfo.kind,
		       link->linkinfo._len.kind);
		interface->kind[link->linkinfo._len.kind] = '\0';
	}
	else
		interface->kind = NULL;

	interface->lnk = (link->_present.link) ? link->link : 0;
	interface->mst = (link->_present.master) ? link->master : 0;
	interface->mtu = link->mtu;
	interface->min_mtu = link->min_mtu;
	interface->max_mtu = link->max_mtu;

	memcpy(&interface->hwadr, link->address, sizeof(interface->hwadr));

	nlif_debug("%s[%u]: interface synchronized.",
	           interface->name,
	           interface->idx);
}

static int
_nlif_iface_load_byidx(struct nlif_iface *      interface,
                       unsigned int             index,
                       const struct nlif_gate * gate)
{
	nlif_assert(interface);
	nlif_assert(!nlif_iface_validate_index(index));
	nlif_gate_assert(gate);

	struct rt_link_getlink_rsp * lnk;
	int                          err;

	err = nlif_gate_load_link_byidx(gate, index, &lnk);
	if (err) {
		interface->state = NLIF_INVALID_STAT;
		nlif_warn("[%u]: cannot load interface by index: %s.",
		          index,
		          strerror(-err));
		return err;
	}

	nlif_iface_fill(interface, lnk);

	nlif_gate_destroy_link(lnk);

	return 0;
}

static int
nlif_iface_sync(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	switch (interface->state) {
	case NLIF_CLEAN_STAT:
	case NLIF_DIRTY_STAT:
		return 0;

	case NLIF_INVALID_STAT:
		return _nlif_iface_load_byidx(interface,
		                              interface->idx,
		                              interface->gate);

	default:
		nlif_assert(0);
		return -EBADFD;
	}
}

#if defined(CONFIG_NLIF_PRINT)

int
nlif_iface_print(struct nlif_iface * interface, FILE * stdio)
{
	nlif_iface_assert(interface);
	nlif_assert(stdio);

	int    ret;
	char * str;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	str = nlif_malloc(stroll_max(stroll_max(NLIF_LINK_FLAGS_STRSZ,
	                                        NLIF_LINK_HWADDR_STRSZ),
	                             NLIF_UINT_STRSZ));
	nlif_assert(str);

	fprintf(stdio, "%3u: %s\n", interface->idx, interface->name);

	fprintf(stdio, "     group:     %u\n", interface->group);


	fprintf(stdio,
	        "     flags:     %s\n",
	        nlif_link_flags_str(interface->flags, str));

	fprintf(stdio,
	        "     operstate: %s\n",
	        nlif_link_operstate_str(interface->opstat));

	fprintf(stdio,
	        "     linkmode:  %s\n",
	        nlif_link_mode_str(interface->lnkmod));

	fprintf(stdio,
	        "     type:      %s\n",
	        nlif_link_type_str(interface->type));

	fprintf(stdio,
	        "     alias:     %s\n",
	        nlif_link_alias_str(interface->alias));

	fprintf(stdio,
	        "     kind:      %s\n",
	        nlif_link_kind_str(interface->kind));

	fprintf(stdio,
	        "     link:      %s\n",
	        nlif_link_link_str(interface->lnk, str));

	fprintf(stdio,
	        "     master:    %s\n",
	        nlif_link_master_str(interface->mst, str));
	
	fprintf(stdio,
	        "     mtu:       %s\n",
	        nlif_link_mtu_str(interface->mtu, str));
	fprintf(stdio,
	        "     min_mtu:   %s\n",
	        nlif_link_mtu_str(interface->min_mtu, str));
	fprintf(stdio,
	        "     max_mtu:   %s\n",
	        nlif_link_mtu_str(interface->max_mtu, str));

	fprintf(stdio,
	        "     hwaddr:    %s\n",
	        nlif_link_hwaddr_str(&interface->hwadr, str));

	nlif_free((void *)str);

	return 0;
}

#endif /* defined(CONFIG_NLIF_PRINT) */

int
nlif_iface_set_name(struct nlif_iface * interface, const char * name)
{
	nlif_iface_assert(interface);
	nlif_assert(nlif_iface_validate_name(name) > 0);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	if (strcmp(interface->name, name)) {
		/*
		 * Replace old name (if any) with the new one provided
		 * that `name' does not conflict with current
		 * interface's alias.
		 */
		if (!strcmp(name, interface->alias))
			return -EEXIST;

		interface->state = NLIF_DIRTY_STAT;
		stroll_bmap_set(&interface->dirty_fields,
		                NLIF_NAME_IFACE_FLD);
		memcpy(interface->name, name, sizeof(interface->name));
	}

	return 0;
}

int
nlif_iface_set_alias(struct nlif_iface * interface, const char * alias)
{
	nlif_iface_assert(interface);
	nlif_assert(nlif_iface_validate_alias(alias) >= 0);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	if (alias) {
		if (!interface->alias || strcmp(interface->alias, alias)) {
			/*
			 * Replace old alias (if any) with the new one provided
			 * that `alias' does not conflict with current
			 * interface's name.
			 */
			if (!strcmp(alias, interface->name))
				return -EEXIST;

			interface->state = NLIF_DIRTY_STAT;
			stroll_bmap_set(&interface->dirty_fields,
			                NLIF_ALIAS_IFACE_FLD);
			nlif_free(interface->alias);
			interface->alias = nlif_dup_str(alias);
		}
	}
	else {
		if (interface->alias) {
			/* Remove existing alias. */
			interface->state = NLIF_DIRTY_STAT;
			stroll_bmap_set(&interface->dirty_fields,
			                NLIF_ALIAS_IFACE_FLD);
			nlif_free(interface->alias);
			interface->alias = NULL;
		}
	}

	return 0;
}

int
nlif_iface_coherent_group(struct nlif_iface * interface, unsigned int * group)
{
	nlif_iface_assert(interface);
	nlif_assert(group);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*group = nlif_iface_group(interface);

	return 0;
}

int
nlif_iface_set_group(struct nlif_iface * interface, unsigned int group)
{
	nlif_iface_assert(interface);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	if (group != interface->group) {
		interface->state = NLIF_DIRTY_STAT;
		stroll_bmap_set(&interface->dirty_fields, NLIF_GROUP_IFACE_FLD);
		interface->group = group;
	}

	return 0;
}

int
nlif_iface_coherent_flags(struct nlif_iface * interface, unsigned int * flags)
{
	nlif_iface_assert(interface);
	nlif_assert(flags);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*flags = nlif_iface_flags(interface);

	return 0;
}

int
nlif_iface_set_admstate(struct nlif_iface * interface, bool up)
{
	nlif_iface_assert(interface);

	int          ret;
	unsigned int msk = up ? IFF_UP : 0;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	if ((interface->flags & IFF_UP) != msk) {
		interface->state = NLIF_DIRTY_STAT;
		interface->flags &= ~IFF_UP;
		interface->flags |= msk;
		interface->dirty_flags |= IFF_UP;
	}

	return 0;
}

int
nlif_iface_coherent_operstate(struct nlif_iface * interface,
                              unsigned char *     state)
{
	nlif_iface_assert(interface);
	nlif_assert(state);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*state = nlif_iface_operstate(interface);

	return 0;
}

int
nlif_iface_coherent_linkmode(struct nlif_iface * interface,
                             unsigned char *     mode)
{
	nlif_iface_assert(interface);
	nlif_assert(mode);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*mode = nlif_iface_linkmode(interface);

	return 0;
}

int
nlif_iface_coherent_type(struct nlif_iface * interface, unsigned short * type)
{
	nlif_iface_assert(interface);
	nlif_assert(type);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*type = nlif_iface_type(interface);

	return 0;
}

int
nlif_iface_coherent_name(struct nlif_iface * interface, const char ** name)
{
	nlif_iface_assert(interface);
	nlif_assert(name);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*name = nlif_iface_name(interface);

	return 0;
}

int
nlif_iface_coherent_alias(struct nlif_iface * interface, const char ** alias)
{
	nlif_iface_assert(interface);
	nlif_assert(alias);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*alias = nlif_iface_alias(interface);

	return 0;
}

int
nlif_iface_coherent_kind(struct nlif_iface * interface, const char ** kind)
{
	nlif_iface_assert(interface);
	nlif_assert(kind);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*kind = nlif_iface_kind(interface);

	return 0;
}

int
nlif_iface_coherent_link(struct nlif_iface * interface, unsigned int * link)
{
	nlif_iface_assert(interface);
	nlif_assert(link);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*link = nlif_iface_link(interface);

	return 0;
}

int
nlif_iface_coherent_master(struct nlif_iface * interface, unsigned int * master)
{
	nlif_iface_assert(interface);
	nlif_assert(master);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*master = nlif_iface_master(interface);

	return 0;
}

int
nlif_iface_coherent_mtu(struct nlif_iface * interface, unsigned int * mtu)
{
	nlif_iface_assert(interface);
	nlif_assert(mtu);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*mtu = nlif_iface_mtu(interface);

	return 0;
}

int
nlif_iface_set_mtu(struct nlif_iface * interface, unsigned int mtu)
{
	nlif_iface_assert(interface);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	if (mtu != interface->mtu) {
		ret = nlif_iface_validate_mtu(mtu, interface);
		if (!ret) {
			interface->state = NLIF_DIRTY_STAT;
			stroll_bmap_set(&interface->dirty_fields,
			                NLIF_MTU_IFACE_FLD);
			interface->mtu = mtu;
		}
	}

	return ret;
}

int
nlif_iface_validate_hwaddr(const struct ether_addr * address)
{
	nlif_assert(address);

	static const struct ether_addr zero = { 0, };

	/*
	 * Reject `00:00:00:00:00:00' address and accept unicast addresses only.
	 *
	 * Note that some device may hold a zero address when their internal ROM
	 * / address logic has not been initialized yet.
	 * This is the reason why a zero address is not rejected at interface /
	 * link creation / loading time.
	 */
	if (memcmp(address, &zero, sizeof(*address)) &&
	    nlif_link_hwaddr_is_ucast(address))
		return 0;

	return -EINVAL;
}

int
nlif_iface_coherent_hwaddr(struct nlif_iface *        interface,
                           const struct ether_addr ** address)
{
	nlif_iface_assert(interface);
	nlif_assert(address);

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	*address = nlif_iface_hwaddr(interface);

	return 0;
}

int
nlif_iface_set_hwaddr(struct nlif_iface *       interface,
                      const struct ether_addr * address)
{
	nlif_iface_assert(interface);
	nlif_assert(!nlif_iface_validate_hwaddr(address));

	int ret;

	ret = nlif_iface_sync(interface);
	if (ret)
		return ret;

	if (memcmp(address, &interface->hwadr, sizeof(*address))) {
		interface->state = NLIF_DIRTY_STAT;
		stroll_bmap_set(&interface->dirty_fields,
		                NLIF_HWADR_IFACE_FLD);
		memcpy(&interface->hwadr, address, sizeof(*address));
	}

	return 0;
}

struct nlif_iface *
nlif_iface_alloc(void)
{
	struct nlif_iface * iface;

	iface = nlif_malloc(sizeof(*iface));
	nlif_assert(iface);

	iface->state = NLIF_STAT_NR;

	return iface;
}

void
nlif_iface_free(struct nlif_iface * interface)
{
	nlif_assert(interface);

	nlif_free(interface);
}

void
nlif_iface_refresh_state(struct nlif_iface *                interface,
                         const struct rt_link_getlink_rsp * link)
{
	nlif_iface_assert(interface);
	nlif_assert(!nlif_gate_islink_valid(link));
	nlif_assert(interface->idx == (unsigned int)link->_hdr.ifi_index);

	interface->flags &= ~IFF_VOLATILE;
	interface->flags |= link->_hdr.ifi_flags & IFF_VOLATILE;
	interface->opstat = link->operstate;
	interface->lnkmod = link->linkmode;

	nlif_info("%s[%u]: operational state changed.",
	          interface->name,
	          interface->idx);
}

void
nlif_iface_reload_bylink(struct nlif_iface *                interface,
                         const struct rt_link_getlink_rsp * link)
{
	nlif_iface_assert(interface);
	nlif_assert(!nlif_gate_islink_valid(link));
	nlif_assert(interface->idx == (unsigned int)link->_hdr.ifi_index);

	nlif_iface_fini(interface);

	nlif_iface_fill(interface, link);
}

int
nlif_iface_reload(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	nlif_iface_fini(interface);

	return _nlif_iface_load_byidx(interface,
	                              interface->idx,
	                              interface->gate);
}

int
nlif_iface_load_byidx(struct nlif_iface *      interface,
                      unsigned int             index,
                      const struct nlif_gate * gate)
{
	nlif_assert(interface);
	nlif_assert(!nlif_iface_validate_index(index));
	nlif_gate_assert(gate);

	int ret;

	ret = _nlif_iface_load_byidx(interface, index, gate);
	if (!ret) {
		interface->gate = gate;
		return 0;
	}

	return ret;
}

int
nlif_iface_load_byname(struct nlif_iface *      interface,
                       const char *             name,
                       const struct nlif_gate * gate)
{
	nlif_assert(interface);
	nlif_assert(nlif_iface_validate_name(name) > 0);
	nlif_gate_assert(gate);

	struct rt_link_getlink_rsp * lnk;
	int                          err;

	err = nlif_gate_load_link_byname(gate, name, &lnk);
	if (err) {
		interface->state = NLIF_INVALID_STAT;
		nlif_warn("%s: cannot load interface by name: %s.",
		          name,
		          strerror(-err));
		return err;
	}

	interface->gate = gate;
	nlif_iface_fill(interface, lnk);

	nlif_gate_destroy_link(lnk);

	return 0;
}

int
nlif_iface_apply(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	int ret = 0;

	if (interface->state == NLIF_DIRTY_STAT) {
		nlif_assert(interface->dirty_fields ||
		            interface->dirty_flags);

		struct rt_link_setlink_req * req;

		req = nlif_gate_create_setlink_req();
		nlif_assert(req);

		req->_hdr.ifi_index = (int)interface->idx;
		req->_hdr.ifi_flags = interface->flags;
		req->_hdr.ifi_change = interface->dirty_flags;

		if (stroll_bmap_test(interface->dirty_fields,
		                     NLIF_GROUP_IFACE_FLD))
			rt_link_setlink_req_set_group(req, interface->group);

		if (stroll_bmap_test(interface->dirty_fields,
		                     NLIF_NAME_IFACE_FLD))
			rt_link_setlink_req_set_ifname(req, interface->name);


		if (stroll_bmap_test(interface->dirty_fields,
		                     NLIF_ALIAS_IFACE_FLD)) {
#if defined(NLIF_YNL_NULL_STR)
			if (!interface->alias) {
				/* Remove alias. */
				free(req->ifalias);
				req->_len.ifalias = 1;
				req->ifalias = NULL;
			}
			else
				rt_link_setlink_req_set_ifalias(
					req,
					interface->alias);
#else
			rt_link_setlink_req_set_ifalias(req, interface->alias);
#endif
		}

		if (stroll_bmap_test(interface->dirty_fields,
		                     NLIF_MTU_IFACE_FLD))
			rt_link_setlink_req_set_mtu(req, interface->mtu);

		if (stroll_bmap_test(interface->dirty_fields,
		                     NLIF_HWADR_IFACE_FLD))
			rt_link_setlink_req_set_address(
				req,
				&interface->hwadr,
				sizeof(interface->hwadr));

		ret = nlif_gate_setlink(interface->gate, req);

		nlif_gate_destroy_setlink_req(req);

		if (!ret) {
			interface->state = NLIF_CLEAN_STAT;
			stroll_bmap_clear_all(&interface->dirty_fields);
			stroll_bmap_clear_all(&interface->dirty_flags);

			nlif_debug("'%s[%u]: interface settings applied.",
			           interface->name,
			           interface->idx);
			ret = 1;
		}
		else {
			interface->state = NLIF_INVALID_STAT;

			nlif_warn("%s[%u]: "
			          "cannot apply interface settings: %s",
			          interface->name,
			          interface->idx,
			          strerror(-ret));
		}
	}

	return ret;
}

void
nlif_iface_fini(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	nlif_free(interface->alias);
	nlif_free(interface->kind);
}

int
nlif_iface_create_byidx(unsigned int             index,
                        const struct nlif_gate * gate,
                        struct nlif_iface **     interface)
{
	nlif_assert(!nlif_iface_validate_index(index));
	nlif_gate_assert(gate);
	nlif_assert(interface);

	struct nlif_iface * iface;
	int                 err;

	iface = nlif_iface_alloc();

	err = nlif_iface_load_byidx(iface, index, gate);
	if (!err) {
		*interface = iface;
		return 0;
	}

	nlif_iface_free(iface);

	return err;
}

int
nlif_iface_create_byname(const char *             name,
                         const struct nlif_gate * gate,
                         struct nlif_iface **     interface)
{
	nlif_assert(nlif_iface_validate_name(name) > 0);
	nlif_gate_assert(gate);
	nlif_assert(interface);

	struct nlif_iface * iface;
	int                 err;

	iface = nlif_iface_alloc();

	err = nlif_iface_load_byname(iface, name, gate);
	if (!err) {
		*interface = iface;
		return 0;
	}

	nlif_iface_free(iface);

	return err;
}

void
nlif_iface_create_bylink(const struct rt_link_getlink_rsp * link,
                         const struct nlif_gate *           gate,
                         struct nlif_iface **               interface)
{
	nlif_assert(link);
	nlif_assert(!nlif_gate_islink_valid(link));
	nlif_gate_assert(gate);
	nlif_assert(interface);

	struct nlif_iface * iface;

	iface = nlif_iface_alloc();

	iface->gate = gate;
	nlif_iface_fill(iface, link);

	*interface = iface;
}

void
nlif_iface_destroy(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	nlif_iface_fini(interface);
	nlif_iface_free(interface);
}
