#include "store.h"
#include "iface.h"
#include "gate.h"
#include <stroll/hash.h>
#include <stroll/bmap.h>

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
		nlif_assert(nlif_iface_validate_name(hndl->iface->name) > 0);

		if (!strcmp(name, hndl->iface->name))
			return hndl->iface;
	}

	return NULL;
}

struct nlif_iface *
nlif_store_find_iface_byname(const struct nlif_store * store, const char * name)
{
	nlif_store_assert(store);
	nlif_assert(name);

	if (nlif_iface_validate_name(name) > 0) {
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
		nlif_assert(nlif_iface_validate_alias(hndl->iface->alias) > 0);

		if (!strcmp(alias, hndl->iface->alias))
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

	if (nlif_iface_validate_alias(alias) > 0) {
		unsigned int hash;

		hash = stroll_hash_str_djb2((const uint8_t *)alias,
		                            NLIF_STORE_NAMEH_BITS);

		return nlif_store_search_iface_byalias(store, hash, alias);
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

	unsigned int        hash;
	struct nlif_iface * iface;

	hash = stroll_hash_str_djb2((const uint8_t *)strid,
	                            NLIF_STORE_NAMEH_BITS);

	if (nlif_iface_validate_name(strid) > 0) {
		iface = nlif_store_search_iface_byname(store, hash, strid);
		if (iface)
			return iface;
	}

	if (nlif_iface_validate_alias(strid) > 0) {
		iface = nlif_store_search_iface_byalias(store, hash, strid);
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

		if (index == hndl->iface->idx)
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
	const char * als = interface->alias;

	return !strcmp(strid, interface->name) || (als && !strcmp(strid, als));
}

static bool
nlif_store_may_register_iface_name(struct nlif_store *    store,
                                   const char *           name,
                                   struct stroll_hlist ** bucket)
{
	nlif_store_assert(store);
	nlif_assert(nlif_iface_validate_name(name) > 0);
	nlif_assert(bucket);

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
	nlif_store_assert(store);
	nlif_assert(nlif_iface_validate_alias(alias) > 0);
	nlif_assert(bucket);

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
nlif_store_may_register_iface_index(struct nlif_store *    store,
                                    unsigned int           index,
                                    struct stroll_hlist ** bucket)
{
	nlif_store_assert(store);
	nlif_assert(!nlif_iface_validate_index(index));
	nlif_assert(bucket);

	unsigned int             hash;
	struct nlif_store_hndl * hndl;

	hash = stroll_hash(index, NLIF_STORE_INDXH_BITS);

	stroll_hlist_foreach_entry(&store->indxh[hash], hndl, indxh) {
		nlif_store_assert_hndl(hndl);

		if (index == hndl->iface->idx)
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
                                     struct stroll_hlist * alias_buck)
{
	struct nlif_store_hndl * hndl;

	hndl = nlif_store_alloc_handle();
	hndl->iface = interface;

	stroll_hlist_add(name_buck, &hndl->nameh);

	stroll_hlist_add(indx_buck, &hndl->indxh);

	if (alias_buck)
		stroll_hlist_add(alias_buck, &hndl->aliash);
	else
		stroll_hlist_init_node(&hndl->aliash);

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

	if (!nlif_store_may_register_iface_name(store,
	                                        interface->name,
	                                        &nameb))
		return -EEXIST;

	if (!nlif_store_may_register_iface_index(store,
	                                         interface->idx,
	                                         &indxb))
		return -EEXIST;

	strid = interface->alias;
	if (strid) {
		if (!nlif_store_may_register_iface_alias(store, strid, &aliasb))
			return -EEXIST;

		nlif_assert(aliasb);
	}

	nlif_store_createn_enroll_iface_hndl(store,
	                                     interface,
	                                     indxb,
	                                     nameb,
	                                     aliasb);

	return 0;
}

int
nlif_store_withdraw_iface(struct nlif_store *       store,
                          const struct nlif_iface * interface)
{
	nlif_store_assert(store);
	nlif_assert(store->count);
	nlif_iface_assert(interface);

	unsigned int             idx;
	struct nlif_store_hndl * hndl;

	idx = interface->idx;
	hndl = nlif_store_search_iface_hndl_byindex(store, idx);
	if (hndl) {
		nlif_assert(hndl->iface == interface);

		stroll_hlist_del(&hndl->nameh);
		stroll_hlist_del(&hndl->indxh);
		stroll_hlist_del(&hndl->aliash);
		stroll_dlist_remove(&hndl->list);

		nlif_store_free_hndl(hndl);

		store->count--;

		return 0;
	}

	return -ENODEV;
}

int
nlif_store_load(struct nlif_store * store)
{
	nlif_store_assert(store);
	nlif_assert(!store->count);

	int ret;

	ret = nlif_gate_load_links(store->gate);
	if (ret)
		return ret;

	nlif_debug("store loaded with %u interfaces.", store->count);

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
	stroll_dlist_init(&store->ifaces);
	store->count = 0;
}

void
nlif_store_clear(struct nlif_store * store)
{
	nlif_store_assert(store);

	nlif_store_release(store);
	nlif_store_reinit(store);

	nlif_debug("store cleared.");
}

#if defined(CONFIG_NLIF_NOTIF_CHANGES)

static
void
nlif_store_on_iface_change(struct nlif_store *                store,
                           const struct rt_link_getlink_rsp * link)
{
	nlif_store_assert(store);
	nlif_assert(link);

	struct nlif_iface * iface;

	iface = nlif_store_find_iface_byindex(store, link->_hdr.ifi_index);
	if (!iface)
		return;

	/*
	 * We might use nlif_iface_reload_bylink() to reload the
	 * interface entirely but an update of operational state
	 * informations is enought since we don't want to coexist with
	 * daemons that configure interfaces in parallel...
	 *
	 * In addition, we would need to make sure store's internal indices are
	 * consistent with the link's ifalias and ifname properties, i.e.,
	 * thanks to calls to nlif_store_rehash_name() /
	 * nlif_store_rehash_alias().
	 */
	if (nlif_iface_refresh_state(iface, link))
		nlif_obsrv_notify(&store->notif,
		                  NLIF_STORE_IFACE_CHANGE_EVT,
		                  iface);
}

#else  /* !defined(CONFIG_NLIF_NOTIF_CHANGES) */

static inline
void
nlif_store_on_iface_change(struct nlif_store *                store __unused,
                           const struct rt_link_getlink_rsp * link __unused)
{
}

#endif /* defined(CONFIG_NLIF_NOTIF_CHANGES) */

static
int
nlif_store_rehash_name(struct nlif_store *      store,
                       struct nlif_store_hndl * handle,
                       const char *             name)
{

	nlif_store_assert(store);
	nlif_store_assert_hndl(handle);
	nlif_assert(nlif_iface_validate_name(name) > 0);

	struct stroll_hlist * buck;

	stroll_hlist_del_init(&handle->nameh);

	if (!nlif_store_may_register_iface_name(store, name, &buck))
		return -EEXIST;

	stroll_hlist_add(buck, &handle->nameh);

	return 0;
}

static
int
nlif_store_rehash_alias(struct nlif_store *      store,
                        struct nlif_store_hndl * handle,
                        const char *             alias)
{
	nlif_store_assert(store);
	nlif_store_assert_hndl(handle);

	struct stroll_hlist * buck;

	stroll_hlist_del_init(&handle->aliash);

	if (alias) {
		if (!nlif_store_may_register_iface_alias(store, alias, &buck))
			return -EEXIST;

		stroll_hlist_add(buck, &handle->aliash);
	}

	return 0;
}

static
int
nlif_store_on_iface_modify(struct nlif_store *       store,
                           const struct nlif_iface * interface)
{
	nlif_store_assert(store);
	nlif_iface_assert(interface);

	struct nlif_store_hndl * hndl;
	int                      ret;

	hndl = nlif_store_search_iface_hndl_byindex(store, interface->idx);
	nlif_assert(hndl);
	nlif_assert(hndl->iface == interface);

	if (stroll_bmap_test(interface->dirty_fields, NLIF_NAME_IFACE_FLD)) {
		ret = nlif_store_rehash_name(store, hndl, interface->name);
		if (ret)
			return ret;
	}

	if (stroll_bmap_test(interface->dirty_fields, NLIF_ALIAS_IFACE_FLD)) {
		ret = nlif_store_rehash_alias(store, hndl, interface->alias);
		if (ret)
			return ret;
	}

	return 0;
}

static
int
nlif_store_on_iface_reload(struct nlif_store *       store,
                           const struct nlif_iface * interface)
{
	nlif_store_assert(store);
	nlif_iface_assert(interface);

	struct nlif_store_hndl * hndl;
	int                      ret;

	hndl = nlif_store_search_iface_hndl_byindex(store, interface->idx);
	nlif_assert(hndl);
	nlif_assert(hndl->iface == interface);

	ret = nlif_store_rehash_name(store, hndl, interface->name);
	if (ret)
		return ret;

	ret = nlif_store_rehash_alias(store, hndl, interface->alias);
	if (ret)
		return ret;

	return 0;
}

static
void
nlif_store_on_new_iface(struct nlif_store *          store,
                        struct rt_link_getlink_rsp * link,
                        const struct nlif_gate *     gate)
{
	nlif_store_assert(store);
	nlif_assert(link);
	nlif_gate_assert(gate);

	struct stroll_hlist * indxb;
	struct stroll_hlist * nameb;
	struct stroll_hlist * aliasb = NULL;
	struct nlif_iface *   iface;

	if (!nlif_store_may_register_iface_index(store,
	                                         link->_hdr.ifi_index,
	                                         &indxb)) {
		nlif_warn("%s[%d]: cannot load link: already exists.",
		          link->ifname,
		          link->_hdr.ifi_index);
		return;
	}

	if (!nlif_store_may_register_iface_name(store, link->ifname, &nameb)) {
		nlif_warn("%s[%d]: cannot load link: duplicate name.",
		          link->ifname,
		          link->_hdr.ifi_index);
		return;
	}

	if (link->_len.ifalias) {
		if (!nlif_store_may_register_iface_alias(store,
		                                         link->ifalias,
		                                         &aliasb)) {
			nlif_warn("%s[%d]: cannot load link: "
			          "'%s': duplicate alias.",
			          link->ifname,
			          link->_hdr.ifi_index,
			          link->ifalias);
			return;
		}

		nlif_assert(aliasb);
	}

	nlif_iface_create_bylink(link, gate, &iface);
	nlif_store_createn_enroll_iface_hndl(store,
	                                     iface,
	                                     indxb,
	                                     nameb,
	                                     aliasb);
}

static
int
nlif_store_on_event(const struct nlif_gate * gate,
                    void *                   store,
                    enum nlif_link_event     event,
                    void *                   data)
{
	struct nlif_store * str = store;

	switch (event) {
	case NLIF_LINK_CHANGE_EVT:
		nlif_store_on_iface_change(str, data);
		return 0;

	case NLIF_LINK_MODIFY_EVT:
		return nlif_store_on_iface_modify(str, data);

	case NLIF_LINK_NEW_EVT:
		nlif_store_on_new_iface(str, data, gate);
		return 0;

	case NLIF_LINK_RELOAD_EVT:
		return nlif_store_on_iface_reload(str, data);

	default:
		nlif_assert(0);
		return -ENOTSUP;
	}
}

#if defined(CONFIG_NLIF_NOTIF_CHANGES)

void
nlif_store_subscribe_changes(struct nlif_store *            store,
                             struct nlif_obsrv_subscriber * subscriber)
{
	nlif_store_assert(store);
	nlif_assert(subscriber);

	nlif_obsrv_subscribe(&store->notif, subscriber);
}

void
nlif_store_unsubscribe_changes(struct nlif_store *            store,
                               struct nlif_obsrv_subscriber * subscriber)
{
	nlif_store_assert(store);
	nlif_assert(subscriber);

	nlif_obsrv_unsubscribe(&store->notif, subscriber);
}

#endif /* defined(CONFIG_NLIF_NOTIF_CHANGES) */

void
nlif_store_init(struct nlif_store * store, struct nlif_gate * gate)
{
	nlif_assert(store);
	nlif_gate_assert(gate);

	nlif_store_reinit(store);
	nlif_gate_subscribe(gate, nlif_store_on_event, store);
#if defined(CONFIG_NLIF_NOTIF_CHANGES)
	nlif_obsrv_setup_notifier(&store->notif);
#endif /* defined(CONFIG_NLIF_NOTIF_CHANGES) */

	store->gate = gate;

	nlif_debug("store opened.");
}

void
nlif_store_fini(struct nlif_store * store)
{
	nlif_store_assert(store);

	nlif_gate_unsubscribe(store->gate);
#if defined(CONFIG_NLIF_NOTIF_CHANGES)
	nlif_assert(nlif_obsrv_notifier_empty(&store->notif));
#endif /* defined(CONFIG_NLIF_NOTIF_CHANGES) */

	nlif_store_release(store);

	nlif_debug("store closed.");
}
