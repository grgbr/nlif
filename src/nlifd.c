#include "store.h"
#include <utils/poll.h>
#include <utils/signal.h>
#include <stdlib.h>

struct nlifd_notif_work {
	struct upoll_worker base;
	struct nlif_gate *  gate;
};

#define nlifd_assert_notif_work(_work) \
	nlif_assert(_work); \
	nlif_assert((_work)->base.dispatch); \
	nlif_gate_assert((_work)->gate)

#define NLIFD_INIT_NOTIF_WORK(_work, _gate) \
	{ \
		.base.dispatch = nlifd_dispatch_notif, \
		.gate          = _gate, \
	}

static
int
nlifd_dispatch_notif(struct upoll_worker * worker,
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

	struct nlifd_notif_work * notif = containerof(worker,
	                                              typeof(*notif),
	                                              base);

	nlifd_assert_notif_work(notif);
	nlif_gate_notify(notif->gate);

	return 0;
}

static int
nlifd_enable_notif(struct nlifd_notif_work * worker,
                   struct nlif_store *       store,
                   const struct upoll *      poller)
{
	nlifd_assert_notif_work(worker);
	nlif_store_assert(store);
	nlif_assert(poller);

	struct nlif_gate * gate = worker->gate;
	int                ret;
	const char *       msg;

	ret = nlif_store_enable_notif(store, gate);
	if (ret) {
		msg = "cannot enable store notification";
		goto err;
	}

	ret = upoll_register(poller,
	                     nlif_gate_fd(gate),
	                     EPOLLIN,
	                     &worker->base);
	if (ret) {
		msg = "cannot enable polling";
		goto disable;
	}

	nlif_dbg("notification worker enabled.");

	return 0;

disable:
	nlif_store_disable_notif(store, gate);
err:
	nlif_err("cannot enable notification worker: %s: %s.",
	         msg,
	         strerror(-ret));

	return ret;
}

void
nlifd_disable_notif(struct nlifd_notif_work * worker,
                    struct nlif_store *       store,
                    const struct upoll *      poller)
{
	nlifd_assert_notif_work(worker);
	nlif_store_assert(store);
	nlif_assert(poller);

	struct nlif_gate * gate = worker->gate;

	upoll_unregister(poller, nlif_gate_fd(gate));
	nlif_store_disable_notif(store, gate);

	nlif_dbg("notification worker disabled.");
}

struct nlifd_sigs_work {
	struct upoll_worker base;
	int                 fd;
};

static
int
nlifd_dispatch_sigs(struct upoll_worker * worker,
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

	const struct nlifd_sigs_work * wk;
	struct signalfd_siginfo        info;
	int                            ret;

	wk = containerof(worker, struct nlifd_sigs_work, base);
	nlif_assert(wk);

	ret = usig_read_fd(wk->fd, &info, 1);
	nlif_assert(ret);
	if (ret < 0)
		return (ret == -EAGAIN) ? 0 : ret;

	switch (info.ssi_signo) {
	case SIGHUP:
		/* TODO: implement reload ! */
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		/* Tell caller we were requested to terminate. */
		nlif_dbg("interrupted by signal '%s'.",
		         strsignal((int)info.ssi_signo));
		return -ESHUTDOWN;

	case SIGUSR1:
	case SIGUSR2:
		/* Silently ignore these... */
		return 0;

	default:
		nlif_assert(0);
	}

	unreachable();
}

static int
nlifd_init_sigs(struct nlifd_sigs_work * worker,
                const struct upoll *     poller)
{
	nlif_assert(worker);
	nlif_assert(poller);

	sigset_t     msk = *usig_empty_msk;
	sigset_t     blk = *usig_full_msk;
	int          ret;
	const char * msg;

	usig_addset(&msk, SIGHUP);
	usig_addset(&msk, SIGINT);
	usig_addset(&msk, SIGQUIT);
	usig_addset(&msk, SIGTERM);
	usig_addset(&msk, SIGUSR1);
	usig_addset(&msk, SIGUSR2);

	ret = usig_open_fd(&msk, SFD_NONBLOCK | SFD_CLOEXEC);
	if (ret < 0) {
		msg = "cannot to open worker";
		goto err;
	}

	worker->base.dispatch = nlifd_dispatch_sigs;
	worker->fd = ret;
	ret = upoll_register(poller, ret, EPOLLIN, &worker->base);
	if (ret) {
		msg = "cannot register worker";
		goto close;
	}

	usig_delset(&blk, SIGCONT);
	usig_delset(&blk, SIGTSTP);
	usig_delset(&blk, SIGTRAP);
	usig_delset(&blk, SIGTTIN);
	usig_delset(&blk, SIGTTOU);
	usig_procmask(SIG_SETMASK, &blk, NULL);

	nlif_dbg("signal handlers registered.");

	return 0;

close:
	usig_close_fd(worker->fd);
err:
	nlif_err("cannot setup signal handlers: %s.", msg);

	return ret;
}

static void
nlifd_fini_sigs(const struct nlifd_sigs_work * worker,
                const struct upoll *           poller)
{
	nlif_assert(worker);
	nlif_assert(worker->fd > 0);
	nlif_assert(poller);

	upoll_unregister(poller, worker->fd);
	usig_close_fd(worker->fd);

	nlif_dbg("signal handlers unregistered.");
}

int
main(int argc, const char * argv[])
{
	struct upoll            poll;
	struct nlifd_sigs_work  sigs;
	struct nlif_gate        gate;
	struct nlifd_notif_work notif = NLIFD_INIT_NOTIF_WORK(notif, &gate);
	struct nlif_store       store = NLIF_STORE_INIT(store);
	int                     ret;

	ret = upoll_open(&poll, 2U);
	if (ret) {
		nlif_err("cannot open poller: %s.", strerror(-ret));
		return EXIT_FAILURE;
	}

	ret = nlifd_init_sigs(&sigs, &poll);
	if (ret)
		goto close_poll;

	ret = nlif_gate_init(&gate);
	if (ret)
		goto fini_sigs;

	nlifd_enable_notif(&notif, &store, &poll);
	nlif_store_load(&store, &gate);

	do {
		ret = upoll_process(&poll, -1);
	} while (!ret || (ret == -EINTR));
	if (ret == -ESHUTDOWN)
		ret = 0;

	nlifd_disable_notif(&notif, &store, &poll);
	nlif_store_fini(&store);

	nlif_gate_fini(&gate);

fini_sigs:
	nlifd_fini_sigs(&sigs, &poll);
close_poll:
	upoll_close(&poll);

	return !ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
