#include "store.h"
#include "iface.h"
#include "gate.h"
#include <stroll/hash.h>

#define nlif_store_assert_hndl(_hndl) \
	nlif_assert(_hndl); \
	nlif_iface_assert((_hndl)->iface)

static struct nlif_store_hndl *
nlif_store_alloc_handle(void)
{
	struct nlif_store_hndl * hndl;

	hndl = nlif_malloc(sizeof(*hndl));
	nlif_assert(hndl);

	return hndl;
}

static void
nlif_store_free_hndl(struct nlif_store_hndl * handle)
{
	nlif_free(handle);
}

static struct nlif_iface *
nlif_store_search_iface_byname(const struct nlif_store * store,
                               unsigned int              hash,
                               const char *              name)
{
	struct nlif_store_hndl * hndl;

	stroll_hlist_foreach_entry(&store->nameh[hash], hndl, nameh) {
		nlif_store_assert_hndl(hndl);

		if (!strcmp(name, nlif_iface_name(hndl->iface)))
			return hndl->iface;
	}

	return NULL;
}

struct nlif_iface *
nlif_store_find_iface_byname(const struct nlif_store * store, const char * name)
{
	nlif_store_assert(store);
	nlif_assert(name);

	if (nlif_iface_validate_strid(name) > 0) {
		unsigned int hash;

		hash = stroll_hash_str_djb2((const uint8_t *)name,
		                            NLIF_STORE_NAMEH_BITS);

		return nlif_store_search_iface_byname(store, hash, name);
	}
	else
		return NULL;
}

static struct nlif_iface *
nlif_store_search_iface_byalias(const struct nlif_store * store,
                                unsigned int              hash,
                                const char *              alias)
{
	struct nlif_store_hndl * hndl;

	stroll_hlist_foreach_entry(&store->aliash[hash], hndl, aliash) {
		nlif_store_assert_hndl(hndl);

		const char * als = nlif_iface_alias(hndl->iface);

		if (!strcmp(alias, als))
			return hndl->iface;
	}

	return NULL;
}

struct nlif_iface *
nlif_store_find_iface_byalias(const struct nlif_store * store,
                              const char *              alias)
{
	nlif_store_assert(store);
	nlif_assert(alias);

	if (nlif_iface_validate_strid(alias) > 0) {
		unsigned int hash;

		hash = stroll_hash_str_djb2((const uint8_t *)alias,
		                            NLIF_STORE_NAMEH_BITS);

		return nlif_store_search_iface_byalias(store, hash, alias);
	}
	else
		return NULL;
}

static struct nlif_iface *
nlif_store_search_iface_byaltname(const struct nlif_store * store,
                                  unsigned int              hash,
                                  const char *              altname)
{
	struct nlif_store_hndl * hndl;

	stroll_hlist_foreach_entry(&store->altnameh[hash], hndl, altnameh) {
		nlif_store_assert_hndl(hndl);

		const char * alt = nlif_iface_altname(hndl->iface);

		if (!strcmp(altname, alt))
			return hndl->iface;
	}

	return NULL;
}

struct nlif_iface *
nlif_store_find_iface_byaltname(const struct nlif_store * store,
                                const char *              altname)
{
	nlif_store_assert(store);
	nlif_assert(altname);

	if (nlif_iface_validate_strid(altname) > 0) {
		unsigned int hash;

		hash = stroll_hash_str_djb2((const uint8_t *)altname,
		                            NLIF_STORE_NAMEH_BITS);

		return nlif_store_search_iface_byaltname(store, hash, altname);
	}
	else
		return NULL;
}

struct nlif_iface *
nlif_store_find_iface_bystrid(const struct nlif_store * store,
                              const char *              strid)
{
	nlif_store_assert(store);
	nlif_assert(strid);

	if (nlif_iface_validate_strid(strid) > 0) {
		unsigned int        hash;
		struct nlif_iface * iface;

		hash = stroll_hash_str_djb2((const uint8_t *)strid,
		                            NLIF_STORE_NAMEH_BITS);

		iface = nlif_store_search_iface_byname(store, hash, strid);
		if (iface)
			return iface;

		iface = nlif_store_search_iface_byalias(store, hash, strid);
		if (iface)
			return iface;

		iface = nlif_store_search_iface_byaltname(store, hash, strid);
		if (iface)
			return iface;
	}

	return NULL;
}

static struct nlif_store_hndl *
nlif_store_search_iface_hndl_byindex(const struct nlif_store * store,
                                     unsigned int              index)
{
	unsigned int             hash;
	struct nlif_store_hndl * hndl;

	hash = stroll_hash(index, NLIF_STORE_INDXH_BITS);

	stroll_hlist_foreach_entry(&store->indxh[hash], hndl, indxh) {
		nlif_store_assert_hndl(hndl);

		if (index == nlif_iface_index(hndl->iface))
			return hndl;
	}

	return NULL;
}

struct nlif_iface *
nlif_store_find_iface_byindex(const struct nlif_store * store,
                              unsigned int              index)
{
	nlif_store_assert(store);

	if (!nlif_iface_validate_index(index)) {
		const struct nlif_store_hndl * hndl;

		hndl = nlif_store_search_iface_hndl_byindex(store, index);
		if (hndl)
			return hndl->iface;
	}

	return NULL;
}

static bool
nlif_store_match_iface_bystrid(const struct nlif_iface * interface,
                               const char *              strid)
{
	const char * als = nlif_iface_alias(interface);
	const char * alt = nlif_iface_altname(interface);

	return (!strcmp(strid, nlif_iface_name(interface))) ||
	        (als && !strcmp(strid, als)) ||
	        (alt && !strcmp(strid, alt));
}

static bool
nlif_store_may_register_iface_name(struct nlif_store *    store,
                                   const char *           name,
                                   struct stroll_hlist ** bucket)
{
	unsigned int             hash;
	struct nlif_store_hndl * hndl;

	hash = stroll_hash_str_djb2((const uint8_t *)name,
	                            NLIF_STORE_NAMEH_BITS);

	stroll_hlist_foreach_entry(&store->nameh[hash], hndl, nameh) {
		nlif_store_assert_hndl(hndl);

		if (nlif_store_match_iface_bystrid(hndl->iface, name))
			return false;
	}

	*bucket = &store->nameh[hash];

	return true;
}

static bool
nlif_store_may_register_iface_alias(struct nlif_store *    store,
                                    const char *           alias,
                                    struct stroll_hlist ** bucket)
{
	unsigned int             hash;
	struct nlif_store_hndl * hndl;

	hash = stroll_hash_str_djb2((const uint8_t *)alias,
	                            NLIF_STORE_NAMEH_BITS);

	stroll_hlist_foreach_entry(&store->aliash[hash], hndl, aliash) {
		nlif_store_assert_hndl(hndl);

		if (nlif_store_match_iface_bystrid(hndl->iface, alias))
			return false;
	}

	*bucket = &store->aliash[hash];

	return true;
}

static bool
nlif_store_may_register_iface_altname(struct nlif_store *    store,
                                      const char *           altname,
                                      struct stroll_hlist ** bucket)
{
	unsigned int             hash;
	struct nlif_store_hndl * hndl;

	hash = stroll_hash_str_djb2((const uint8_t *)altname,
	                            NLIF_STORE_NAMEH_BITS);

	stroll_hlist_foreach_entry(&store->altnameh[hash], hndl, altnameh) {
		nlif_store_assert_hndl(hndl);

		if (nlif_store_match_iface_bystrid(hndl->iface, altname))
			return false;
	}

	*bucket = &store->altnameh[hash];

	return true;
}

static bool
nlif_store_may_register_iface_index(struct nlif_store *    store,
                                    unsigned int           index,
                                    struct stroll_hlist ** bucket)
{
	unsigned int             hash;
	struct nlif_store_hndl * hndl;

	hash = stroll_hash(index, NLIF_STORE_INDXH_BITS);

	stroll_hlist_foreach_entry(&store->indxh[hash], hndl, indxh) {
		nlif_store_assert_hndl(hndl);

		if (index == nlif_iface_index(hndl->iface))
			return false;
	}

	*bucket = &store->indxh[hash];

	return true;
}

static void
nlif_store_createn_enroll_iface_hndl(struct nlif_store *   store,
                                     struct nlif_iface *   interface,
                                     struct stroll_hlist * indx_buck,
                                     struct stroll_hlist * name_buck,
                                     struct stroll_hlist * alias_buck,
                                     struct stroll_hlist * alt_buck)
{
	struct nlif_store_hndl * hndl;

	hndl = nlif_store_alloc_handle();
	hndl->iface = interface;

	stroll_hlist_add(name_buck, &hndl->nameh);

	stroll_hlist_add(indx_buck, &hndl->indxh);

	if (alias_buck)
		stroll_hlist_add(alias_buck, &hndl->aliash);

	if (alt_buck)
		stroll_hlist_add(alt_buck, &hndl->altnameh);

	stroll_dlist_nqueue_back(&store->ifaces, &hndl->list);

	store->count++;
}

int
nlif_store_enroll_iface(struct nlif_store * store,
                        struct nlif_iface * interface)
{
	nlif_store_assert(store);
	nlif_iface_assert(interface);

	struct stroll_hlist * nameb;
	struct stroll_hlist * indxb;
	const char *          strid;
	struct stroll_hlist * aliasb = NULL;
	struct stroll_hlist * altb = NULL;

	if (!nlif_store_may_register_iface_name(store,
	                                        nlif_iface_name(interface),
	                                        &nameb))
		return -EEXIST;

	if (!nlif_store_may_register_iface_index(store,
	                                         nlif_iface_index(interface),
	                                         &indxb))
		return -EEXIST;

	strid = nlif_iface_alias(interface);
	if (strid) {
		if (!nlif_store_may_register_iface_alias(store, strid, &aliasb))
			return -EEXIST;

		nlif_assert(aliasb);
	}

	strid = nlif_iface_altname(interface);
	if (strid) {
		if (!nlif_store_may_register_iface_altname(store, strid, &altb))
			return -EEXIST;

		nlif_assert(aliasb);
	}

	nlif_store_createn_enroll_iface_hndl(store,
	                                     interface,
	                                     indxb,
	                                     nameb,
	                                     aliasb,
	                                     altb);

	return 0;
}

int
nlif_store_withdraw_iface(struct nlif_store *       store,
                          const struct nlif_iface * interface)
{
	nlif_store_assert(store);
	nlif_iface_assert(interface);

	if (store->count) {
		unsigned int             index;
		struct nlif_store_hndl * hndl;

		index = nlif_iface_index(interface);
		hndl = nlif_store_search_iface_hndl_byindex(store, index);
		if (hndl) {
			nlif_assert(hndl->iface == interface);

			const char * als = nlif_iface_alias(interface);
			const char * alt = nlif_iface_altname(interface);

			stroll_hlist_del(&hndl->nameh);
			stroll_hlist_del(&hndl->indxh);
			if (als)
				stroll_hlist_del(&hndl->aliash);
			if (alt)
				stroll_hlist_del(&hndl->altnameh);
			stroll_dlist_remove(&hndl->list);

			nlif_store_free_hndl(hndl);

			store->count--;

			return 0;
		}
	}

	return -ENODEV;
}

static int
nlif_store_on_link_loaded(const struct nlif_gate *     gate __unused,
                          struct rt_link_getlink_rsp * link,
                          void *                       data)
{
	struct stroll_hlist * indxb;
	struct stroll_hlist * nameb;
	struct stroll_hlist * aliasb = NULL;
	struct stroll_hlist * altb = NULL;
	struct nlif_iface *   iface;

	struct nlif_store * store = data;

	if (!nlif_store_may_register_iface_index(store,
	                                         link->_hdr.ifi_index,
	                                         &indxb)) {
		nlif_warn("'%s[%u]': cannot load link: already exists.",
		          link->ifname,
		          link->_hdr.ifi_index);

		/* Tell the caller to keep processing link entries. */
		return 0;
	}

	if (!nlif_store_may_register_iface_name(store, link->ifname, &nameb)) {
		nlif_warn("'%s[%u]': cannot load link: duplicate name.",
		          link->ifname,
		          link->_hdr.ifi_index);

		/* Tell the caller to keep processing link entries. */
		return 0;
	}

	if (link->_len.ifalias) {
		if (!nlif_store_may_register_iface_alias(store,
		                                         link->ifalias,
		                                         &aliasb)) {
			nlif_warn("'%s[%u]': cannot load link: "
			          "'%s': duplicate alias.",
			          link->ifname,
			          link->_hdr.ifi_index,
			          link->ifalias);

			/* Tell the caller to keep processing link entries. */
			return 0;
		}

		nlif_assert(aliasb);
	}

	if (link->_present.prop_list && link->prop_list._count.alt_ifname) {
		const struct ynl_string * alt = link->prop_list.alt_ifname[0];

		if (!nlif_store_may_register_iface_altname(store,
		                                           alt->str,
		                                           &altb)) {
			nlif_warn("'%s[%u]': cannot load link: "
			          "'%s': duplicate alternate name.",
			          link->ifname,
			          link->_hdr.ifi_index,
			          alt->str);

			/* Tell the caller to keep processing link entries. */
			return 0;
		}

		nlif_assert(aliasb);
	}

	nlif_iface_create_bylink(link, &iface);
	nlif_store_createn_enroll_iface_hndl(store,
	                                     iface,
	                                     indxb,
	                                     nameb,
	                                     aliasb,
	                                     altb);

	return 0;
}

int
nlif_store_load(struct nlif_store * store, const struct nlif_gate * gate)
{
	nlif_store_assert(store);
	nlif_assert(!store->count);
	nlif_assert(gate);

	int ret;

	ret = nlif_gate_load_links(gate, nlif_store_on_link_loaded, store);
	if (ret)
		return ret;

	nlif_dbg("store loaded with %u interfaces.", store->count);

	return 0;
}

static void
nlif_store_release(struct nlif_store * store)
{
	struct nlif_store_hndl * hndl;
	struct nlif_store_hndl * tmp;

	stroll_dlist_foreach_entry_safe(&store->ifaces, hndl, list, tmp) {
		nlif_iface_destroy(hndl->iface);
		nlif_store_free_hndl(hndl);
	}
}

static void
nlif_store_reinit(struct nlif_store * store)
{
	stroll_hlist_init_buckets(store->nameh, NLIF_STORE_NAMEH_BITS);
	stroll_hlist_init_buckets(store->indxh, NLIF_STORE_INDXH_BITS);
	stroll_hlist_init_buckets(store->aliash, NLIF_STORE_NAMEH_BITS);
	stroll_hlist_init_buckets(store->altnameh, NLIF_STORE_NAMEH_BITS);
	stroll_dlist_init(&store->ifaces);
	store->count = 0;
}

void
nlif_store_clear(struct nlif_store * store)
{
	nlif_store_assert(store);

	nlif_store_release(store);
	nlif_store_reinit(store);

	nlif_dbg("store cleared.");
}

#if defined(CONFIG_NLIF_OBSRV)

static inline struct nlif_store *
nlif_store_from_subscriber(struct nlif_obsrv_subscriber * subscriber)
{
	nlif_assert(subscriber);

	struct nlif_store * store = containerof(subscriber,
	                                        typeof(*store),
	                                        sub);

	nlif_store_assert(store);
	return store;
}

void
nlif_store_on_event(struct nlif_obsrv_subscriber * subscriber,
                    void *                         event,
                    struct nlif_obsrv_notifier *   notifier __unused)
{
	struct nlif_store *                store =
		nlif_store_from_subscriber(subscriber);
	const struct rt_link_getlink_rsp * lnk =
		(const struct rt_link_getlink_rsp *)event;
	struct nlif_iface *                iface;

	iface = nlif_store_find_iface_byindex(store, lnk->_hdr.ifi_index);
	if (!iface)
		return;

	/*
	 * We might use nlif_iface_reload_bylink() to reload the interface
	 * entirely but an update of operational state informations is enought
	 * since we don't want to coexist with daemons that configure interfaces
	 * in parallel...
	 */
	nlif_iface_refresh_state(iface, lnk);

	/* nlif_iface_print(iface, stderr); */
}

int
nlif_store_enable_notif(struct nlif_store * store, struct nlif_gate * gate)
{
	nlif_store_assert(store);
	nlif_assert(!nlif_obsrv_subscribed(&store->sub));
	nlif_assert(gate);

	int err;

	err = nlif_gate_subscribe(gate, &store->sub);
	if (!err) {
		nlif_dbg("store notification enabled.");
		return 0;
	}

	return err;
}

void
nlif_store_disable_notif(struct nlif_store * store, struct nlif_gate * gate)
{
	nlif_store_assert(store);
	nlif_assert(nlif_obsrv_subscribed(&store->sub));
	nlif_assert(gate);

	nlif_gate_unsubscribe(gate, &store->sub);

	nlif_dbg("store notification disabled.");
}

#endif /* defined(CONFIG_NLIF_OBSRV) */

void
nlif_store_init(struct nlif_store * store)
{
	nlif_assert(store);

	nlif_store_reinit(store);
#if defined(CONFIG_NLIF_OBSRV)
	nlif_obsrv_setup_subscriber(&store->sub, nlif_store_on_event);
#endif /* defined(CONFIG_NLIF_OBSRV) */

	nlif_dbg("store opened.");
}

void
nlif_store_fini(struct nlif_store * store)
{
	nlif_store_assert(store);
#if defined(CONFIG_NLIF_OBSRV)
	nlif_assert(!nlif_obsrv_subscribed(&store->sub));
#endif /* defined(CONFIG_NLIF_OBSRV) */

	nlif_store_release(store);

	nlif_dbg("store closed.");
}
