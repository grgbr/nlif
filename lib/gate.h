#ifndef _NLIF_GATE_H
#define _NLIF_GATE_H

#include "link.h"

struct nlif_gate;

typedef int nlif_gate_notify_fn(const struct nlif_gate *,
                                void *,
                                enum nlif_link_event,
                                void *);

struct nlif_gate {
	struct ynl_sock *      sock;
	nlif_gate_notify_fn *  notif;
	void *                 subscr;
};

#define nlif_gate_assert(_gate) \
	nlif_assert(_gate); \
	nlif_assert((_gate)->sock)

static inline int
nlif_gate_fd(const struct nlif_gate * gate)
{
	nlif_gate_assert(gate);

	return ynl_socket_get_fd(gate->sock);
}

static inline struct rt_link_setlink_req *
nlif_gate_create_setlink_req(void)
{
	struct rt_link_setlink_req * req;

	req = rt_link_setlink_req_alloc();
	if (req)
		return req;

	abort();
}

static inline void
nlif_gate_destroy_setlink_req(struct rt_link_setlink_req * request)
{
	nlif_assert(request);

	rt_link_setlink_req_free(request);
}

extern int
nlif_gate_load_link_stats(const struct nlif_gate *       gate,
                          unsigned int                   index,
                          struct rt_link_getstats_rsp ** stats);

static inline
void
nlif_gate_destroy_link_stats(struct rt_link_getstats_rsp * statistics)
{
	nlif_assert(statistics);

	rt_link_getstats_rsp_free(statistics);
}

extern int
nlif_gate_islink_valid(const struct rt_link_getlink_rsp * link);

extern int
nlif_gate_load_link_byidx(const struct nlif_gate *      gate,
                          unsigned int                  index,
                          struct rt_link_getlink_rsp ** link);

extern int
nlif_gate_load_link_byname(const struct nlif_gate *      gate,
                           const char *                  name,
                           struct rt_link_getlink_rsp ** link);

extern int
nlif_gate_setlink(const struct nlif_gate *     gate,
                  struct rt_link_setlink_req * request);

extern int
nlif_gate_load_links(const struct nlif_gate * gate);

static inline
void
nlif_gate_destroy_link(struct rt_link_getlink_rsp * link)
{
	nlif_assert(link);

	rt_link_getlink_rsp_free(link);
}

static inline
void
nlif_gate_subscribe(struct nlif_gate *    gate,
                    nlif_gate_notify_fn * notify,
                    void *                subscriber)
{
	nlif_gate_assert(gate);
	nlif_assert(!gate->notif);
	nlif_assert(notify);

	gate->notif = notify;
	gate->subscr = subscriber;
}

static inline
void
nlif_gate_unsubscribe(struct nlif_gate * gate)
{
	nlif_gate_assert(gate);

	gate->notif = NULL;
	gate->subscr = NULL;
}

static inline
int
nlif_gate_notify(const struct nlif_gate * gate,
                 enum nlif_link_event     event,
                 void *                   data)
{
	nlif_gate_assert(gate);
	nlif_assert(event >= 0);
	nlif_assert(event < NLIF_LINK_EVT_NR);

	int ret = 0;

	if (gate->notif)
		ret = gate->notif(gate, gate->subscr, event, data);

	return ret;
}

#if defined(CONFIG_NLIF_NOTIF_CHANGES)

extern int
nlif_gate_notify_changes(struct nlif_gate * gate);

#endif /* defined(CONFIG_NLIF_NOTIF_CHANGES) */

extern int
nlif_gate_init(struct nlif_gate * gate);

extern void
nlif_gate_fini(struct nlif_gate * gate);

#endif /* _NLIF_GATE_H */
