/*	$FreeBSD$	*/
/*	$OpenBSD: pf_ioctl.c,v 1.81.2.2 2004/04/30 23:28:58 brad Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#ifdef __FreeBSD__
#include "opt_inet.h"
#include "opt_inet6.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/malloc.h>
#ifdef __FreeBSD__
#include <sys/conf.h>
#else
#include <sys/timeout.h>
#include <sys/pool.h>
#endif

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>

#include <net/pfvar.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet/in_pcb.h>
#if defined(__FreeBSD__) && (__FreeBSD_version < 501108)
#include <netinet6/ip6protosw.h>
#endif
#endif /* INET6 */

#ifdef ALTQ
#include <altq/altq.h>
#endif

#ifdef __FreeBSD__
#if (__FreeBSD_version >= 500112)
#include <sys/limits.h>
#else
#include <machine/limits.h>
#endif
#include <sys/lock.h>
#include <sys/mutex.h>
#if __FreeBSD_version < 501108
#include <sys/protosw.h>
#endif
#include <net/pfil.h>
#endif /* __FreeBSD__ */

#ifdef __FreeBSD__
void			 init_zone_var(void);
void			 cleanup_pf_zone(void);
int			 pfattach(void);
#else
void			 pfattach(int);
int			 pfopen(dev_t, int, int, struct proc *);
int			 pfclose(dev_t, int, int, struct proc *);
#endif
struct pf_pool		*pf_get_pool(char *, char *, u_int32_t,
			    u_int8_t, u_int32_t, u_int8_t, u_int8_t, u_int8_t);
int			 pf_get_ruleset_number(u_int8_t);
void			 pf_init_ruleset(struct pf_ruleset *);
void			 pf_mv_pool(struct pf_palist *, struct pf_palist *);
void			 pf_empty_pool(struct pf_palist *);
#ifdef __FreeBSD__
int			 pfioctl(dev_t, u_long, caddr_t, int, struct thread *);
#else
int			 pfioctl(dev_t, u_long, caddr_t, int, struct proc *);
#endif

#ifdef __FreeBSD__
extern struct callout	 pf_expire_to;
#if __FreeBSD_version < 501108
extern struct protosw	 inetsw[];
#endif
#else
extern struct timeout	 pf_expire_to;
#endif

struct pf_rule		 pf_default_rule;

#define	TAGID_MAX	 50000
TAILQ_HEAD(pf_tags, pf_tagname)	pf_tags = TAILQ_HEAD_INITIALIZER(pf_tags);

#define DPFPRINTF(n, x) if (pf_status.debug >= (n)) printf x


#ifdef __FreeBSD__
static dev_t		pf_dev;

/*
 * XXX - These are new and need to be checked when moveing to a new version
 */
static int pf_beginrules(void *addr);
static int pf_commitrules(void *addr);
#ifdef ALTQ
static int pf_beginaltqs(void *addr);
static int pf_commitaltqs(void *addr);
static int pf_stopaltq(void);
#endif
static void pf_clearstates(void);
static int pf_clear_tables(void *addr);
/*
 * XXX - These are new and need to be checked when moveing to a new version
 */
 
#if (__FreeBSD_version < 501108)
static int pf_check_in(void *ip, int hlen, struct ifnet *ifp, int dir,
		struct mbuf **m);
static int pf_check_out(void *ip, int hlen, struct ifnet *ifp, int dir,
		struct mbuf **m);
#ifdef INET6
static int pf_check6_in(void *ip, int hlen, struct ifnet *ifp, int dir,
		struct mbuf **m);
static int pf_check6_out(void *ip, int hlen, struct ifnet *ifp, int dir,
		struct mbuf **m);
#endif
#else /* (__FreeBSD_version >= 501108) */
static int pf_check_in(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
static int pf_check_out(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
#ifdef INET6
static int pf_check6_in(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
static int pf_check6_out(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
#endif
#endif /* (__FreeBSD_version >= 501108) */
static int hook_pf(void);
static int dehook_pf(void);
static int shutdown_pf(void);
static int pf_load(void);
static int pf_unload(void);



static struct cdevsw pf_cdevsw = {
#if (__FreeBSD_version < 500105)
	/* open */	noopen,
	/* close */	noclose,
	/* read */	noread,
	/* write */	nowrite,
	/* ioctl */	pfioctl,
	/* poll */	nopoll,
	/* mmap */	nommap,
	/* strategy */	nostrategy,
	/* name */	PF_NAME,
	/* maj */	PF_CDEV_MAJOR,
	/* dump */	nodump,
	/* psize */	nopsize,
	/* flags */	0,
	/* kqfilter */	nokqfilter,
#elif (__FreeBSD_version < 501110)
	.d_open =	noopen,
	.d_close =	noclose,
	.d_read =	noread,
	.d_write =	nowrite,
	.d_ioctl =	pfioctl,
	.d_poll =	nopoll,
	.d_mmap =	nommap,
	.d_strategy =	nostrategy,
	.d_name =	PF_NAME,
	.d_maj =	MAJOR_AUTO, /* PF_CDEV_MAJOR */
	.d_dump =	nodump,
	.d_flags =	0,
	.d_kqfilter =	nokqfilter,
#else
	.d_ioctl =	pfioctl,
	.d_name =	PF_NAME,
	.d_version =	D_VERSION,
#endif
};

static volatile int pf_pfil_hooked = 0;
struct mtx pf_task_mtx;

void
init_pf_mutex(void)
{
	mtx_init(&pf_task_mtx, "pf task mtx", NULL, MTX_DEF);
/*
 * pf_altq_mtx is initialized at altq_subr.c.
 *
 * #if defined(ALTQ) && !defined(ALTQ3_COMPAT)
 *	mtx_init(&pf_altq_mtx, "pf altq mtx", NULL, MTX_DEF);
 * #endif
 */
}

void
destroy_pf_mutex(void)
{
	mtx_destroy(&pf_task_mtx);
/*
 * pf_altq_mtx is initialized at altq_subr.c.
 *
 * #if defined(ALTQ) && !defined(ALTQ3_COMPAT)
 * 	mtx_destroy(&pf_altq_mtx);
 * #endif
 */
}

void
init_zone_var(void)
{
	pf_tree_pl = pf_rule_pl = pf_addr_pl = NULL;
	pf_state_pl = pf_altq_pl = pf_pooladdr_pl = NULL;
	pf_frent_pl = pf_frag_pl = pf_cache_pl = pf_cent_pl = NULL;
	pf_state_scrub_pl = NULL;
	pfr_ktable_pl = pfr_kentry_pl = NULL;
}

void
cleanup_pf_zone(void)
{
	UMA_DESTROY(pf_tree_pl);
	UMA_DESTROY(pf_rule_pl);
	UMA_DESTROY(pf_addr_pl);
	UMA_DESTROY(pf_state_pl);
	UMA_DESTROY(pf_altq_pl);
	UMA_DESTROY(pf_pooladdr_pl);
	UMA_DESTROY(pf_frent_pl);
	UMA_DESTROY(pf_frag_pl);
	UMA_DESTROY(pf_cache_pl);
	UMA_DESTROY(pf_cent_pl);
	UMA_DESTROY(pfr_ktable_pl);
	UMA_DESTROY(pfr_kentry_pl);
	UMA_DESTROY(pf_state_scrub_pl);
}

int
pfattach(void)
{
	u_int32_t *my_timeout = pf_default_rule.timeout;
	int error = 1;

	do {
		UMA_CREATE(pf_tree_pl,	  struct pf_tree_node, "pftrpl");
		UMA_CREATE(pf_rule_pl,	  struct pf_rule, "pfrulepl");
		UMA_CREATE(pf_addr_pl,	  struct pf_addr_dyn, "pfaddrpl");
		UMA_CREATE(pf_state_pl,	  struct pf_state, "pfstatepl");
		UMA_CREATE(pf_altq_pl,	  struct pf_altq, "pfaltqpl");
		UMA_CREATE(pf_pooladdr_pl, struct pf_pooladdr, "pfpooladdrpl");
		UMA_CREATE(pfr_ktable_pl,  struct pfr_ktable, "pfrktable");
		UMA_CREATE(pfr_kentry_pl,  struct pfr_kentry, "pfrkentry");
		UMA_CREATE(pf_frent_pl,	  struct pf_frent, "pffrent");
		UMA_CREATE(pf_frag_pl,	  struct pf_fragment, "pffrag");
		UMA_CREATE(pf_cache_pl,	  struct pf_fragment, "pffrcache");
		UMA_CREATE(pf_cent_pl,	  struct pf_frcache, "pffrcent");
		UMA_CREATE(pf_state_scrub_pl, struct pf_state_scrub, 
		    "pfstatescrub");
		error = 0;
	} while(0);
	if (error) {
		cleanup_pf_zone();
		return (error);
	}
	pfr_initialize();
	if ( (error = pf_osfp_initialize()) ) {
		cleanup_pf_zone();
		pf_osfp_cleanup();
		return (error);
	}

	pf_pool_limits[PF_LIMIT_STATES].pp = pf_state_pl;
	pf_pool_limits[PF_LIMIT_STATES].limit = PFSTATE_HIWAT;
	pf_pool_limits[PF_LIMIT_FRAGS].pp = pf_frent_pl;
	pf_pool_limits[PF_LIMIT_FRAGS].limit = PFFRAG_FRENT_HIWAT;
	uma_zone_set_max(pf_pool_limits[PF_LIMIT_STATES].pp,
		pf_pool_limits[PF_LIMIT_STATES].limit);

	RB_INIT(&tree_lan_ext);
	RB_INIT(&tree_ext_gwy);
	TAILQ_INIT(&pf_anchors);
	pf_init_ruleset(&pf_main_ruleset); 
	TAILQ_INIT(&pf_altqs[0]);
	TAILQ_INIT(&pf_altqs[1]);
	TAILQ_INIT(&pf_pabuf);
	pf_altqs_active = &pf_altqs[0];
	pf_altqs_inactive = &pf_altqs[1];

	/* default rule should never be garbage collected */
	pf_default_rule.entries.tqe_prev = &pf_default_rule.entries.tqe_next;
	pf_default_rule.action = PF_PASS;
	pf_default_rule.nr = -1;

	/* initialize default timeouts */
	my_timeout[PFTM_TCP_FIRST_PACKET] = 120;	/* First TCP packet */
	my_timeout[PFTM_TCP_OPENING] = 30; 		/* No response yet */
	my_timeout[PFTM_TCP_ESTABLISHED] = 24*60*60;	/* Established */
	my_timeout[PFTM_TCP_CLOSING] = 15 * 60;		/* Half closed */
	my_timeout[PFTM_TCP_FIN_WAIT] = 45;		/* Got both FINs */
	my_timeout[PFTM_TCP_CLOSED] = 90;		/* Got a RST */
	my_timeout[PFTM_UDP_FIRST_PACKET] = 60;		/* First UDP packet */
	my_timeout[PFTM_UDP_SINGLE] = 30;		/* Unidirectional */
	my_timeout[PFTM_UDP_MULTIPLE] = 60;		/* Bidirectional */
	my_timeout[PFTM_ICMP_FIRST_PACKET] = 20;	/* First ICMP packet */
	my_timeout[PFTM_ICMP_ERROR_REPLY] = 10;		/* Got error response */
	my_timeout[PFTM_OTHER_FIRST_PACKET] = 60;	/* First packet */
	my_timeout[PFTM_OTHER_SINGLE] = 30;		/* Unidirectional */
	my_timeout[PFTM_OTHER_MULTIPLE] = 60;		/* Bidirectional */
	my_timeout[PFTM_FRAG] = 30;			/* Fragment expire */
	my_timeout[PFTM_INTERVAL] = 10;			/* Expire interval */

	/*
	 * XXX
	 *  The 2nd arg. 0 to callout_init(9) shoule be set to CALLOUT_MPSAFE
	 * if Gaint lock is removed from the network stack.
	 */
	callout_init(&pf_expire_to, 0);
	callout_reset(&pf_expire_to, my_timeout[PFTM_INTERVAL] * hz,
	    pf_purge_timeout, &pf_expire_to);

	pf_normalize_init();
	pf_status.debug = PF_DEBUG_URGENT;
	pf_pfil_hooked = 0;
	return (error);
}
#else /* !__FreeBSD__ */
void
pfattach(int num)
{
	u_int32_t *timeout = pf_default_rule.timeout;

	pool_init(&pf_tree_pl, sizeof(struct pf_tree_node), 0, 0, 0, "pftrpl",
	    NULL);
	pool_init(&pf_rule_pl, sizeof(struct pf_rule), 0, 0, 0, "pfrulepl",
	    &pool_allocator_nointr);
	pool_init(&pf_addr_pl, sizeof(struct pf_addr_dyn), 0, 0, 0, "pfaddrpl",
	    &pool_allocator_nointr);
	pool_init(&pf_state_pl, sizeof(struct pf_state), 0, 0, 0, "pfstatepl",
	    NULL);
	pool_init(&pf_altq_pl, sizeof(struct pf_altq), 0, 0, 0, "pfaltqpl",
	    NULL);
	pool_init(&pf_pooladdr_pl, sizeof(struct pf_pooladdr), 0, 0, 0,
	    "pfpooladdrpl", NULL);
	pfr_initialize();
	pf_osfp_initialize();

	pool_sethardlimit(&pf_state_pl, pf_pool_limits[PF_LIMIT_STATES].limit,
	    NULL, 0);

	RB_INIT(&tree_lan_ext);
	RB_INIT(&tree_ext_gwy);
	TAILQ_INIT(&pf_anchors);
	pf_init_ruleset(&pf_main_ruleset);
	TAILQ_INIT(&pf_altqs[0]);
	TAILQ_INIT(&pf_altqs[1]);
	TAILQ_INIT(&pf_pabuf);
	pf_altqs_active = &pf_altqs[0];
	pf_altqs_inactive = &pf_altqs[1];

	/* default rule should never be garbage collected */
	pf_default_rule.entries.tqe_prev = &pf_default_rule.entries.tqe_next;
	pf_default_rule.action = PF_PASS;
	pf_default_rule.nr = -1;

	/* initialize default timeouts */
	timeout[PFTM_TCP_FIRST_PACKET] = 120;		/* First TCP packet */
	timeout[PFTM_TCP_OPENING] = 30; 		/* No response yet */
	timeout[PFTM_TCP_ESTABLISHED] = 24*60*60;	/* Established */
	timeout[PFTM_TCP_CLOSING] = 15 * 60;		/* Half closed */
	timeout[PFTM_TCP_FIN_WAIT] = 45;		/* Got both FINs */
	timeout[PFTM_TCP_CLOSED] = 90;			/* Got a RST */
	timeout[PFTM_UDP_FIRST_PACKET] = 60;		/* First UDP packet */
	timeout[PFTM_UDP_SINGLE] = 30;			/* Unidirectional */
	timeout[PFTM_UDP_MULTIPLE] = 60;		/* Bidirectional */
	timeout[PFTM_ICMP_FIRST_PACKET] = 20;		/* First ICMP packet */
	timeout[PFTM_ICMP_ERROR_REPLY] = 10;		/* Got error response */
	timeout[PFTM_OTHER_FIRST_PACKET] = 60;		/* First packet */
	timeout[PFTM_OTHER_SINGLE] = 30;		/* Unidirectional */
	timeout[PFTM_OTHER_MULTIPLE] = 60;		/* Bidirectional */
	timeout[PFTM_FRAG] = 30;			/* Fragment expire */
	timeout[PFTM_INTERVAL] = 10;			/* Expire interval */

	timeout_set(&pf_expire_to, pf_purge_timeout, &pf_expire_to);
	timeout_add(&pf_expire_to, timeout[PFTM_INTERVAL] * hz);

	pf_normalize_init();
	pf_status.debug = PF_DEBUG_URGENT;
}

int
pfopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	if (minor(dev) >= 1)
		return (ENXIO);
	return (0);
}

int
pfclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	if (minor(dev) >= 1)
		return (ENXIO);
	return (0);
}
#endif /* __FreeBSD__ */

struct pf_pool *
pf_get_pool(char *anchorname, char *rulesetname, u_int32_t ticket,
    u_int8_t rule_action, u_int32_t rule_number, u_int8_t r_last,
    u_int8_t active, u_int8_t check_ticket)
{
	struct pf_ruleset	*ruleset;
	struct pf_rule		*rule;
	int			 rs_num;

	ruleset = pf_find_ruleset(anchorname, rulesetname);
	if (ruleset == NULL)
		return (NULL);
	rs_num = pf_get_ruleset_number(rule_action);
	if (rs_num >= PF_RULESET_MAX)
		return (NULL);
	if (active) {
		if (check_ticket && ticket !=
		    ruleset->rules[rs_num].active.ticket)
			return (NULL);
		if (r_last)
			rule = TAILQ_LAST(ruleset->rules[rs_num].active.ptr,
			    pf_rulequeue);
		else
			rule = TAILQ_FIRST(ruleset->rules[rs_num].active.ptr);
	} else {
		if (check_ticket && ticket !=
		    ruleset->rules[rs_num].inactive.ticket)
			return (NULL);
		if (r_last)
			rule = TAILQ_LAST(ruleset->rules[rs_num].inactive.ptr,
			    pf_rulequeue);
		else
			rule = TAILQ_FIRST(ruleset->rules[rs_num].inactive.ptr);
	}
	if (!r_last) {
		while ((rule != NULL) && (rule->nr != rule_number))
			rule = TAILQ_NEXT(rule, entries);
	}
	if (rule == NULL)
		return (NULL);

	return (&rule->rpool);
}

int
pf_get_ruleset_number(u_int8_t action)
{
	switch (action) {
	case PF_SCRUB:
		return (PF_RULESET_SCRUB);
		break;
	case PF_PASS:
	case PF_DROP:
		return (PF_RULESET_FILTER);
		break;
	case PF_NAT:
	case PF_NONAT:
		return (PF_RULESET_NAT);
		break;
	case PF_BINAT:
	case PF_NOBINAT:
		return (PF_RULESET_BINAT);
		break;
	case PF_RDR:
	case PF_NORDR:
		return (PF_RULESET_RDR);
		break;
	default:
		return (PF_RULESET_MAX);
		break;
	}
}

void
pf_init_ruleset(struct pf_ruleset *ruleset)
{
	int	i;

	memset(ruleset, 0, sizeof(struct pf_ruleset));
	for (i = 0; i < PF_RULESET_MAX; i++) {
		TAILQ_INIT(&ruleset->rules[i].queues[0]);
		TAILQ_INIT(&ruleset->rules[i].queues[1]);
		ruleset->rules[i].active.ptr = &ruleset->rules[i].queues[0];
		ruleset->rules[i].inactive.ptr = &ruleset->rules[i].queues[1];
	}
}

struct pf_anchor *
pf_find_anchor(const char *anchorname)
{
	struct pf_anchor	*anchor;
	int			 n = -1;

	anchor = TAILQ_FIRST(&pf_anchors);
	while (anchor != NULL && (n = strcmp(anchor->name, anchorname)) < 0)
		anchor = TAILQ_NEXT(anchor, entries);
	if (n == 0)
		return (anchor);
	else
		return (NULL);
}

struct pf_ruleset *
pf_find_ruleset(char *anchorname, char *rulesetname)
{
	struct pf_anchor	*anchor;
	struct pf_ruleset	*ruleset;

	if (!anchorname[0] && !rulesetname[0])
		return (&pf_main_ruleset);
	if (!anchorname[0] || !rulesetname[0])
		return (NULL);
	anchorname[PF_ANCHOR_NAME_SIZE-1] = 0;
	rulesetname[PF_RULESET_NAME_SIZE-1] = 0;
	anchor = pf_find_anchor(anchorname);
	if (anchor == NULL)
		return (NULL);
	ruleset = TAILQ_FIRST(&anchor->rulesets);
	while (ruleset != NULL && strcmp(ruleset->name, rulesetname) < 0)
		ruleset = TAILQ_NEXT(ruleset, entries);
	if (ruleset != NULL && !strcmp(ruleset->name, rulesetname))
		return (ruleset);
	else
		return (NULL);
}

struct pf_ruleset *
pf_find_or_create_ruleset(char *anchorname, char *rulesetname)
{
	struct pf_anchor	*anchor, *a;
	struct pf_ruleset	*ruleset, *r;

	if (!anchorname[0] && !rulesetname[0])
		return (&pf_main_ruleset);
	if (!anchorname[0] || !rulesetname[0])
		return (NULL);
	anchorname[PF_ANCHOR_NAME_SIZE-1] = 0;
	rulesetname[PF_RULESET_NAME_SIZE-1] = 0;
	a = TAILQ_FIRST(&pf_anchors);
	while (a != NULL && strcmp(a->name, anchorname) < 0)
		a = TAILQ_NEXT(a, entries);
	if (a != NULL && !strcmp(a->name, anchorname))
		anchor = a;
	else {
		anchor = (struct pf_anchor *)malloc(sizeof(struct pf_anchor),
		    M_TEMP, M_NOWAIT);
		if (anchor == NULL)
			return (NULL);
		memset(anchor, 0, sizeof(struct pf_anchor));
		bcopy(anchorname, anchor->name, sizeof(anchor->name));
		TAILQ_INIT(&anchor->rulesets);
		if (a != NULL)
			TAILQ_INSERT_BEFORE(a, anchor, entries);
		else
			TAILQ_INSERT_TAIL(&pf_anchors, anchor, entries);
	}
	r = TAILQ_FIRST(&anchor->rulesets);
	while (r != NULL && strcmp(r->name, rulesetname) < 0)
		r = TAILQ_NEXT(r, entries);
	if (r != NULL && !strcmp(r->name, rulesetname))
		return (r);
	ruleset = (struct pf_ruleset *)malloc(sizeof(struct pf_ruleset),
	    M_TEMP, M_NOWAIT);
	if (ruleset != NULL) {
		pf_init_ruleset(ruleset);
		bcopy(rulesetname, ruleset->name, sizeof(ruleset->name));
		ruleset->anchor = anchor;
		if (r != NULL)
			TAILQ_INSERT_BEFORE(r, ruleset, entries);
		else
			TAILQ_INSERT_TAIL(&anchor->rulesets, ruleset, entries);
	}
	return (ruleset);
}

void
pf_remove_if_empty_ruleset(struct pf_ruleset *ruleset)
{
	struct pf_anchor	*anchor;
	int			 i;

	if (ruleset == NULL || ruleset->anchor == NULL || ruleset->tables > 0 ||
	    ruleset->topen)
		return;
	for (i = 0; i < PF_RULESET_MAX; ++i)
		if (!TAILQ_EMPTY(ruleset->rules[i].active.ptr) ||
		    !TAILQ_EMPTY(ruleset->rules[i].inactive.ptr))
			return;

	anchor = ruleset->anchor;
	TAILQ_REMOVE(&anchor->rulesets, ruleset, entries);
	free(ruleset, M_TEMP);

	if (TAILQ_EMPTY(&anchor->rulesets)) {
		TAILQ_REMOVE(&pf_anchors, anchor, entries);
		free(anchor, M_TEMP);
		pf_update_anchor_rules();
	}
}

void
pf_mv_pool(struct pf_palist *poola, struct pf_palist *poolb)
{
	struct pf_pooladdr	*mv_pool_pa;

	while ((mv_pool_pa = TAILQ_FIRST(poola)) != NULL) {
		TAILQ_REMOVE(poola, mv_pool_pa, entries);
		TAILQ_INSERT_TAIL(poolb, mv_pool_pa, entries);
	}
}

void
pf_empty_pool(struct pf_palist *poola)
{
	struct pf_pooladdr	*empty_pool_pa;

	while ((empty_pool_pa = TAILQ_FIRST(poola)) != NULL) {
		pf_dynaddr_remove(&empty_pool_pa->addr);
		pf_tbladdr_remove(&empty_pool_pa->addr);
		TAILQ_REMOVE(poola, empty_pool_pa, entries);
		pool_put(&pf_pooladdr_pl, empty_pool_pa);
	}
}

void
pf_rm_rule(struct pf_rulequeue *rulequeue, struct pf_rule *rule)
{
	if (rulequeue != NULL) {
		if (rule->states <= 0) {
			/*
			 * XXX - we need to remove the table *before* detaching
			 * the rule to make sure the table code does not delete
			 * the anchor under our feet.
			 */
			pf_tbladdr_remove(&rule->src.addr);
			pf_tbladdr_remove(&rule->dst.addr);
		}
		TAILQ_REMOVE(rulequeue, rule, entries);
		rule->entries.tqe_prev = NULL;
		rule->nr = -1;
	}
	if (rule->states > 0 || rule->entries.tqe_prev != NULL)
		return;
	pf_tag_unref(rule->tag);
	pf_tag_unref(rule->match_tag);
	pf_dynaddr_remove(&rule->src.addr);
	pf_dynaddr_remove(&rule->dst.addr);
	if (rulequeue == NULL) {
		pf_tbladdr_remove(&rule->src.addr);
		pf_tbladdr_remove(&rule->dst.addr);
	}
	pf_empty_pool(&rule->rpool.list);
	pool_put(&pf_rule_pl, rule);
}

u_int16_t
pf_tagname2tag(char *tagname)
{
	struct pf_tagname	*tag, *p = NULL;
	u_int16_t		 new_tagid = 1;

	TAILQ_FOREACH(tag, &pf_tags, entries)
		if (strcmp(tagname, tag->name) == 0) {
			tag->ref++;
			return (tag->tag);
		}

	/*
	 * to avoid fragmentation, we do a linear search from the beginning
	 * and take the first free slot we find. if there is none or the list
	 * is empty, append a new entry at the end.
	 */

	/* new entry */
	if (!TAILQ_EMPTY(&pf_tags))
		for (p = TAILQ_FIRST(&pf_tags); p != NULL &&
		    p->tag == new_tagid; p = TAILQ_NEXT(p, entries))
			new_tagid = p->tag + 1;

	if (new_tagid > TAGID_MAX)
		return (0);

	/* allocate and fill new struct pf_tagname */
	tag = (struct pf_tagname *)malloc(sizeof(struct pf_tagname),
	    M_TEMP, M_NOWAIT);
	if (tag == NULL)
		return (0);
	bzero(tag, sizeof(struct pf_tagname));
	strlcpy(tag->name, tagname, sizeof(tag->name));
	tag->tag = new_tagid;
	tag->ref++;

	if (p != NULL)	/* insert new entry before p */
		TAILQ_INSERT_BEFORE(p, tag, entries);
	else	/* either list empty or no free slot in between */
		TAILQ_INSERT_TAIL(&pf_tags, tag, entries);

	return (tag->tag);
}

void
pf_tag2tagname(u_int16_t tagid, char *p)
{
	struct pf_tagname	*tag;

	TAILQ_FOREACH(tag, &pf_tags, entries)
		if (tag->tag == tagid) {
			strlcpy(p, tag->name, PF_TAG_NAME_SIZE);
			return;
		}
}

void
pf_tag_unref(u_int16_t tag)
{
	struct pf_tagname	*p, *next;

	if (tag == 0)
		return;

	for (p = TAILQ_FIRST(&pf_tags); p != NULL; p = next) {
		next = TAILQ_NEXT(p, entries);
		if (tag == p->tag) {
			if (--p->ref == 0) {
				TAILQ_REMOVE(&pf_tags, p, entries);
				free(p, M_TEMP);
			}
			break;
		}
	}
}

#ifdef __FreeBSD__
int
pfioctl(dev_t dev, u_long cmd, caddr_t addr, int flags, struct thread *td)
#else
int
pfioctl(dev_t dev, u_long cmd, caddr_t addr, int flags, struct proc *p)
#endif
{
	struct pf_pooladdr	*pa = NULL;
	struct pf_pool		*pool = NULL;
	int			 s;
	int			 error = 0;

	/* XXX keep in sync with switch() below */
	if (securelevel > 1)
		switch (cmd) {
		case DIOCGETRULES:
		case DIOCGETRULE:
		case DIOCGETADDRS:
		case DIOCGETADDR:
		case DIOCGETSTATE:
		case DIOCSETSTATUSIF:
		case DIOCGETSTATUS:
		case DIOCCLRSTATUS:
		case DIOCNATLOOK:
		case DIOCSETDEBUG:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCCLRRULECTRS:
		case DIOCGETLIMIT:
		case DIOCGETALTQS:
		case DIOCGETALTQ:
		case DIOCGETQSTATS:
		case DIOCGETANCHORS:
		case DIOCGETANCHOR:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
		case DIOCRGETTABLES:
		case DIOCRGETTSTATS:
		case DIOCRCLRTSTATS:
		case DIOCRCLRADDRS:
		case DIOCRADDADDRS:
		case DIOCRDELADDRS:
		case DIOCRSETADDRS:
		case DIOCRGETADDRS:
		case DIOCRGETASTATS:
		case DIOCRCLRASTATS:
		case DIOCRTSTADDRS:
		case DIOCOSFPGET:
#ifdef __FreeBSD__
		case DIOCGIFSPEED:
#endif
			break;
		default:
			return (EPERM);
		}

	if (!(flags & FWRITE))
		switch (cmd) {
		case DIOCGETRULES:
		case DIOCGETRULE:
		case DIOCGETADDRS:
		case DIOCGETADDR:
		case DIOCGETSTATE:
		case DIOCGETSTATUS:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCGETLIMIT:
		case DIOCGETALTQS:
		case DIOCGETALTQ:
		case DIOCGETQSTATS:
		case DIOCGETANCHORS:
		case DIOCGETANCHOR:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
		case DIOCRGETTABLES:
		case DIOCRGETTSTATS:
		case DIOCRGETADDRS:
		case DIOCRGETASTATS:
		case DIOCRTSTADDRS:
		case DIOCOSFPGET:
#ifdef __FreeBSD__
		case DIOCGIFSPEED:
#endif
			break;
		default:
			return (EACCES);
		}

#ifdef __FreeBSD__
	PF_LOCK();
#endif

	switch (cmd) {

	case DIOCSTART:
		if (pf_status.running)
			error = EEXIST;
		else {
			u_int32_t states = pf_status.states;
#ifdef __FreeBSD__
			PF_UNLOCK();
			error = hook_pf();
			PF_LOCK();
			if (error) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: pfil registeration fail\n"));
				break;
			}
#endif
			bzero(&pf_status, sizeof(struct pf_status));
			pf_status.running = 1;
			pf_status.states = states;
#ifdef __FreeBSD__
			pf_status.since = time_second;
#else
			pf_status.since = time.tv_sec;
#endif
			if (status_ifp != NULL)
#if defined(__FreeBSD__) && (__FreeBSD_version < 501113)
				snprintf(pf_status.ifname, IFNAMSIZ, "%s%d",
				    status_ifp->if_name, status_ifp->if_unit);
#else
				strlcpy(pf_status.ifname,
				    status_ifp->if_xname, IFNAMSIZ);
#endif
			DPFPRINTF(PF_DEBUG_MISC, ("pf: started\n"));
		}
		break;

	case DIOCSTOP:
		if (!pf_status.running)
			error = ENOENT;
		else {
			pf_status.running = 0;
#ifdef __FreeBSD__
			PF_UNLOCK();
			error = dehook_pf();
			PF_LOCK();
			if (error) {
				pf_status.running = 1;
				DPFPRINTF(PF_DEBUG_MISC,
					("pf: pfil unregisteration failed\n"));
			}
#endif
			DPFPRINTF(PF_DEBUG_MISC, ("pf: stopped\n"));
		}
		break;

	case DIOCBEGINRULES: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule;
		int			 rs_num;

		ruleset = pf_find_or_create_ruleset(pr->anchor, pr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		while ((rule =
		    TAILQ_FIRST(ruleset->rules[rs_num].inactive.ptr)) != NULL)
			pf_rm_rule(ruleset->rules[rs_num].inactive.ptr, rule);
		pr->ticket = ++ruleset->rules[rs_num].inactive.ticket;
		break;
	}

	case DIOCADDRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule, *tail;
		struct pf_pooladdr	*pa;
		int			 rs_num;

		ruleset = pf_find_ruleset(pr->anchor, pr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		if (pr->rule.anchorname[0] && ruleset != &pf_main_ruleset) {
			error = EINVAL;
			break;
		}
		if (pr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
			error = EINVAL;
			break;
		}
		if (pr->ticket != ruleset->rules[rs_num].inactive.ticket) {
			error = EBUSY;
			break;
		}
		if (pr->pool_ticket != ticket_pabuf) {
			error = EBUSY;
			break;
		}
		rule = pool_get(&pf_rule_pl, PR_NOWAIT);
		if (rule == NULL) {
			error = ENOMEM;
			break;
		}
		bcopy(&pr->rule, rule, sizeof(struct pf_rule));
		rule->anchor = NULL;
		rule->ifp = NULL;
		TAILQ_INIT(&rule->rpool.list);
		/* initialize refcounting */
		rule->states = 0;
		rule->entries.tqe_prev = NULL;
#ifndef INET
		if (rule->af == AF_INET) {
			pool_put(&pf_rule_pl, rule);
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET */
#ifndef INET6
		if (rule->af == AF_INET6) {
			pool_put(&pf_rule_pl, rule);
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET6 */
		tail = TAILQ_LAST(ruleset->rules[rs_num].inactive.ptr,
		    pf_rulequeue);
		if (tail)
			rule->nr = tail->nr + 1;
		else
			rule->nr = 0;
		if (rule->ifname[0]) {
			rule->ifp = ifunit(rule->ifname);
			if (rule->ifp == NULL) {
				pool_put(&pf_rule_pl, rule);
				error = EINVAL;
				break;
			}
		}

		if (rule->tagname[0])
			if ((rule->tag = pf_tagname2tag(rule->tagname)) == 0)
				error = EBUSY;
		if (rule->match_tagname[0])
			if ((rule->match_tag =
			    pf_tagname2tag(rule->match_tagname)) == 0)
				error = EBUSY;
		if (rule->rt && !rule->direction)
			error = EINVAL;
		if (pf_dynaddr_setup(&rule->src.addr, rule->af))
			error = EINVAL;
		if (pf_dynaddr_setup(&rule->dst.addr, rule->af))
			error = EINVAL;
		if (pf_tbladdr_setup(ruleset, &rule->src.addr))
			error = EINVAL;
		if (pf_tbladdr_setup(ruleset, &rule->dst.addr))
			error = EINVAL;
		TAILQ_FOREACH(pa, &pf_pabuf, entries)
			if (pf_tbladdr_setup(ruleset, &pa->addr))
				error = EINVAL;

		pf_mv_pool(&pf_pabuf, &rule->rpool.list);
		if (((((rule->action == PF_NAT) || (rule->action == PF_RDR) ||
		    (rule->action == PF_BINAT)) && !rule->anchorname[0]) ||
		    (rule->rt > PF_FASTROUTE)) &&
		    (TAILQ_FIRST(&rule->rpool.list) == NULL))
			error = EINVAL;

		if (error) {
			pf_rm_rule(NULL, rule);
			break;
		}
		rule->rpool.cur = TAILQ_FIRST(&rule->rpool.list);
		rule->evaluations = rule->packets = rule->bytes = 0;
		TAILQ_INSERT_TAIL(ruleset->rules[rs_num].inactive.ptr,
		    rule, entries);
		break;
	}

	case DIOCCOMMITRULES: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rulequeue	*old_rules;
		struct pf_rule		*rule;
		int			 rs_num;

		ruleset = pf_find_ruleset(pr->anchor, pr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		if (pr->ticket != ruleset->rules[rs_num].inactive.ticket) {
			error = EBUSY;
			break;
		}

#ifdef ALTQ
		/* set queue IDs */
		if (rs_num == PF_RULESET_FILTER)
			pf_rule_set_qid(ruleset->rules[rs_num].inactive.ptr);
#endif

		/* Swap rules, keep the old. */
		s = splsoftnet();
		old_rules = ruleset->rules[rs_num].active.ptr;
		ruleset->rules[rs_num].active.ptr =
		    ruleset->rules[rs_num].inactive.ptr;
		ruleset->rules[rs_num].inactive.ptr = old_rules;
		ruleset->rules[rs_num].active.ticket =
		    ruleset->rules[rs_num].inactive.ticket;
		pf_calc_skip_steps(ruleset->rules[rs_num].active.ptr);

		/* Purge the old rule list. */
		while ((rule = TAILQ_FIRST(old_rules)) != NULL)
			pf_rm_rule(old_rules, rule);
		pf_remove_if_empty_ruleset(ruleset);
		pf_update_anchor_rules();
		splx(s);
		break;
	}

	case DIOCGETRULES: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*tail;
		int			 rs_num;

		ruleset = pf_find_ruleset(pr->anchor, pr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		s = splsoftnet();
		tail = TAILQ_LAST(ruleset->rules[rs_num].active.ptr,
		    pf_rulequeue);
		if (tail)
			pr->nr = tail->nr + 1;
		else
			pr->nr = 0;
		pr->ticket = ruleset->rules[rs_num].active.ticket;
		splx(s);
		break;
	}

	case DIOCGETRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule;
		int			 rs_num, i;

		ruleset = pf_find_ruleset(pr->anchor, pr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		if (pr->ticket != ruleset->rules[rs_num].active.ticket) {
			error = EBUSY;
			break;
		}
		s = splsoftnet();
		rule = TAILQ_FIRST(ruleset->rules[rs_num].active.ptr);
		while ((rule != NULL) && (rule->nr != pr->nr))
			rule = TAILQ_NEXT(rule, entries);
		if (rule == NULL) {
			error = EBUSY;
			splx(s);
			break;
		}
		bcopy(rule, &pr->rule, sizeof(struct pf_rule));
		pf_dynaddr_copyout(&pr->rule.src.addr);
		pf_dynaddr_copyout(&pr->rule.dst.addr);
		pf_tbladdr_copyout(&pr->rule.src.addr);
		pf_tbladdr_copyout(&pr->rule.dst.addr);
		for (i = 0; i < PF_SKIP_COUNT; ++i)
			if (rule->skip[i].ptr == NULL)
				pr->rule.skip[i].nr = -1;
			else
				pr->rule.skip[i].nr =
				    rule->skip[i].ptr->nr;
		splx(s);
		break;
	}

	case DIOCCHANGERULE: {
		struct pfioc_rule	*pcr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*oldrule = NULL, *newrule = NULL;
		u_int32_t		 nr = 0;
		int			 rs_num;

		if (!(pcr->action == PF_CHANGE_REMOVE ||
		    pcr->action == PF_CHANGE_GET_TICKET) &&
		    pcr->pool_ticket != ticket_pabuf) {
			error = EBUSY;
			break;
		}

		if (pcr->action < PF_CHANGE_ADD_HEAD ||
		    pcr->action > PF_CHANGE_GET_TICKET) {
			error = EINVAL;
			break;
		}
		ruleset = pf_find_ruleset(pcr->anchor, pcr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pcr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}

		if (pcr->action == PF_CHANGE_GET_TICKET) {
			pcr->ticket = ++ruleset->rules[rs_num].active.ticket;
			break;
		} else {
			if (pcr->ticket !=
			    ruleset->rules[rs_num].active.ticket) {
				error = EINVAL;
				break;
			}
			if (pcr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
				error = EINVAL;
				break;
			}
		}

		if (pcr->action != PF_CHANGE_REMOVE) {
			newrule = pool_get(&pf_rule_pl, PR_NOWAIT);
			if (newrule == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&pcr->rule, newrule, sizeof(struct pf_rule));
			TAILQ_INIT(&newrule->rpool.list);
			/* initialize refcounting */
			newrule->states = 0;
			newrule->entries.tqe_prev = NULL;
#ifndef INET
			if (newrule->af == AF_INET) {
				pool_put(&pf_rule_pl, newrule);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET */
#ifndef INET6
			if (newrule->af == AF_INET6) {
				pool_put(&pf_rule_pl, newrule);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET6 */
			if (newrule->ifname[0]) {
				newrule->ifp = ifunit(newrule->ifname);
				if (newrule->ifp == NULL) {
					pool_put(&pf_rule_pl, newrule);
					error = EINVAL;
					break;
				}
			} else
				newrule->ifp = NULL;

#ifdef ALTQ
			/* set queue IDs */
			if (newrule->qname[0] != 0) {
				newrule->qid = pf_qname_to_qid(newrule->qname);
				if (newrule->pqname[0] != 0)
					newrule->pqid =
					    pf_qname_to_qid(newrule->pqname);
				else
					newrule->pqid = newrule->qid;
			}
#endif
			if (newrule->tagname[0])
				if ((newrule->tag =
				    pf_tagname2tag(newrule->tagname)) == 0)
					error = EBUSY;
			if (newrule->match_tagname[0])
				if ((newrule->match_tag = pf_tagname2tag(
				    newrule->match_tagname)) == 0)
					error = EBUSY;

			if (newrule->rt && !newrule->direction)
				error = EINVAL;
			if (pf_dynaddr_setup(&newrule->src.addr, newrule->af))
				error = EINVAL;
			if (pf_dynaddr_setup(&newrule->dst.addr, newrule->af))
				error = EINVAL;
			if (pf_tbladdr_setup(ruleset, &newrule->src.addr))
				error = EINVAL;
			if (pf_tbladdr_setup(ruleset, &newrule->dst.addr))
				error = EINVAL;

			pf_mv_pool(&pf_pabuf, &newrule->rpool.list);
			if (((((newrule->action == PF_NAT) ||
			    (newrule->action == PF_RDR) ||
			    (newrule->action == PF_BINAT) ||
			    (newrule->rt > PF_FASTROUTE)) &&
			    !newrule->anchorname[0])) &&
			    (TAILQ_FIRST(&newrule->rpool.list) == NULL))
				error = EINVAL;

			if (error) {
				pf_rm_rule(NULL, newrule);
				break;
			}
			newrule->rpool.cur = TAILQ_FIRST(&newrule->rpool.list);
			newrule->evaluations = newrule->packets = 0;
			newrule->bytes = 0;
		}
		pf_empty_pool(&pf_pabuf);

		s = splsoftnet();

		if (pcr->action == PF_CHANGE_ADD_HEAD)
			oldrule = TAILQ_FIRST(
			    ruleset->rules[rs_num].active.ptr);
		else if (pcr->action == PF_CHANGE_ADD_TAIL)
			oldrule = TAILQ_LAST(
			    ruleset->rules[rs_num].active.ptr, pf_rulequeue);
		else {
			oldrule = TAILQ_FIRST(
			    ruleset->rules[rs_num].active.ptr);
			while ((oldrule != NULL) && (oldrule->nr != pcr->nr))
				oldrule = TAILQ_NEXT(oldrule, entries);
			if (oldrule == NULL) {
				pf_rm_rule(NULL, newrule);
				error = EINVAL;
				splx(s);
				break;
			}
		}

		if (pcr->action == PF_CHANGE_REMOVE)
			pf_rm_rule(ruleset->rules[rs_num].active.ptr, oldrule);
		else {
			if (oldrule == NULL)
				TAILQ_INSERT_TAIL(
				    ruleset->rules[rs_num].active.ptr,
				    newrule, entries);
			else if (pcr->action == PF_CHANGE_ADD_HEAD ||
			    pcr->action == PF_CHANGE_ADD_BEFORE)
				TAILQ_INSERT_BEFORE(oldrule, newrule, entries);
			else
				TAILQ_INSERT_AFTER(
				    ruleset->rules[rs_num].active.ptr,
				    oldrule, newrule, entries);
		}

		nr = 0;
		TAILQ_FOREACH(oldrule,
		    ruleset->rules[rs_num].active.ptr, entries)
			oldrule->nr = nr++;

		pf_calc_skip_steps(ruleset->rules[rs_num].active.ptr);
		pf_remove_if_empty_ruleset(ruleset);
		pf_update_anchor_rules();

		ruleset->rules[rs_num].active.ticket++;
		splx(s);
		break;
	}

	case DIOCCLRSTATES: {
		struct pf_tree_node	*n;

		s = splsoftnet();
		RB_FOREACH(n, pf_state_tree, &tree_ext_gwy)
			n->state->timeout = PFTM_PURGE;
		pf_purge_expired_states();
		pf_status.states = 0;
		splx(s);
		break;
	}

	case DIOCKILLSTATES: {
		struct pf_tree_node	*n;
		struct pf_state		*st;
		struct pfioc_state_kill	*psk = (struct pfioc_state_kill *)addr;
		int			 killed = 0;

		s = splsoftnet();
		RB_FOREACH(n, pf_state_tree, &tree_ext_gwy) {
			st = n->state;
			if ((!psk->psk_af || st->af == psk->psk_af) &&
			    (!psk->psk_proto || psk->psk_proto == st->proto) &&
			    PF_MATCHA(psk->psk_src.not,
			    &psk->psk_src.addr.v.a.addr,
			    &psk->psk_src.addr.v.a.mask, &st->lan.addr,
			    st->af) &&
			    PF_MATCHA(psk->psk_dst.not,
			    &psk->psk_dst.addr.v.a.addr,
			    &psk->psk_dst.addr.v.a.mask, &st->ext.addr,
			    st->af) &&
			    (psk->psk_src.port_op == 0 ||
			    pf_match_port(psk->psk_src.port_op,
			    psk->psk_src.port[0], psk->psk_src.port[1],
			    st->lan.port)) &&
			    (psk->psk_dst.port_op == 0 ||
			    pf_match_port(psk->psk_dst.port_op,
			    psk->psk_dst.port[0], psk->psk_dst.port[1],
			    st->ext.port))) {
				st->timeout = PFTM_PURGE;
				killed++;
			}
		}
		pf_purge_expired_states();
		splx(s);
		psk->psk_af = killed;
		break;
	}

	case DIOCADDSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pf_state		*state;

		if (ps->state.timeout >= PFTM_MAX &&
		    ps->state.timeout != PFTM_UNTIL_PACKET) {
			error = EINVAL;
			break;
		}
		state = pool_get(&pf_state_pl, PR_NOWAIT);
		if (state == NULL) {
			error = ENOMEM;
			break;
		}
		s = splsoftnet();
		bcopy(&ps->state, state, sizeof(struct pf_state));
		state->rule.ptr = NULL;
		state->nat_rule.ptr = NULL;
		state->anchor.ptr = NULL;
		state->rt_ifp = NULL;
#ifdef __FreeBSD__
		state->creation = time_second;
#else
		state->creation = time.tv_sec;
#endif
		state->packets[0] = state->packets[1] = 0;
		state->bytes[0] = state->bytes[1] = 0;
		if (pf_insert_state(state)) {
			pool_put(&pf_state_pl, state);
			error = ENOMEM;
		}
		splx(s);
		break;
	}

	case DIOCGETSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pf_tree_node	*n;
		u_int32_t		 nr;

		nr = 0;
		s = splsoftnet();
		RB_FOREACH(n, pf_state_tree, &tree_ext_gwy) {
			if (nr >= ps->nr)
				break;
			nr++;
		}
		if (n == NULL) {
			error = EBUSY;
			splx(s);
			break;
		}
		bcopy(n->state, &ps->state, sizeof(struct pf_state));
		ps->state.rule.nr = n->state->rule.ptr->nr;
		ps->state.nat_rule.nr = (n->state->nat_rule.ptr == NULL) ?
		    -1 : n->state->nat_rule.ptr->nr;
		ps->state.anchor.nr = (n->state->anchor.ptr == NULL) ?
		    -1 : n->state->anchor.ptr->nr;
		splx(s);
		ps->state.expire = pf_state_expires(n->state);
#ifdef __FreeBSD__
		if (ps->state.expire > time_second)
			ps->state.expire -= time_second;
#else
		if (ps->state.expire > time.tv_sec)
			ps->state.expire -= time.tv_sec;
#endif
		else
			ps->state.expire = 0;
		break;
	}

	case DIOCGETSTATES: {
		struct pfioc_states	*ps = (struct pfioc_states *)addr;
		struct pf_tree_node	*n;
		struct pf_state		*p, pstore;
		u_int32_t		 nr = 0;
		int			 space = ps->ps_len;

		if (space == 0) {
			s = splsoftnet();
			RB_FOREACH(n, pf_state_tree, &tree_ext_gwy)
				nr++;
			splx(s);
			ps->ps_len = sizeof(struct pf_state) * nr;
#ifdef __FreeBSD__
			PF_UNLOCK();
#endif
			return (0);
		}

		s = splsoftnet();
		p = ps->ps_states;
		RB_FOREACH(n, pf_state_tree, &tree_ext_gwy) {
#ifdef __FreeBSD__
			int	secs = time_second;
#else
			int	secs = time.tv_sec;
#endif

			if ((nr + 1) * sizeof(*p) > (unsigned)ps->ps_len)
				break;

			bcopy(n->state, &pstore, sizeof(pstore));
			pstore.rule.nr = n->state->rule.ptr->nr;
			pstore.nat_rule.nr = (n->state->nat_rule.ptr == NULL) ?
			    -1 : n->state->nat_rule.ptr->nr;
			pstore.anchor.nr = (n->state->anchor.ptr == NULL) ?
			    -1 : n->state->anchor.ptr->nr;
			pstore.creation = secs - pstore.creation;
			pstore.expire = pf_state_expires(n->state);
			if (pstore.expire > secs)
				pstore.expire -= secs;
			else
				pstore.expire = 0;
#ifdef __FreeBSD__
			PF_COPYOUT(&pstore, p, sizeof(*p), error);
#else
			error = copyout(&pstore, p, sizeof(*p));
#endif
			if (error) {
				splx(s);
				goto fail;
			}
			p++;
			nr++;
		}
		ps->ps_len = sizeof(struct pf_state) * nr;
		splx(s);
		break;
	}

	case DIOCGETSTATUS: {
		struct pf_status *s = (struct pf_status *)addr;
		bcopy(&pf_status, s, sizeof(struct pf_status));
		break;
	}

	case DIOCSETSTATUSIF: {
		struct pfioc_if	*pi = (struct pfioc_if *)addr;
		struct ifnet	*ifp;

		if (pi->ifname[0] == 0) {
			status_ifp = NULL;
			bzero(pf_status.ifname, IFNAMSIZ);
			break;
		}
		if ((ifp = ifunit(pi->ifname)) == NULL) {
			error = EINVAL;
			break;
		} else if (ifp == status_ifp)
			break;
		status_ifp = ifp;
		/* fallthrough into DIOCCLRSTATUS */
	}

	case DIOCCLRSTATUS: {
		u_int32_t	running = pf_status.running;
		u_int32_t	states = pf_status.states;
		u_int32_t	since = pf_status.since;
		u_int32_t	debug = pf_status.debug;

		bzero(&pf_status, sizeof(struct pf_status));
		pf_status.running = running;
		pf_status.states = states;
		pf_status.since = since;
		pf_status.debug = debug;
		if (status_ifp != NULL)
#if defined(__FreeBSD__) && (__FreeBSD_version < 501113)
			snprintf(pf_status.ifname, IFNAMSIZ, "%s%d",
			    status_ifp->if_name, status_ifp->if_unit);
#else
			strlcpy(pf_status.ifname,
			    status_ifp->if_xname, IFNAMSIZ);
#endif
		break;
	}

	case DIOCNATLOOK: {
		struct pfioc_natlook	*pnl = (struct pfioc_natlook *)addr;
		struct pf_state		*st;
		struct pf_tree_node	 key;
		int			 direction = pnl->direction;

		key.af = pnl->af;
		key.proto = pnl->proto;

		/*
		 * userland gives us source and dest of connection, reverse
		 * the lookup so we ask for what happens with the return
		 * traffic, enabling us to find it in the state tree.
		 */
		PF_ACPY(&key.addr[1], &pnl->saddr, pnl->af);
		key.port[1] = pnl->sport;
		PF_ACPY(&key.addr[0], &pnl->daddr, pnl->af);
		key.port[0] = pnl->dport;

		if (!pnl->proto ||
		    PF_AZERO(&pnl->saddr, pnl->af) ||
		    PF_AZERO(&pnl->daddr, pnl->af) ||
		    !pnl->dport || !pnl->sport)
			error = EINVAL;
		else {
			s = splsoftnet();
			if (direction == PF_IN)
				st = pf_find_state(&tree_ext_gwy, &key);
			else
				st = pf_find_state(&tree_lan_ext, &key);
			if (st != NULL) {
				if (direction == PF_IN) {
					PF_ACPY(&pnl->rsaddr, &st->lan.addr,
					    st->af);
					pnl->rsport = st->lan.port;
					PF_ACPY(&pnl->rdaddr, &pnl->daddr,
					    pnl->af);
					pnl->rdport = pnl->dport;
				} else {
					PF_ACPY(&pnl->rdaddr, &st->gwy.addr,
					    st->af);
					pnl->rdport = st->gwy.port;
					PF_ACPY(&pnl->rsaddr, &pnl->saddr,
					    pnl->af);
					pnl->rsport = pnl->sport;
				}
			} else
				error = ENOENT;
			splx(s);
		}
		break;
	}

	case DIOCSETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;
		int		 old;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX ||
		    pt->seconds < 0) {
			error = EINVAL;
			goto fail;
		}
		old = pf_default_rule.timeout[pt->timeout];
		pf_default_rule.timeout[pt->timeout] = pt->seconds;
		pt->seconds = old;
		break;
	}

	case DIOCGETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX) {
			error = EINVAL;
			goto fail;
		}
		pt->seconds = pf_default_rule.timeout[pt->timeout];
		break;
	}

	case DIOCGETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;

		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX) {
			error = EINVAL;
			goto fail;
		}
		pl->limit = pf_pool_limits[pl->index].limit;
		break;
	}

	case DIOCSETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;
		int			 old_limit;

		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX) {
			error = EINVAL;
			goto fail;
		}
#ifdef __FreeBSD__
		uma_zone_set_max(pf_pool_limits[pl->index].pp, pl->limit);
#else
		if (pool_sethardlimit(pf_pool_limits[pl->index].pp,
		    pl->limit, NULL, 0) != 0) {
			error = EBUSY;
			goto fail;
		}
#endif
		old_limit = pf_pool_limits[pl->index].limit;
		pf_pool_limits[pl->index].limit = pl->limit;
		pl->limit = old_limit;
		break;
	}

	case DIOCSETDEBUG: {
		u_int32_t	*level = (u_int32_t *)addr;

		pf_status.debug = *level;
		break;
	}

	case DIOCCLRRULECTRS: {
		struct pf_ruleset	*ruleset = &pf_main_ruleset;
		struct pf_rule		*rule;

		s = splsoftnet();
		TAILQ_FOREACH(rule,
		    ruleset->rules[PF_RULESET_FILTER].active.ptr, entries)
			rule->evaluations = rule->packets =
			    rule->bytes = 0;
		splx(s);
		break;
	}

#ifdef __FreeBSD__
	case DIOCGIFSPEED: {
		struct pf_ifspeed	*psp = (struct pf_ifspeed *)addr;
		struct pf_ifspeed	ps;
		struct ifnet		*ifp;

		if (psp->ifname[0] != 0) {
			/* Can we completely trust user-land? */
			strlcpy(ps.ifname, psp->ifname, IFNAMSIZ);
			ifp = ifunit(ps.ifname);
			if (ifp )
				psp->baudrate = ifp->if_baudrate;
			else
				error = EINVAL;
		} else
			error = EINVAL;
		break;
	}
#endif /* __FreeBSD__ */

#ifdef ALTQ
	case DIOCSTARTALTQ: {
		struct pf_altq		*altq;
		struct ifnet		*ifp;
		struct tb_profile	 tb;

		/* enable all altq interfaces on active list */
		s = splsoftnet();
		TAILQ_FOREACH(altq, pf_altqs_active, entries) {
			if (altq->qname[0] == 0) {
				if ((ifp = ifunit(altq->ifname)) == NULL) {
					error = EINVAL;
					break;
				}
				if (ifp->if_snd.altq_type != ALTQT_NONE)
					error = altq_enable(&ifp->if_snd);
				if (error != 0)
					break;
				/* set tokenbucket regulator */
				tb.rate = altq->ifbandwidth;
				tb.depth = altq->tbrsize;
				error = tbr_set(&ifp->if_snd, &tb);
				if (error != 0)
					break;
			}
		}
#ifdef __FreeBSD__
		if (error == 0) {
			mtx_lock(&pf_altq_mtx);
			pfaltq_running = 1;
			mtx_unlock(&pf_altq_mtx);
		}
#else
		if (error == 0)
			pfaltq_running = 1;
#endif
		splx(s);
		DPFPRINTF(PF_DEBUG_MISC, ("altq: started\n"));
		break;
	}

	case DIOCSTOPALTQ: {
		struct pf_altq		*altq;
		struct ifnet		*ifp;
		struct tb_profile	 tb;
		int			 err;

		/* disable all altq interfaces on active list */
		s = splsoftnet();
		TAILQ_FOREACH(altq, pf_altqs_active, entries) {
			if (altq->qname[0] == 0) {
				if ((ifp = ifunit(altq->ifname)) == NULL) {
					error = EINVAL;
					break;
				}
				if (ifp->if_snd.altq_type != ALTQT_NONE) {
					err = altq_disable(&ifp->if_snd);
					if (err != 0 && error == 0)
						error = err;
				}
				/* clear tokenbucket regulator */
				tb.rate = 0;
				err = tbr_set(&ifp->if_snd, &tb);
				if (err != 0 && error == 0)
					error = err;
			}
		}
#ifdef __FreeBSD__
		if (error == 0) {
			mtx_lock(&pf_altq_mtx);
			pfaltq_running = 0;
			mtx_unlock(&pf_altq_mtx);
		}
#else
		if (error == 0)
			pfaltq_running = 0;
#endif
		splx(s);
		DPFPRINTF(PF_DEBUG_MISC, ("altq: stopped\n"));
		break;
	}

	case DIOCBEGINALTQS: {
		u_int32_t	*ticket = (u_int32_t *)addr;
		struct pf_altq	*altq;

		/* Purge the old altq list */
		while ((altq = TAILQ_FIRST(pf_altqs_inactive)) != NULL) {
			TAILQ_REMOVE(pf_altqs_inactive, altq, entries);
			if (altq->qname[0] == 0) {
				/* detach and destroy the discipline */
#ifdef __FreeBSD__
				PF_UNLOCK();
#endif
				error = altq_remove(altq);
#ifdef __FreeBSD__
				PF_LOCK();
#endif
			}
			pool_put(&pf_altq_pl, altq);
		}
		*ticket = ++ticket_altqs_inactive;
		break;
	}

	case DIOCADDALTQ: {
		struct pfioc_altq	*pa = (struct pfioc_altq *)addr;
		struct pf_altq		*altq, *a;

		if (pa->ticket != ticket_altqs_inactive) {
			error = EBUSY;
			break;
		}
		altq = pool_get(&pf_altq_pl, PR_NOWAIT);
		if (altq == NULL) {
			error = ENOMEM;
			break;
		}
		bcopy(&pa->altq, altq, sizeof(struct pf_altq));

		/*
		 * if this is for a queue, find the discipline and
		 * copy the necessary fields
		 */
		if (altq->qname[0] != 0) {
			TAILQ_FOREACH(a, pf_altqs_inactive, entries) {
				if (strncmp(a->ifname, altq->ifname,
				    IFNAMSIZ) == 0 && a->qname[0] == 0) {
					altq->altq_disc = a->altq_disc;
					break;
				}
			}
		}

#ifdef __FreeBSD__
		PF_UNLOCK();
#endif		
		error = altq_add(altq);
#ifdef __FreeBSD__
		PF_LOCK();
#endif
		if (error) {
			pool_put(&pf_altq_pl, altq);
			break;
		}

		TAILQ_INSERT_TAIL(pf_altqs_inactive, altq, entries);
		bcopy(altq, &pa->altq, sizeof(struct pf_altq));
		break;
	}

	case DIOCCOMMITALTQS: {
		u_int32_t		*ticket = (u_int32_t *)addr;
		struct pf_altqqueue	*old_altqs;
		struct pf_altq		*altq;
		struct pf_anchor	*anchor;
		struct pf_ruleset	*ruleset;
		int			 err;

		if (*ticket != ticket_altqs_inactive) {
			error = EBUSY;
			break;
		}

		/* Swap altqs, keep the old. */
		s = splsoftnet();
		old_altqs = pf_altqs_active;
		pf_altqs_active = pf_altqs_inactive;
		pf_altqs_inactive = old_altqs;
		ticket_altqs_active = ticket_altqs_inactive;

		/* Attach new disciplines */
		TAILQ_FOREACH(altq, pf_altqs_active, entries) {
			if (altq->qname[0] == 0) {
				/* attach the discipline */
#ifdef __FreeBSD__
				PF_UNLOCK();
#endif
				error = altq_pfattach(altq);
#ifdef __FreeBSD__
				PF_LOCK();
#endif
				if (error) {
					splx(s);
					goto fail;
				}
			}
		}

		/* Purge the old altq list */
		while ((altq = TAILQ_FIRST(pf_altqs_inactive)) != NULL) {
			TAILQ_REMOVE(pf_altqs_inactive, altq, entries);
			if (altq->qname[0] == 0) {
				/* detach and destroy the discipline */
#ifdef __FreeBSD__
				PF_UNLOCK();
#endif
				err = altq_pfdetach(altq);
				if (err != 0 && error == 0)
					error = err;
				err = altq_remove(altq);
				if (err != 0 && error == 0)
					error = err;
#ifdef __FreeBSD__
				PF_LOCK();
#endif
			}
			pool_put(&pf_altq_pl, altq);
		}
		splx(s);

		/* update queue IDs */
		pf_rule_set_qid(
		    pf_main_ruleset.rules[PF_RULESET_FILTER].active.ptr);
		TAILQ_FOREACH(anchor, &pf_anchors, entries) {
			TAILQ_FOREACH(ruleset, &anchor->rulesets, entries) {
				pf_rule_set_qid(
				    ruleset->rules[PF_RULESET_FILTER].active.ptr
				    );
			}
		}
		break;
	}

	case DIOCGETALTQS: {
		struct pfioc_altq	*pa = (struct pfioc_altq *)addr;
		struct pf_altq		*altq;

		pa->nr = 0;
		s = splsoftnet();
		TAILQ_FOREACH(altq, pf_altqs_active, entries)
			pa->nr++;
		pa->ticket = ticket_altqs_active;
		splx(s);
		break;
	}

	case DIOCGETALTQ: {
		struct pfioc_altq	*pa = (struct pfioc_altq *)addr;
		struct pf_altq		*altq;
		u_int32_t		 nr;

		if (pa->ticket != ticket_altqs_active) {
			error = EBUSY;
			break;
		}
		nr = 0;
		s = splsoftnet();
		altq = TAILQ_FIRST(pf_altqs_active);
		while ((altq != NULL) && (nr < pa->nr)) {
			altq = TAILQ_NEXT(altq, entries);
			nr++;
		}
		if (altq == NULL) {
			error = EBUSY;
			splx(s);
			break;
		}
		bcopy(altq, &pa->altq, sizeof(struct pf_altq));
		splx(s);
		break;
	}

	case DIOCCHANGEALTQ:
		/* CHANGEALTQ not supported yet! */
		error = ENODEV;
		break;

	case DIOCGETQSTATS: {
		struct pfioc_qstats	*pq = (struct pfioc_qstats *)addr;
		struct pf_altq		*altq;
		u_int32_t		 nr;
		int			 nbytes;

		if (pq->ticket != ticket_altqs_active) {
			error = EBUSY;
			break;
		}
		nbytes = pq->nbytes;
		nr = 0;
		s = splsoftnet();
		altq = TAILQ_FIRST(pf_altqs_active);
		while ((altq != NULL) && (nr < pq->nr)) {
			altq = TAILQ_NEXT(altq, entries);
			nr++;
		}
		if (altq == NULL) {
			error = EBUSY;
			splx(s);
			break;
		}
#ifdef __FreeBSD__
		PF_UNLOCK();
#endif
		error = altq_getqstats(altq, pq->buf, &nbytes);
#ifdef __FreeBSD__
		PF_LOCK();
#endif
		splx(s);
		if (error == 0) {
			pq->scheduler = altq->scheduler;
			pq->nbytes = nbytes;
		}
		break;
	}
#endif /* ALTQ */

	case DIOCBEGINADDRS: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;

		pf_empty_pool(&pf_pabuf);
		pp->ticket = ++ticket_pabuf;
		break;
	}

	case DIOCADDADDR: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;

#ifndef INET
		if (pp->af == AF_INET) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET */
#ifndef INET6
		if (pp->af == AF_INET6) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET6 */
		if (pp->addr.addr.type != PF_ADDR_ADDRMASK &&
		    pp->addr.addr.type != PF_ADDR_DYNIFTL &&
		    pp->addr.addr.type != PF_ADDR_TABLE) {
			error = EINVAL;
			break;
		}
		pa = pool_get(&pf_pooladdr_pl, PR_NOWAIT);
		if (pa == NULL) {
			error = ENOMEM;
			break;
		}
		bcopy(&pp->addr, pa, sizeof(struct pf_pooladdr));
		if (pa->ifname[0]) {
			pa->ifp = ifunit(pa->ifname);
			if (pa->ifp == NULL) {
				pool_put(&pf_pooladdr_pl, pa);
				error = EINVAL;
				break;
			}
		}
		if (pf_dynaddr_setup(&pa->addr, pp->af)) {
			pf_dynaddr_remove(&pa->addr);
			pool_put(&pf_pooladdr_pl, pa);
			error = EINVAL;
			break;
		}
		TAILQ_INSERT_TAIL(&pf_pabuf, pa, entries);
		break;
	}

	case DIOCGETADDRS: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;

		pp->nr = 0;
		s = splsoftnet();
		pool = pf_get_pool(pp->anchor, pp->ruleset, pp->ticket,
		    pp->r_action, pp->r_num, 0, 1, 0);
		if (pool == NULL) {
			error = EBUSY;
			splx(s);
			break;
		}
		TAILQ_FOREACH(pa, &pool->list, entries)
			pp->nr++;
		splx(s);
		break;
	}

	case DIOCGETADDR: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;
		u_int32_t		 nr = 0;

		s = splsoftnet();
		pool = pf_get_pool(pp->anchor, pp->ruleset, pp->ticket,
		    pp->r_action, pp->r_num, 0, 1, 1);
		if (pool == NULL) {
			error = EBUSY;
			splx(s);
			break;
		}
		pa = TAILQ_FIRST(&pool->list);
		while ((pa != NULL) && (nr < pp->nr)) {
			pa = TAILQ_NEXT(pa, entries);
			nr++;
		}
		if (pa == NULL) {
			error = EBUSY;
			splx(s);
			break;
		}
		bcopy(pa, &pp->addr, sizeof(struct pf_pooladdr));
		pf_dynaddr_copyout(&pp->addr.addr);
		pf_tbladdr_copyout(&pp->addr.addr);
		splx(s);
		break;
	}

	case DIOCCHANGEADDR: {
		struct pfioc_pooladdr	*pca = (struct pfioc_pooladdr *)addr;
		struct pf_pooladdr	*oldpa = NULL, *newpa = NULL;
		struct pf_ruleset	*ruleset;

		if (pca->action < PF_CHANGE_ADD_HEAD ||
		    pca->action > PF_CHANGE_REMOVE) {
			error = EINVAL;
			break;
		}
		if (pca->addr.addr.type != PF_ADDR_ADDRMASK &&
		    pca->addr.addr.type != PF_ADDR_DYNIFTL &&
		    pca->addr.addr.type != PF_ADDR_TABLE) {
			error = EINVAL;
			break;
		}

		ruleset = pf_find_ruleset(pca->anchor, pca->ruleset);
		if (ruleset == NULL) {
			error = EBUSY;
			break;
		}
		pool = pf_get_pool(pca->anchor, pca->ruleset, pca->ticket,
		    pca->r_action, pca->r_num, pca->r_last, 1, 1);
		if (pool == NULL) {
			error = EBUSY;
			break;
		}
		if (pca->action != PF_CHANGE_REMOVE) {
			newpa = pool_get(&pf_pooladdr_pl, PR_NOWAIT);
			if (newpa == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&pca->addr, newpa, sizeof(struct pf_pooladdr));
#ifndef INET
			if (pca->af == AF_INET) {
				pool_put(&pf_pooladdr_pl, newpa);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET */
#ifndef INET6
			if (pca->af == AF_INET6) {
				pool_put(&pf_pooladdr_pl, newpa);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET6 */
			if (newpa->ifname[0]) {
				newpa->ifp = ifunit(newpa->ifname);
				if (newpa->ifp == NULL) {
					pool_put(&pf_pooladdr_pl, newpa);
					error = EINVAL;
					break;
				}
			} else
				newpa->ifp = NULL;
			if (pf_dynaddr_setup(&newpa->addr, pca->af) ||
			    pf_tbladdr_setup(ruleset, &newpa->addr)) {
				pf_dynaddr_remove(&newpa->addr);
				pool_put(&pf_pooladdr_pl, newpa);
				error = EINVAL;
				break;
			}
		}

		s = splsoftnet();

		if (pca->action == PF_CHANGE_ADD_HEAD)
			oldpa = TAILQ_FIRST(&pool->list);
		else if (pca->action == PF_CHANGE_ADD_TAIL)
			oldpa = TAILQ_LAST(&pool->list, pf_palist);
		else {
			int	i = 0;

			oldpa = TAILQ_FIRST(&pool->list);
			while ((oldpa != NULL) && (i < pca->nr)) {
				oldpa = TAILQ_NEXT(oldpa, entries);
				i++;
			}
			if (oldpa == NULL) {
				error = EINVAL;
				splx(s);
				break;
			}
		}

		if (pca->action == PF_CHANGE_REMOVE) {
			TAILQ_REMOVE(&pool->list, oldpa, entries);
			pf_dynaddr_remove(&oldpa->addr);
			pf_tbladdr_remove(&oldpa->addr);
			pool_put(&pf_pooladdr_pl, oldpa);
		} else {
			if (oldpa == NULL)
				TAILQ_INSERT_TAIL(&pool->list, newpa, entries);
			else if (pca->action == PF_CHANGE_ADD_HEAD ||
			    pca->action == PF_CHANGE_ADD_BEFORE)
				TAILQ_INSERT_BEFORE(oldpa, newpa, entries);
			else
				TAILQ_INSERT_AFTER(&pool->list, oldpa,
				    newpa, entries);
		}

		pool->cur = TAILQ_FIRST(&pool->list);
		PF_ACPY(&pool->counter, &pool->cur->addr.v.a.addr,
		    pca->af);
		splx(s);
		break;
	}

	case DIOCGETANCHORS: {
		struct pfioc_anchor	*pa = (struct pfioc_anchor *)addr;
		struct pf_anchor	*anchor;

		pa->nr = 0;
		TAILQ_FOREACH(anchor, &pf_anchors, entries)
			pa->nr++;
		break;
	}

	case DIOCGETANCHOR: {
		struct pfioc_anchor	*pa = (struct pfioc_anchor *)addr;
		struct pf_anchor	*anchor;
		u_int32_t		 nr = 0;

		anchor = TAILQ_FIRST(&pf_anchors);
		while (anchor != NULL && nr < pa->nr) {
			anchor = TAILQ_NEXT(anchor, entries);
			nr++;
		}
		if (anchor == NULL)
			error = EBUSY;
		else
			bcopy(anchor->name, pa->name, sizeof(pa->name));
		break;
	}

	case DIOCGETRULESETS: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_anchor	*anchor;
		struct pf_ruleset	*ruleset;

		pr->anchor[PF_ANCHOR_NAME_SIZE-1] = 0;
		if ((anchor = pf_find_anchor(pr->anchor)) == NULL) {
			error = EINVAL;
			break;
		}
		pr->nr = 0;
		TAILQ_FOREACH(ruleset, &anchor->rulesets, entries)
			pr->nr++;
		break;
	}

	case DIOCGETRULESET: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_anchor	*anchor;
		struct pf_ruleset	*ruleset;
		u_int32_t		 nr = 0;

		if ((anchor = pf_find_anchor(pr->anchor)) == NULL) {
			error = EINVAL;
			break;
		}
		ruleset = TAILQ_FIRST(&anchor->rulesets);
		while (ruleset != NULL && nr < pr->nr) {
			ruleset = TAILQ_NEXT(ruleset, entries);
			nr++;
		}
		if (ruleset == NULL)
			error = EBUSY;
		else
			bcopy(ruleset->name, pr->name, sizeof(pr->name));
		break;
	}

	case DIOCRCLRTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_tables(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags);
		break;
	}

	case DIOCRADDTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_add_tables(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_nadd, io->pfrio_flags);
		break;
	}

	case DIOCRDELTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_del_tables(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_ndel, io->pfrio_flags);
		break;
	}

	case DIOCRGETTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_tables(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags);
		break;
	}

	case DIOCRGETTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_tstats)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_tstats(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags);
		break;
	}

	case DIOCRCLRTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_tstats(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_nzero, io->pfrio_flags);
		break;
	}

	case DIOCRSETTFLAGS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_set_tflags(io->pfrio_buffer, io->pfrio_size,
		    io->pfrio_setflag, io->pfrio_clrflag, &io->pfrio_nchange,
		    &io->pfrio_ndel, io->pfrio_flags);
		break;
	}

	case DIOCRCLRADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_addrs(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags);
		break;
	}

	case DIOCRADDADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_add_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nadd, io->pfrio_flags);
		break;
	}

	case DIOCRDELADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_del_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_ndel, io->pfrio_flags);
		break;
	}

	case DIOCRSETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_set_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_size2, &io->pfrio_nadd,
		    &io->pfrio_ndel, &io->pfrio_nchange, io->pfrio_flags);
		break;
	}

	case DIOCRGETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_addrs(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags);
		break;
	}

	case DIOCRGETASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_astats)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_astats(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags);
		break;
	}

	case DIOCRCLRASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_astats(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nzero, io->pfrio_flags);
		break;
	}

	case DIOCRTSTADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_tst_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nmatch, io->pfrio_flags);
		break;
	}

	case DIOCRINABEGIN: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		error = pfr_ina_begin(&io->pfrio_table, &io->pfrio_ticket,
		    &io->pfrio_ndel, io->pfrio_flags);
		break;
	}

	case DIOCRINACOMMIT: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		error = pfr_ina_commit(&io->pfrio_table, io->pfrio_ticket,
		    &io->pfrio_nadd, &io->pfrio_nchange, io->pfrio_flags);
		break;
	}

	case DIOCRINADEFINE: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_ina_define(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nadd, &io->pfrio_naddr,
		    io->pfrio_ticket, io->pfrio_flags);
		break;
	}

	case DIOCOSFPFLUSH:
		s = splsoftnet();
		pf_osfp_flush();
		splx(s);
		break;

	case DIOCOSFPADD: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		s = splsoftnet();
		error = pf_osfp_add(io);
		splx(s);
		break;
	}

	case DIOCOSFPGET: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		s = splsoftnet();
		error = pf_osfp_get(io);
		splx(s);
		break;
	}

	default:
		error = ENODEV;
		break;
	}
fail:
#ifdef __FreeBSD__
	PF_UNLOCK();
#endif
	return (error);
}

#ifdef __FreeBSD__
/*
 * XXX - Check for version missmatch!!!
 */
static int
pf_beginrules(void *addr)
{
	struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
	struct pf_ruleset	*ruleset;
	struct pf_rule		*rule;
	int			 rs_num;
	int			 error = 0;

	do {
		ruleset = pf_find_or_create_ruleset(pr->anchor, pr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		while ((rule =
		    TAILQ_FIRST(ruleset->rules[rs_num].inactive.ptr)) != NULL)
			pf_rm_rule(ruleset->rules[rs_num].inactive.ptr, rule);
		pr->ticket = ++ruleset->rules[rs_num].inactive.ticket;
	} while(0);

	return (error);
}

static int
pf_commitrules(void *addr)
{
	struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
	struct pf_ruleset	*ruleset;
	struct pf_rulequeue	*old_rules;
	struct pf_rule		*rule;
	int			 rs_num, s;
	int			 error = 0;

	do {
		ruleset = pf_find_ruleset(pr->anchor, pr->ruleset);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		if (pr->ticket != ruleset->rules[rs_num].inactive.ticket) {
			error = EBUSY;
			break;
		}

#ifdef ALTQ
		/* set queue IDs */
		if (rs_num == PF_RULESET_FILTER)
			pf_rule_set_qid(ruleset->rules[rs_num].inactive.ptr);
#endif

		/* Swap rules, keep the old. */
		s = splsoftnet();
		old_rules = ruleset->rules[rs_num].active.ptr;
		ruleset->rules[rs_num].active.ptr =
		    ruleset->rules[rs_num].inactive.ptr;
		ruleset->rules[rs_num].inactive.ptr = old_rules;
		ruleset->rules[rs_num].active.ticket =
		    ruleset->rules[rs_num].inactive.ticket;
		pf_calc_skip_steps(ruleset->rules[rs_num].active.ptr);

		/* Purge the old rule list. */
		while ((rule = TAILQ_FIRST(old_rules)) != NULL)
			pf_rm_rule(old_rules, rule);
		pf_remove_if_empty_ruleset(ruleset);
		pf_update_anchor_rules();
		splx(s);
	} while (0);

	return (error);
}

#ifdef ALTQ
static int
pf_beginaltqs(void *addr)
{
	u_int32_t	*ticket = (u_int32_t *)addr;
	struct pf_altq	*altq;
	int		error = 0;

	/* Purge the old altq list */
	while ((altq = TAILQ_FIRST(pf_altqs_inactive)) != NULL) {
		TAILQ_REMOVE(pf_altqs_inactive, altq, entries);
		if (altq->qname[0] == 0) {
#ifdef __FreeBSD__
			PF_UNLOCK();
#endif
			/* detach and destroy the discipline */
			error = altq_remove(altq);
#ifdef __FreeBSD__
			PF_LOCK();
#endif
		}
		uma_zfree(pf_altq_pl, altq);
	}
	*ticket = ++ticket_altqs_inactive;

	return (error);
}

static int
pf_commitaltqs(void *addr)
{
	u_int32_t		*ticket = (u_int32_t *)addr;
	struct pf_altqqueue	*old_altqs;
	struct pf_altq		*altq;
	struct pf_anchor	*anchor;
	struct pf_ruleset	*ruleset;
	int			 err;
	int			 s;
	int			 error = 0;

	do {
		if (*ticket != ticket_altqs_inactive) {
			error = EBUSY;
			break;
		}

		/* Swap altqs, keep the old. */
		s = splsoftnet();
		old_altqs = pf_altqs_active;
		pf_altqs_active = pf_altqs_inactive;
		pf_altqs_inactive = old_altqs;
		ticket_altqs_active = ticket_altqs_inactive;

		/* Attach new disciplines */
		TAILQ_FOREACH(altq, pf_altqs_active, entries) {
			if (altq->qname[0] == 0) {
				/* attach the discipline */
#ifdef __FreeBSD__
				PF_UNLOCK();
#endif
				error = altq_pfattach(altq);
#ifdef __FreeBSD__
				PF_LOCK();
#endif
				if (error) {
					splx(s);
					goto altq_fail;
				}
			}
		}

		/* Purge the old altq list */
		while ((altq = TAILQ_FIRST(pf_altqs_inactive)) != NULL) {
			TAILQ_REMOVE(pf_altqs_inactive, altq, entries);
			if (altq->qname[0] == 0) {
				/* detach and destroy the discipline */
#ifdef __FreeBSD__
				PF_UNLOCK();
#endif
				err = altq_pfdetach(altq);
				if (err != 0 && error == 0)
					error = err;
				err = altq_remove(altq);
				if (err != 0 && error == 0)
					error = err;
#ifdef __FreeBSD__
				PF_LOCK();
#endif
			}
			uma_zfree(pf_altq_pl, altq);
		}
		splx(s);

		/* update queue IDs */
		pf_rule_set_qid(
		    pf_main_ruleset.rules[PF_RULESET_FILTER].active.ptr);
		TAILQ_FOREACH(anchor, &pf_anchors, entries) {
			TAILQ_FOREACH(ruleset, &anchor->rulesets, entries) {
				pf_rule_set_qid(
				    ruleset->rules[PF_RULESET_FILTER].active.ptr
				    );
			}
		}
	} while (0);

altq_fail:

	return (error);
}

static int
pf_stopaltq(void)
{
	struct pf_altq		*altq;
	struct ifnet		*ifp;
	struct tb_profile	 tb;
	int			 err;
	int			 s;
	int			 error = 0;

	do {
		/* disable all altq interfaces on active list */
		s = splsoftnet();
		TAILQ_FOREACH(altq, pf_altqs_active, entries) {
			if (altq->qname[0] == 0) {
				if ((ifp = ifunit(altq->ifname)) == NULL) {
					error = EINVAL;
					break;
				}
				if (ifp->if_snd.altq_type != ALTQT_NONE) {
					err = altq_disable(&ifp->if_snd);
					if (err != 0 && error == 0)
						error = err;
				}
				/* clear tokenbucket regulator */
				tb.rate = 0;
				err = tbr_set(&ifp->if_snd, &tb);
				if (err != 0 && error == 0)
					error = err;
			}
		}
#ifdef __FreeBSD__
		if (error == 0) {
			mtx_lock(&pf_altq_mtx);
			pfaltq_running = 0;
			mtx_unlock(&pf_altq_mtx);
		}
#else
		if (error == 0)
			pfaltq_running = 0;
#endif
		splx(s);
	} while (0);

	return (error);
}
#endif

static void
pf_clearstates(void)
{
	struct pf_tree_node	*n;
	int s;

	s = splsoftnet();
	RB_FOREACH(n, pf_state_tree, &tree_ext_gwy)
		n->state->timeout = PFTM_PURGE;
	pf_purge_expired_states();
	pf_status.states = 0;
	splx(s);
}

static int
pf_clear_tables(void *addr)
{
	struct pfioc_table *io = (struct pfioc_table *)addr;
	int error;

	error = pfr_clr_tables(&io->pfrio_table, &io->pfrio_ndel,
	    io->pfrio_flags);

	return (error);
}

static int
shutdown_pf(void)
{
	struct pfioc_rule pr;
#ifdef ALTQ
	struct pfioc_altq pa;
#endif
	struct pfioc_table io;
	int error = 0;

	callout_stop(&pf_expire_to);

	PF_LOCK();
	pf_status.running = 0;
	do {
#ifdef ALTQ
		if ((error = pf_stopaltq())) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("ALTQ: stop(%i)\n", error));
			break;
		}
#endif
		bzero(&pr, sizeof(pr));
		pr.rule.action = PF_SCRUB;
		if ((error = pf_beginrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_SCRUB: begin(%i)\n", error));
			break;
		}
		if ((error = pf_commitrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_SCRUB: commit(%i)\n", error));
			break;
		}

		pr.rule.action = PF_PASS;
		if ((error = pf_beginrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_PASS: begin(%i)\n", error));
			break;
		}
		if ((error = pf_commitrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_PASS: commit(%i)\n", error));
			break;
		}

/*
 * XXX not sure, but can't hurt:
 */
		bzero(&pr, sizeof(pr));
		pr.rule.action = PF_NAT;
		if ((error = pf_beginrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_NAT: begin(%i)\n", error));
			break;
		}
		if ((error = pf_commitrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_NAT: commit(%i)\n", error));
			break;
		}

		pr.rule.action = PF_BINAT;
		if ((error = pf_beginrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_BINAT: begin(%i)\n", error));
			break;
		}
		if ((error = pf_commitrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_BINAT: begin(%i)\n", error));
			break;
		}

		pr.rule.action = PF_RDR;
		if ((error = pf_beginrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_RDR: begin(%i)\n", error));
			break;
		}
		if ((error = pf_commitrules(&pr))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("PF_RDR: commit(%i)\n", error));
			break;
		}

#ifdef ALTQ
		bzero(&pa, sizeof(pa));
		if ((error = pf_beginaltqs(&pa))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("ALTQ: begin(%i)\n", error));
			break;
		}
		if ((error = pf_commitaltqs(&pa))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("ALTQ: commit(%i)\n", error));
			break;
		}
#endif
		pf_clearstates();

		bzero(&io, sizeof(io));
		if ((error = pf_clear_tables(&io))) {
			DPFPRINTF(PF_DEBUG_MISC, 
			    ("TABLES: clear(%i)\n", error));
			break;
		}
		pf_osfp_flush();
	} while(0);

	PF_UNLOCK();
        return (error);
}

static int
#if (__FreeBSD_version < 501108)
pf_check_in(void *ip, int hlen, struct ifnet *ifp, int dir, struct mbuf **m)
#else
pf_check_in(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
#endif
{
	/*
	 * XXX Wed Jul 9 22:03:16 2003 UTC
	 * OpenBSD has changed its byte ordering convention on ip_len/ip_off
	 * in network stack. OpenBSD's network stack have converted
	 * ip_len/ip_off to host byte order frist as FreeBSD.
	 * Now this is not true anymore , so we should convert back to network
	 * byte order. 
	 */
	struct ip *h = NULL;
	int chk;

	if ((*m)->m_pkthdr.len >= (int)sizeof(struct ip)) {
		/* if m_pkthdr.len is less than ip header, pf will handle. */
		h = mtod(*m, struct ip *);
	        HTONS(h->ip_len);
	        HTONS(h->ip_off);
	}
	chk = pf_test(PF_IN, ifp, m);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	if (*m != NULL) {
		/* pf_test can change ip header location */
		h = mtod(*m, struct ip *);
		NTOHS(h->ip_len);
		NTOHS(h->ip_off);
	}
	return chk;
}

static int
#if (__FreeBSD_version < 501108)
pf_check_out(void *ip, int hlen, struct ifnet *ifp, int dir, struct mbuf **m)
#else
pf_check_out(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
#endif
{
	/*
	 * XXX Wed Jul 9 22:03:16 2003 UTC
	 * OpenBSD has changed its byte ordering convention on ip_len/ip_off
	 * in network stack. OpenBSD's network stack have converted
	 * ip_len/ip_off to host byte order frist as FreeBSD.
	 * Now this is not true anymore , so we should convert back to network
	 * byte order. 
	 */
	struct ip *h = NULL;
	int chk;

	/* We need a proper CSUM befor we start (s. OpenBSD ip_output) */
	if ((*m)->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(*m);
		(*m)->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
	if ((*m)->m_pkthdr.len >= (int)sizeof(*h)) {
		/* if m_pkthdr.len is less than ip header, pf will handle. */
		h = mtod(*m, struct ip *);
	        HTONS(h->ip_len);
	        HTONS(h->ip_off);
	}
	chk = pf_test(PF_OUT, ifp, m);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	if (*m != NULL) {
		/* pf_test can change ip header location */
		h = mtod(*m, struct ip *);
		NTOHS(h->ip_len);
		NTOHS(h->ip_off);
	}
	return chk;
}

#ifdef INET6
static int
#if (__FreeBSD_version < 501108)
pf_check6_in(void *ip, int hlen, struct ifnet *ifp, int dir, struct mbuf **m)
#else
pf_check6_in(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
#endif
{
	/*
	 * IPv6 does not affected ip_len/ip_off byte order changes.
	 */
	int chk;

	chk = pf_test6(PF_IN, ifp, m);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	return chk;
}

static int
#if (__FreeBSD_version < 501108)
pf_check6_out(void *ip, int hlen, struct ifnet *ifp, int dir, struct mbuf **m)
#else
pf_check6_out(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
#endif
{
	/*
	 * IPv6 does not affected ip_len/ip_off byte order changes.
	 */
	int chk;

	/* We need a proper CSUM befor we start (s. OpenBSD ip_output) */
	if ((*m)->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(*m);
		(*m)->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
	chk = pf_test6(PF_OUT, ifp, m);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	return chk;
}
#endif /* INET6 */

static int
hook_pf(void)
{
#if (__FreeBSD_version >= 501108)
	struct pfil_head *pfh_inet;
#ifdef INET6
	struct pfil_head *pfh_inet6;
#endif
#endif
	
	PF_ASSERT(MA_NOTOWNED);

	if (pf_pfil_hooked)
		return (0); 
	
#if (__FreeBSD_version < 501108)
	/*
	 * XXX
	 * There is no easy way to get pfil header pointer with address
	 * family such as AF_INET, AF_INET6.
	 * Needs direct variable reference.
	 */

	pfil_add_hook(pf_check_in, PFIL_IN,
		&inetsw[ip_protox[IPPROTO_IP]].pr_pfh);
	pfil_add_hook(pf_check_out, PFIL_OUT,
		&inetsw[ip_protox[IPPROTO_IP]].pr_pfh);
#ifdef INET6
	pfil_add_hook(pf_check6_in, PFIL_IN,
		&inet6sw[ip6_protox[IPPROTO_IPV6]].pr_pfh);
	pfil_add_hook(pf_check6_out, PFIL_OUT,
		&inet6sw[ip6_protox[IPPROTO_IPV6]].pr_pfh);
#endif
#else /* __FreeBSD_version >= 501108 */
	pfh_inet = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh_inet == NULL)
		return (ESRCH); /* XXX */
	pfil_add_hook(pf_check_in, NULL, PFIL_IN | PFIL_WAITOK, pfh_inet);
	pfil_add_hook(pf_check_out, NULL, PFIL_OUT | PFIL_WAITOK, pfh_inet);
#ifdef INET6
	pfh_inet6 = pfil_head_get(PFIL_TYPE_AF, AF_INET6);
	if (pfh_inet6 == NULL) {
		pfil_remove_hook(pf_check_in, NULL, PFIL_IN | PFIL_WAITOK,
		    pfh_inet);
		pfil_remove_hook(pf_check_out, NULL, PFIL_OUT | PFIL_WAITOK,
		    pfh_inet);
		return (ESRCH); /* XXX */
	}
	pfil_add_hook(pf_check6_in, NULL, PFIL_IN | PFIL_WAITOK, pfh_inet6);
	pfil_add_hook(pf_check6_out, NULL, PFIL_OUT | PFIL_WAITOK, pfh_inet6);
#endif
#endif /* __FreeBSD_version >= 501108 */

	pf_pfil_hooked = 1;
	return (0);
}

static int
dehook_pf(void)
{
#if (__FreeBSD_version >= 501108)
	struct pfil_head *pfh_inet;
#ifdef INET6
	struct pfil_head *pfh_inet6;
#endif
#endif

	PF_ASSERT(MA_NOTOWNED);

	if (pf_pfil_hooked == 0)
		return (0);

#if (__FreeBSD_version < 501108)
	pfil_remove_hook(pf_check_in, PFIL_IN,
		&inetsw[ip_protox[IPPROTO_IP]].pr_pfh);
	pfil_remove_hook(pf_check_out, PFIL_OUT,
		&inetsw[ip_protox[IPPROTO_IP]].pr_pfh);
#ifdef INET6
	pfil_remove_hook(pf_check6_in, PFIL_IN,
		&inet6sw[ip6_protox[IPPROTO_IPV6]].pr_pfh);
	pfil_remove_hook(pf_check6_out, PFIL_OUT,
		&inet6sw[ip6_protox[IPPROTO_IPV6]].pr_pfh);
#endif
#else /* __FreeBSD_version >= 501108 */
	pfh_inet = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh_inet == NULL)
		return (ESRCH); /* XXX */
	pfil_remove_hook(pf_check_in, NULL, PFIL_IN | PFIL_WAITOK,
	    pfh_inet);
	pfil_remove_hook(pf_check_out, NULL, PFIL_OUT | PFIL_WAITOK,
	    pfh_inet);
#ifdef INET6
	pfh_inet6 = pfil_head_get(PFIL_TYPE_AF, AF_INET6);
	if (pfh_inet6 == NULL)
		return (ESRCH); /* XXX */
	pfil_remove_hook(pf_check6_in, NULL, PFIL_IN | PFIL_WAITOK,
	    pfh_inet6);
	pfil_remove_hook(pf_check6_out, NULL, PFIL_OUT | PFIL_WAITOK,
	    pfh_inet6);
#endif
#endif /* __FreeBSD_version >= 501108 */

	pf_pfil_hooked = 0;
	return (0);
}

static int
pf_load(void)
{
	init_zone_var();
	init_pf_mutex();
	pf_dev = make_dev(&pf_cdevsw, 0, 0, 0, 0600, PF_NAME);
	if (pfattach() < 0) {
		destroy_dev(pf_dev);
		destroy_pf_mutex();
		return (ENOMEM);
	}
#ifdef ALTQ
	mtx_lock(&pf_altq_mtx);
	++pfaltq_ref;
	mtx_unlock(&pf_altq_mtx);
#endif
	return (0);
}

static int
pf_unload(void)
{
	int error = 0;

	PF_LOCK();
	pf_status.running = 0;
	PF_UNLOCK();
	error = dehook_pf();
	if (error) {
		/*
		 * Should not happen!
		 * XXX Due to error code ESRCH, kldunload will show
		 * a message like 'No such process'.
		 */
		printf("%s : pfil unregisteration fail\n", __FUNCTION__);
		return error;
	}
	shutdown_pf();
	cleanup_pf_zone();
	pf_osfp_cleanup();
	destroy_dev(pf_dev);
#ifdef ALTQ
	mtx_lock(&pf_altq_mtx);
	--pfaltq_ref;
	mtx_unlock(&pf_altq_mtx);
#endif
	destroy_pf_mutex();
	return error;
}

static int
pf_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	switch(type) {
	case MOD_LOAD:
		error = pf_load();
		break;

	case MOD_UNLOAD:
		error = pf_unload();
		break;
	default:
		error = EINVAL;
		break;
	}
	return error;
}

static moduledata_t pf_mod = {
	"pf",
	pf_modevent,
	0
};

DECLARE_MODULE(pf, pf_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_DEPEND(pf, pflog, PFLOG_MINVER, PFLOG_PREFVER, PFLOG_MAXVER);
MODULE_DEPEND(pf, pfsync, PFSYNC_MINVER, PFSYNC_PREFVER, PFSYNC_MAXVER);
#ifdef ALTQ
MODULE_DEPEND(pf, pfaltq, PFALTQ_MINVER, PFALTQ_PREFVER, PFALTQ_MAXVER);
#endif
MODULE_VERSION(pf, PF_MODVER);
#endif	/* __FreeBSD__ */
