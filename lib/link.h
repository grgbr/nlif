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
	nlif_assert(nlif_link_validate_name((_lnk)->ifname) == \
	            (_lnk)->_len.ifname); \
	nlif_assert((nlif_link_validate_alias((_lnk)->ifalias) == \
	             (_lnk)->_len.ifalias)); \
	nlif_assert(!((_lnk)->_present.linkinfo && \
	              (_lnk)->linkinfo._len.kind) || \
	            (strlen((_lnk)->linkinfo.kind) == \
	             (_lnk)->linkinfo._len.kind)); \
	nlif_assert(!(_lnk)->_present.link || (_lnk)->link); \
	nlif_assert(!(_lnk)->_present.master || (_lnk)->master); \
	nlif_assert((_lnk)->_present.mtu); \
	nlif_assert((_lnk)->_present.min_mtu); \
	nlif_assert((_lnk)->_present.max_mtu); \
	nlif_assert(!(_lnk)->min_mtu || ((_lnk)->mtu >= (_lnk)->min_mtu)); \
	nlif_assert(!(_lnk)->max_mtu || ((_lnk)->mtu <= (_lnk)->max_mtu)); \
	nlif_assert((_lnk)->_len.address == sizeof(struct ether_addr)); \
	nlif_assert(nlif_link_hwaddr_is_ucast((_lnk)->address))

static inline int
nlif_link_validate_index(unsigned int index)
{
	return (index && (index <= (unsigned int)INT_MAX)) ? 0 : -EINVAL;
}

extern ssize_t
nlif_link_validate_name(const char * name);

extern ssize_t
nlif_link_validate_alias(const char * alias);

static inline bool
nlif_link_hwaddr_is_ucast(const struct ether_addr * address)
{
	return !(address->ether_addr_octet[0] & 0x1);
}

static inline bool
nlif_link_hwaddr_is_mcast(const struct ether_addr * address)
{
	return !nlif_link_hwaddr_is_ucast(address);
}

#if defined(CONFIG_NLIF_PRINT)

#include <stdio.h>

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
