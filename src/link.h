#ifndef _NLIF_LINK_H
#define _NLIF_LINK_H

#include "common.h"
#include <ynl/rt-link-user.h>
#include <netinet/ether.h>

#define nlif_link_assert(_lnk) \
	nlif_assert(_lnk); \
	nlif_assert((_lnk)->_hdr.ifi_index); \
	nlif_assert((_lnk)->_present.operstate); \
	nlif_assert((_lnk)->_present.linkmode); \
	nlif_assert((_lnk)->_present.carrier); \
	nlif_assert((_lnk)->_present.group); \
	nlif_assert((_lnk)->_len.ifname); \
	nlif_assert((_lnk)->_len.ifname < IFNAMSIZ); \
	nlif_assert(strlen((_lnk)->ifname) == (_lnk)->_len.ifname); \
	nlif_assert(!(_lnk)->_len.ifalias || \
	            (((_lnk)->_len.ifalias < IFNAMSIZ) && \
	             (strlen((_lnk)->ifalias) == (_lnk)->_len.ifalias))); \
	nlif_assert(!((_lnk)->_present.linkinfo && \
	              (_lnk)->linkinfo._len.kind) || \
	            (strlen((_lnk)->linkinfo.kind) == \
	             (_lnk)->linkinfo._len.kind)); \
	nlif_assert(!(_lnk)->_present.link || (_lnk)->link); \
	nlif_assert(!(_lnk)->_present.master || (_lnk)->master); \
	nlif_assert(!(_lnk)->_present.mtu || (_lnk)->mtu); \
	nlif_assert((_lnk)->_len.address == sizeof(struct ether_addr))

static inline int
nlif_link_validate_index(unsigned int index)
{
	return (index && (index <= (unsigned int)INT_MAX)) ? 0 : -EINVAL;
}

extern ssize_t
nlif_link_validate_strid(const char * string);

#if defined(CONFIG_NLIF_PRINT)

#define NLIF_LINK_FLAGS_STRSZ (256U)

extern char *
nlif_link_flags_str(unsigned int flags, char string[NLIF_LINK_FLAGS_STRSZ]);

extern const char *
nlif_link_operstate_str(unsigned char operstate);

extern const char *
nlif_link_mode_str(unsigned char mode);

extern const char *
nlif_link_type_str(unsigned short type);

#define nlif_link_alias_str(_alias) \
	nlif_maybe_empty_str(_alias)

#define nlif_link_altname_str(_altname) \
	nlif_maybe_empty_str(_altname)

#define nlif_link_kind_str(_kind) \
	nlif_maybe_empty_str(_kind)

#define nlif_link_link_str(_ifindex, _string) \
	nlif_nozero_str(_ifindex, _string)

#define nlif_link_master_str(_ifindex, _string) \
	nlif_nozero_str(_ifindex, _string)

#define nlif_link_mtu_str(_mtu, _string) \
	nlif_nozero_str(_mtu, _string)

#define NLIF_LINK_HWADDR_STRSZ \
	((2U * ETH_ALEN) + (ETH_ALEN - 1U) + 1U)

static inline char *
nlif_link_hwaddr_str(const struct ether_addr * hwaddr,
                     char                      string[NLIF_LINK_HWADDR_STRSZ])
{
	nlif_assert(hwaddr);
	nlif_assert(string);

	return ether_ntoa_r(hwaddr, string);
}

struct rt_link_getlink_rsp;

extern void
nlif_link_print(const struct rt_link_getlink_rsp * link, FILE * stdio);

#endif /* defined(CONFIG_NLIF_PRINT) */

#endif /* _NLIF_LINK_H */
