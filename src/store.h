#ifndef _NLIF_STORE_H
#define _NLIF_STORE_H

#include "common.h"
#include "gate.h"
#include <stroll/hlist.h>

struct nlif_gate;

/* Up to 128 buckets per indices hash table. */
#define NLIF_STORE_INDXH_BITS (7U)
#define NLIF_STORE_NAMEH_BITS (7U)

struct nlif_store {
	struct stroll_hlist          nameh[1U << NLIF_STORE_NAMEH_BITS];
	struct stroll_hlist          indxh[1U << NLIF_STORE_INDXH_BITS];
	struct stroll_hlist          aliash[1U << NLIF_STORE_NAMEH_BITS];
	struct stroll_hlist          altnameh[1U << NLIF_STORE_NAMEH_BITS];
	struct stroll_dlist_node     ifaces;
	unsigned int                 count;
#if defined(CONFIG_NLIF_OBSRV)
	struct nlif_obsrv_subscriber sub;
#endif /* defined(CONFIG_NLIF_OBSRV) */
};

#define _NLIF_STORE_INIT(_store) \
		.nameh    = STROLL_HLIST_INIT_BUCKETS((_store).nameh), \
		.indxh    = STROLL_HLIST_INIT_BUCKETS((_store).indxh), \
		.aliash   = STROLL_HLIST_INIT_BUCKETS((_store).aliash), \
		.altnameh = STROLL_HLIST_INIT_BUCKETS((_store).altnameh), \
		.ifaces   = STROLL_DLIST_INIT((_store).ifaces), \
		.count    = 0


#if defined(CONFIG_NLIF_OBSRV)

#define NLIF_STORE_INIT(_store) \
	{ \
		_NLIF_STORE_INIT(_store), \
		.sub = NLIF_OBSRV_SETUP_SUBSCRIBER((_store).sub, \
		                                   nlif_store_on_event) \
	}

#else  /* !defined(CONFIG_NLIF_OBSRV) */

#define NLIF_STORE_INIT(_store) \
	{ \
		_NLIF_STORE_INIT(_store) \
	}

#endif /* defined(CONFIG_NLIF_OBSRV) */

#define nlif_store_assert(_store) \
	nlif_assert(_store); \
	nlif_assert(!(_store)->count || \
	            !stroll_hlist_buckets_empty((_store)->nameh, \
	                                        NLIF_STORE_NAMEH_BITS)); \
	nlif_assert(!(_store)->count || \
	            !stroll_hlist_buckets_empty((_store)->indxh, \
	                                        NLIF_STORE_INDXH_BITS)); \
	nlif_assert(!(_store)->count || \
	            !stroll_dlist_empty(&(_store)->ifaces))

struct nlif_store_hndl {
	struct stroll_hlist_node    nameh;
	struct stroll_hlist_node    indxh;
	struct stroll_hlist_node    aliash;
	struct stroll_hlist_node    altnameh;
	struct stroll_dlist_node    list;
	union {
		struct nlif_iface * iface;
	};
};

#define nlif_store_foreach_iface(_store, _hndl, _iface) \
	for (_hndl = stroll_dlist_entry((_store)->ifaces.next, \
	                                typeof(*(_hndl)), \
	                                list); \
	     (&(_hndl)->list != &(_store)->ifaces) && \
	     (_iface = (_hndl)->iface, true); \
	     _hndl = stroll_dlist_next_entry(_hndl, list))

static inline unsigned int
nlif_store_count(const struct nlif_store * store)
{
	nlif_store_assert(store);

	return store->count;
}

extern struct nlif_iface *
nlif_store_find_iface_byname(const struct nlif_store * store,
                             const char *              name);

extern struct nlif_iface *
nlif_store_find_iface_byalias(const struct nlif_store * store,
                              const char *              alias);

extern struct nlif_iface *
nlif_store_find_iface_byaltname(const struct nlif_store * store,
                                const char *              altname);

extern struct nlif_iface *
nlif_store_find_iface_bystrid(const struct nlif_store * store,
                              const char *              strid);

extern struct nlif_iface *
nlif_store_find_iface_byindex(const struct nlif_store * store,
                              unsigned int              index);

extern int
nlif_store_enroll_iface(struct nlif_store * store,
                        struct nlif_iface * interface);

extern int
nlif_store_withdraw_iface(struct nlif_store *       store,
                          const struct nlif_iface * interface);

extern void
nlif_store_clear(struct nlif_store * store);

extern int
nlif_store_load(struct nlif_store * store, const struct nlif_gate * gate);

static inline
int
nlif_store_reload(struct nlif_store * store, const struct nlif_gate * gate)
{
	nlif_store_clear(store);

	return nlif_store_load(store, gate);
}

#if defined(CONFIG_NLIF_OBSRV)

extern void
nlif_store_on_event(struct nlif_obsrv_subscriber * subscriber,
                    void *                         event,
                    struct nlif_obsrv_notifier *   notifier);

static inline void
nlif_store_enable_notif(struct nlif_store * store, struct nlif_gate * gate)
{
	nlif_store_assert(store);

	nlif_gate_subscribe(gate, &store->sub);
}

static inline void
nlif_store_disable_notif(struct nlif_store * store, struct nlif_gate * gate)
{
	nlif_store_assert(store);

	nlif_gate_unsubscribe(gate, &store->sub);
}

#endif /* defined(CONFIG_NLIF_OBSRV) */

extern void
nlif_store_init(struct nlif_store * store);

extern void
nlif_store_fini(struct nlif_store * store);

#endif /* _NLIF_STORE_H */
