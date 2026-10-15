#include "gate.h"
#include "link.h"
#include <stdarg.h>

static struct rt_link_getlink_req *
nlif_gate_alloc_link_req(void)
{
	struct rt_link_getlink_req * req;

	req = rt_link_getlink_req_alloc();
	if (req)
		return req;

	abort();
}

static bool
nlif_gate_names_equal(size_t       alength,
                      size_t       blength,
                      const char * aname,
                      const char * bname)
{
	return (alength == blength) && !strcmp(aname, bname);
}

int
nlif_gate_islink_valid(const struct rt_link_getlink_rsp * link)
{
	nlif_link_assert(link);

	if (nlif_gate_names_equal(link->_len.ifname,
	                          link->_len.ifalias,
	                          link->ifname,
	                          link->ifalias))
		return -EINVAL;

	if (link->_present.prop_list && link->prop_list._count.alt_ifname) {
		const struct ynl_string * alt = link->prop_list.alt_ifname[0];

		nlif_assert(alt->len &&
		            (alt->len < IFNAMSIZ) &&
		            (strlen(alt->str) == alt->len));

		if (nlif_gate_names_equal(link->_len.ifname,
		                          alt->len,
		                          link->ifname,
		                          alt->str))
			return -EINVAL;

		if (nlif_gate_names_equal(link->_len.ifalias,
		                          alt->len,
		                          link->ifalias,
		                          alt->str))
			return -EINVAL;
	}

	return 0;
}

#if 0
static void
nlif_gate_make_error(struct ynl_error *  error,
                     enum ynl_error_code code,
                     const char *        format,
                     ...)
{
	va_list args;

	error->code = code;
	error->attr_offs = 0;

	va_start(args, format);
	vsnprintf(error->msg, sizeof(error->msg), format, args);
	va_end(args);
}
#endif

static int
nlif_gate_getlink(const struct nlif_gate *      gate,
                  struct rt_link_getlink_req *  request,
                  struct rt_link_getlink_rsp ** link)
{
	struct rt_link_getlink_rsp * lnk;
	int                          err;

	/* Do not load statistics. */
	rt_link_getlink_req_set_ext_mask(request, RTEXT_FILTER_SKIP_STATS);

	/* Acquire interface data. */
	lnk = rt_link_getlink(gate->sock, request);
	if (!lnk) {
		nlif_info("cannot fetch link: %s.", gate->sock->err.msg);
		return nlif_ynl_err(&gate->sock->err);
	}

	err = nlif_gate_islink_valid(lnk);
	if (!err) {
		*link = lnk;
		return 0;
	}

	nlif_warn("'%s[%u]': cannot load link: "
	          "inconsistent attributes.",
	          lnk->ifname,
	          lnk->_hdr.ifi_index);

	rt_link_getlink_rsp_free(lnk);

	return err;
}

int
nlif_gate_load_link_byidx(const struct nlif_gate *      gate,
                          unsigned int                  index,
                          struct rt_link_getlink_rsp ** link)
{
	nlif_gate_assert(gate);
	nlif_assert(!nlif_link_validate_index(index));
	nlif_assert(link);

	struct rt_link_getlink_req * req;
	int                          ret;

	/* Allocate link request. */
	req = nlif_gate_alloc_link_req();

	/* Search link by its ifindex. */
	req->_hdr.ifi_index = index;

	/* Get link. */
	ret = nlif_gate_getlink(gate, req, link);
	if (ret)
		goto free;

	ret = 0;

free:
	rt_link_getlink_req_free(req);

	return ret;
}

int
nlif_gate_load_link_byname(const struct nlif_gate *      gate,
                           const char *                  name,
                           struct rt_link_getlink_rsp ** link)
{
	nlif_gate_assert(gate);
	nlif_assert(nlif_link_validate_strid(name) > 0);
	nlif_assert(link);

	struct rt_link_getlink_req * req;
	int                          ret;

	/* Allocate link request. */
	req = nlif_gate_alloc_link_req();

	/* Setup name of link to search for. */
	rt_link_getlink_req_set_ifname(req, name);

	/* Get link. */
	ret = nlif_gate_getlink(gate, req, link);
	if (ret)
		goto free;

	ret = 0;

free:
	rt_link_getlink_req_free(req);

	return ret;
}

int
nlif_gate_load_links(const struct nlif_gate *      gate,
                     nlif_gate_on_link_loaded_fn * on_loaded,
                     void *                        data)
{
	nlif_gate_assert(gate);
	nlif_assert(on_loaded);

	struct rt_link_getlink_req_dump * req;
	struct rt_link_getlink_list *     lst;
	int                               ret;

	/* Allocate link dump request. */
	req = rt_link_getlink_req_dump_alloc();
	if (!req)
		abort();

	/* Do not load statistics. */
	rt_link_getlink_req_dump_set_ext_mask(req, RTEXT_FILTER_SKIP_STATS);

	/* Exclude interfaces that are enslaved to a master one. */
	rt_link_getlink_req_dump_set_master(req, 0);

	lst = rt_link_getlink_dump(gate->sock, req);
	if (!lst) {
		nlif_warn("cannot dump links: %s.", gate->sock->err.msg);
		ret = nlif_ynl_err(&gate->sock->err);
		goto free_req;
	}

	if (ynl_dump_empty(lst)) {
		nlif_warn("no available link.");
		ret = -ENODEV;
		goto free_lst;
	}

	ynl_dump_foreach(lst, lnk) {
		ret = nlif_gate_islink_valid(lnk);
		if (ret) {
			nlif_warn("'%s[%u]': cannot load link: "
			          "inconsistent attributes.",
			          lnk->ifname,
			          lnk->_hdr.ifi_index);
			continue;
		}

		ret = on_loaded(gate, lnk, data);
		if (ret)
			goto free_lst;
	}

	ret = 0;

free_lst:
	rt_link_getlink_list_free(lst);

free_req:
	rt_link_getlink_req_dump_free(req);

	return ret;
}

#if defined(CONFIG_NLIF_OBSRV)

int
nlif_gate_subscribe(struct nlif_gate *             gate,
                    struct nlif_obsrv_subscriber * subscriber,
                    const struct upoll *           poller)
{
	nlif_gate_assert(gate);
	nlif_assert(subscriber);
	nlif_assert(poller);

	if (nlif_obsrv_notifier_empty(&gate->notif)) {
		int          fd = ynl_socket_get_fd(gate->sock);
		unsigned int grp = RTNLGRP_LINK;
		int          ret;

		ret = upoll_register(poller, fd, EPOLLIN, &gate->work);
		if (ret) {
			nlif_warn("cannot register notification worker: %s",
			          strerror(-ret));
			return ret;
		}

		if (setsockopt(fd,
		               SOL_NETLINK,
		               NETLINK_ADD_MEMBERSHIP,
		               &grp,
		               sizeof(grp))) {
			ret = errno;
			upoll_unregister(poller, fd);
			nlif_warn("cannot join netlink multicast group: %s.",
			          strerror(ret));
			return -ret;
		}

		nlif_dbg("gate notification enabled.");
	}

	nlif_obsrv_subscribe(&gate->notif, subscriber);

	return 0;
}

void
nlif_gate_unsubscribe(struct nlif_gate *             gate,
                      struct nlif_obsrv_subscriber * subscriber,
                      const struct upoll *           poller)
{
	nlif_gate_assert(gate);
	nlif_assert(subscriber);
	nlif_assert(poller);

	nlif_obsrv_unsubscribe(&gate->notif, subscriber);

	if (nlif_obsrv_notifier_empty(&gate->notif)) {
		int          fd = ynl_socket_get_fd(gate->sock);
		unsigned int grp = RTNLGRP_LINK;

		setsockopt(fd,
		           SOL_NETLINK,
		           NETLINK_DROP_MEMBERSHIP,
		           &grp,
		           sizeof(grp));
		upoll_unregister(poller, fd);

		nlif_dbg("gate notification disabled.");
	}
}

static
int
nlif_gate_dispatch_notif(struct upoll_worker * worker,
                         uint32_t              state __unused,
                         const struct upoll *  poller __unused)
{
	nlif_assert(worker);
	nlif_assert(state);
	nlif_assert(!(state & EPOLLOUT));
	nlif_assert(!(state & EPOLLRDHUP));
	nlif_assert(!(state & EPOLLPRI));
	nlif_assert(!(state & EPOLLHUP));
	nlif_assert(!(state & EPOLLERR));
	nlif_assert(state & EPOLLIN);
	nlif_assert(poller);

	struct nlif_gate *         gate = containerof(worker,
	                                              typeof(*gate),
	                                              work);
	struct ynl_ntf_base_type * ntf;

	nlif_gate_assert(gate);
	nlif_assert(!nlif_obsrv_notifier_empty(&gate->notif));

	/*
	 * Retrieve notification messages from underlying socket, parse
	 * them if known to the ynl socket family and queue them to the
	 * notification queue.
	 */
	ynl_ntf_check(gate->sock);

	/* Dequeue and process parsed notification messages. */
	ntf = ynl_ntf_dequeue(gate->sock);
	while (ntf) {
		if (ntf->cmd == RTM_NEWLINK) {
			struct rt_link_getlink_rsp * lnk;
			int                          err;

			lnk = &((struct rt_link_getlink_ntf *)ntf)->obj;
			err = nlif_gate_islink_valid(lnk);
			if (!err) {
				/* Notify subscribers. */
				nlif_obsrv_notify(&gate->notif, lnk);
			}
			else
				nlif_warn("'%s[%u]': "
				          "invalid link notification: "
				          "inconsistent attributes.",
				          lnk->ifname,
				          lnk->_hdr.ifi_index);
		}

		ynl_ntf_free(ntf);

		ntf = ynl_ntf_dequeue(gate->sock);
	}

	return 0;
}

#endif /* defined(CONFIG_NLIF_OBSRV) */

int
nlif_gate_init(struct nlif_gate * gate)
{
	nlif_assert(gate);

	struct ynl_error err;

	gate->sock = ynl_sock_create(&ynl_rt_link_family, &err);
	if (!gate->sock) {
		nlif_err("cannot open gate: %s.", err.msg);
		return nlif_ynl_err(&err);
	}

#if defined(CONFIG_NLIF_OBSRV)
	gate->work.dispatch = nlif_gate_dispatch_notif;
	nlif_obsrv_setup_notifier(&gate->notif);
#endif /* defined(CONFIG_NLIF_OBSRV) */

	nlif_dbg("gate opened.");

	return 0;
}

void
nlif_gate_fini(struct nlif_gate * gate)
{
	nlif_gate_assert(gate);
#if defined(CONFIG_NLIF_OBSRV)
	nlif_assert(nlif_obsrv_notifier_empty(&gate->notif));
#endif /* defined(CONFIG_NLIF_OBSRV) */

	ynl_sock_destroy(gate->sock);

	nlif_dbg("gate closed.");
}
