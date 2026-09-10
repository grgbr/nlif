#ifndef _NLIF_REPO_H
#define _NLIF_REPO_H

#include "store.h"
#include <utils/poll.h>

struct nlif_repo {
	struct nlif_store   store;
	struct upoll_worker notif;
	struct nlif_gate    gate;
};

#define nlif_repo_assert(_repo) \
	nlif_assert(_repo); \
	nlif_store_assert(&(_repo)->store); \
	nlif_gate_assert(&(_repo)->gate)

extern int
nlif_repo_open(struct nlif_repo * repo, const struct upoll * poller);

extern void
nlif_repo_close(struct nlif_repo * repo, const struct upoll * poller);

#endif /* _NLIF_REPO_H */
