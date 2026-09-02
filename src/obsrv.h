#ifndef _NILF_OBSRV_H
#define _NILF_OBSRV_H

#include "common.h"
#include <stroll/dlist.h>

struct nlif_obsrv_subscriber;
struct nlif_obsrv_notifier;

/*
 * Callback notifying a subscriber.
 */
typedef void nlif_obsrv_on_event_fn(struct nlif_obsrv_subscriber *,
                                    void *,
                                    struct nlif_obsrv_notifier *);
/*
 * Event subscriber
 */
struct nlif_obsrv_subscriber {
	struct stroll_dlist_node node;
	nlif_obsrv_on_event_fn * on_event;
};

#define NLIF_OBSRV_SETUP_SUBSCRIBER(_sub, _on_event) \
	{ \
		.node     = STROLL_DLIST_INIT((_sub).node), \
		.on_event = _on_event \
	}

static inline bool
nlif_obsrv_subscribed(const struct nlif_obsrv_subscriber * subscriber)
{
	return !stroll_dlist_empty(&subscriber->node);
}

extern void
nlif_obsrv_setup_subscriber(struct nlif_obsrv_subscriber * subscriber,
                            nlif_obsrv_on_event_fn *       on_event);

/*
 * Event notifier
 */
struct nlif_obsrv_notifier {
	struct stroll_dlist_node subs;
#if defined(CONFIG_NLIF_DEBUG)
	bool                     run;
#endif /* defined(CONFIG_NLIF_DEBUG) */
};

#define _NLIF_OBSRV_SETUP_NOTIFIER(_notif) \
	.subs = STROLL_DLIST_INIT((_notif).subs)

#if defined(CONFIG_NLIF_DEBUG)

#define NLIF_OBSRV_SETUP_NOTIFIER(_notif) \
	{ \
		_NLIF_OBSRV_SETUP_NOTIFIER(_notif), \
		.run  = false \
	}

#else  /* defined(CONFIG_NLIF_DEBUG) */

#define NLIF_OBSRV_SETUP_NOTIFIER(_notif) \
	{ \
		_NLIF_OBSRV_SETUP_NOTIFIER(_notif) \
	}

#endif /* !defined(CONFIG_NLIF_DEBUG) */

static inline bool
nlif_obsrv_notifier_empty(const struct nlif_obsrv_notifier * notifier)
{
	return stroll_dlist_empty(&notifier->subs);
}

extern void
nlif_obsrv_notify(struct nlif_obsrv_notifier * notifier, void * event);

extern void
nlif_obsrv_notify_safe(struct nlif_obsrv_notifier * notifier, void * event);

static inline void
nlif_obsrv_subscribe(struct nlif_obsrv_notifier *   notifier,
                     struct nlif_obsrv_subscriber * subscriber)
{
	nlif_assert(notifier);
	nlif_assert(!notifier->run);
	nlif_assert(subscriber);
	nlif_assert(stroll_dlist_empty(&subscriber->node));
	nlif_assert(subscriber->on_event);

	stroll_dlist_nqueue_back(&notifier->subs, &subscriber->node);
}

static inline void
nlif_obsrv_unsubscribe(struct nlif_obsrv_notifier *   notifier __unused,
                       struct nlif_obsrv_subscriber * subscriber)
{
	nlif_assert(subscriber);
	nlif_assert(!notifier->run);

	stroll_dlist_remove_init(&subscriber->node);
}

static inline void
nlif_obsrv_setup_notifier(struct nlif_obsrv_notifier * notifier)
{
	nlif_assert(notifier);

	stroll_dlist_init(&notifier->subs);
#if defined(CONFIG_NLIF_DEBUG)
	notifier->run = false;
#endif /* defined(CONFIG_NLIF_DEBUG) */
}

#endif /* _NILF_OBSRV_H */
