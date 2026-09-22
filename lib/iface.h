#ifndef _NLIF_IFACE_H
#define _NLIF_IFACE_H

#include "link.h"
#include "gate.h"
#include <linux/if.h>
#include <net/ethernet.h>
#include <stdbool.h>

struct nlif_gate;
struct rt_link_getlink_rsp;

/******************************************************************************
 * Interface handling.
 ******************************************************************************/

enum nlif_iface_field {
	NLIF_GROUP_IFACE_FLD  = 0,
	NLIF_NAME_IFACE_FLD,
	NLIF_ALIAS_IFACE_FLD,
	NLIF_MTU_IFACE_FLD,
	NLIF_HWADR_IFACE_FLD,
	NLIF_IFACE_FLD_NR
};

struct nlif_iface {
	enum nlif_state          state;
	unsigned int             dirty_fields;
	const struct nlif_gate * gate;
	unsigned int             idx;
	unsigned int             group;
	unsigned int             flags;
	unsigned int             dirty_flags;
	unsigned char            opstat;
	unsigned char            lnkmod;
	unsigned short           type;
	char                     name[IFNAMSIZ];
	char *                   alias;
	char *                   kind;
	unsigned int             lnk;
	unsigned int             mst;
	unsigned int             mtu;
	unsigned int             min_mtu;
	unsigned int             max_mtu;
	struct ether_addr        hwadr;
};

#define nlif_iface_assert(_iface) \
	nlif_assert(_iface); \
	nlif_assert((_iface)->state >= 0); \
	nlif_assert((_iface)->state < NLIF_STAT_NR); \
	nlif_assert(!((_iface)->dirty_fields & \
	              ~((1U << NLIF_IFACE_FLD_NR) - 1))); \
	nlif_gate_assert((_iface)->gate); \
	nlif_assert((_iface)->idx); \
	nlif_assert(!((_iface)->dirty_flags & IFF_VOLATILE)); \
	nlif_assert(nlif_iface_validate_name((_iface)->name) > 0); \
	nlif_assert(nlif_iface_validate_alias((_iface)->alias) >= 0); \
	nlif_assert(!(_iface)->kind || ((_iface)->kind[0] != '\0')); \
	nlif_assert((_iface)->min_mtu <= (_iface)->max_mtu); \
	nlif_assert((_iface)->mtu >= (_iface)->min_mtu); \
	nlif_assert(!(_iface)->max_mtu || \
	            ((_iface)->mtu <= (_iface)->max_mtu)); \
	nlif_assert(nlif_link_hwaddr_is_ucast(&(_iface)->hwadr))

static inline int
nlif_iface_validate_index(unsigned int index)
{
	return nlif_link_validate_index(index);
}

static inline ssize_t
nlif_iface_validate_name(const char * name)
{
	return nlif_link_validate_name(name);
}

static inline ssize_t
nlif_iface_validate_alias(const char * alias)
{
	return nlif_link_validate_alias(alias);
}

static inline unsigned int
nlif_iface_state(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	return interface->state;
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
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->group;
}

extern int
nlif_iface_coherent_group(struct nlif_iface * interface, unsigned int * group);

extern int
nlif_iface_set_group(struct nlif_iface * interface, unsigned int group);

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
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->flags;
}

extern int
nlif_iface_coherent_flags(struct nlif_iface * interface, unsigned int * flags);

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
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return !!(nlif_iface_flags(interface) & IFF_UP);
}

static inline int
nlif_iface_coherent_admstate(struct nlif_iface * interface, bool * up)
{
	nlif_iface_assert(interface);

	unsigned int fl;
	int          ret;

	ret = nlif_iface_coherent_flags(interface, &fl);
	if (ret)
		return ret;

	*up = !!(fl & IFF_UP);

	return 0;
}

extern int
nlif_iface_set_admstate(struct nlif_iface * interface, bool up);

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
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->opstat;
}

extern int
nlif_iface_coherent_operstate(struct nlif_iface * interface,
                              unsigned char *     state);

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
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return !!(nlif_iface_flags(interface) & IFF_LOWER_UP);
}

static inline int
nlif_iface_coherent_carrier(struct nlif_iface * interface, bool * on)
{
	nlif_iface_assert(interface);

	unsigned int fl;
	int          ret;

	ret = nlif_iface_coherent_flags(interface, &fl);
	if (ret)
		return ret;

	*on = !!(fl & IFF_LOWER_UP);

	return 0;
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
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->lnkmod;
}

extern int
nlif_iface_coherent_linkmode(struct nlif_iface * interface,
                             unsigned char *     mode);

static inline unsigned short
nlif_iface_type(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->type;
}

extern int
nlif_iface_coherent_type(struct nlif_iface * interface, unsigned short * type);

static inline const char *
nlif_iface_name(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->name;
}

extern int
nlif_iface_coherent_name(struct nlif_iface * interface, const char ** name);

extern int
nlif_iface_set_name(struct nlif_iface * interface, const char * name);

static inline const char *
nlif_iface_alias(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->alias;
}

extern int
nlif_iface_coherent_alias(struct nlif_iface * interface, const char ** alias);

/* `alias' may be given as `NULL' to remove existing interface's alias. */
extern int
nlif_iface_set_alias(struct nlif_iface * interface, const char * alias);

static inline const char *
nlif_iface_kind(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->kind;
}

extern int
nlif_iface_coherent_kind(struct nlif_iface * interface, const char ** kind);

static inline unsigned int
nlif_iface_link(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->lnk;
}

extern int
nlif_iface_coherent_link(struct nlif_iface * interface, unsigned int * link);

static inline unsigned int
nlif_iface_master(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->mst;
}

extern int
nlif_iface_coherent_master(struct nlif_iface * interface,
                           unsigned int *      master);

static inline int
nlif_iface_validate_mtu(unsigned int mtu, const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	if ((mtu >= interface->min_mtu) &&
	    (!interface->max_mtu || (mtu <= interface->max_mtu)))
		return 0;
	else
		return -EINVAL;
}

static inline unsigned int
nlif_iface_mtu(const struct nlif_iface * interface)
{
	nlif_iface_assert(interface);
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return interface->mtu;
}

extern int
nlif_iface_coherent_mtu(struct nlif_iface * interface, unsigned int * mtu);

extern int
nlif_iface_set_mtu(struct nlif_iface * interface, unsigned int mtu);

extern int
nlif_iface_validate_hwaddr(const struct ether_addr * address);

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
	nlif_assert(interface->state != NLIF_INVALID_STAT);

	return &interface->hwadr;
}

extern int
nlif_iface_coherent_hwaddr(struct nlif_iface *        interface,
                           const struct ether_addr ** address);

extern int
nlif_iface_set_hwaddr(struct nlif_iface *       interface,
                      const struct ether_addr * address);

#if defined(CONFIG_NLIF_PRINT)

extern int
nlif_iface_print(struct nlif_iface * interface, FILE * stdio);

#endif /* defined(CONFIG_NLIF_PRINT) */

extern int
nlif_iface_reload(struct nlif_iface * interface);

extern void
nlif_iface_refresh_state(struct nlif_iface *                interface,
                         const struct rt_link_getlink_rsp * link);

extern void
nlif_iface_reload_bylink(struct nlif_iface *                interface,
                         const struct rt_link_getlink_rsp * link);

extern int
nlif_iface_load_byidx(struct nlif_iface *      interface,
                      unsigned int             index,
                      const struct nlif_gate * gate);

extern int
nlif_iface_load_byname(struct nlif_iface *      interface,
                       const char *             name,
                       const struct nlif_gate * gate);

extern int
nlif_iface_apply(struct nlif_iface * interface);

extern void
nlif_iface_fini(struct nlif_iface * interface);

extern struct nlif_iface *
nlif_iface_alloc(void);

extern void
nlif_iface_free(struct nlif_iface * interface);

extern int
nlif_iface_create_byidx(unsigned int             index,
                        const struct nlif_gate * gate,
                        struct nlif_iface **     interface);

extern int
nlif_iface_create_byname(const char *             name,
                         const struct nlif_gate * gate,
                         struct nlif_iface **     interface);

extern void
nlif_iface_create_bylink(const struct rt_link_getlink_rsp * link,
                         const struct nlif_gate *           gate,
                         struct nlif_iface **               interface);

extern void
nlif_iface_destroy(struct nlif_iface * interface);

#endif /* _NLIF_IFACE_H */
