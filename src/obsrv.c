#include "obsrv.h"

static void
nlif_obsrv_notify_subscriber(struct nlif_obsrv_subscriber * subscriber,
                             void *                         event,
                             struct nlif_obsrv_notifier *   notifier)

{
	nlif_assert(subscriber);
	nlif_assert(!stroll_dlist_empty(&subscriber->node));
	nlif_assert(subscriber->on_event);
	nlif_assert(notifier);

	subscriber->on_event(subscriber, event, notifier);
}

void
nlif_obsrv_setup_subscriber(struct nlif_obsrv_subscriber * subscriber,
                            nlif_obsrv_on_event_fn *       on_event)
{
	nlif_assert(subscriber);
	nlif_assert(on_event);

	stroll_dlist_init(&subscriber->node);
	subscriber->on_event = on_event;
}

void
nlif_obsrv_notify(struct nlif_obsrv_notifier * notifier, void * event)
{
	struct nlif_obsrv_subscriber * sub;

#if defined(CONFIG_NLIF_DEBUG)
	notifier->run = true;
#endif /* defined(CONFIG_NLIF_DEBUG) */

	stroll_dlist_foreach_entry(&notifier->subs, sub, node)
		nlif_obsrv_notify_subscriber(sub, event, notifier);

#if defined(CONFIG_NLIF_DEBUG)
	notifier->run = false;
#endif /* defined(CONFIG_NLIF_DEBUG) */
}

void
nlif_obsrv_notify_safe(struct nlif_obsrv_notifier * notifier, void * event)
{
	struct nlif_obsrv_subscriber * sub;
	struct nlif_obsrv_subscriber * tmp;

	stroll_dlist_foreach_entry_safe(&notifier->subs, sub, node, tmp)
		nlif_obsrv_notify_subscriber(sub, event, notifier);
}
