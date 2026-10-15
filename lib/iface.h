#ifndef _NLIF_IFACE_H
#define _NLIF_IFACE_H

#include "link.h"
#include <linux/if.h>
#include <net/ethernet.h>
#include <stdbool.h>

struct nlif_gate;
struct rt_link_getlink_rsp;

/******************************************************************************
 * Interface handling.
 ******************************************************************************/

struct nlif_iface {
	unsigned int      idx;
	unsigned int      group;
	unsigned int      flags;
	unsigned char     opstat;
	unsigned char     lnkmod;
	unsigned short    type;
	char              name[IFNAMSIZ];
	char *            alias;
	char *            altname;
	char *            kind;
	unsigned int      lnk;
	unsigned int      mst;
	unsigned int      mtu;
	struct ether_addr hwadr;
};

#define nlif_iface_assert(_iface) \
	nlif_assert(_iface); \
	nlif_assert((_iface)->idx); \
	nlif_assert((_iface)->name[0] != '\0'); \
	nlif_assert(strlen((_iface)->name) < sizeof((_iface)->name)); \
	nlif_assert(!(_iface)->alias || ((_iface)->alias[0] != '\0')); \
	nlif_assert(!(_iface)->alias || \
	             (strlen((_iface)->alias) < sizeof((_iface)->name))); \
	nlif_assert(!(_iface)->altname || ((_iface)->altname[0] != '\0')); \
	nlif_assert(!(_iface)->altname || \
	             (strlen((_iface)->altname) < sizeof((_iface)->name))); \
	nlif_assert(!(_iface)->kind || ((_iface)->kind[0] != '\0'))

static inline int
nlif_iface_validate_index(unsigned int index)
{
	return nlif_link_validate_index(index);
}

static inline ssize_t
nlif_iface_validate_strid(const char * string)
{
	return nlif_link_validate_strid(string);
}

static inline unsigned int
nlif_iface_index(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->idx;
}

static inline unsigned int
nlif_iface_group(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->group;
}

/*
 * Return netdevice IFF_* flags.
 *
 * These are described as `ifr_flags' within the netdevice(7) man page.
 * These are are also described at the `enum net_device_flags' definition within
 * the <linux>/if.h uapi header file.
 * Last but not least, these are refered to as `ifi_flags' within the
 * `ifinfomsg' structure described in rtnetlink(7) man page.
 *
 * Note that these are visible through the /sys/class/net/<netdev>/flags file
 * exposed by the sysfs.
 *
 * For more informations about interface states (and flags), see:
 * <linux>/Documentation/networking/operstates.rst
 */
static inline unsigned int
nlif_iface_flags(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->flags;
}

/*
 * Return administrative state for the interface given in argument.
 *
 * @return: Administrative state.
 * @retval true  Interface administrative state is up.
 * @retval false Interface administrative state is down.
 *
 * For more informations about interface states (and flags), see:
 * <linux>/Documentation/networking/operstates.rst
 */
static inline bool
nlif_iface_admstate(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return nlif_iface_flags(interface) & IFF_UP;
}

/*
 * RFC 2863 operational status.
 *
 * Defined into the <linux>/if.h uapi header file.
 *
 * Document me (and relations to undelying carrier state) !
 *
 * Note that this is visible through the /sys/class/net/<netdev>/operstate file
 * exposed by the sysfs.
 *
 * For more informations about interface states, see:
 * <linux>/Documentation/networking/operstates.rst
 */
static inline unsigned char
nlif_iface_operstate(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->opstat;
}

/*
 * Return carrier state of underlying physical layer for the interface given in
 * argument.
 *
 * @return: Lower layer carrier state.
 * @retval true  state is on.
 * @retval false state is off.
 *
 * The kernel scheduler stops sending packets over this interface when its lower
 * layer carrier if off.
 * See nlif_iface_operstate() documentation for more informations about relation
 * to interface's operational state.
 *
 * Note that this is visible through the /sys/class/net/<netdev>/carrier file
 * exposed by the sysfs.
 *
 * For more informations about interface states (and flags), see:
 * <linux>/Documentation/networking/operstates.rst
 */
static inline bool
nlif_iface_carrier(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return nlif_iface_flags(interface) & IFF_LOWER_UP;
}

/*
 * RFC 2863 operational status (link) mode.
 *
 * Defined into the <linux>/if.h uapi header file.
 *
 * Note that this is visible through the /sys/class/net/<netdev>/link_mode file
 * exposed by the sysfs.
 *
 * For more informations about interface states, see:
 * <linux>/Documentation/networking/operstates.rst
 */
static inline unsigned char
nlif_iface_linkmode(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->lnkmod;
}

static inline unsigned short
nlif_iface_type(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->type;
}

static inline const char *
nlif_iface_name(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->name;
}

static inline const char *
nlif_iface_alias(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->alias;
}

static inline const char *
nlif_iface_altname(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->altname;
}

static inline const char *
nlif_iface_kind(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->kind;
}

static inline unsigned int
nlif_iface_link(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->lnk;
}

static inline unsigned int
nlif_iface_master(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->mst;
}

static inline unsigned int
nlif_iface_mtu(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->mtu;
}

/*
 * Return the hardware MAC address for the interface given in argument.
 *
 * @return A pointer to the hardware MAC address structure.
 *
 * See ether_aton_r(3) ether_ntoa_r(3) man pages.
 */
static inline const struct ether_addr *
nlif_iface_hwaddr(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return &interface->hwadr;
}

#if defined(CONFIG_NLIF_PRINT)

extern void
nlif_iface_print(const struct nlif_iface * interface, FILE * stdio);

#endif /* defined(CONFIG_NLIF_PRINT) */

extern int
nlif_iface_reload(struct nlif_iface * interface, const struct nlif_gate * gate);

extern void
nlif_iface_refresh_state(struct nlif_iface *                interface,
                         const struct rt_link_getlink_rsp * link);

extern void
nlif_iface_reload_bylink(struct nlif_iface *                interface,
                         const struct rt_link_getlink_rsp * link);

extern void
nlif_iface_load_bylink(struct nlif_iface *                interface,
                       const struct rt_link_getlink_rsp * link);

extern int
nlif_iface_load_byidx(struct nlif_iface *      interface,
                      unsigned int             index,
                      const struct nlif_gate * gate);

extern int
nlif_iface_load_byname(struct nlif_iface *      interface,
                       const char *             name,
                       const struct nlif_gate * gate);

extern void
nlif_iface_fini(struct nlif_iface * interface);

extern struct nlif_iface *
nlif_iface_alloc(void);

extern void
nlif_iface_free(struct nlif_iface * interface);

extern void
nlif_iface_create_bylink(const struct rt_link_getlink_rsp * link,
                         struct nlif_iface **               interface);

extern int
nlif_iface_create_byidx(unsigned int             index,
                        const struct nlif_gate * gate,
                        struct nlif_iface **     interface);

extern int
nlif_iface_create_byname(const char *             name,
                         const struct nlif_gate * gate,
                         struct nlif_iface **     interface);

extern void
nlif_iface_destroy(struct nlif_iface * interface);

#endif /* _NLIF_IFACE_H */
