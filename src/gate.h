#ifndef _NLIF_GATE_H
#define _NLIF_GATE_H

#include "obsrv.h"
#include <ynl/rt-link-user.h>

struct nlif_gate {
	struct ynl_sock *          sock;
#if defined(CONFIG_NLIF_OBSRV)
	struct nlif_obsrv_notifier notif;
#endif /* defined(CONFIG_NLIF_OBSRV) */
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

extern int
nlif_gate_load_link_byidx(const struct nlif_gate *      gate,
                          unsigned int                  index,
                          struct rt_link_getlink_rsp ** link);

extern int
nlif_gate_load_link_byname(const struct nlif_gate *      gate,
                           const char *                  name,
                           struct rt_link_getlink_rsp ** link);

typedef int nlif_gate_on_link_loaded_fn(const struct nlif_gate *,
                                        struct rt_link_getlink_rsp *,
                                        void *);

extern int
nlif_gate_load_links(const struct nlif_gate *      gate,
                     nlif_gate_on_link_loaded_fn * on_loaded,
                     void *                        data);

extern int
nlif_gate_islink_valid(const struct rt_link_getlink_rsp * link);

static inline
void
nlif_gate_destroy_link(struct rt_link_getlink_rsp * link)
{
	nlif_assert(link);

	rt_link_getlink_rsp_free(link);
}

#if defined(CONFIG_NLIF_OBSRV)

static inline struct nlif_gate *
nlif_gate_from_notifier(struct nlif_obsrv_notifier * notifier)
{
	nlif_assert(notifier);

	struct nlif_gate * gate = containerof(notifier, typeof(*gate), notif);

	nlif_gate_assert(gate);
	return gate;
}

static inline bool
nlif_gate_subscribed(const struct nlif_gate * gate)
{
	nlif_gate_assert(gate);

	return !nlif_obsrv_notifier_empty(&gate->notif);
}

extern int
nlif_gate_subscribe(struct nlif_gate *             gate,
                    struct nlif_obsrv_subscriber * subscriber);

extern void
nlif_gate_unsubscribe(struct nlif_gate *             gate,
                      struct nlif_obsrv_subscriber * subscriber);

extern void
nlif_gate_notify(struct nlif_gate * gate);

#endif /* defined(CONFIG_NLIF_OBSRV) */

extern int
nlif_gate_init(struct nlif_gate * gate);

extern void
nlif_gate_fini(struct nlif_gate * gate);

#endif /* _NLIF_GATE_H */
