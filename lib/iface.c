#include "iface.h"
#include "gate.h"
#include "link.h"
#include <stroll/cdefs.h>

#if defined(CONFIG_NLIF_PRINT)

void
nlif_iface_print(const struct nlif_iface * interface, FILE * stdio)
{
	nlif_iface_assert(interface);

	char * str;

	str = nlif_malloc(stroll_max(stroll_max(NLIF_LINK_FLAGS_STRSZ,
	                                        NLIF_LINK_HWADDR_STRSZ),
	                             NLIF_UINT_STRSZ));
	nlif_assert(str);

	fprintf(stdio,
	        "%3u: %s\n",
	        nlif_iface_index(interface),
	        nlif_iface_name(interface));

	fprintf(stdio, "     group:     %u\n", interface->group);


	fprintf(stdio,
	        "     flags:     %s\n",
	        nlif_link_flags_str(nlif_iface_flags(interface), str));

	fprintf(stdio,
	        "     operstate: %s\n",
	        nlif_link_operstate_str(nlif_iface_operstate(interface)));

	fprintf(stdio,
	        "     linkmode:  %s\n",
	        nlif_link_mode_str(nlif_iface_linkmode(interface)));

	fprintf(stdio,
	        "     type:      %s\n",
	        nlif_link_type_str(nlif_iface_type(interface)));

	fprintf(stdio,
	        "     alias:     %s\n",
	        nlif_link_alias_str(nlif_iface_alias(interface)));

	fprintf(stdio,
	        "     altname:   %s\n",
	        nlif_link_altname_str(nlif_iface_altname(interface)));

	fprintf(stdio,
	        "     kind:      %s\n",
	        nlif_link_kind_str(nlif_iface_kind(interface)));

	fprintf(stdio,
	        "     link:      %s\n",
	        nlif_link_link_str(nlif_iface_link(interface), str));

	fprintf(stdio,
	        "     master:    %s\n",
	        nlif_link_master_str(nlif_iface_master(interface), str));
	

	fprintf(stdio,
	        "     mtu:       %s\n",
	        nlif_link_mtu_str(nlif_iface_mtu(interface), str));

	fprintf(stdio,
	        "     hwaddr:    %s\n",
	        nlif_link_hwaddr_str(nlif_iface_hwaddr(interface), str));

	nlif_free((void *)str);
}

#endif /* defined(CONFIG_NLIF_PRINT) */

struct nlif_iface *
nlif_iface_alloc(void)
{
	struct nlif_iface * iface;

	iface = nlif_malloc(sizeof(*iface));
	nlif_assert(iface);

	return iface;
}

void
nlif_iface_free(struct nlif_iface * interface)
{
	nlif_assert(interface);

	nlif_free(interface);
}

static void
nlif_iface_fill(struct nlif_iface *                interface,
                const struct rt_link_getlink_rsp * link)
{
	nlif_link_assert(link);

	interface->idx = link->_hdr.ifi_index;
	interface->group = link->group;
	interface->flags = link->_hdr.ifi_flags;
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

	if (link->_present.prop_list && link->prop_list._count.alt_ifname) {
		const struct ynl_string * alt = link->prop_list.alt_ifname[0];

		nlif_assert(alt->len);
		nlif_assert(alt->len < sizeof(interface->name));
		nlif_assert(strlen(alt->str) == alt->len);

		interface->altname = nlif_malloc(alt->len + 1);
		memcpy(interface->altname, alt->str, alt->len);
		interface->altname[alt->len] = '\0';
	}
	else
		interface->altname = NULL;

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
	interface->mtu = (link->_present.mtu) ? link->mtu : 0;

	memcpy(&interface->hwadr, link->address, sizeof(interface->hwadr));
}

void
nlif_iface_refresh_state(struct nlif_iface *                interface,
                         const struct rt_link_getlink_rsp * link)
{
	nlif_iface_assert(interface);
	nlif_assert(!nlif_gate_islink_valid(link));
	nlif_assert(interface->idx == (unsigned int)link->_hdr.ifi_index);

	interface->flags = link->_hdr.ifi_flags;
	interface->opstat = link->operstate;
	interface->lnkmod = link->linkmode;
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
nlif_iface_reload(struct nlif_iface * interface, const struct nlif_gate * gate)
{
	nlif_iface_assert(interface);

	unsigned int idx = interface->idx;

	nlif_iface_fini(interface);

	return nlif_iface_load_byidx(interface, idx, gate);
}

void
nlif_iface_load_bylink(struct nlif_iface *                interface,
                       const struct rt_link_getlink_rsp * link)
{
	nlif_assert(link);
	nlif_assert(!nlif_gate_islink_valid(link));

	nlif_iface_fill(interface, link);
}

int
nlif_iface_load_byidx(struct nlif_iface *      interface,
                      unsigned int             index,
                      const struct nlif_gate * gate)
{
	nlif_assert(interface);
	nlif_assert(!nlif_iface_validate_index(index));
	nlif_assert(gate);

	struct rt_link_getlink_rsp * lnk;
	int                          err;

	err = nlif_gate_load_link_byidx(gate, index, &lnk);
	if (err)
		return err;

	nlif_iface_fill(interface, lnk);

	nlif_gate_destroy_link(lnk);

	return 0;
}

int
nlif_iface_load_byname(struct nlif_iface *      interface,
                       const char *             name,
                       const struct nlif_gate * gate)
{
	nlif_assert(interface);
	nlif_assert(nlif_iface_validate_strid(name) > 0);
	nlif_assert(gate);

	struct rt_link_getlink_rsp * lnk;
	int                          err;

	err = nlif_gate_load_link_byname(gate, name, &lnk);
	if (err)
		return err;

	nlif_iface_fill(interface, lnk);

	nlif_gate_destroy_link(lnk);

	return 0;
}

void
nlif_iface_fini(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	nlif_free(interface->alias);
	nlif_free(interface->altname);
	nlif_free(interface->kind);
}

void
nlif_iface_create_bylink(const struct rt_link_getlink_rsp * link,
                         struct nlif_iface **               interface)
{
	nlif_assert(link);
	nlif_assert(!nlif_gate_islink_valid(link));
	nlif_assert(interface);

	struct nlif_iface * iface;

	iface = nlif_iface_alloc();
	nlif_iface_load_bylink(iface, link);

	*interface = iface;
}

int
nlif_iface_create_byidx(unsigned int             index,
                        const struct nlif_gate * gate,
                        struct nlif_iface **     interface)
{
	nlif_assert(gate);
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
	nlif_assert(name);
	nlif_assert(gate);
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
nlif_iface_destroy(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	nlif_iface_fini(interface);
	nlif_iface_free(interface);
}
