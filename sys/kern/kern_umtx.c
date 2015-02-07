/*-
 * Copyright (c) 2004, David Xu <davidxu@freebsd.org>
 * Copyright (c) 2002, Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_compat.h"
#include "opt_umtx_profiling.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/syscallsubr.h>
#include <sys/eventhandler.h>
#include <sys/umtx.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include <machine/cpu.h>

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32_proto.h>
#endif

#define _UMUTEX_TRY		1
#define _UMUTEX_WAIT		2

#ifdef UMTX_PROFILING
#define	UPROF_PERC_BIGGER(w, f, sw, sf)					\
	(((w) > (sw)) || ((w) == (sw) && (f) > (sf)))
#endif

/* Priority inheritance mutex info. */
struct umtx_pi {
	/* Owner thread */
	struct thread		*pi_owner;

	/* Reference count */
	int			pi_refcount;

 	/* List entry to link umtx holding by thread */
	TAILQ_ENTRY(umtx_pi)	pi_link;

	/* List entry in hash */
	TAILQ_ENTRY(umtx_pi)	pi_hashlink;

	/* List for waiters */
	TAILQ_HEAD(,umtx_q)	pi_blocked;

	/* Identify a userland lock object */
	struct umtx_key		pi_key;
};

/* A userland synchronous object user. */
struct umtx_q {
	/* Linked list for the hash. */
	TAILQ_ENTRY(umtx_q)	uq_link;

	/* Umtx key. */
	struct umtx_key		uq_key;

	/* Umtx flags. */
	int			uq_flags;
#define UQF_UMTXQ	0x0001

	/* The thread waits on. */
	struct thread		*uq_thread;

	/*
	 * Blocked on PI mutex. read can use chain lock
	 * or umtx_lock, write must have both chain lock and
	 * umtx_lock being hold.
	 */
	struct umtx_pi		*uq_pi_blocked;

	/* On blocked list */
	TAILQ_ENTRY(umtx_q)	uq_lockq;

	/* Thread contending with us */
	TAILQ_HEAD(,umtx_pi)	uq_pi_contested;

	/* Inherited priority from PP mutex */
	u_char			uq_inherited_pri;
	
	/* Spare queue ready to be reused */
	struct umtxq_queue	*uq_spare_queue;

	/* The queue we on */
	struct umtxq_queue	*uq_cur_queue;
};

TAILQ_HEAD(umtxq_head, umtx_q);

/* Per-key wait-queue */
struct umtxq_queue {
	struct umtxq_head	head;
	struct umtx_key		key;
	LIST_ENTRY(umtxq_queue)	link;
	int			length;
};

LIST_HEAD(umtxq_list, umtxq_queue);

/* Userland lock object's wait-queue chain */
struct umtxq_chain {
	/* Lock for this chain. */
	struct mtx		uc_lock;

	/* List of sleep queues. */
	struct umtxq_list	uc_queue[2];
#define UMTX_SHARED_QUEUE	0
#define UMTX_EXCLUSIVE_QUEUE	1

	LIST_HEAD(, umtxq_queue) uc_spare_queue;

	/* Busy flag */
	char			uc_busy;

	/* Chain lock waiters */
	int			uc_waiters;

	/* All PI in the list */
	TAILQ_HEAD(,umtx_pi)	uc_pi_list;

#ifdef UMTX_PROFILING
	u_int 			length;
	u_int			max_length;
#endif
};

#define	UMTXQ_LOCKED_ASSERT(uc)		mtx_assert(&(uc)->uc_lock, MA_OWNED)

/*
 * Don't propagate time-sharing priority, there is a security reason,
 * a user can simply introduce PI-mutex, let thread A lock the mutex,
 * and let another thread B block on the mutex, because B is
 * sleeping, its priority will be boosted, this causes A's priority to
 * be boosted via priority propagating too and will never be lowered even
 * if it is using 100%CPU, this is unfair to other processes.
 */

#define UPRI(td)	(((td)->td_user_pri >= PRI_MIN_TIMESHARE &&\
			  (td)->td_user_pri <= PRI_MAX_TIMESHARE) ?\
			 PRI_MAX_TIMESHARE : (td)->td_user_pri)

#define	GOLDEN_RATIO_PRIME	2654404609U
#define	UMTX_CHAINS		512
#define	UMTX_SHIFTS		(__WORD_BIT - 9)

#define	GET_SHARE(flags)	\
    (((flags) & USYNC_PROCESS_SHARED) == 0 ? THREAD_SHARE : PROCESS_SHARE)

#define BUSY_SPINS		200

struct abs_timeout {
	int clockid;
	struct timespec cur;
	struct timespec end;
};

static uma_zone_t		umtx_pi_zone;
static struct umtxq_chain	umtxq_chains[2][UMTX_CHAINS];
static MALLOC_DEFINE(M_UMTX, "umtx", "UMTX queue memory");
static int			umtx_pi_allocated;

static SYSCTL_NODE(_debug, OID_AUTO, umtx, CTLFLAG_RW, 0, "umtx debug");
SYSCTL_INT(_debug_umtx, OID_AUTO, umtx_pi_allocated, CTLFLAG_RD,
    &umtx_pi_allocated, 0, "Allocated umtx_pi");

#ifdef UMTX_PROFILING
static long max_length;
SYSCTL_LONG(_debug_umtx, OID_AUTO, max_length, CTLFLAG_RD, &max_length, 0, "max_length");
static SYSCTL_NODE(_debug_umtx, OID_AUTO, chains, CTLFLAG_RD, 0, "umtx chain stats");
#endif

static void umtxq_sysinit(void *);
static void umtxq_hash(struct umtx_key *key);
static struct umtxq_chain *umtxq_getchain(struct umtx_key *key);
static void umtxq_lock(struct umtx_key *key);
static void umtxq_unlock(struct umtx_key *key);
static void umtxq_busy(struct umtx_key *key);
static void umtxq_unbusy(struct umtx_key *key);
static void umtxq_insert_queue(struct umtx_q *uq, int q);
static void umtxq_remove_queue(struct umtx_q *uq, int q);
static int umtxq_sleep(struct umtx_q *uq, const char *wmesg, struct abs_timeout *);
static int umtxq_count(struct umtx_key *key);
static struct umtx_pi *umtx_pi_alloc(int);
static void umtx_pi_free(struct umtx_pi *pi);
static int do_unlock_pp(struct thread *td, struct umutex *m, uint32_t flags);
static void umtx_thread_cleanup(struct thread *td);
static void umtx_exec_hook(void *arg __unused, struct proc *p __unused,
	struct image_params *imgp __unused);
SYSINIT(umtx, SI_SUB_EVENTHANDLER+1, SI_ORDER_MIDDLE, umtxq_sysinit, NULL);

#define umtxq_signal(key, nwake)	umtxq_signal_queue((key), (nwake), UMTX_SHARED_QUEUE)
#define umtxq_insert(uq)	umtxq_insert_queue((uq), UMTX_SHARED_QUEUE)
#define umtxq_remove(uq)	umtxq_remove_queue((uq), UMTX_SHARED_QUEUE)

static struct mtx umtx_lock;

#ifdef UMTX_PROFILING
static void
umtx_init_profiling(void) 
{
	struct sysctl_oid *chain_oid;
	char chain_name[10];
	int i;

	for (i = 0; i < UMTX_CHAINS; ++i) {
		snprintf(chain_name, sizeof(chain_name), "%d", i);
		chain_oid = SYSCTL_ADD_NODE(NULL, 
		    SYSCTL_STATIC_CHILDREN(_debug_umtx_chains), OID_AUTO, 
		    chain_name, CTLFLAG_RD, NULL, "umtx hash stats");
		SYSCTL_ADD_INT(NULL, SYSCTL_CHILDREN(chain_oid), OID_AUTO,
		    "max_length0", CTLFLAG_RD, &umtxq_chains[0][i].max_length, 0, NULL);
		SYSCTL_ADD_INT(NULL, SYSCTL_CHILDREN(chain_oid), OID_AUTO,
		    "max_length1", CTLFLAG_RD, &umtxq_chains[1][i].max_length, 0, NULL);
	}
}

static int
sysctl_debug_umtx_chains_peaks(SYSCTL_HANDLER_ARGS)
{
	char buf[512];
	struct sbuf sb;
	struct umtxq_chain *uc;
	u_int fract, i, j, tot, whole;
	u_int sf0, sf1, sf2, sf3, sf4;
	u_int si0, si1, si2, si3, si4;
	u_int sw0, sw1, sw2, sw3, sw4;

	sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);
	for (i = 0; i < 2; i++) {
		tot = 0;
		for (j = 0; j < UMTX_CHAINS; ++j) {
			uc = &umtxq_chains[i][j];
			mtx_lock(&uc->uc_lock);
			tot += uc->max_length;
			mtx_unlock(&uc->uc_lock);
		}
		if (tot == 0)
			sbuf_printf(&sb, "%u) Empty ", i);
		else {
			sf0 = sf1 = sf2 = sf3 = sf4 = 0;
			si0 = si1 = si2 = si3 = si4 = 0;
			sw0 = sw1 = sw2 = sw3 = sw4 = 0;
			for (j = 0; j < UMTX_CHAINS; j++) {
				uc = &umtxq_chains[i][j];
				mtx_lock(&uc->uc_lock);
				whole = uc->max_length * 100;
				mtx_unlock(&uc->uc_lock);
				fract = (whole % tot) * 100;
				if (UPROF_PERC_BIGGER(whole, fract, sw0, sf0)) {
					sf0 = fract;
					si0 = j;
					sw0 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw1,
				    sf1)) {
					sf1 = fract;
					si1 = j;
					sw1 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw2,
				    sf2)) {
					sf2 = fract;
					si2 = j;
					sw2 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw3,
				    sf3)) {
					sf3 = fract;
					si3 = j;
					sw3 = whole;
				} else if (UPROF_PERC_BIGGER(whole, fract, sw4,
				    sf4)) {
					sf4 = fract;
					si4 = j;
					sw4 = whole;
				}
			}
			sbuf_printf(&sb, "queue %u:\n", i);
			sbuf_printf(&sb, "1st: %u.%u%% idx: %u\n", sw0 / tot,
			    sf0 / tot, si0);
			sbuf_printf(&sb, "2nd: %u.%u%% idx: %u\n", sw1 / tot,
			    sf1 / tot, si1);
			sbuf_printf(&sb, "3rd: %u.%u%% idx: %u\n", sw2 / tot,
			    sf2 / tot, si2);
			sbuf_printf(&sb, "4th: %u.%u%% idx: %u\n", sw3 / tot,
			    sf3 / tot, si3);
			sbuf_printf(&sb, "5th: %u.%u%% idx: %u\n", sw4 / tot,
			    sf4 / tot, si4);
		}
	}
	sbuf_trim(&sb);
	sbuf_finish(&sb);
	sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
	sbuf_delete(&sb);
	return (0);
}

static int
sysctl_debug_umtx_chains_clear(SYSCTL_HANDLER_ARGS)
{
	struct umtxq_chain *uc;
	u_int i, j;
	int clear, error;

	clear = 0;
	error = sysctl_handle_int(oidp, &clear, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (clear != 0) {
		for (i = 0; i < 2; ++i) {
			for (j = 0; j < UMTX_CHAINS; ++j) {
				uc = &umtxq_chains[i][j];
				mtx_lock(&uc->uc_lock);
				uc->length = 0;
				uc->max_length = 0;	
				mtx_unlock(&uc->uc_lock);
			}
		}
	}
	return (0);
}

SYSCTL_PROC(_debug_umtx_chains, OID_AUTO, clear,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, 0, 0,
    sysctl_debug_umtx_chains_clear, "I", "Clear umtx chains statistics");
SYSCTL_PROC(_debug_umtx_chains, OID_AUTO, peaks,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, 0, 0,
    sysctl_debug_umtx_chains_peaks, "A", "Highest peaks in chains max length");
#endif

static void
umtxq_sysinit(void *arg __unused)
{
	int i, j;

	umtx_pi_zone = uma_zcreate("umtx pi", sizeof(struct umtx_pi),
		NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	for (i = 0; i < 2; ++i) {
		for (j = 0; j < UMTX_CHAINS; ++j) {
			mtx_init(&umtxq_chains[i][j].uc_lock, "umtxql", NULL,
				 MTX_DEF | MTX_DUPOK);
			LIST_INIT(&umtxq_chains[i][j].uc_queue[0]);
			LIST_INIT(&umtxq_chains[i][j].uc_queue[1]);
			LIST_INIT(&umtxq_chains[i][j].uc_spare_queue);
			TAILQ_INIT(&umtxq_chains[i][j].uc_pi_list);
			umtxq_chains[i][j].uc_busy = 0;
			umtxq_chains[i][j].uc_waiters = 0;
#ifdef UMTX_PROFILING
			umtxq_chains[i][j].length = 0;
			umtxq_chains[i][j].max_length = 0;	
#endif
		}
	}
#ifdef UMTX_PROFILING
	umtx_init_profiling();
#endif
	mtx_init(&umtx_lock, "umtx lock", NULL, MTX_SPIN);
	EVENTHANDLER_REGISTER(process_exec, umtx_exec_hook, NULL,
	    EVENTHANDLER_PRI_ANY);
}

struct umtx_q *
umtxq_alloc(void)
{
	struct umtx_q *uq;

	uq = malloc(sizeof(struct umtx_q), M_UMTX, M_WAITOK | M_ZERO);
	uq->uq_spare_queue = malloc(sizeof(struct umtxq_queue), M_UMTX, M_WAITOK | M_ZERO);
	TAILQ_INIT(&uq->uq_spare_queue->head);
	TAILQ_INIT(&uq->uq_pi_contested);
	uq->uq_inherited_pri = PRI_MAX;
	return (uq);
}

void
umtxq_free(struct umtx_q *uq)
{
	MPASS(uq->uq_spare_queue != NULL);
	free(uq->uq_spare_queue, M_UMTX);
	free(uq, M_UMTX);
}

static inline void
umtxq_hash(struct umtx_key *key)
{
	unsigned n = (uintptr_t)key->info.both.a + key->info.both.b;
	key->hash = ((n * GOLDEN_RATIO_PRIME) >> UMTX_SHIFTS) % UMTX_CHAINS;
}

static inline struct umtxq_chain *
umtxq_getchain(struct umtx_key *key)
{
	if (key->type <= TYPE_SEM)
		return (&umtxq_chains[1][key->hash]);
	return (&umtxq_chains[0][key->hash]);
}

/*
 * Lock a chain.
 */
static inline void
umtxq_lock(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_lock(&uc->uc_lock);
}

/*
 * Unlock a chain.
 */
static inline void
umtxq_unlock(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_unlock(&uc->uc_lock);
}

/*
 * Set chain to busy state when following operation
 * may be blocked (kernel mutex can not be used).
 */
static inline void
umtxq_busy(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_assert(&uc->uc_lock, MA_OWNED);
	if (uc->uc_busy) {
#ifdef SMP
		if (smp_cpus > 1) {
			int count = BUSY_SPINS;
			if (count > 0) {
				umtxq_unlock(key);
				while (uc->uc_busy && --count > 0)
					cpu_spinwait();
				umtxq_lock(key);
			}
		}
#endif
		while (uc->uc_busy) {
			uc->uc_waiters++;
			msleep(uc, &uc->uc_lock, 0, "umtxqb", 0);
			uc->uc_waiters--;
		}
	}
	uc->uc_busy = 1;
}

/*
 * Unbusy a chain.
 */
static inline void
umtxq_unbusy(struct umtx_key *key)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	mtx_assert(&uc->uc_lock, MA_OWNED);
	KASSERT(uc->uc_busy != 0, ("not busy"));
	uc->uc_busy = 0;
	if (uc->uc_waiters)
		wakeup_one(uc);
}

static inline void
umtxq_unbusy_unlocked(struct umtx_key *key)
{

	umtxq_lock(key);
	umtxq_unbusy(key);
	umtxq_unlock(key);
}

static struct umtxq_queue *
umtxq_queue_lookup(struct umtx_key *key, int q)
{
	struct umtxq_queue *uh;
	struct umtxq_chain *uc;

	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	LIST_FOREACH(uh, &uc->uc_queue[q], link) {
		if (umtx_key_match(&uh->key, key))
			return (uh);
	}

	return (NULL);
}

static inline void
umtxq_insert_queue(struct umtx_q *uq, int q)
{
	struct umtxq_queue *uh;
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	KASSERT((uq->uq_flags & UQF_UMTXQ) == 0, ("umtx_q is already on queue"));
	uh = umtxq_queue_lookup(&uq->uq_key, q);
	if (uh != NULL) {
		LIST_INSERT_HEAD(&uc->uc_spare_queue, uq->uq_spare_queue, link);
	} else {
		uh = uq->uq_spare_queue;
		uh->key = uq->uq_key;
		LIST_INSERT_HEAD(&uc->uc_queue[q], uh, link);
#ifdef UMTX_PROFILING
		uc->length++;
		if (uc->length > uc->max_length) {
			uc->max_length = uc->length;
			if (uc->max_length > max_length)
				max_length = uc->max_length;	
		}
#endif
	}
	uq->uq_spare_queue = NULL;

	TAILQ_INSERT_TAIL(&uh->head, uq, uq_link);
	uh->length++;
	uq->uq_flags |= UQF_UMTXQ;
	uq->uq_cur_queue = uh;
	return;
}

static inline void
umtxq_remove_queue(struct umtx_q *uq, int q)
{
	struct umtxq_chain *uc;
	struct umtxq_queue *uh;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	if (uq->uq_flags & UQF_UMTXQ) {
		uh = uq->uq_cur_queue;
		TAILQ_REMOVE(&uh->head, uq, uq_link);
		uh->length--;
		uq->uq_flags &= ~UQF_UMTXQ;
		if (TAILQ_EMPTY(&uh->head)) {
			KASSERT(uh->length == 0,
			    ("inconsistent umtxq_queue length"));
#ifdef UMTX_PROFILING
			uc->length--;
#endif
			LIST_REMOVE(uh, link);
		} else {
			uh = LIST_FIRST(&uc->uc_spare_queue);
			KASSERT(uh != NULL, ("uc_spare_queue is empty"));
			LIST_REMOVE(uh, link);
		}
		uq->uq_spare_queue = uh;
		uq->uq_cur_queue = NULL;
	}
}

/*
 * Check if there are multiple waiters
 */
static int
umtxq_count(struct umtx_key *key)
{
	struct umtxq_chain *uc;
	struct umtxq_queue *uh;

	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	uh = umtxq_queue_lookup(key, UMTX_SHARED_QUEUE);
	if (uh != NULL)
		return (uh->length);
	return (0);
}

/*
 * Check if there are multiple PI waiters and returns first
 * waiter.
 */
static int
umtxq_count_pi(struct umtx_key *key, struct umtx_q **first)
{
	struct umtxq_chain *uc;
	struct umtxq_queue *uh;

	*first = NULL;
	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	uh = umtxq_queue_lookup(key, UMTX_SHARED_QUEUE);
	if (uh != NULL) {
		*first = TAILQ_FIRST(&uh->head);
		return (uh->length);
	}
	return (0);
}

static int
umtxq_check_susp(struct thread *td)
{
	struct proc *p;
	int error;

	/*
	 * The check for TDF_NEEDSUSPCHK is racy, but it is enough to
	 * eventually break the lockstep loop.
	 */
	if ((td->td_flags & TDF_NEEDSUSPCHK) == 0)
		return (0);
	error = 0;
	p = td->td_proc;
	PROC_LOCK(p);
	if (P_SHOULDSTOP(p) ||
	    ((p->p_flag & P_TRACED) && (td->td_dbgflags & TDB_SUSPEND))) {
		if (p->p_flag & P_SINGLE_EXIT)
			error = EINTR;
		else
			error = ERESTART;
	}
	PROC_UNLOCK(p);
	return (error);
}

/*
 * Wake up threads waiting on an userland object.
 */

static int
umtxq_signal_queue(struct umtx_key *key, int n_wake, int q)
{
	struct umtxq_chain *uc;
	struct umtxq_queue *uh;
	struct umtx_q *uq;
	int ret;

	ret = 0;
	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);
	uh = umtxq_queue_lookup(key, q);
	if (uh != NULL) {
		while ((uq = TAILQ_FIRST(&uh->head)) != NULL) {
			umtxq_remove_queue(uq, q);
			wakeup(uq);
			if (++ret >= n_wake)
				return (ret);
		}
	}
	return (ret);
}


/*
 * Wake up specified thread.
 */
static inline void
umtxq_signal_thread(struct umtx_q *uq)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	umtxq_remove(uq);
	wakeup(uq);
}

static inline int 
tstohz(const struct timespec *tsp)
{
	struct timeval tv;

	TIMESPEC_TO_TIMEVAL(&tv, tsp);
	return tvtohz(&tv);
}

static void
abs_timeout_init(struct abs_timeout *timo, int clockid, int absolute,
	const struct timespec *timeout)
{

	timo->clockid = clockid;
	if (!absolute) {
		kern_clock_gettime(curthread, clockid, &timo->end);
		timo->cur = timo->end;
		timespecadd(&timo->end, timeout);
	} else {
		timo->end = *timeout;
		kern_clock_gettime(curthread, clockid, &timo->cur);
	}
}

static void
abs_timeout_init2(struct abs_timeout *timo, const struct _umtx_time *umtxtime)
{

	abs_timeout_init(timo, umtxtime->_clockid,
		(umtxtime->_flags & UMTX_ABSTIME) != 0,
		&umtxtime->_timeout);
}

static inline void
abs_timeout_update(struct abs_timeout *timo)
{
	kern_clock_gettime(curthread, timo->clockid, &timo->cur);
}

static int
abs_timeout_gethz(struct abs_timeout *timo)
{
	struct timespec tts;

	if (timespeccmp(&timo->end, &timo->cur, <=))
		return (-1); 
	tts = timo->end;
	timespecsub(&tts, &timo->cur);
	return (tstohz(&tts));
}

/*
 * Put thread into sleep state, before sleeping, check if
 * thread was removed from umtx queue.
 */
static inline int
umtxq_sleep(struct umtx_q *uq, const char *wmesg, struct abs_timeout *abstime)
{
	struct umtxq_chain *uc;
	int error, timo;

	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	for (;;) {
		if (!(uq->uq_flags & UQF_UMTXQ))
			return (0);
		if (abstime != NULL) {
			timo = abs_timeout_gethz(abstime);
			if (timo < 0)
				return (ETIMEDOUT);
		} else
			timo = 0;
		error = msleep(uq, &uc->uc_lock, PCATCH | PDROP, wmesg, timo);
		if (error != EWOULDBLOCK) {
			umtxq_lock(&uq->uq_key);
			break;
		}
		if (abstime != NULL)
			abs_timeout_update(abstime);
		umtxq_lock(&uq->uq_key);
	}
	return (error);
}

/*
 * Convert userspace address into unique logical address.
 */
int
umtx_key_get(void *addr, int type, int share, struct umtx_key *key)
{
	struct thread *td = curthread;
	vm_map_t map;
	vm_map_entry_t entry;
	vm_pindex_t pindex;
	vm_prot_t prot;
	boolean_t wired;

	key->type = type;
	if (share == THREAD_SHARE) {
		key->shared = 0;
		key->info.private.vs = td->td_proc->p_vmspace;
		key->info.private.addr = (uintptr_t)addr;
	} else {
		MPASS(share == PROCESS_SHARE || share == AUTO_SHARE);
		map = &td->td_proc->p_vmspace->vm_map;
		if (vm_map_lookup(&map, (vm_offset_t)addr, VM_PROT_WRITE,
		    &entry, &key->info.shared.object, &pindex, &prot,
		    &wired) != KERN_SUCCESS) {
			return EFAULT;
		}

		if ((share == PROCESS_SHARE) ||
		    (share == AUTO_SHARE &&
		     VM_INHERIT_SHARE == entry->inheritance)) {
			key->shared = 1;
			key->info.shared.offset = entry->offset + entry->start -
				(vm_offset_t)addr;
			vm_object_reference(key->info.shared.object);
		} else {
			key->shared = 0;
			key->info.private.vs = td->td_proc->p_vmspace;
			key->info.private.addr = (uintptr_t)addr;
		}
		vm_map_lookup_done(map, entry);
	}

	umtxq_hash(key);
	return (0);
}

/*
 * Release key.
 */
void
umtx_key_release(struct umtx_key *key)
{
	if (key->shared)
		vm_object_deallocate(key->info.shared.object);
}

/*
 * Lock a umtx object.
 */
static int
do_lock_umtx(struct thread *td, struct umtx *umtx, u_long id,
	const struct timespec *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	u_long owner;
	u_long old;
	int error = 0;

	uq = td->td_umtxq;
	if (timeout != NULL)
		abs_timeout_init(&timo, CLOCK_REALTIME, 0, timeout);

	/*
	 * Care must be exercised when dealing with umtx structure. It
	 * can fault on any access.
	 */
	for (;;) {
		/*
		 * Try the uncontested case.  This should be done in userland.
		 */
		owner = casuword(&umtx->u_owner, UMTX_UNOWNED, id);

		/* The acquire succeeded. */
		if (owner == UMTX_UNOWNED)
			return (0);

		/* The address was invalid. */
		if (owner == -1)
			return (EFAULT);

		/* If no one owns it but it is contested try to acquire it. */
		if (owner == UMTX_CONTESTED) {
			owner = casuword(&umtx->u_owner,
			    UMTX_CONTESTED, id | UMTX_CONTESTED);

			if (owner == UMTX_CONTESTED)
				return (0);

			/* The address was invalid. */
			if (owner == -1)
				return (EFAULT);

			error = umtxq_check_susp(td);
			if (error != 0)
				break;

			/* If this failed the lock has changed, restart. */
			continue;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;

		if ((error = umtx_key_get(umtx, TYPE_SIMPLE_LOCK,
			AUTO_SHARE, &uq->uq_key)) != 0)
			return (error);

		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		old = casuword(&umtx->u_owner, owner, owner | UMTX_CONTESTED);

		/* The address was invalid. */
		if (old == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		if (old == owner)
			error = umtxq_sleep(uq, "umtx", timeout == NULL ? NULL :
			    &timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);

		if (error == 0)
			error = umtxq_check_susp(td);
	}

	if (timeout == NULL) {
		/* Mutex locking is restarted if it is interrupted. */
		if (error == EINTR)
			error = ERESTART;
	} else {
		/* Timed-locking is not restarted. */
		if (error == ERESTART)
			error = EINTR;
	}
	return (error);
}

/*
 * Unlock a umtx object.
 */
static int
do_unlock_umtx(struct thread *td, struct umtx *umtx, u_long id)
{
	struct umtx_key key;
	u_long owner;
	u_long old;
	int error;
	int count;

	/*
	 * Make sure we own this mtx.
	 */
	owner = fuword(__DEVOLATILE(u_long *, &umtx->u_owner));
	if (owner == -1)
		return (EFAULT);

	if ((owner & ~UMTX_CONTESTED) != id)
		return (EPERM);

	/* This should be done in userland */
	if ((owner & UMTX_CONTESTED) == 0) {
		old = casuword(&umtx->u_owner, owner, UMTX_UNOWNED);
		if (old == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
		owner = old;
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(umtx, TYPE_SIMPLE_LOCK, AUTO_SHARE,
		&key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	old = casuword(&umtx->u_owner, owner,
		count <= 1 ? UMTX_UNOWNED : UMTX_CONTESTED);
	umtxq_lock(&key);
	umtxq_signal(&key,1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	if (old == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

#ifdef COMPAT_FREEBSD32

/*
 * Lock a umtx object.
 */
static int
do_lock_umtx32(struct thread *td, uint32_t *m, uint32_t id,
	const struct timespec *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t owner;
	uint32_t old;
	int error = 0;

	uq = td->td_umtxq;

	if (timeout != NULL)
		abs_timeout_init(&timo, CLOCK_REALTIME, 0, timeout);

	/*
	 * Care must be exercised when dealing with umtx structure. It
	 * can fault on any access.
	 */
	for (;;) {
		/*
		 * Try the uncontested case.  This should be done in userland.
		 */
		owner = casuword32(m, UMUTEX_UNOWNED, id);

		/* The acquire succeeded. */
		if (owner == UMUTEX_UNOWNED)
			return (0);

		/* The address was invalid. */
		if (owner == -1)
			return (EFAULT);

		/* If no one owns it but it is contested try to acquire it. */
		if (owner == UMUTEX_CONTESTED) {
			owner = casuword32(m,
			    UMUTEX_CONTESTED, id | UMUTEX_CONTESTED);
			if (owner == UMUTEX_CONTESTED)
				return (0);

			/* The address was invalid. */
			if (owner == -1)
				return (EFAULT);

			error = umtxq_check_susp(td);
			if (error != 0)
				break;

			/* If this failed the lock has changed, restart. */
			continue;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			return (error);

		if ((error = umtx_key_get(m, TYPE_SIMPLE_LOCK,
			AUTO_SHARE, &uq->uq_key)) != 0)
			return (error);

		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		old = casuword32(m, owner, owner | UMUTEX_CONTESTED);

		/* The address was invalid. */
		if (old == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		if (old == owner)
			error = umtxq_sleep(uq, "umtx", timeout == NULL ?
			    NULL : &timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);

		if (error == 0)
			error = umtxq_check_susp(td);
	}

	if (timeout == NULL) {
		/* Mutex locking is restarted if it is interrupted. */
		if (error == EINTR)
			error = ERESTART;
	} else {
		/* Timed-locking is not restarted. */
		if (error == ERESTART)
			error = EINTR;
	}
	return (error);
}

/*
 * Unlock a umtx object.
 */
static int
do_unlock_umtx32(struct thread *td, uint32_t *m, uint32_t id)
{
	struct umtx_key key;
	uint32_t owner;
	uint32_t old;
	int error;
	int count;

	/*
	 * Make sure we own this mtx.
	 */
	owner = fuword32(m);
	if (owner == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	/* This should be done in userland */
	if ((owner & UMUTEX_CONTESTED) == 0) {
		old = casuword32(m, owner, UMUTEX_UNOWNED);
		if (old == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
		owner = old;
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_SIMPLE_LOCK, AUTO_SHARE,
		&key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	old = casuword32(m, owner,
		count <= 1 ? UMUTEX_UNOWNED : UMUTEX_CONTESTED);
	umtxq_lock(&key);
	umtxq_signal(&key,1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	if (old == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}
#endif

/*
 * Fetch and compare value, sleep on the address if value is not changed.
 */
static int
do_wait(struct thread *td, void *addr, u_long id,
	struct _umtx_time *timeout, int compat32, int is_private)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	u_long tmp;
	uint32_t tmp32;
	int error = 0;

	uq = td->td_umtxq;
	if ((error = umtx_key_get(addr, TYPE_SIMPLE_WAIT,
		is_private ? THREAD_SHARE : AUTO_SHARE, &uq->uq_key)) != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	umtxq_lock(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);
	if (compat32 == 0) {
		error = fueword(addr, &tmp);
		if (error != 0)
			error = EFAULT;
	} else {
		error = fueword32(addr, &tmp32);
		if (error == 0)
			tmp = tmp32;
		else
			error = EFAULT;
	}
	umtxq_lock(&uq->uq_key);
	if (error == 0) {
		if (tmp == id)
			error = umtxq_sleep(uq, "uwait", timeout == NULL ?
			    NULL : &timo);
		if ((uq->uq_flags & UQF_UMTXQ) == 0)
			error = 0;
		else
			umtxq_remove(uq);
	} else if ((uq->uq_flags & UQF_UMTXQ) != 0) {
		umtxq_remove(uq);
	}
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

/*
 * Wake up threads sleeping on the specified address.
 */
int
kern_umtx_wake(struct thread *td, void *uaddr, int n_wake, int is_private)
{
	struct umtx_key key;
	int ret;
	
	if ((ret = umtx_key_get(uaddr, TYPE_SIMPLE_WAIT,
		is_private ? THREAD_SHARE : AUTO_SHARE, &key)) != 0)
		return (ret);
	umtxq_lock(&key);
	ret = umtxq_signal(&key, n_wake);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (0);
}

/*
 * Lock PTHREAD_PRIO_NONE protocol POSIX mutex.
 */
static int
do_lock_normal(struct thread *td, struct umutex *m, uint32_t flags,
	struct _umtx_time *timeout, int mode)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t owner, old, id;
	int error, rv;

	id = td->td_tid;
	uq = td->td_umtxq;
	error = 0;
	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	/*
	 * Care must be exercised when dealing with umtx structure. It
	 * can fault on any access.
	 */
	for (;;) {
		rv = fueword32(&m->m_owner, &owner);
		if (rv == -1)
			return (EFAULT);
		if (mode == _UMUTEX_WAIT) {
			if (owner == UMUTEX_UNOWNED || owner == UMUTEX_CONTESTED)
				return (0);
		} else {
			/*
			 * Try the uncontested case.  This should be done in userland.
			 */
			rv = casueword32(&m->m_owner, UMUTEX_UNOWNED,
			    &owner, id);
			/* The address was invalid. */
			if (rv == -1)
				return (EFAULT);

			/* The acquire succeeded. */
			if (owner == UMUTEX_UNOWNED)
				return (0);

			/* If no one owns it but it is contested try to acquire it. */
			if (owner == UMUTEX_CONTESTED) {
				rv = casueword32(&m->m_owner,
				    UMUTEX_CONTESTED, &owner,
				    id | UMUTEX_CONTESTED);
				/* The address was invalid. */
				if (rv == -1)
					return (EFAULT);

				if (owner == UMUTEX_CONTESTED)
					return (0);

				rv = umtxq_check_susp(td);
				if (rv != 0)
					return (rv);

				/* If this failed the lock has changed, restart. */
				continue;
			}
		}

		if ((flags & UMUTEX_ERROR_CHECK) != 0 &&
		    (owner & ~UMUTEX_CONTESTED) == id)
			return (EDEADLK);

		if (mode == _UMUTEX_TRY)
			return (EBUSY);

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			return (error);

		if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX,
		    GET_SHARE(flags), &uq->uq_key)) != 0)
			return (error);

		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		rv = casueword32(&m->m_owner, owner, &old,
		    owner | UMUTEX_CONTESTED);

		/* The address was invalid. */
		if (rv == -1) {
			umtxq_lock(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unbusy(&uq->uq_key);
			umtxq_unlock(&uq->uq_key);
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		umtxq_unbusy(&uq->uq_key);
		if (old == owner)
			error = umtxq_sleep(uq, "umtxn", timeout == NULL ?
			    NULL : &timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);

		if (error == 0)
			error = umtxq_check_susp(td);
	}

	return (0);
}

/*
 * Unlock PTHREAD_PRIO_NONE protocol POSIX mutex.
 */
static int
do_unlock_normal(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	uint32_t owner, old, id;
	int error;
	int count;

	id = td->td_tid;
	/*
	 * Make sure we own this mtx.
	 */
	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	if ((owner & UMUTEX_CONTESTED) == 0) {
		error = casueword32(&m->m_owner, owner, &old, UMUTEX_UNOWNED);
		if (error == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
		owner = old;
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	error = casueword32(&m->m_owner, owner, &old,
	    count <= 1 ? UMUTEX_UNOWNED : UMUTEX_CONTESTED);
	umtxq_lock(&key);
	umtxq_signal(&key,1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	if (error == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

/*
 * Check if the mutex is available and wake up a waiter,
 * only for simple mutex.
 */
static int
do_wake_umutex(struct thread *td, struct umutex *m)
{
	struct umtx_key key;
	uint32_t owner;
	uint32_t flags;
	int error;
	int count;

	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != 0)
		return (0);

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_NORMAL_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);

	if (count <= 1) {
		error = casueword32(&m->m_owner, UMUTEX_CONTESTED, &owner,
		    UMUTEX_UNOWNED);
		if (error == -1)
			error = EFAULT;
	}

	umtxq_lock(&key);
	if (error == 0 && count != 0 && (owner & ~UMUTEX_CONTESTED) == 0)
		umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

/*
 * Check if the mutex has waiters and tries to fix contention bit.
 */
static int
do_wake2_umutex(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	uint32_t owner, old;
	int type;
	int error;
	int count;

	switch(flags & (UMUTEX_PRIO_INHERIT | UMUTEX_PRIO_PROTECT)) {
	case 0:
		type = TYPE_NORMAL_UMUTEX;
		break;
	case UMUTEX_PRIO_INHERIT:
		type = TYPE_PI_UMUTEX;
		break;
	case UMUTEX_PRIO_PROTECT:
		type = TYPE_PP_UMUTEX;
		break;
	default:
		return (EINVAL);
	}
	if ((error = umtx_key_get(m, type, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	owner = 0;
	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count(&key);
	umtxq_unlock(&key);
	/*
	 * Only repair contention bit if there is a waiter, this means the mutex
	 * is still being referenced by userland code, otherwise don't update
	 * any memory.
	 */
	if (count > 1) {
		error = fueword32(&m->m_owner, &owner);
		if (error == -1)
			error = EFAULT;
		while (error == 0 && (owner & UMUTEX_CONTESTED) == 0) {
			error = casueword32(&m->m_owner, owner, &old,
			    owner | UMUTEX_CONTESTED);
			if (error == -1) {
				error = EFAULT;
				break;
			}
			if (old == owner)
				break;
			owner = old;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
	} else if (count == 1) {
		error = fueword32(&m->m_owner, &owner);
		if (error == -1)
			error = EFAULT;
		while (error == 0 && (owner & ~UMUTEX_CONTESTED) != 0 &&
		       (owner & UMUTEX_CONTESTED) == 0) {
			error = casueword32(&m->m_owner, owner, &old,
			    owner | UMUTEX_CONTESTED);
			if (error == -1) {
				error = EFAULT;
				break;
			}
			if (old == owner)
				break;
			owner = old;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
	}
	umtxq_lock(&key);
	if (error == EFAULT) {
		umtxq_signal(&key, INT_MAX);
	} else if (count != 0 && (owner & ~UMUTEX_CONTESTED) == 0)
		umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

static inline struct umtx_pi *
umtx_pi_alloc(int flags)
{
	struct umtx_pi *pi;

	pi = uma_zalloc(umtx_pi_zone, M_ZERO | flags);
	TAILQ_INIT(&pi->pi_blocked);
	atomic_add_int(&umtx_pi_allocated, 1);
	return (pi);
}

static inline void
umtx_pi_free(struct umtx_pi *pi)
{
	uma_zfree(umtx_pi_zone, pi);
	atomic_add_int(&umtx_pi_allocated, -1);
}

/*
 * Adjust the thread's position on a pi_state after its priority has been
 * changed.
 */
static int
umtx_pi_adjust_thread(struct umtx_pi *pi, struct thread *td)
{
	struct umtx_q *uq, *uq1, *uq2;
	struct thread *td1;

	mtx_assert(&umtx_lock, MA_OWNED);
	if (pi == NULL)
		return (0);

	uq = td->td_umtxq;

	/*
	 * Check if the thread needs to be moved on the blocked chain.
	 * It needs to be moved if either its priority is lower than
	 * the previous thread or higher than the next thread.
	 */
	uq1 = TAILQ_PREV(uq, umtxq_head, uq_lockq);
	uq2 = TAILQ_NEXT(uq, uq_lockq);
	if ((uq1 != NULL && UPRI(td) < UPRI(uq1->uq_thread)) ||
	    (uq2 != NULL && UPRI(td) > UPRI(uq2->uq_thread))) {
		/*
		 * Remove thread from blocked chain and determine where
		 * it should be moved to.
		 */
		TAILQ_REMOVE(&pi->pi_blocked, uq, uq_lockq);
		TAILQ_FOREACH(uq1, &pi->pi_blocked, uq_lockq) {
			td1 = uq1->uq_thread;
			MPASS(td1->td_proc->p_magic == P_MAGIC);
			if (UPRI(td1) > UPRI(td))
				break;
		}

		if (uq1 == NULL)
			TAILQ_INSERT_TAIL(&pi->pi_blocked, uq, uq_lockq);
		else
			TAILQ_INSERT_BEFORE(uq1, uq, uq_lockq);
	}
	return (1);
}

static struct umtx_pi *
umtx_pi_next(struct umtx_pi *pi)
{
	struct umtx_q *uq_owner;

	if (pi->pi_owner == NULL)
		return (NULL);
	uq_owner = pi->pi_owner->td_umtxq;
	if (uq_owner == NULL)
		return (NULL);
	return (uq_owner->uq_pi_blocked);
}

/*
 * Floyd's Cycle-Finding Algorithm.
 */
static bool
umtx_pi_check_loop(struct umtx_pi *pi)
{
	struct umtx_pi *pi1;	/* fast iterator */

	mtx_assert(&umtx_lock, MA_OWNED);
	if (pi == NULL)
		return (false);
	pi1 = pi;
	for (;;) {
		pi = umtx_pi_next(pi);
		if (pi == NULL)
			break;
		pi1 = umtx_pi_next(pi1);
		if (pi1 == NULL)
			break;
		pi1 = umtx_pi_next(pi1);
		if (pi1 == NULL)
			break;
		if (pi == pi1)
			return (true);
	}
	return (false);
}

/*
 * Propagate priority when a thread is blocked on POSIX
 * PI mutex.
 */ 
static void
umtx_propagate_priority(struct thread *td)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;
	int pri;

	mtx_assert(&umtx_lock, MA_OWNED);
	pri = UPRI(td);
	uq = td->td_umtxq;
	pi = uq->uq_pi_blocked;
	if (pi == NULL)
		return;
	if (umtx_pi_check_loop(pi))
		return;

	for (;;) {
		td = pi->pi_owner;
		if (td == NULL || td == curthread)
			return;

		MPASS(td->td_proc != NULL);
		MPASS(td->td_proc->p_magic == P_MAGIC);

		thread_lock(td);
		if (td->td_lend_user_pri > pri)
			sched_lend_user_prio(td, pri);
		else {
			thread_unlock(td);
			break;
		}
		thread_unlock(td);

		/*
		 * Pick up the lock that td is blocked on.
		 */
		uq = td->td_umtxq;
		pi = uq->uq_pi_blocked;
		if (pi == NULL)
			break;
		/* Resort td on the list if needed. */
		umtx_pi_adjust_thread(pi, td);
	}
}

/*
 * Unpropagate priority for a PI mutex when a thread blocked on
 * it is interrupted by signal or resumed by others.
 */
static void
umtx_repropagate_priority(struct umtx_pi *pi)
{
	struct umtx_q *uq, *uq_owner;
	struct umtx_pi *pi2;
	int pri;

	mtx_assert(&umtx_lock, MA_OWNED);

	if (umtx_pi_check_loop(pi))
		return;
	while (pi != NULL && pi->pi_owner != NULL) {
		pri = PRI_MAX;
		uq_owner = pi->pi_owner->td_umtxq;

		TAILQ_FOREACH(pi2, &uq_owner->uq_pi_contested, pi_link) {
			uq = TAILQ_FIRST(&pi2->pi_blocked);
			if (uq != NULL) {
				if (pri > UPRI(uq->uq_thread))
					pri = UPRI(uq->uq_thread);
			}
		}

		if (pri > uq_owner->uq_inherited_pri)
			pri = uq_owner->uq_inherited_pri;
		thread_lock(pi->pi_owner);
		sched_lend_user_prio(pi->pi_owner, pri);
		thread_unlock(pi->pi_owner);
		if ((pi = uq_owner->uq_pi_blocked) != NULL)
			umtx_pi_adjust_thread(pi, uq_owner->uq_thread);
	}
}

/*
 * Insert a PI mutex into owned list.
 */
static void
umtx_pi_setowner(struct umtx_pi *pi, struct thread *owner)
{
	struct umtx_q *uq_owner;

	uq_owner = owner->td_umtxq;
	mtx_assert(&umtx_lock, MA_OWNED);
	if (pi->pi_owner != NULL)
		panic("pi_ower != NULL");
	pi->pi_owner = owner;
	TAILQ_INSERT_TAIL(&uq_owner->uq_pi_contested, pi, pi_link);
}

/*
 * Claim ownership of a PI mutex.
 */
static int
umtx_pi_claim(struct umtx_pi *pi, struct thread *owner)
{
	struct umtx_q *uq, *uq_owner;

	uq_owner = owner->td_umtxq;
	mtx_lock_spin(&umtx_lock);
	if (pi->pi_owner == owner) {
		mtx_unlock_spin(&umtx_lock);
		return (0);
	}

	if (pi->pi_owner != NULL) {
		/*
		 * userland may have already messed the mutex, sigh.
		 */
		mtx_unlock_spin(&umtx_lock);
		return (EPERM);
	}
	umtx_pi_setowner(pi, owner);
	uq = TAILQ_FIRST(&pi->pi_blocked);
	if (uq != NULL) {
		int pri;

		pri = UPRI(uq->uq_thread);
		thread_lock(owner);
		if (pri < UPRI(owner))
			sched_lend_user_prio(owner, pri);
		thread_unlock(owner);
	}
	mtx_unlock_spin(&umtx_lock);
	return (0);
}

/*
 * Adjust a thread's order position in its blocked PI mutex,
 * this may result new priority propagating process.
 */
void
umtx_pi_adjust(struct thread *td, u_char oldpri)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;

	uq = td->td_umtxq;
	mtx_lock_spin(&umtx_lock);
	/*
	 * Pick up the lock that td is blocked on.
	 */
	pi = uq->uq_pi_blocked;
	if (pi != NULL) {
		umtx_pi_adjust_thread(pi, td);
		umtx_repropagate_priority(pi);
	}
	mtx_unlock_spin(&umtx_lock);
}

/*
 * Sleep on a PI mutex.
 */
static int
umtxq_sleep_pi(struct umtx_q *uq, struct umtx_pi *pi,
	uint32_t owner, const char *wmesg, struct abs_timeout *timo)
{
	struct umtxq_chain *uc;
	struct thread *td, *td1;
	struct umtx_q *uq1;
	int pri;
	int error = 0;

	td = uq->uq_thread;
	KASSERT(td == curthread, ("inconsistent uq_thread"));
	uc = umtxq_getchain(&uq->uq_key);
	UMTXQ_LOCKED_ASSERT(uc);
	KASSERT(uc->uc_busy != 0, ("umtx chain is not busy"));
	umtxq_insert(uq);
	mtx_lock_spin(&umtx_lock);
	if (pi->pi_owner == NULL) {
		mtx_unlock_spin(&umtx_lock);
		/* XXX Only look up thread in current process. */
		td1 = tdfind(owner, curproc->p_pid);
		mtx_lock_spin(&umtx_lock);
		if (td1 != NULL) {
			if (pi->pi_owner == NULL)
				umtx_pi_setowner(pi, td1);
			PROC_UNLOCK(td1->td_proc);
		}
	}

	TAILQ_FOREACH(uq1, &pi->pi_blocked, uq_lockq) {
		pri = UPRI(uq1->uq_thread);
		if (pri > UPRI(td))
			break;
	}

	if (uq1 != NULL)
		TAILQ_INSERT_BEFORE(uq1, uq, uq_lockq);
	else
		TAILQ_INSERT_TAIL(&pi->pi_blocked, uq, uq_lockq);

	uq->uq_pi_blocked = pi;
	thread_lock(td);
	td->td_flags |= TDF_UPIBLOCKED;
	thread_unlock(td);
	umtx_propagate_priority(td);
	mtx_unlock_spin(&umtx_lock);
	umtxq_unbusy(&uq->uq_key);

	error = umtxq_sleep(uq, wmesg, timo);
	umtxq_remove(uq);

	mtx_lock_spin(&umtx_lock);
	uq->uq_pi_blocked = NULL;
	thread_lock(td);
	td->td_flags &= ~TDF_UPIBLOCKED;
	thread_unlock(td);
	TAILQ_REMOVE(&pi->pi_blocked, uq, uq_lockq);
	umtx_repropagate_priority(pi);
	mtx_unlock_spin(&umtx_lock);
	umtxq_unlock(&uq->uq_key);

	return (error);
}

/*
 * Add reference count for a PI mutex.
 */
static void
umtx_pi_ref(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	pi->pi_refcount++;
}

/*
 * Decrease reference count for a PI mutex, if the counter
 * is decreased to zero, its memory space is freed.
 */ 
static void
umtx_pi_unref(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	KASSERT(pi->pi_refcount > 0, ("invalid reference count"));
	if (--pi->pi_refcount == 0) {
		mtx_lock_spin(&umtx_lock);
		if (pi->pi_owner != NULL) {
			TAILQ_REMOVE(&pi->pi_owner->td_umtxq->uq_pi_contested,
				pi, pi_link);
			pi->pi_owner = NULL;
		}
		KASSERT(TAILQ_EMPTY(&pi->pi_blocked),
			("blocked queue not empty"));
		mtx_unlock_spin(&umtx_lock);
		TAILQ_REMOVE(&uc->uc_pi_list, pi, pi_hashlink);
		umtx_pi_free(pi);
	}
}

/*
 * Find a PI mutex in hash table.
 */
static struct umtx_pi *
umtx_pi_lookup(struct umtx_key *key)
{
	struct umtxq_chain *uc;
	struct umtx_pi *pi;

	uc = umtxq_getchain(key);
	UMTXQ_LOCKED_ASSERT(uc);

	TAILQ_FOREACH(pi, &uc->uc_pi_list, pi_hashlink) {
		if (umtx_key_match(&pi->pi_key, key)) {
			return (pi);
		}
	}
	return (NULL);
}

/*
 * Insert a PI mutex into hash table.
 */
static inline void
umtx_pi_insert(struct umtx_pi *pi)
{
	struct umtxq_chain *uc;

	uc = umtxq_getchain(&pi->pi_key);
	UMTXQ_LOCKED_ASSERT(uc);
	TAILQ_INSERT_TAIL(&uc->uc_pi_list, pi, pi_hashlink);
}

/*
 * Lock a PI mutex.
 */
static int
do_lock_pi(struct thread *td, struct umutex *m, uint32_t flags,
    struct _umtx_time *timeout, int try)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	struct umtx_pi *pi, *new_pi;
	uint32_t id, owner, old;
	int error, rv;

	id = td->td_tid;
	uq = td->td_umtxq;

	if ((error = umtx_key_get(m, TYPE_PI_UMUTEX, GET_SHARE(flags),
	    &uq->uq_key)) != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	umtxq_lock(&uq->uq_key);
	pi = umtx_pi_lookup(&uq->uq_key);
	if (pi == NULL) {
		new_pi = umtx_pi_alloc(M_NOWAIT);
		if (new_pi == NULL) {
			umtxq_unlock(&uq->uq_key);
			new_pi = umtx_pi_alloc(M_WAITOK);
			umtxq_lock(&uq->uq_key);
			pi = umtx_pi_lookup(&uq->uq_key);
			if (pi != NULL) {
				umtx_pi_free(new_pi);
				new_pi = NULL;
			}
		}
		if (new_pi != NULL) {
			new_pi->pi_key = uq->uq_key;
			umtx_pi_insert(new_pi);
			pi = new_pi;
		}
	}
	umtx_pi_ref(pi);
	umtxq_unlock(&uq->uq_key);

	/*
	 * Care must be exercised when dealing with umtx structure.  It
	 * can fault on any access.
	 */
	for (;;) {
		/*
		 * Try the uncontested case.  This should be done in userland.
		 */
		rv = casueword32(&m->m_owner, UMUTEX_UNOWNED, &owner, id);
		/* The address was invalid. */
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		/* The acquire succeeded. */
		if (owner == UMUTEX_UNOWNED) {
			error = 0;
			break;
		}

		/* If no one owns it but it is contested try to acquire it. */
		if (owner == UMUTEX_CONTESTED) {
			rv = casueword32(&m->m_owner,
			    UMUTEX_CONTESTED, &owner, id | UMUTEX_CONTESTED);
			/* The address was invalid. */
			if (rv == -1) {
				error = EFAULT;
				break;
			}

			if (owner == UMUTEX_CONTESTED) {
				umtxq_lock(&uq->uq_key);
				umtxq_busy(&uq->uq_key);
				error = umtx_pi_claim(pi, td);
				umtxq_unbusy(&uq->uq_key);
				umtxq_unlock(&uq->uq_key);
				break;
			}

			error = umtxq_check_susp(td);
			if (error != 0)
				break;

			/* If this failed the lock has changed, restart. */
			continue;
		}

		if ((owner & ~UMUTEX_CONTESTED) == id) {
			error = EDEADLK;
			break;
		}

		if (try != 0) {
			error = EBUSY;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;
			
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * Set the contested bit so that a release in user space
		 * knows to use the system call for unlock.  If this fails
		 * either some one else has acquired the lock or it has been
		 * released.
		 */
		rv = casueword32(&m->m_owner, owner, &old,
		    owner | UMUTEX_CONTESTED);

		/* The address was invalid. */
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}

		umtxq_lock(&uq->uq_key);
		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		if (old == owner) {
			error = umtxq_sleep_pi(uq, pi, owner & ~UMUTEX_CONTESTED,
			    "umtxpi", timeout == NULL ? NULL : &timo);
			if (error != 0)
				continue;
		} else {
			umtxq_unbusy(&uq->uq_key);
			umtxq_unlock(&uq->uq_key);
		}

		error = umtxq_check_susp(td);
		if (error != 0)
			break;
	}

	umtxq_lock(&uq->uq_key);
	umtx_pi_unref(pi);
	umtxq_unlock(&uq->uq_key);

	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Unlock a PI mutex.
 */
static int
do_unlock_pi(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	struct umtx_q *uq_first, *uq_first2, *uq_me;
	struct umtx_pi *pi, *pi2;
	uint32_t owner, old, id;
	int error;
	int count;
	int pri;

	id = td->td_tid;
	/*
	 * Make sure we own this mtx.
	 */
	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	/* This should be done in userland */
	if ((owner & UMUTEX_CONTESTED) == 0) {
		error = casueword32(&m->m_owner, owner, &old, UMUTEX_UNOWNED);
		if (error == -1)
			return (EFAULT);
		if (old == owner)
			return (0);
		owner = old;
	}

	/* We should only ever be in here for contested locks */
	if ((error = umtx_key_get(m, TYPE_PI_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);

	umtxq_lock(&key);
	umtxq_busy(&key);
	count = umtxq_count_pi(&key, &uq_first);
	if (uq_first != NULL) {
		mtx_lock_spin(&umtx_lock);
		pi = uq_first->uq_pi_blocked;
		KASSERT(pi != NULL, ("pi == NULL?"));
		if (pi->pi_owner != curthread) {
			mtx_unlock_spin(&umtx_lock);
			umtxq_unbusy(&key);
			umtxq_unlock(&key);
			umtx_key_release(&key);
			/* userland messed the mutex */
			return (EPERM);
		}
		uq_me = curthread->td_umtxq;
		pi->pi_owner = NULL;
		TAILQ_REMOVE(&uq_me->uq_pi_contested, pi, pi_link);
		/* get highest priority thread which is still sleeping. */
		uq_first = TAILQ_FIRST(&pi->pi_blocked);
		while (uq_first != NULL && 
		       (uq_first->uq_flags & UQF_UMTXQ) == 0) {
			uq_first = TAILQ_NEXT(uq_first, uq_lockq);
		}
		pri = PRI_MAX;
		TAILQ_FOREACH(pi2, &uq_me->uq_pi_contested, pi_link) {
			uq_first2 = TAILQ_FIRST(&pi2->pi_blocked);
			if (uq_first2 != NULL) {
				if (pri > UPRI(uq_first2->uq_thread))
					pri = UPRI(uq_first2->uq_thread);
			}
		}
		thread_lock(curthread);
		sched_lend_user_prio(curthread, pri);
		thread_unlock(curthread);
		mtx_unlock_spin(&umtx_lock);
		if (uq_first)
			umtxq_signal_thread(uq_first);
	}
	umtxq_unlock(&key);

	/*
	 * When unlocking the umtx, it must be marked as unowned if
	 * there is zero or one thread only waiting for it.
	 * Otherwise, it must be marked as contested.
	 */
	error = casueword32(&m->m_owner, owner, &old,
	    count <= 1 ? UMUTEX_UNOWNED : UMUTEX_CONTESTED);

	umtxq_unbusy_unlocked(&key);
	umtx_key_release(&key);
	if (error == -1)
		return (EFAULT);
	if (old != owner)
		return (EINVAL);
	return (0);
}

/*
 * Lock a PP mutex.
 */
static int
do_lock_pp(struct thread *td, struct umutex *m, uint32_t flags,
    struct _umtx_time *timeout, int try)
{
	struct abs_timeout timo;
	struct umtx_q *uq, *uq2;
	struct umtx_pi *pi;
	uint32_t ceiling;
	uint32_t owner, id;
	int error, pri, old_inherited_pri, su, rv;

	id = td->td_tid;
	uq = td->td_umtxq;
	if ((error = umtx_key_get(m, TYPE_PP_UMUTEX, GET_SHARE(flags),
	    &uq->uq_key)) != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	su = (priv_check(td, PRIV_SCHED_RTPRIO) == 0);
	for (;;) {
		old_inherited_pri = uq->uq_inherited_pri;
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		rv = fueword32(&m->m_ceilings[0], &ceiling);
		if (rv == -1) {
			error = EFAULT;
			goto out;
		}
		ceiling = RTP_PRIO_MAX - ceiling;
		if (ceiling > RTP_PRIO_MAX) {
			error = EINVAL;
			goto out;
		}

		mtx_lock_spin(&umtx_lock);
		if (UPRI(td) < PRI_MIN_REALTIME + ceiling) {
			mtx_unlock_spin(&umtx_lock);
			error = EINVAL;
			goto out;
		}
		if (su && PRI_MIN_REALTIME + ceiling < uq->uq_inherited_pri) {
			uq->uq_inherited_pri = PRI_MIN_REALTIME + ceiling;
			thread_lock(td);
			if (uq->uq_inherited_pri < UPRI(td))
				sched_lend_user_prio(td, uq->uq_inherited_pri);
			thread_unlock(td);
		}
		mtx_unlock_spin(&umtx_lock);

		rv = casueword32(&m->m_owner,
		    UMUTEX_CONTESTED, &owner, id | UMUTEX_CONTESTED);
		/* The address was invalid. */
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		if (owner == UMUTEX_CONTESTED) {
			error = 0;
			break;
		}

		if ((flags & UMUTEX_ERROR_CHECK) != 0 &&
		    (owner & ~UMUTEX_CONTESTED) == id) {
			error = EDEADLK;
			break;
		}

		if (try != 0) {
			error = EBUSY;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;

		umtxq_lock(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		error = umtxq_sleep(uq, "umtxpp", timeout == NULL ?
		    NULL : &timo);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);

		mtx_lock_spin(&umtx_lock);
		uq->uq_inherited_pri = old_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		thread_lock(td);
		sched_lend_user_prio(td, pri);
		thread_unlock(td);
		mtx_unlock_spin(&umtx_lock);
	}

	if (error != 0) {
		mtx_lock_spin(&umtx_lock);
		uq->uq_inherited_pri = old_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		thread_lock(td);
		sched_lend_user_prio(td, pri);
		thread_unlock(td);
		mtx_unlock_spin(&umtx_lock);
	}

out:
	umtxq_unbusy_unlocked(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Unlock a PP mutex.
 */
static int
do_unlock_pp(struct thread *td, struct umutex *m, uint32_t flags)
{
	struct umtx_key key;
	struct umtx_q *uq, *uq2;
	struct umtx_pi *pi;
	uint32_t owner, id;
	uint32_t rceiling;
	int error, pri, new_inherited_pri, su;

	id = td->td_tid;
	uq = td->td_umtxq;
	su = (priv_check(td, PRIV_SCHED_RTPRIO) == 0);

	/*
	 * Make sure we own this mtx.
	 */
	error = fueword32(&m->m_owner, &owner);
	if (error == -1)
		return (EFAULT);

	if ((owner & ~UMUTEX_CONTESTED) != id)
		return (EPERM);

	error = copyin(&m->m_ceilings[1], &rceiling, sizeof(uint32_t));
	if (error != 0)
		return (error);

	if (rceiling == -1)
		new_inherited_pri = PRI_MAX;
	else {
		rceiling = RTP_PRIO_MAX - rceiling;
		if (rceiling > RTP_PRIO_MAX)
			return (EINVAL);
		new_inherited_pri = PRI_MIN_REALTIME + rceiling;
	}

	if ((error = umtx_key_get(m, TYPE_PP_UMUTEX, GET_SHARE(flags),
	    &key)) != 0)
		return (error);
	umtxq_lock(&key);
	umtxq_busy(&key);
	umtxq_unlock(&key);
	/*
	 * For priority protected mutex, always set unlocked state
	 * to UMUTEX_CONTESTED, so that userland always enters kernel
	 * to lock the mutex, it is necessary because thread priority
	 * has to be adjusted for such mutex.
	 */
	error = suword32(&m->m_owner, UMUTEX_CONTESTED);

	umtxq_lock(&key);
	if (error == 0)
		umtxq_signal(&key, 1);
	umtxq_unbusy(&key);
	umtxq_unlock(&key);

	if (error == -1)
		error = EFAULT;
	else {
		mtx_lock_spin(&umtx_lock);
		if (su != 0)
			uq->uq_inherited_pri = new_inherited_pri;
		pri = PRI_MAX;
		TAILQ_FOREACH(pi, &uq->uq_pi_contested, pi_link) {
			uq2 = TAILQ_FIRST(&pi->pi_blocked);
			if (uq2 != NULL) {
				if (pri > UPRI(uq2->uq_thread))
					pri = UPRI(uq2->uq_thread);
			}
		}
		if (pri > uq->uq_inherited_pri)
			pri = uq->uq_inherited_pri;
		thread_lock(td);
		sched_lend_user_prio(td, pri);
		thread_unlock(td);
		mtx_unlock_spin(&umtx_lock);
	}
	umtx_key_release(&key);
	return (error);
}

static int
do_set_ceiling(struct thread *td, struct umutex *m, uint32_t ceiling,
	uint32_t *old_ceiling)
{
	struct umtx_q *uq;
	uint32_t save_ceiling;
	uint32_t owner, id;
	uint32_t flags;
	int error, rv;

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((flags & UMUTEX_PRIO_PROTECT) == 0)
		return (EINVAL);
	if (ceiling > RTP_PRIO_MAX)
		return (EINVAL);
	id = td->td_tid;
	uq = td->td_umtxq;
	if ((error = umtx_key_get(m, TYPE_PP_UMUTEX, GET_SHARE(flags),
	   &uq->uq_key)) != 0)
		return (error);
	for (;;) {
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		rv = fueword32(&m->m_ceilings[0], &save_ceiling);
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		rv = casueword32(&m->m_owner,
		    UMUTEX_CONTESTED, &owner, id | UMUTEX_CONTESTED);
		if (rv == -1) {
			error = EFAULT;
			break;
		}

		if (owner == UMUTEX_CONTESTED) {
			suword32(&m->m_ceilings[0], ceiling);
			suword32(&m->m_owner, UMUTEX_CONTESTED);
			error = 0;
			break;
		}

		if ((owner & ~UMUTEX_CONTESTED) == id) {
			suword32(&m->m_ceilings[0], ceiling);
			error = 0;
			break;
		}

		/*
		 * If we caught a signal, we have retried and now
		 * exit immediately.
		 */
		if (error != 0)
			break;

		/*
		 * We set the contested bit, sleep. Otherwise the lock changed
		 * and we need to retry or we lost a race to the thread
		 * unlocking the umtx.
		 */
		umtxq_lock(&uq->uq_key);
		umtxq_insert(uq);
		umtxq_unbusy(&uq->uq_key);
		error = umtxq_sleep(uq, "umtxpp", NULL);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
	}
	umtxq_lock(&uq->uq_key);
	if (error == 0)
		umtxq_signal(&uq->uq_key, INT_MAX);
	umtxq_unbusy(&uq->uq_key);
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	if (error == 0 && old_ceiling != NULL)
		suword32(old_ceiling, save_ceiling);
	return (error);
}

/*
 * Lock a userland POSIX mutex.
 */
static int
do_lock_umutex(struct thread *td, struct umutex *m,
    struct _umtx_time *timeout, int mode)
{
	uint32_t flags;
	int error;

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);

	switch(flags & (UMUTEX_PRIO_INHERIT | UMUTEX_PRIO_PROTECT)) {
	case 0:
		error = do_lock_normal(td, m, flags, timeout, mode);
		break;
	case UMUTEX_PRIO_INHERIT:
		error = do_lock_pi(td, m, flags, timeout, mode);
		break;
	case UMUTEX_PRIO_PROTECT:
		error = do_lock_pp(td, m, flags, timeout, mode);
		break;
	default:
		return (EINVAL);
	}
	if (timeout == NULL) {
		if (error == EINTR && mode != _UMUTEX_WAIT)
			error = ERESTART;
	} else {
		/* Timed-locking is not restarted. */
		if (error == ERESTART)
			error = EINTR;
	}
	return (error);
}

/*
 * Unlock a userland POSIX mutex.
 */
static int
do_unlock_umutex(struct thread *td, struct umutex *m)
{
	uint32_t flags;
	int error;

	error = fueword32(&m->m_flags, &flags);
	if (error == -1)
		return (EFAULT);

	switch(flags & (UMUTEX_PRIO_INHERIT | UMUTEX_PRIO_PROTECT)) {
	case 0:
		return (do_unlock_normal(td, m, flags));
	case UMUTEX_PRIO_INHERIT:
		return (do_unlock_pi(td, m, flags));
	case UMUTEX_PRIO_PROTECT:
		return (do_unlock_pp(td, m, flags));
	}

	return (EINVAL);
}

static int
do_cv_wait(struct thread *td, struct ucond *cv, struct umutex *m,
	struct timespec *timeout, u_long wflags)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags, clockid, hasw;
	int error;

	uq = td->td_umtxq;
	error = fueword32(&cv->c_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(cv, TYPE_CV, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if ((wflags & CVWAIT_CLOCKID) != 0) {
		error = fueword32(&cv->c_clockid, &clockid);
		if (error == -1) {
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}
		if (clockid < CLOCK_REALTIME ||
		    clockid >= CLOCK_THREAD_CPUTIME_ID) {
			/* hmm, only HW clock id will work. */
			umtx_key_release(&uq->uq_key);
			return (EINVAL);
		}
	} else {
		clockid = CLOCK_REALTIME;
	}

	umtxq_lock(&uq->uq_key);
	umtxq_busy(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);

	/*
	 * Set c_has_waiters to 1 before releasing user mutex, also
	 * don't modify cache line when unnecessary.
	 */
	error = fueword32(&cv->c_has_waiters, &hasw);
	if (error == 0 && hasw == 0)
		suword32(&cv->c_has_waiters, 1);

	umtxq_unbusy_unlocked(&uq->uq_key);

	error = do_unlock_umutex(td, m);

	if (timeout != NULL)
		abs_timeout_init(&timo, clockid, ((wflags & CVWAIT_ABSTIME) != 0),
			timeout);
	
	umtxq_lock(&uq->uq_key);
	if (error == 0) {
		error = umtxq_sleep(uq, "ucond", timeout == NULL ?
		    NULL : &timo);
	}

	if ((uq->uq_flags & UQF_UMTXQ) == 0)
		error = 0;
	else {
		/*
		 * This must be timeout,interrupted by signal or
		 * surprious wakeup, clear c_has_waiter flag when
		 * necessary.
		 */
		umtxq_busy(&uq->uq_key);
		if ((uq->uq_flags & UQF_UMTXQ) != 0) {
			int oldlen = uq->uq_cur_queue->length;
			umtxq_remove(uq);
			if (oldlen == 1) {
				umtxq_unlock(&uq->uq_key);
				suword32(&cv->c_has_waiters, 0);
				umtxq_lock(&uq->uq_key);
			}
		}
		umtxq_unbusy(&uq->uq_key);
		if (error == ERESTART)
			error = EINTR;
	}

	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Signal a userland condition variable.
 */
static int
do_cv_signal(struct thread *td, struct ucond *cv)
{
	struct umtx_key key;
	int error, cnt, nwake;
	uint32_t flags;

	error = fueword32(&cv->c_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((error = umtx_key_get(cv, TYPE_CV, GET_SHARE(flags), &key)) != 0)
		return (error);	
	umtxq_lock(&key);
	umtxq_busy(&key);
	cnt = umtxq_count(&key);
	nwake = umtxq_signal(&key, 1);
	if (cnt <= nwake) {
		umtxq_unlock(&key);
		error = suword32(&cv->c_has_waiters, 0);
		if (error == -1)
			error = EFAULT;
		umtxq_lock(&key);
	}
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

static int
do_cv_broadcast(struct thread *td, struct ucond *cv)
{
	struct umtx_key key;
	int error;
	uint32_t flags;

	error = fueword32(&cv->c_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((error = umtx_key_get(cv, TYPE_CV, GET_SHARE(flags), &key)) != 0)
		return (error);	

	umtxq_lock(&key);
	umtxq_busy(&key);
	umtxq_signal(&key, INT_MAX);
	umtxq_unlock(&key);

	error = suword32(&cv->c_has_waiters, 0);
	if (error == -1)
		error = EFAULT;

	umtxq_unbusy_unlocked(&key);

	umtx_key_release(&key);
	return (error);
}

static int
do_rw_rdlock(struct thread *td, struct urwlock *rwlock, long fflag, struct _umtx_time *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags, wrflags;
	int32_t state, oldstate;
	int32_t blocked_readers;
	int error, rv;

	uq = td->td_umtxq;
	error = fueword32(&rwlock->rw_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(rwlock, TYPE_RWLOCK, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	wrflags = URWLOCK_WRITE_OWNER;
	if (!(fflag & URWLOCK_PREFER_READER) && !(flags & URWLOCK_PREFER_READER))
		wrflags |= URWLOCK_WRITE_WAITERS;

	for (;;) {
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1) {
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}

		/* try to lock it */
		while (!(state & wrflags)) {
			if (__predict_false(URWLOCK_READER_COUNT(state) == URWLOCK_MAX_READERS)) {
				umtx_key_release(&uq->uq_key);
				return (EAGAIN);
			}
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state + 1);
			if (rv == -1) {
				umtx_key_release(&uq->uq_key);
				return (EFAULT);
			}
			if (oldstate == state) {
				umtx_key_release(&uq->uq_key);
				return (0);
			}
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
			state = oldstate;
		}

		if (error)
			break;

		/* grab monitor lock */
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * re-read the state, in case it changed between the try-lock above
		 * and the check below
		 */
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1)
			error = EFAULT;

		/* set read contention bit */
		while (error == 0 && (state & wrflags) &&
		    !(state & URWLOCK_READ_WAITERS)) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state | URWLOCK_READ_WAITERS);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
			if (oldstate == state)
				goto sleep;
			state = oldstate;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
		if (error != 0) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			break;
		}

		/* state is changed while setting flags, restart */
		if (!(state & wrflags)) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
			continue;
		}

sleep:
		/* contention bit is set, before sleeping, increase read waiter count */
		rv = fueword32(&rwlock->rw_blocked_readers,
		    &blocked_readers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_readers, blocked_readers+1);

		while (state & wrflags) {
			umtxq_lock(&uq->uq_key);
			umtxq_insert(uq);
			umtxq_unbusy(&uq->uq_key);

			error = umtxq_sleep(uq, "urdlck", timeout == NULL ?
			    NULL : &timo);

			umtxq_busy(&uq->uq_key);
			umtxq_remove(uq);
			umtxq_unlock(&uq->uq_key);
			if (error)
				break;
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
		}

		/* decrease read waiter count, and may clear read contention bit */
		rv = fueword32(&rwlock->rw_blocked_readers,
		    &blocked_readers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_readers, blocked_readers-1);
		if (blocked_readers == 1) {
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1)
				error = EFAULT;
			while (error == 0) {
				rv = casueword32(&rwlock->rw_state, state,
				    &oldstate, state & ~URWLOCK_READ_WAITERS);
				if (rv == -1) {
					error = EFAULT;
					break;
				}
				if (oldstate == state)
					break;
				state = oldstate;
				error = umtxq_check_susp(td);
			}
		}

		umtxq_unbusy_unlocked(&uq->uq_key);
		if (error != 0)
			break;
	}
	umtx_key_release(&uq->uq_key);
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

static int
do_rw_wrlock(struct thread *td, struct urwlock *rwlock, struct _umtx_time *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags;
	int32_t state, oldstate;
	int32_t blocked_writers;
	int32_t blocked_readers;
	int error, rv;

	uq = td->td_umtxq;
	error = fueword32(&rwlock->rw_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(rwlock, TYPE_RWLOCK, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	blocked_readers = 0;
	for (;;) {
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1) {
			umtx_key_release(&uq->uq_key);
			return (EFAULT);
		}
		while (!(state & URWLOCK_WRITE_OWNER) && URWLOCK_READER_COUNT(state) == 0) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state | URWLOCK_WRITE_OWNER);
			if (rv == -1) {
				umtx_key_release(&uq->uq_key);
				return (EFAULT);
			}
			if (oldstate == state) {
				umtx_key_release(&uq->uq_key);
				return (0);
			}
			state = oldstate;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}

		if (error) {
			if (!(state & (URWLOCK_WRITE_OWNER|URWLOCK_WRITE_WAITERS)) &&
			    blocked_readers != 0) {
				umtxq_lock(&uq->uq_key);
				umtxq_busy(&uq->uq_key);
				umtxq_signal_queue(&uq->uq_key, INT_MAX, UMTX_SHARED_QUEUE);
				umtxq_unbusy(&uq->uq_key);
				umtxq_unlock(&uq->uq_key);
			}

			break;
		}

		/* grab monitor lock */
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);

		/*
		 * re-read the state, in case it changed between the try-lock above
		 * and the check below
		 */
		rv = fueword32(&rwlock->rw_state, &state);
		if (rv == -1)
			error = EFAULT;

		while (error == 0 && ((state & URWLOCK_WRITE_OWNER) ||
		    URWLOCK_READER_COUNT(state) != 0) &&
		    (state & URWLOCK_WRITE_WAITERS) == 0) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state | URWLOCK_WRITE_WAITERS);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
			if (oldstate == state)
				goto sleep;
			state = oldstate;
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
		}
		if (error != 0) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			break;
		}

		if (!(state & URWLOCK_WRITE_OWNER) && URWLOCK_READER_COUNT(state) == 0) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = umtxq_check_susp(td);
			if (error != 0)
				break;
			continue;
		}
sleep:
		rv = fueword32(&rwlock->rw_blocked_writers,
		    &blocked_writers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_writers, blocked_writers+1);

		while ((state & URWLOCK_WRITE_OWNER) || URWLOCK_READER_COUNT(state) != 0) {
			umtxq_lock(&uq->uq_key);
			umtxq_insert_queue(uq, UMTX_EXCLUSIVE_QUEUE);
			umtxq_unbusy(&uq->uq_key);

			error = umtxq_sleep(uq, "uwrlck", timeout == NULL ?
			    NULL : &timo);

			umtxq_busy(&uq->uq_key);
			umtxq_remove_queue(uq, UMTX_EXCLUSIVE_QUEUE);
			umtxq_unlock(&uq->uq_key);
			if (error)
				break;
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1) {
				error = EFAULT;
				break;
			}
		}

		rv = fueword32(&rwlock->rw_blocked_writers,
		    &blocked_writers);
		if (rv == -1) {
			umtxq_unbusy_unlocked(&uq->uq_key);
			error = EFAULT;
			break;
		}
		suword32(&rwlock->rw_blocked_writers, blocked_writers-1);
		if (blocked_writers == 1) {
			rv = fueword32(&rwlock->rw_state, &state);
			if (rv == -1) {
				umtxq_unbusy_unlocked(&uq->uq_key);
				error = EFAULT;
				break;
			}
			for (;;) {
				rv = casueword32(&rwlock->rw_state, state,
				    &oldstate, state & ~URWLOCK_WRITE_WAITERS);
				if (rv == -1) {
					error = EFAULT;
					break;
				}
				if (oldstate == state)
					break;
				state = oldstate;
				error = umtxq_check_susp(td);
				/*
				 * We are leaving the URWLOCK_WRITE_WAITERS
				 * behind, but this should not harm the
				 * correctness.
				 */
				if (error != 0)
					break;
			}
			rv = fueword32(&rwlock->rw_blocked_readers,
			    &blocked_readers);
			if (rv == -1) {
				umtxq_unbusy_unlocked(&uq->uq_key);
				error = EFAULT;
				break;
			}
		} else
			blocked_readers = 0;

		umtxq_unbusy_unlocked(&uq->uq_key);
	}

	umtx_key_release(&uq->uq_key);
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

static int
do_rw_unlock(struct thread *td, struct urwlock *rwlock)
{
	struct umtx_q *uq;
	uint32_t flags;
	int32_t state, oldstate;
	int error, rv, q, count;

	uq = td->td_umtxq;
	error = fueword32(&rwlock->rw_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(rwlock, TYPE_RWLOCK, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	error = fueword32(&rwlock->rw_state, &state);
	if (error == -1) {
		error = EFAULT;
		goto out;
	}
	if (state & URWLOCK_WRITE_OWNER) {
		for (;;) {
			rv = casueword32(&rwlock->rw_state, state, 
			    &oldstate, state & ~URWLOCK_WRITE_OWNER);
			if (rv == -1) {
				error = EFAULT;
				goto out;
			}
			if (oldstate != state) {
				state = oldstate;
				if (!(oldstate & URWLOCK_WRITE_OWNER)) {
					error = EPERM;
					goto out;
				}
				error = umtxq_check_susp(td);
				if (error != 0)
					goto out;
			} else
				break;
		}
	} else if (URWLOCK_READER_COUNT(state) != 0) {
		for (;;) {
			rv = casueword32(&rwlock->rw_state, state,
			    &oldstate, state - 1);
			if (rv == -1) {
				error = EFAULT;
				goto out;
			}
			if (oldstate != state) {
				state = oldstate;
				if (URWLOCK_READER_COUNT(oldstate) == 0) {
					error = EPERM;
					goto out;
				}
				error = umtxq_check_susp(td);
				if (error != 0)
					goto out;
			} else
				break;
		}
	} else {
		error = EPERM;
		goto out;
	}

	count = 0;

	if (!(flags & URWLOCK_PREFER_READER)) {
		if (state & URWLOCK_WRITE_WAITERS) {
			count = 1;
			q = UMTX_EXCLUSIVE_QUEUE;
		} else if (state & URWLOCK_READ_WAITERS) {
			count = INT_MAX;
			q = UMTX_SHARED_QUEUE;
		}
	} else {
		if (state & URWLOCK_READ_WAITERS) {
			count = INT_MAX;
			q = UMTX_SHARED_QUEUE;
		} else if (state & URWLOCK_WRITE_WAITERS) {
			count = 1;
			q = UMTX_EXCLUSIVE_QUEUE;
		}
	}

	if (count) {
		umtxq_lock(&uq->uq_key);
		umtxq_busy(&uq->uq_key);
		umtxq_signal_queue(&uq->uq_key, count, q);
		umtxq_unbusy(&uq->uq_key);
		umtxq_unlock(&uq->uq_key);
	}
out:
	umtx_key_release(&uq->uq_key);
	return (error);
}

static int
do_sem_wait(struct thread *td, struct _usem *sem, struct _umtx_time *timeout)
{
	struct abs_timeout timo;
	struct umtx_q *uq;
	uint32_t flags, count, count1;
	int error, rv;

	uq = td->td_umtxq;
	error = fueword32(&sem->_flags, &flags);
	if (error == -1)
		return (EFAULT);
	error = umtx_key_get(sem, TYPE_SEM, GET_SHARE(flags), &uq->uq_key);
	if (error != 0)
		return (error);

	if (timeout != NULL)
		abs_timeout_init2(&timo, timeout);

	umtxq_lock(&uq->uq_key);
	umtxq_busy(&uq->uq_key);
	umtxq_insert(uq);
	umtxq_unlock(&uq->uq_key);
	rv = casueword32(&sem->_has_waiters, 0, &count1, 1);
	if (rv == 0)
		rv = fueword32(&sem->_count, &count);
	if (rv == -1 || count != 0) {
		umtxq_lock(&uq->uq_key);
		umtxq_unbusy(&uq->uq_key);
		umtxq_remove(uq);
		umtxq_unlock(&uq->uq_key);
		umtx_key_release(&uq->uq_key);
		return (rv == -1 ? EFAULT : 0);
	}
	umtxq_lock(&uq->uq_key);
	umtxq_unbusy(&uq->uq_key);

	error = umtxq_sleep(uq, "usem", timeout == NULL ? NULL : &timo);

	if ((uq->uq_flags & UQF_UMTXQ) == 0)
		error = 0;
	else {
		umtxq_remove(uq);
		/* A relative timeout cannot be restarted. */
		if (error == ERESTART && timeout != NULL &&
		    (timeout->_flags & UMTX_ABSTIME) == 0)
			error = EINTR;
	}
	umtxq_unlock(&uq->uq_key);
	umtx_key_release(&uq->uq_key);
	return (error);
}

/*
 * Signal a userland condition variable.
 */
static int
do_sem_wake(struct thread *td, struct _usem *sem)
{
	struct umtx_key key;
	int error, cnt;
	uint32_t flags;

	error = fueword32(&sem->_flags, &flags);
	if (error == -1)
		return (EFAULT);
	if ((error = umtx_key_get(sem, TYPE_SEM, GET_SHARE(flags), &key)) != 0)
		return (error);	
	umtxq_lock(&key);
	umtxq_busy(&key);
	cnt = umtxq_count(&key);
	if (cnt > 0) {
		umtxq_signal(&key, 1);
		/*
		 * Check if count is greater than 0, this means the memory is
		 * still being referenced by user code, so we can safely
		 * update _has_waiters flag.
		 */
		if (cnt == 1) {
			umtxq_unlock(&key);
			error = suword32(&sem->_has_waiters, 0);
			umtxq_lock(&key);
			if (error == -1)
				error = EFAULT;
		}
	}
	umtxq_unbusy(&key);
	umtxq_unlock(&key);
	umtx_key_release(&key);
	return (error);
}

int
sys__umtx_lock(struct thread *td, struct _umtx_lock_args *uap)
    /* struct umtx *umtx */
{
	return do_lock_umtx(td, uap->umtx, td->td_tid, 0);
}

int
sys__umtx_unlock(struct thread *td, struct _umtx_unlock_args *uap)
    /* struct umtx *umtx */
{
	return do_unlock_umtx(td, uap->umtx, td->td_tid);
}

inline int
umtx_copyin_timeout(const void *addr, struct timespec *tsp)
{
	int error;

	error = copyin(addr, tsp, sizeof(struct timespec));
	if (error == 0) {
		if (tsp->tv_sec < 0 ||
		    tsp->tv_nsec >= 1000000000 ||
		    tsp->tv_nsec < 0)
			error = EINVAL;
	}
	return (error);
}

static inline int
umtx_copyin_umtx_time(const void *addr, size_t size, struct _umtx_time *tp)
{
	int error;
	
	if (size <= sizeof(struct timespec)) {
		tp->_clockid = CLOCK_REALTIME;
		tp->_flags = 0;
		error = copyin(addr, &tp->_timeout, sizeof(struct timespec));
	} else 
		error = copyin(addr, tp, sizeof(struct _umtx_time));
	if (error != 0)
		return (error);
	if (tp->_timeout.tv_sec < 0 ||
	    tp->_timeout.tv_nsec >= 1000000000 || tp->_timeout.tv_nsec < 0)
		return (EINVAL);
	return (0);
}

static int
__umtx_op_lock_umtx(struct thread *td, struct _umtx_op_args *uap)
{
	struct timespec *ts, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		ts = NULL;
	else {
		error = umtx_copyin_timeout(uap->uaddr2, &timeout);
		if (error != 0)
			return (error);
		ts = &timeout;
	}
	return (do_lock_umtx(td, uap->obj, uap->val, ts));
}

static int
__umtx_op_unlock_umtx(struct thread *td, struct _umtx_op_args *uap)
{
	return (do_unlock_umtx(td, uap->obj, uap->val));
}

static int
__umtx_op_wait(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout, *tm_p;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_wait(td, uap->obj, uap->val, tm_p, 0, 0);
}

static int
__umtx_op_wait_uint(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout, *tm_p;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_wait(td, uap->obj, uap->val, tm_p, 1, 0);
}

static int
__umtx_op_wait_uint_private(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_wait(td, uap->obj, uap->val, tm_p, 1, 1);
}

static int
__umtx_op_wake(struct thread *td, struct _umtx_op_args *uap)
{
	return (kern_umtx_wake(td, uap->obj, uap->val, 0));
}

#define BATCH_SIZE	128
static int
__umtx_op_nwake_private(struct thread *td, struct _umtx_op_args *uap)
{
	int count = uap->val;
	void *uaddrs[BATCH_SIZE];
	char **upp = (char **)uap->obj;
	int tocopy;
	int error = 0;
	int i, pos = 0;

	while (count > 0) {
		tocopy = count;
		if (tocopy > BATCH_SIZE)
			tocopy = BATCH_SIZE;
		error = copyin(upp+pos, uaddrs, tocopy * sizeof(char *));
		if (error != 0)
			break;
		for (i = 0; i < tocopy; ++i)
			kern_umtx_wake(td, uaddrs[i], INT_MAX, 1);
		count -= tocopy;
		pos += tocopy;
	}
	return (error);
}

static int
__umtx_op_wake_private(struct thread *td, struct _umtx_op_args *uap)
{
	return (kern_umtx_wake(td, uap->obj, uap->val, 1));
}

static int
__umtx_op_lock_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_lock_umutex(td, uap->obj, tm_p, 0);
}

static int
__umtx_op_trylock_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	return do_lock_umutex(td, uap->obj, NULL, _UMUTEX_TRY);
}

static int
__umtx_op_wait_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_lock_umutex(td, uap->obj, tm_p, _UMUTEX_WAIT);
}

static int
__umtx_op_wake_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	return do_wake_umutex(td, uap->obj);
}

static int
__umtx_op_unlock_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	return do_unlock_umutex(td, uap->obj);
}

static int
__umtx_op_set_ceiling(struct thread *td, struct _umtx_op_args *uap)
{
	return do_set_ceiling(td, uap->obj, uap->val, uap->uaddr1);
}

static int
__umtx_op_cv_wait(struct thread *td, struct _umtx_op_args *uap)
{
	struct timespec *ts, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		ts = NULL;
	else {
		error = umtx_copyin_timeout(uap->uaddr2, &timeout);
		if (error != 0)
			return (error);
		ts = &timeout;
	}
	return (do_cv_wait(td, uap->obj, uap->uaddr1, ts, uap->val));
}

static int
__umtx_op_cv_signal(struct thread *td, struct _umtx_op_args *uap)
{
	return do_cv_signal(td, uap->obj);
}

static int
__umtx_op_cv_broadcast(struct thread *td, struct _umtx_op_args *uap)
{
	return do_cv_broadcast(td, uap->obj);
}

static int
__umtx_op_rw_rdlock(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_rdlock(td, uap->obj, uap->val, 0);
	} else {
		error = umtx_copyin_umtx_time(uap->uaddr2,
		   (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		error = do_rw_rdlock(td, uap->obj, uap->val, &timeout);
	}
	return (error);
}

static int
__umtx_op_rw_wrlock(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_wrlock(td, uap->obj, 0);
	} else {
		error = umtx_copyin_umtx_time(uap->uaddr2, 
		   (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);

		error = do_rw_wrlock(td, uap->obj, &timeout);
	}
	return (error);
}

static int
__umtx_op_rw_unlock(struct thread *td, struct _umtx_op_args *uap)
{
	return do_rw_unlock(td, uap->obj);
}

static int
__umtx_op_sem_wait(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(
		    uap->uaddr2, (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_sem_wait(td, uap->obj, tm_p));
}

static int
__umtx_op_sem_wake(struct thread *td, struct _umtx_op_args *uap)
{
	return do_sem_wake(td, uap->obj);
}

static int
__umtx_op_wake2_umutex(struct thread *td, struct _umtx_op_args *uap)
{
	return do_wake2_umutex(td, uap->obj, uap->val);
}

typedef int (*_umtx_op_func)(struct thread *td, struct _umtx_op_args *uap);

static _umtx_op_func op_table[] = {
	__umtx_op_lock_umtx,		/* UMTX_OP_LOCK */
	__umtx_op_unlock_umtx,		/* UMTX_OP_UNLOCK */
	__umtx_op_wait,			/* UMTX_OP_WAIT */
	__umtx_op_wake,			/* UMTX_OP_WAKE */
	__umtx_op_trylock_umutex,	/* UMTX_OP_MUTEX_TRYLOCK */
	__umtx_op_lock_umutex,		/* UMTX_OP_MUTEX_LOCK */
	__umtx_op_unlock_umutex,	/* UMTX_OP_MUTEX_UNLOCK */
	__umtx_op_set_ceiling,		/* UMTX_OP_SET_CEILING */
	__umtx_op_cv_wait,		/* UMTX_OP_CV_WAIT*/
	__umtx_op_cv_signal,		/* UMTX_OP_CV_SIGNAL */
	__umtx_op_cv_broadcast,		/* UMTX_OP_CV_BROADCAST */
	__umtx_op_wait_uint,		/* UMTX_OP_WAIT_UINT */
	__umtx_op_rw_rdlock,		/* UMTX_OP_RW_RDLOCK */
	__umtx_op_rw_wrlock,		/* UMTX_OP_RW_WRLOCK */
	__umtx_op_rw_unlock,		/* UMTX_OP_RW_UNLOCK */
	__umtx_op_wait_uint_private,	/* UMTX_OP_WAIT_UINT_PRIVATE */
	__umtx_op_wake_private,		/* UMTX_OP_WAKE_PRIVATE */
	__umtx_op_wait_umutex,		/* UMTX_OP_UMUTEX_WAIT */
	__umtx_op_wake_umutex,		/* UMTX_OP_UMUTEX_WAKE */
	__umtx_op_sem_wait,		/* UMTX_OP_SEM_WAIT */
	__umtx_op_sem_wake,		/* UMTX_OP_SEM_WAKE */
	__umtx_op_nwake_private,	/* UMTX_OP_NWAKE_PRIVATE */
	__umtx_op_wake2_umutex		/* UMTX_OP_UMUTEX_WAKE2 */
};

int
sys__umtx_op(struct thread *td, struct _umtx_op_args *uap)
{
	if ((unsigned)uap->op < UMTX_OP_MAX)
		return (*op_table[uap->op])(td, uap);
	return (EINVAL);
}

#ifdef COMPAT_FREEBSD32
int
freebsd32_umtx_lock(struct thread *td, struct freebsd32_umtx_lock_args *uap)
    /* struct umtx *umtx */
{
	return (do_lock_umtx32(td, (uint32_t *)uap->umtx, td->td_tid, NULL));
}

int
freebsd32_umtx_unlock(struct thread *td, struct freebsd32_umtx_unlock_args *uap)
    /* struct umtx *umtx */
{
	return (do_unlock_umtx32(td, (uint32_t *)uap->umtx, td->td_tid));
}

struct timespec32 {
	int32_t tv_sec;
	int32_t tv_nsec;
};

struct umtx_time32 {
	struct	timespec32	timeout;
	uint32_t		flags;
	uint32_t		clockid;
};

static inline int
umtx_copyin_timeout32(void *addr, struct timespec *tsp)
{
	struct timespec32 ts32;
	int error;

	error = copyin(addr, &ts32, sizeof(struct timespec32));
	if (error == 0) {
		if (ts32.tv_sec < 0 ||
		    ts32.tv_nsec >= 1000000000 ||
		    ts32.tv_nsec < 0)
			error = EINVAL;
		else {
			tsp->tv_sec = ts32.tv_sec;
			tsp->tv_nsec = ts32.tv_nsec;
		}
	}
	return (error);
}

static inline int
umtx_copyin_umtx_time32(const void *addr, size_t size, struct _umtx_time *tp)
{
	struct umtx_time32 t32;
	int error;
	
	t32.clockid = CLOCK_REALTIME;
	t32.flags   = 0;
	if (size <= sizeof(struct timespec32))
		error = copyin(addr, &t32.timeout, sizeof(struct timespec32));
	else 
		error = copyin(addr, &t32, sizeof(struct umtx_time32));
	if (error != 0)
		return (error);
	if (t32.timeout.tv_sec < 0 ||
	    t32.timeout.tv_nsec >= 1000000000 || t32.timeout.tv_nsec < 0)
		return (EINVAL);
	tp->_timeout.tv_sec = t32.timeout.tv_sec;
	tp->_timeout.tv_nsec = t32.timeout.tv_nsec;
	tp->_flags = t32.flags;
	tp->_clockid = t32.clockid;
	return (0);
}

static int
__umtx_op_lock_umtx_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct timespec *ts, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		ts = NULL;
	else {
		error = umtx_copyin_timeout32(uap->uaddr2, &timeout);
		if (error != 0)
			return (error);
		ts = &timeout;
	}
	return (do_lock_umtx32(td, uap->obj, uap->val, ts));
}

static int
__umtx_op_unlock_umtx_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	return (do_unlock_umtx32(td, uap->obj, (uint32_t)uap->val));
}

static int
__umtx_op_wait_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
			(size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_wait(td, uap->obj, uap->val, tm_p, 1, 0);
}

static int
__umtx_op_lock_umutex_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time(uap->uaddr2,
			    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_lock_umutex(td, uap->obj, tm_p, 0);
}

static int
__umtx_op_wait_umutex_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(uap->uaddr2, 
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_lock_umutex(td, uap->obj, tm_p, _UMUTEX_WAIT);
}

static int
__umtx_op_cv_wait_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct timespec *ts, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		ts = NULL;
	else {
		error = umtx_copyin_timeout32(uap->uaddr2, &timeout);
		if (error != 0)
			return (error);
		ts = &timeout;
	}
	return (do_cv_wait(td, uap->obj, uap->uaddr1, ts, uap->val));
}

static int
__umtx_op_rw_rdlock_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_rdlock(td, uap->obj, uap->val, 0);
	} else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		error = do_rw_rdlock(td, uap->obj, uap->val, &timeout);
	}
	return (error);
}

static int
__umtx_op_rw_wrlock_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL) {
		error = do_rw_wrlock(td, uap->obj, 0);
	} else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		error = do_rw_wrlock(td, uap->obj, &timeout);
	}
	return (error);
}

static int
__umtx_op_wait_uint_private_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(
		    uap->uaddr2, (size_t)uap->uaddr1,&timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return do_wait(td, uap->obj, uap->val, tm_p, 1, 1);
}

static int
__umtx_op_sem_wait_compat32(struct thread *td, struct _umtx_op_args *uap)
{
	struct _umtx_time *tm_p, timeout;
	int error;

	/* Allow a null timespec (wait forever). */
	if (uap->uaddr2 == NULL)
		tm_p = NULL;
	else {
		error = umtx_copyin_umtx_time32(uap->uaddr2,
		    (size_t)uap->uaddr1, &timeout);
		if (error != 0)
			return (error);
		tm_p = &timeout;
	}
	return (do_sem_wait(td, uap->obj, tm_p));
}

static int
__umtx_op_nwake_private32(struct thread *td, struct _umtx_op_args *uap)
{
	int count = uap->val;
	uint32_t uaddrs[BATCH_SIZE];
	uint32_t **upp = (uint32_t **)uap->obj;
	int tocopy;
	int error = 0;
	int i, pos = 0;

	while (count > 0) {
		tocopy = count;
		if (tocopy > BATCH_SIZE)
			tocopy = BATCH_SIZE;
		error = copyin(upp+pos, uaddrs, tocopy * sizeof(uint32_t));
		if (error != 0)
			break;
		for (i = 0; i < tocopy; ++i)
			kern_umtx_wake(td, (void *)(intptr_t)uaddrs[i],
				INT_MAX, 1);
		count -= tocopy;
		pos += tocopy;
	}
	return (error);
}

static _umtx_op_func op_table_compat32[] = {
	__umtx_op_lock_umtx_compat32,	/* UMTX_OP_LOCK */
	__umtx_op_unlock_umtx_compat32,	/* UMTX_OP_UNLOCK */
	__umtx_op_wait_compat32,	/* UMTX_OP_WAIT */
	__umtx_op_wake,			/* UMTX_OP_WAKE */
	__umtx_op_trylock_umutex,	/* UMTX_OP_MUTEX_LOCK */
	__umtx_op_lock_umutex_compat32,	/* UMTX_OP_MUTEX_TRYLOCK */
	__umtx_op_unlock_umutex,	/* UMTX_OP_MUTEX_UNLOCK	*/
	__umtx_op_set_ceiling,		/* UMTX_OP_SET_CEILING */
	__umtx_op_cv_wait_compat32,	/* UMTX_OP_CV_WAIT*/
	__umtx_op_cv_signal,		/* UMTX_OP_CV_SIGNAL */
	__umtx_op_cv_broadcast,		/* UMTX_OP_CV_BROADCAST */
	__umtx_op_wait_compat32,	/* UMTX_OP_WAIT_UINT */
	__umtx_op_rw_rdlock_compat32,	/* UMTX_OP_RW_RDLOCK */
	__umtx_op_rw_wrlock_compat32,	/* UMTX_OP_RW_WRLOCK */
	__umtx_op_rw_unlock,		/* UMTX_OP_RW_UNLOCK */
	__umtx_op_wait_uint_private_compat32,	/* UMTX_OP_WAIT_UINT_PRIVATE */
	__umtx_op_wake_private,		/* UMTX_OP_WAKE_PRIVATE */
	__umtx_op_wait_umutex_compat32, /* UMTX_OP_UMUTEX_WAIT */
	__umtx_op_wake_umutex,		/* UMTX_OP_UMUTEX_WAKE */
	__umtx_op_sem_wait_compat32,	/* UMTX_OP_SEM_WAIT */
	__umtx_op_sem_wake,		/* UMTX_OP_SEM_WAKE */
	__umtx_op_nwake_private32,	/* UMTX_OP_NWAKE_PRIVATE */
	__umtx_op_wake2_umutex		/* UMTX_OP_UMUTEX_WAKE2 */
};

int
freebsd32_umtx_op(struct thread *td, struct freebsd32_umtx_op_args *uap)
{
	if ((unsigned)uap->op < UMTX_OP_MAX)
		return (*op_table_compat32[uap->op])(td,
			(struct _umtx_op_args *)uap);
	return (EINVAL);
}
#endif

void
umtx_thread_init(struct thread *td)
{
	td->td_umtxq = umtxq_alloc();
	td->td_umtxq->uq_thread = td;
}

void
umtx_thread_fini(struct thread *td)
{
	umtxq_free(td->td_umtxq);
}

/*
 * It will be called when new thread is created, e.g fork().
 */
void
umtx_thread_alloc(struct thread *td)
{
	struct umtx_q *uq;

	uq = td->td_umtxq;
	uq->uq_inherited_pri = PRI_MAX;

	KASSERT(uq->uq_flags == 0, ("uq_flags != 0"));
	KASSERT(uq->uq_thread == td, ("uq_thread != td"));
	KASSERT(uq->uq_pi_blocked == NULL, ("uq_pi_blocked != NULL"));
	KASSERT(TAILQ_EMPTY(&uq->uq_pi_contested), ("uq_pi_contested is not empty"));
}

/*
 * exec() hook.
 */
static void
umtx_exec_hook(void *arg __unused, struct proc *p __unused,
	struct image_params *imgp __unused)
{
	umtx_thread_cleanup(curthread);
}

/*
 * thread_exit() hook.
 */
void
umtx_thread_exit(struct thread *td)
{
	umtx_thread_cleanup(td);
}

/*
 * clean up umtx data.
 */
static void
umtx_thread_cleanup(struct thread *td)
{
	struct umtx_q *uq;
	struct umtx_pi *pi;

	if ((uq = td->td_umtxq) == NULL)
		return;

	mtx_lock_spin(&umtx_lock);
	uq->uq_inherited_pri = PRI_MAX;
	while ((pi = TAILQ_FIRST(&uq->uq_pi_contested)) != NULL) {
		pi->pi_owner = NULL;
		TAILQ_REMOVE(&uq->uq_pi_contested, pi, pi_link);
	}
	mtx_unlock_spin(&umtx_lock);
	thread_lock(td);
	sched_lend_user_prio(td, PRI_MAX);
	thread_unlock(td);
}
