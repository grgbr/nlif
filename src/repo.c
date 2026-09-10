#include "repo.h"

static
int
nlif_repo_dispatch_notif(struct upoll_worker * worker,
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

	struct nlif_repo * repo = containerof(worker, typeof(*repo), notif);

	nlif_repo_assert(repo);
	nlif_gate_notify(&repo->gate);

	return 0;
}

static int
nlif_repo_enable_notif(struct nlif_repo * repo, const struct upoll * poller)
{
	nlif_repo_assert(repo);
	nlif_assert(poller);

	struct nlif_store * store = &repo->store;
	struct nlif_gate *  gate = &repo->gate;
	int                 ret;
	const char *        msg __unused;

	ret = nlif_store_enable_notif(store, gate);
	if (ret) {
		msg = "cannot enable store notification";
		goto err;
	}

	ret = upoll_register_dispatch(poller,
	                              nlif_gate_fd(gate),
	                              EPOLLIN,
	                              &repo->notif,
	                              nlif_repo_dispatch_notif);
	if (ret) {
		msg = "cannot enable polling";
		goto disable;
	}

	nlif_debug("notification worker enabled");

	return 0;

disable:
	nlif_store_disable_notif(store, gate);
err:
	nlif_err("cannot enable notification worker: %s: %s",
	         msg,
	         strerror(-ret));

	return ret;
}

static void
nlif_repo_disable_notif(struct nlif_repo * repo, const struct upoll * poller)
{
	nlif_repo_assert(repo);
	nlif_assert(poller);

	struct nlif_gate * gate = &repo->gate;

	upoll_unregister(poller, nlif_gate_fd(gate));
	nlif_store_disable_notif(&repo->store, gate);

	nlif_debug("notification worker disabled");
}

int
nlif_repo_open(struct nlif_repo * repo, const struct upoll * poller)
{
	nlif_assert(repo);
	nlif_assert(poller);

	int err;

	err = nlif_gate_init(&repo->gate);
	if (err)
		return err;

	nlif_store_init(&repo->store);

	err = nlif_repo_enable_notif(repo, poller);
	if (err)
		goto fini_store;

	err = nlif_store_load(&repo->store, &repo->gate);
	if (err)
		goto disable_notif;

	nlif_debug("repository ready");

	return 0;

disable_notif:
	nlif_repo_disable_notif(repo, poller);
fini_store:
	nlif_store_fini(&repo->store);
	nlif_gate_fini(&repo->gate);

	nlif_err("cannot open repository: %s", strerror(-err));

	return err;
}

void
nlif_repo_close(struct nlif_repo * repo, const struct upoll * poller)
{
	nlif_repo_disable_notif(repo, poller);
	nlif_store_fini(&repo->store);
	nlif_gate_fini(&repo->gate);

	nlif_debug("repository closed");
}
