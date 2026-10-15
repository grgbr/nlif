#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <ynl/ynl.h>
#include <ynl/rt-link-user.h>
#include <netinet/ether.h>
#include <stroll/dlist.h>
#include <stroll/hlist.h>
#include <stroll/hash.h>

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

static struct nlif_iface *
nlif_iface_create(struct rt_link_getlink_rsp * link_response)
{
	nlif_assert(link_response);

	struct nlif_iface * iface = nlif_iface_alloc();

	stroll_hlist_init_node(&iface->idxh);
	stroll_hlist_init_node(&iface->namh);
	stroll_dlist_init(&iface->list);
	nlif_iface_fill(iface, link_response);

	return iface;
}

static void
_nlif_iface_destroy(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	nlif_iface_release(interface);
	nlif_iface_free(interface);
}

static void
nlif_iface_destroy(struct nlif_iface * interface)
{
	nlif_iface_assert(interface);

	stroll_hlist_del(&interface->idxh);
	stroll_hlist_del(&interface->namh);
	stroll_dlist_remove(&interface->list);
	_nlif_iface_destroy(interface);
}

static int
nlif_reload(struct ynl_sock * sock, struct nlif_iface * interface)
{
	nlif_assert(sock);
	nlif_iface_assert(interface);

	struct rt_link_getlink_rsp * rsp;
	int                          err;

	err = _nlif_getlink_byidx(sock, nlif_iface_index(interface), &rsp);
	if (err)
		return err;

	nlif_iface_release(interface);
	nlif_iface_fill(interface, rsp);

	rt_link_getlink_rsp_free(rsp);

	return 0;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

static int
nlif_store_load(struct nlif_store * store, struct ynl_sock * sock)
{
	nlif_store_assert(store);
	nlif_store_assert(stroll_dlist_empty(&store->ifaces));
	nlif_store_assert(!store->count);
	nlif_assert(sock);

	struct rt_link_getlink_req_dump * req;
	struct rt_link_getlink_list *     lst;
	struct rt_link_getlink_rsp *      rsp;
	int                               ret;

	req = rt_link_getlink_req_dump_alloc();
	if (!req)
		abort();

	/* Do not load statistics. */
	rt_link_getlink_req_dump_set_ext_mask(req, RTEXT_FILTER_SKIP_STATS);

	/* Exclude interfaces that are enslaved to a master one. */
	rt_link_getlink_req_dump_set_master(req, 0);

	lst = rt_link_getlink_dump(sock, req);
	if (!lst) {
		nlif_log("cannot retrieve network interfaces: %s.",
		         sock->err.msg);

		nlif_assert(sock->err.code);
		ret = sock->err.code;
		goto free_req;
	}

	if (ynl_dump_empty(lst)) {
		nlif_log("no available network interface.");
		ret = ENODEV;
		goto free_lst;
	}

	ynl_dump_foreach(lst, rsp) {
		struct nlif_iface * iface = nlif_iface_create(rsp);
		unsigned int        buck;

		nlif_assert(iface);

		buck = nlif_iface_index_hash(iface);
		stroll_hlist_add(&store->indxh[buck], &iface->idxh);

#warning make sure keys are unique (name/alias/altname)!!
		buck = nlif_iface_name_hash(iface);
		stroll_hlist_add(&store->nameh[buck], &iface->namh);

		stroll_dlist_nqueue_back(&store->ifaces,
		                         &nlif_iface_create(rsp)->list);

		store->count++;
	}

	ret = 0;

free_lst:
	rt_link_getlink_list_free(lst);

free_req:
	rt_link_getlink_req_dump_free(req);

	return ret;
}

static void
nlif_store_clear(struct nlif_store * store)
{
	nlif_store_assert(store);

	struct nlif_iface * iface;
	struct nlif_iface * tmp;

	stroll_dlist_foreach_entry_safe(&store->ifaces, iface, list, tmp)
		_nlif_iface_destroy(iface);

	stroll_dlist_init(&store->ifaces);
	store->count = 0;
}

int
main(int argc, char * const argv[])
{
	struct ynl_sock *         sk;
	struct ynl_error          err;
	struct nlif_store         store = NLIF_STORE_INIT(store);
	const struct nlif_iface * iface;
	int                       ret;

	sk = ynl_sock_create(&ynl_rt_link_family, &err);
	if (!sk) {
		nlif_log("cannot open socket: %s.\n", err.msg);
		return EXIT_FAILURE;
	}

	ret = nlif_store_load(&store, sk);
	if (ret)
		goto destroy;

	nlif_store_foreach_iface(&store, iface)
		nlif_iface_print(iface, stdout);

	nlif_store_fini(&store);

	//ret = nlif_load_byname(sk, "eno1", &iface);
	//ret = nlif_load_byidx(sk, 2, &iface);
	//if (!ret) {
	//	nlif_iface_print(iface, stdout);
	//	nlif_iface_destroy(iface);
	//}

destroy:
	ynl_sock_destroy(sk);

	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
