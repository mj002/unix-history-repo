/*-
 * Copyright (c) 2004 Ruslan Ermilov and Vsevolod Lobko.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Lookup table support for ipfw.
 *
 * This file contains handlers for all generic tables' operations:
 * add/del/flush entries, list/dump tables etc..
 *
 * Table data modification is protected by both UH and runtimg lock
 * while reading configuration/data is protected by UH lock.
 *
 * Lookup algorithms for all table types are located in ip_fw_table_algo.c
 */

#include "opt_ipfw.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/queue.h>
#include <net/if.h>	/* ip_fw.h requires IFNAMSIZ */

#include <netinet/in.h>
#include <netinet/ip_var.h>	/* struct ipfw_rule_ref */
#include <netinet/ip_fw.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/ip_fw_table.h>

 /*
 * Table has the following `type` concepts:
 *
 * `no.type` represents lookup key type (addr, ifp, uid, etc..)
 * `vtype` represents table value type (currently U32)
 * `ftype` (at the moment )is pure userland field helping to properly
 *     format value data e.g. "value is IPv4 nexthop" or "value is DSCP"
 *     or "value is port".
 *
 */
struct table_config {
	struct named_object	no;
	uint8_t		vtype;		/* value type */
	uint8_t		vftype;		/* value format type */
	uint8_t		tflags;		/* type flags */
	uint8_t		locked;		/* 1 if locked from changes */
	uint32_t	count;		/* Number of records */
	uint32_t	limit;		/* Max number of records */
	uint8_t		linked;		/* 1 if already linked */
	uint8_t		ochanged;	/* used by set swapping */
	uint16_t	spare1;
	uint32_t	spare2;
	uint32_t	ocount;		/* used by set swapping */
	uint64_t	gencnt;		/* generation count */
	char		tablename[64];	/* table name */
	struct table_algo	*ta;	/* Callbacks for given algo */
	void		*astate;	/* algorithm state */
	struct table_info	ti;	/* data to put to table_info */
};

struct tables_config {
	struct namedobj_instance	*namehash;
	int				algo_count;
	struct table_algo 		*algo[256];
	struct table_algo		*def_algo[IPFW_TABLE_MAXTYPE + 1];
};

static struct table_config *find_table(struct namedobj_instance *ni,
    struct tid_info *ti);
static struct table_config *alloc_table_config(struct ip_fw_chain *ch,
    struct tid_info *ti, struct table_algo *ta, char *adata, uint8_t tflags,
    uint8_t vtype);
static void free_table_config(struct namedobj_instance *ni,
    struct table_config *tc);
static int create_table_internal(struct ip_fw_chain *ch, struct tid_info *ti,
    char *aname, ipfw_xtable_info *i, struct table_config **ptc,
    struct table_algo **pta, uint16_t *pkidx, int ref);
static void link_table(struct ip_fw_chain *ch, struct table_config *tc);
static void unlink_table(struct ip_fw_chain *ch, struct table_config *tc);
static int export_tables(struct ip_fw_chain *ch, ipfw_obj_lheader *olh,
    struct sockopt_data *sd);
static void export_table_info(struct ip_fw_chain *ch, struct table_config *tc,
    ipfw_xtable_info *i);
static int dump_table_tentry(void *e, void *arg);
static int dump_table_xentry(void *e, void *arg);

static int ipfw_dump_table_v0(struct ip_fw_chain *ch, struct sockopt_data *sd);
static int ipfw_dump_table_v1(struct ip_fw_chain *ch, struct sockopt_data *sd);
static int ipfw_manage_table_ent_v0(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd);
static int ipfw_manage_table_ent_v1(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd);
static int swap_tables(struct ip_fw_chain *ch, struct tid_info *a,
    struct tid_info *b);

static int check_table_space(struct ip_fw_chain *ch, struct table_config *tc,
    struct table_info *ti, uint32_t count);
static int destroy_table(struct ip_fw_chain *ch, struct tid_info *ti);

static struct table_algo *find_table_algo(struct tables_config *tableconf,
    struct tid_info *ti, char *name);

static void objheader_to_ti(struct _ipfw_obj_header *oh, struct tid_info *ti);
static void ntlv_to_ti(struct _ipfw_obj_ntlv *ntlv, struct tid_info *ti);
static int classify_table_opcode(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype);

#define	CHAIN_TO_TCFG(chain)	((struct tables_config *)(chain)->tblcfg)
#define	CHAIN_TO_NI(chain)	(CHAIN_TO_TCFG(chain)->namehash)
#define	KIDX_TO_TI(ch, k)	(&(((struct table_info *)(ch)->tablestate)[k]))

#define	TA_BUF_SZ	128	/* On-stack buffer for add/delete state */

/*
 * Checks if we're able to insert/update entry @tei into table
 * w.r.t @tc limits.
 * May alter @tei to indicate insertion error / insert
 * options.
 *
 * Returns 0 if operation can be performed/
 */
static int
check_table_limit(struct table_config *tc, struct tentry_info *tei)
{

	if (tc->limit == 0 || tc->count < tc->limit)
		return (0);

	if ((tei->flags & TEI_FLAGS_UPDATE) == 0) {
		/* Notify userland on error cause */
		tei->flags |= TEI_FLAGS_LIMIT;
		return (EFBIG);
	}

	/*
	 * We have UPDATE flag set.
	 * Permit updating record (if found),
	 * but restrict adding new one since we've
	 * already hit the limit.
	 */
	tei->flags |= TEI_FLAGS_DONTADD;

	return (0);
}

/*
 * Convert algorithm callback return code into
 * one of pre-defined states known by userland.
 */
static void
store_tei_result(struct tentry_info *tei, int do_add, int error, uint32_t num)
{
	int flag;

	flag = 0;

	switch (error) {
	case 0:
		if (do_add && num != 0)
			flag = TEI_FLAGS_ADDED;
		if (do_add == 0)
			flag = TEI_FLAGS_DELETED;
		break;
	case ENOENT:
		flag = TEI_FLAGS_NOTFOUND;
		break;
	case EEXIST:
		flag = TEI_FLAGS_EXISTS;
		break;
	default:
		flag = TEI_FLAGS_ERROR;
	}

	tei->flags |= flag;
}

/*
 * Creates and references table with default parameters.
 * Saves table config, algo and allocated kidx info @ptc, @pta and
 * @pkidx if non-zero.
 * Used for table auto-creation to support old binaries.
 *
 * Returns 0 on success.
 */
static int
create_table_compat(struct ip_fw_chain *ch, struct tid_info *ti,
    struct table_config **ptc, struct table_algo **pta, uint16_t *pkidx)
{
	ipfw_xtable_info xi;
	int error;

	memset(&xi, 0, sizeof(xi));
	/* Set u32 as default value type for legacy clients */
	xi.vtype = IPFW_VTYPE_U32;

	error = create_table_internal(ch, ti, NULL, &xi, ptc, pta, pkidx, 1);
	if (error != 0)
		return (error);

	return (0);
}

/*
 * Find and reference existing table optionally
 * creating new one.
 *
 * Saves found table config/table algo into @ptc / @pta.
 * Returns 0 if table was found/created and referenced
 * or non-zero return code.
 */
static int
find_ref_table(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint32_t count, int do_add,
    struct table_config **ptc, struct table_algo **pta)
{
	struct namedobj_instance *ni;
	struct table_config *tc;
	struct table_algo *ta;
	int error;

	IPFW_UH_WLOCK(ch);

	ni = CHAIN_TO_NI(ch);
	tc = NULL;
	ta = NULL;
	if ((tc = find_table(ni, ti)) != NULL) {
		/* check table type */
		if (tc->no.type != ti->type) {
			IPFW_UH_WUNLOCK(ch);
			return (EINVAL);
		}

		if (tc->locked != 0) {
			IPFW_UH_WUNLOCK(ch);
			return (EACCES);
		}

		/* Try to exit early on limit hit */
		if (do_add != 0 && count == 1 &&
		    check_table_limit(tc, tei) != 0) {
			IPFW_UH_WUNLOCK(ch);
			return (EFBIG);
		}

		/* Reference and unlock */
		tc->no.refcnt++;
		ta = tc->ta;
	}
	IPFW_UH_WUNLOCK(ch);

	if (tc == NULL) {
		if (do_add == 0)
			return (ESRCH);

		/* Compability mode: create new table for old clients */
		if ((tei->flags & TEI_FLAGS_COMPAT) == 0)
			return (ESRCH);

		error = create_table_compat(ch, ti, &tc, &ta, NULL);
		if (error != 0)
			return (error);

		/* OK, now we've got referenced table. */
	}

	*ptc = tc;
	*pta = ta;
	return (0);
}

/*
 * Rolls back already @added to @tc entries using state arrat @ta_buf_m.
 * Assume the following layout:
 * 1) ADD state (ta_buf_m[0] ... t_buf_m[added - 1]) for handling update cases
 * 2) DEL state (ta_buf_m[count[ ... t_buf_m[count + added - 1])
 *   for storing deleted state
 */
static void
rollback_added_entries(struct ip_fw_chain *ch, struct table_config *tc,
    struct table_info *tinfo, struct tentry_info *tei, caddr_t ta_buf_m,
    uint32_t count, uint32_t added)
{
	struct table_algo *ta;
	struct tentry_info *ptei;
	caddr_t v, vv;
	size_t ta_buf_sz;
	int error, i;
	uint32_t num;

	IPFW_UH_WLOCK_ASSERT(ch);

	ta = tc->ta;
	ta_buf_sz = ta->ta_buf_size;
	v = ta_buf_m;
	vv = v + count * ta_buf_sz;
	for (i = 0; i < added; i++, v += ta_buf_sz, vv += ta_buf_sz) {
		ptei = &tei[i];
		if ((ptei->flags & TEI_FLAGS_UPDATED) != 0) {

			/*
			 * We have old value stored by previous
			 * call in @ptei->value. Do add once again
			 * to restore it.
			 */
			error = ta->add(tc->astate, tinfo, ptei, v, &num);
			KASSERT(error == 0, ("rollback UPDATE fail"));
			KASSERT(num == 0, ("rollback UPDATE fail2"));
			continue;
		}

		error = ta->prepare_del(ch, ptei, vv);
		KASSERT(error == 0, ("pre-rollback INSERT failed"));
		error = ta->del(tc->astate, tinfo, ptei, vv, &num);
		KASSERT(error == 0, ("rollback INSERT failed"));
		tc->count -= num;
	}
}

/*
 * Prepares add/del state for all @count entries in @tei.
 * Uses either stack buffer (@ta_buf) or allocates a new one.
 * Stores pointer to allocated buffer back to @ta_buf.
 *
 * Returns 0 on success.
 */
static int
prepare_batch_buffer(struct ip_fw_chain *ch, struct table_algo *ta,
    struct tentry_info *tei, uint32_t count, int do_add, caddr_t *ta_buf)
{
	caddr_t ta_buf_m, v;
	size_t ta_buf_sz, sz;
	struct tentry_info *ptei;
	int error, i;

	error = 0;
	ta_buf_sz = ta->ta_buf_size;
	if (count == 1) {
		/* Sigle add/delete, use on-stack buffer */
		memset(*ta_buf, 0, TA_BUF_SZ);
		ta_buf_m = *ta_buf;
	} else {

		/*
		 * Multiple adds/deletes, allocate larger buffer
		 *
		 * Note we need 2xcount buffer for add case:
		 * we have hold both ADD state
		 * and DELETE state (this may be needed
		 * if we need to rollback all changes)
		 */
		sz = count * ta_buf_sz;
		ta_buf_m = malloc((do_add != 0) ? sz * 2 : sz, M_TEMP,
		    M_WAITOK | M_ZERO);
	}

	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta_buf_sz) {
		ptei = &tei[i];
		error = (do_add != 0) ?
		    ta->prepare_add(ch, ptei, v) : ta->prepare_del(ch, ptei, v);

		/*
		 * Some syntax error (incorrect mask, or address, or
		 * anything). Return error regardless of atomicity
		 * settings.
		 */
		if (error != 0)
			break;
	}

	*ta_buf = ta_buf_m;
	return (error);
}

/*
 * Flushes allocated state for each @count entries in @tei.
 * Frees @ta_buf_m if differs from stack buffer @ta_buf.
 */
static void
flush_batch_buffer(struct ip_fw_chain *ch, struct table_algo *ta,
    struct tentry_info *tei, uint32_t count, int do_add, int rollback,
    caddr_t ta_buf_m, caddr_t ta_buf)
{
	caddr_t v;
	size_t ta_buf_sz;
	int i;

	ta_buf_sz = ta->ta_buf_size;

	/* Run cleaning callback anyway */
	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta_buf_sz)
		ta->flush_entry(ch, &tei[i], v);

	/* Clean up "deleted" state in case of rollback */
	if (rollback != 0) {
		v = ta_buf_m + count * ta_buf_sz;
		for (i = 0; i < count; i++, v += ta_buf_sz)
			ta->flush_entry(ch, &tei[i], v);
	}

	if (ta_buf_m != ta_buf)
		free(ta_buf_m, M_TEMP);
}

/*
 * Adds/updates one or more entries in table @ti.
 * Function references @ti first to ensure table won't
 * disappear or change its type.
 * After that, prepare_add callback is called for each @tei entry.
 * Next, we try to add each entry under UH+WHLOCK
 * using add() callback.
 * Finally, we free all state by calling flush_entry callback
 * for each @tei.
 *
 * Returns 0 on success.
 */
int
add_table_entry(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint8_t flags, uint32_t count)
{
	struct table_config *tc;
	struct table_algo *ta;
	uint16_t kidx;
	int error, first_error, i, rollback;
	uint32_t num, numadd;
	struct tentry_info *ptei;
	char ta_buf[TA_BUF_SZ];
	caddr_t ta_buf_m, v;

	/*
	 * Find and reference existing table.
	 */
	if ((error = find_ref_table(ch, ti, tei, count, 1, &tc, &ta)) != 0)
		return (error);

	/* Allocate memory and prepare record(s) */
	rollback = 0;
	/* Pass stack buffer by default */
	ta_buf_m = ta_buf;
	error = prepare_batch_buffer(ch, ta, tei, count, 1, &ta_buf_m);
	if (error != 0)
		goto cleanup;

	IPFW_UH_WLOCK(ch);

	/* Drop reference we've used in first search */
	tc->no.refcnt--;

	/*
	 * Ensure we are able to add all entries without additional
	 * memory allocations. May release/reacquire UH_WLOCK.
	 * check_table_space() guarantees us @tc won't disappear
	 * by referencing it internally.
	 */
	kidx = tc->no.kidx;
	error = check_table_space(ch, tc, KIDX_TO_TI(ch, kidx), count);
	if (error != 0) {
		IPFW_UH_WUNLOCK(ch);
		goto cleanup;
	}

	/*
	 * Check if table algo is still the same.
	 * (changed ta may be the result of table swap).
	 */
	if (ta != tc->ta) {
		IPFW_UH_WUNLOCK(ch);
		error = EINVAL;
		goto cleanup;
	}

	/* We've got valid table in @tc. Let's try to add data */
	kidx = tc->no.kidx;
	ta = tc->ta;
	numadd = 0;
	first_error = 0;

	IPFW_WLOCK(ch);

	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta->ta_buf_size) {
		ptei = &tei[i];
		num = 0;
		/* check limit before adding */
		if ((error = check_table_limit(tc, ptei)) == 0) {
			error = ta->add(tc->astate, KIDX_TO_TI(ch, kidx),
			    ptei, v, &num);
			/* Set status flag to inform userland */
			store_tei_result(ptei, 1, error, num);
		}
		if (error == 0) {
			/* Update number of records to ease limit checking */
			tc->count += num;
			numadd += num;
			continue;
		}

		if (first_error == 0)
			first_error = error;

		/*
		 * Some error have happened. Check our atomicity
		 * settings: continue if atomicity is not required,
		 * rollback changes otherwise.
		 */
		if ((flags & IPFW_CTF_ATOMIC) == 0)
			continue;

		rollback_added_entries(ch, tc, KIDX_TO_TI(ch, kidx),
		    tei, ta_buf_m, count, i);
		break;
	}

	IPFW_WUNLOCK(ch);

	/* Permit post-add algorithm grow/rehash. */
	if (numadd != 0)
		check_table_space(ch, tc, KIDX_TO_TI(ch, kidx), 0);

	IPFW_UH_WUNLOCK(ch);

	/* Return first error to user, if any */
	error = first_error;

cleanup:
	flush_batch_buffer(ch, ta, tei, count, 1, rollback, ta_buf_m, ta_buf);

	return (error);
}

/*
 * Deletes one or more entries in table @ti.
 *
 * Returns 0 on success.
 */
int
del_table_entry(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint8_t flags, uint32_t count)
{
	struct table_config *tc;
	struct table_algo *ta;
	struct tentry_info *ptei;
	uint16_t kidx;
	int error, first_error, i;
	uint32_t num, numdel;
	char ta_buf[TA_BUF_SZ];
	caddr_t ta_buf_m, v;

	/*
	 * Find and reference existing table.
	 */
	if ((error = find_ref_table(ch, ti, tei, count, 0, &tc, &ta)) != 0)
		return (error);

	/* Allocate memory and prepare record(s) */
	/* Pass stack buffer by default */
	ta_buf_m = ta_buf;
	error = prepare_batch_buffer(ch, ta, tei, count, 0, &ta_buf_m);
	if (error != 0)
		goto cleanup;

	IPFW_UH_WLOCK(ch);

	/* Drop reference we've used in first search */
	tc->no.refcnt--;

	/*
	 * Check if table algo is still the same.
	 * (changed ta may be the result of table swap).
	 */
	if (ta != tc->ta) {
		IPFW_UH_WUNLOCK(ch);
		error = EINVAL;
		goto cleanup;
	}

	kidx = tc->no.kidx;
	numdel = 0;
	first_error = 0;

	IPFW_WLOCK(ch);
	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta->ta_buf_size) {
		ptei = &tei[i];
		num = 0;
		error = ta->del(tc->astate, KIDX_TO_TI(ch, kidx), ptei, v,
		    &num);
		/* Save state for userland */
		store_tei_result(ptei, 0, error, num);
		if (error != 0 && first_error == 0)
			first_error = error;
		tc->count -= num;
		numdel += num;
	}
	IPFW_WUNLOCK(ch);

	if (numdel != 0) {
		/* Run post-del hook to permit shrinking */
		error = check_table_space(ch, tc, KIDX_TO_TI(ch, kidx), 0);
	}

	IPFW_UH_WUNLOCK(ch);

	/* Return first error to user, if any */
	error = first_error;

cleanup:
	flush_batch_buffer(ch, ta, tei, count, 0, 0, ta_buf_m, ta_buf);

	return (error);
}

/*
 * Ensure that table @tc has enough space to add @count entries without
 * need for reallocation.
 *
 * Callbacks order:
 * 0) need_modify() (UH_WLOCK) - checks if @count items can be added w/o resize.
 *
 * 1) alloc_modify (no locks, M_WAITOK) - alloc new state based on @pflags.
 * 2) prepare_modifyt (UH_WLOCK) - copy old data into new storage
 * 3) modify (UH_WLOCK + WLOCK) - switch pointers
 * 4) flush_modify (UH_WLOCK) - free state, if needed
 *
 * Returns 0 on success.
 */
static int
check_table_space(struct ip_fw_chain *ch, struct table_config *tc,
    struct table_info *ti, uint32_t count)
{
	struct table_algo *ta;
	uint64_t pflags;
	char ta_buf[TA_BUF_SZ];
	int error;

	IPFW_UH_WLOCK_ASSERT(ch);

	error = 0;
	ta = tc->ta;
	if (ta->need_modify == NULL)
		return (0);

	/* Acquire reference not to loose @tc between locks/unlocks */
	tc->no.refcnt++;

	/*
	 * TODO: think about avoiding race between large add/large delete
	 * operation on algorithm which implements shrinking along with
	 * growing.
	 */
	while (true) {
		pflags = 0;
		if (ta->need_modify(tc->astate, ti, count, &pflags) == 0) {
			error = 0;
			break;
		}

		/* We have to shrink/grow table */
		IPFW_UH_WUNLOCK(ch);

		memset(&ta_buf, 0, sizeof(ta_buf));
		if ((error = ta->prepare_mod(ta_buf, &pflags)) != 0) {
			IPFW_UH_WLOCK(ch);
			break;
		}

		IPFW_UH_WLOCK(ch);

		/* Check if we still need to alter table */
		ti = KIDX_TO_TI(ch, tc->no.kidx);
		if (ta->need_modify(tc->astate, ti, count, &pflags) == 0) {
			IPFW_UH_WUNLOCK(ch);

			/*
			 * Other thread has already performed resize.
			 * Flush our state and return.
			 */
			ta->flush_mod(ta_buf);
			break;
		}
	
		error = ta->fill_mod(tc->astate, ti, ta_buf, &pflags);
		if (error == 0) {
			/* Do actual modification */
			IPFW_WLOCK(ch);
			ta->modify(tc->astate, ti, ta_buf, pflags);
			IPFW_WUNLOCK(ch);
		}

		/* Anyway, flush data and retry */
		ta->flush_mod(ta_buf);
	}

	tc->no.refcnt--;
	return (error);
}

/*
 * Selects appropriate table operation handler
 * depending on opcode version.
 */
int
ipfw_manage_table_ent(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	int error;

	switch (op3->version) {
	case 0:
		error = ipfw_manage_table_ent_v0(ch, op3, sd);
		break;
	case 1:
		error = ipfw_manage_table_ent_v1(ch, op3, sd);
		break;
	default:
		error = ENOTSUP;
	}

	return (error);
}

/*
 * Adds or deletes record in table.
 * Data layout (v0):
 * Request: [ ip_fw3_opheader ipfw_table_xentry ]
 *
 * Returns 0 on success
 */
static int
ipfw_manage_table_ent_v0(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_table_xentry *xent;
	struct tentry_info tei;
	struct tid_info ti;
	int error, hdrlen, read;

	hdrlen = offsetof(ipfw_table_xentry, k);

	/* Check minimum header size */
	if (sd->valsize < (sizeof(*op3) + hdrlen))
		return (EINVAL);

	read = sizeof(ip_fw3_opheader);

	/* Check if xentry len field is valid */
	xent = (ipfw_table_xentry *)(op3 + 1);
	if (xent->len < hdrlen || xent->len + read > sd->valsize)
		return (EINVAL);
	
	memset(&tei, 0, sizeof(tei));
	tei.paddr = &xent->k;
	tei.masklen = xent->masklen;
	tei.value = xent->value;
	/* Old requests compability */
	tei.flags = TEI_FLAGS_COMPAT;
	if (xent->type == IPFW_TABLE_ADDR) {
		if (xent->len - hdrlen == sizeof(in_addr_t))
			tei.subtype = AF_INET;
		else
			tei.subtype = AF_INET6;
	}

	memset(&ti, 0, sizeof(ti));
	ti.uidx = xent->tbl;
	ti.type = xent->type;

	error = (op3->opcode == IP_FW_TABLE_XADD) ?
	    add_table_entry(ch, &ti, &tei, 0, 1) :
	    del_table_entry(ch, &ti, &tei, 0, 1);

	return (error);
}

/*
 * Adds or deletes record in table.
 * Data layout (v1)(current):
 * Request: [ ipfw_obj_header
 *   ipfw_obj_ctlv(IPFW_TLV_TBLENT_LIST) [ ipfw_obj_tentry x N ]
 * ]
 *
 * Returns 0 on success
 */
static int
ipfw_manage_table_ent_v1(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_tentry *tent, *ptent;
	ipfw_obj_ctlv *ctlv;
	ipfw_obj_header *oh;
	struct tentry_info *ptei, tei, *tei_buf;
	struct tid_info ti;
	int error, i, kidx, read;

	/* Check minimum header size */
	if (sd->valsize < (sizeof(*oh) + sizeof(*ctlv)))
		return (EINVAL);

	/* Check if passed data is too long */
	if (sd->valsize != sd->kavail)
		return (EINVAL);

	oh = (ipfw_obj_header *)sd->kbuf;

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	read = sizeof(*oh);

	ctlv = (ipfw_obj_ctlv *)(oh + 1);
	if (ctlv->head.length + read != sd->valsize)
		return (EINVAL);

	read += sizeof(*ctlv);
	tent = (ipfw_obj_tentry *)(ctlv + 1);
	if (ctlv->count * sizeof(*tent) + read != sd->valsize)
		return (EINVAL);

	if (ctlv->count == 0)
		return (0);

	/*
	 * Mark entire buffer as "read".
	 * This instructs sopt api write it back
	 * after function return.
	 */
	ipfw_get_sopt_header(sd, sd->valsize);

	/* Perform basic checks for each entry */
	ptent = tent;
	kidx = tent->idx;
	for (i = 0; i < ctlv->count; i++, ptent++) {
		if (ptent->head.length != sizeof(*ptent))
			return (EINVAL);
		if (ptent->idx != kidx)
			return (ENOTSUP);
	}

	/* Convert data into kernel request objects */
	objheader_to_ti(oh, &ti);
	ti.type = oh->ntlv.type;
	ti.uidx = kidx;

	/* Use on-stack buffer for single add/del */
	if (ctlv->count == 1) {
		memset(&tei, 0, sizeof(tei));
		tei_buf = &tei;
	} else
		tei_buf = malloc(ctlv->count * sizeof(tei), M_TEMP,
		    M_WAITOK | M_ZERO);

	ptei = tei_buf;
	ptent = tent;
	for (i = 0; i < ctlv->count; i++, ptent++, ptei++) {
		ptei->paddr = &ptent->k;
		ptei->subtype = ptent->subtype;
		ptei->masklen = ptent->masklen;
		if (ptent->head.flags & IPFW_TF_UPDATE)
			ptei->flags |= TEI_FLAGS_UPDATE;
		ptei->value = ptent->value;
	}

	error = (oh->opheader.opcode == IP_FW_TABLE_XADD) ?
	    add_table_entry(ch, &ti, tei_buf, ctlv->flags, ctlv->count) :
	    del_table_entry(ch, &ti, tei_buf, ctlv->flags, ctlv->count);

	/* Translate result back to userland */
	ptei = tei_buf;
	ptent = tent;
	for (i = 0; i < ctlv->count; i++, ptent++, ptei++) {
		if (ptei->flags & TEI_FLAGS_ADDED)
			ptent->result = IPFW_TR_ADDED;
		else if (ptei->flags & TEI_FLAGS_DELETED)
			ptent->result = IPFW_TR_DELETED;
		else if (ptei->flags & TEI_FLAGS_UPDATED)
			ptent->result = IPFW_TR_UPDATED;
		else if (ptei->flags & TEI_FLAGS_LIMIT)
			ptent->result = IPFW_TR_LIMIT;
		else if (ptei->flags & TEI_FLAGS_ERROR)
			ptent->result = IPFW_TR_ERROR;
		else if (ptei->flags & TEI_FLAGS_NOTFOUND)
			ptent->result = IPFW_TR_NOTFOUND;
		else if (ptei->flags & TEI_FLAGS_EXISTS)
			ptent->result = IPFW_TR_EXISTS;
	}

	if (tei_buf != &tei)
		free(tei_buf, M_TEMP);

	return (error);
}

/*
 * Looks up an entry in given table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_obj_tentry ]
 * Reply: [ ipfw_obj_header ipfw_obj_tentry ]
 *
 * Returns 0 on success
 */
int
ipfw_find_table_entry(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_tentry *tent;
	ipfw_obj_header *oh;
	struct tid_info ti;
	struct table_config *tc;
	struct table_algo *ta;
	struct table_info *kti;
	struct namedobj_instance *ni;
	int error;
	size_t sz;

	/* Check minimum header size */
	sz = sizeof(*oh) + sizeof(*tent);
	if (sd->valsize != sz)
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	tent = (ipfw_obj_tentry *)(oh + 1);

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	objheader_to_ti(oh, &ti);
	ti.type = oh->ntlv.type;
	ti.uidx = tent->idx;

	IPFW_UH_RLOCK(ch);
	ni = CHAIN_TO_NI(ch);

	/*
	 * Find existing table and check its type .
	 */
	ta = NULL;
	if ((tc = find_table(ni, &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ESRCH);
	}

	/* check table type */
	if (tc->no.type != ti.type) {
		IPFW_UH_RUNLOCK(ch);
		return (EINVAL);
	}

	kti = KIDX_TO_TI(ch, tc->no.kidx);
	ta = tc->ta;

	if (ta->find_tentry == NULL)
		return (ENOTSUP);

	error = ta->find_tentry(tc->astate, kti, tent);

	IPFW_UH_RUNLOCK(ch);

	return (error);
}

/*
 * Flushes all entries or destroys given table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 *
 * Returns 0 on success
 */
int
ipfw_flush_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	int error;
	struct _ipfw_obj_header *oh;
	struct tid_info ti;

	if (sd->valsize != sizeof(*oh))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)op3;
	objheader_to_ti(oh, &ti);

	if (op3->opcode == IP_FW_TABLE_XDESTROY)
		error = destroy_table(ch, &ti);
	else if (op3->opcode == IP_FW_TABLE_XFLUSH)
		error = flush_table(ch, &ti);
	else
		return (ENOTSUP);

	return (error);
}

/*
 * Flushes given table.
 *
 * Function create new table instance with the same
 * parameters, swaps it with old one and
 * flushes state without holding any locks.
 *
 * Returns 0 on success.
 */
int
flush_table(struct ip_fw_chain *ch, struct tid_info *ti)
{
	struct namedobj_instance *ni;
	struct table_config *tc;
	struct table_algo *ta;
	struct table_info ti_old, ti_new, *tablestate;
	void *astate_old, *astate_new;
	char algostate[64], *pstate;
	int error;
	uint16_t kidx;
	uint8_t tflags;

	/*
	 * Stage 1: save table algoritm.
	 * Reference found table to ensure it won't disappear.
	 */
	IPFW_UH_WLOCK(ch);
	ni = CHAIN_TO_NI(ch);
	if ((tc = find_table(ni, ti)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}
	ta = tc->ta;
	/* Do not flush readonly tables */
	if ((ta->flags & TA_FLAG_READONLY) != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EACCES);
	}
	tc->no.refcnt++;
	/* Save startup algo parameters */
	if (ta->print_config != NULL) {
		ta->print_config(tc->astate, KIDX_TO_TI(ch, tc->no.kidx),
		    algostate, sizeof(algostate));
		pstate = algostate;
	} else
		pstate = NULL;
	tflags = tc->tflags;
	IPFW_UH_WUNLOCK(ch);

	/*
	 * Stage 2: allocate new table instance using same algo.
	 */
	memset(&ti_new, 0, sizeof(struct table_info));
	if ((error = ta->init(ch, &astate_new, &ti_new, pstate, tflags)) != 0) {
		IPFW_UH_WLOCK(ch);
		tc->no.refcnt--;
		IPFW_UH_WUNLOCK(ch);
		return (error);
	}

	/*
	 * Stage 3: swap old state pointers with newly-allocated ones.
	 * Decrease refcount.
	 */
	IPFW_UH_WLOCK(ch);

	ni = CHAIN_TO_NI(ch);
	kidx = tc->no.kidx;
	tablestate = (struct table_info *)ch->tablestate;

	IPFW_WLOCK(ch);
	ti_old = tablestate[kidx];
	tablestate[kidx] = ti_new;
	IPFW_WUNLOCK(ch);

	astate_old = tc->astate;
	tc->astate = astate_new;
	tc->ti = ti_new;
	tc->count = 0;
	tc->no.refcnt--;

	IPFW_UH_WUNLOCK(ch);

	/*
	 * Stage 4: perform real flush.
	 */
	ta->destroy(astate_old, &ti_old);

	return (0);
}

/*
 * Swaps two tables.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_obj_ntlv ]
 *
 * Returns 0 on success
 */
int
ipfw_swap_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	int error;
	struct _ipfw_obj_header *oh;
	struct tid_info ti_a, ti_b;

	if (sd->valsize != sizeof(*oh) + sizeof(ipfw_obj_ntlv))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)op3;
	ntlv_to_ti(&oh->ntlv, &ti_a);
	ntlv_to_ti((ipfw_obj_ntlv *)(oh + 1), &ti_b);

	error = swap_tables(ch, &ti_a, &ti_b);

	return (error);
}

/*
 * Swaps two tables of the same type/valtype.
 *
 * Checks if tables are compatible and limits
 * permits swap, than actually perform swap.
 *
 * Each table consists of 2 different parts:
 * config:
 *   @tc (with name, set, kidx) and rule bindings, which is "stable".
 *   number of items
 *   table algo
 * runtime:
 *   runtime data @ti (ch->tablestate)
 *   runtime cache in @tc
 *   algo-specific data (@tc->astate)
 *
 * So we switch:
 *  all runtime data
 *   number of items
 *   table algo
 *
 * After that we call @ti change handler for each table.
 *
 * Note that referencing @tc won't protect tc->ta from change.
 * XXX: Do we need to restrict swap between locked tables?
 * XXX: Do we need to exchange ftype?
 *
 * Returns 0 on success.
 */
static int
swap_tables(struct ip_fw_chain *ch, struct tid_info *a,
    struct tid_info *b)
{
	struct namedobj_instance *ni;
	struct table_config *tc_a, *tc_b;
	struct table_algo *ta;
	struct table_info ti, *tablestate;
	void *astate;
	uint32_t count;

	/*
	 * Stage 1: find both tables and ensure they are of
	 * the same type.
	 */
	IPFW_UH_WLOCK(ch);
	ni = CHAIN_TO_NI(ch);
	if ((tc_a = find_table(ni, a)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}
	if ((tc_b = find_table(ni, b)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}

	/* It is very easy to swap between the same table */
	if (tc_a == tc_b) {
		IPFW_UH_WUNLOCK(ch);
		return (0);
	}

	/* Check type and value are the same */
	if (tc_a->no.type != tc_b->no.type || tc_a->tflags != tc_b->tflags ||
	    tc_a->vtype != tc_b->vtype) {
		IPFW_UH_WUNLOCK(ch);
		return (EINVAL);
	}

	/* Check limits before swap */
	if ((tc_a->limit != 0 && tc_b->count > tc_a->limit) ||
	    (tc_b->limit != 0 && tc_a->count > tc_b->limit)) {
		IPFW_UH_WUNLOCK(ch);
		return (EFBIG);
	}

	/* Check if one of the tables is readonly */
	if (((tc_a->ta->flags | tc_b->ta->flags) & TA_FLAG_READONLY) != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EACCES);
	}

	/* Everything is fine, prepare to swap */
	tablestate = (struct table_info *)ch->tablestate;
	ti = tablestate[tc_a->no.kidx];
	ta = tc_a->ta;
	astate = tc_a->astate;
	count = tc_a->count;

	IPFW_WLOCK(ch);
	/* a <- b */
	tablestate[tc_a->no.kidx] = tablestate[tc_b->no.kidx];
	tc_a->ta = tc_b->ta;
	tc_a->astate = tc_b->astate;
	tc_a->count = tc_b->count;
	/* b <- a */
	tablestate[tc_b->no.kidx] = ti;
	tc_b->ta = ta;
	tc_b->astate = astate;
	tc_b->count = count;
	IPFW_WUNLOCK(ch);

	/* Ensure tc.ti copies are in sync */
	tc_a->ti = tablestate[tc_a->no.kidx];
	tc_b->ti = tablestate[tc_b->no.kidx];

	/* Notify both tables on @ti change */
	if (tc_a->ta->change_ti != NULL)
		tc_a->ta->change_ti(tc_a->astate, &tablestate[tc_a->no.kidx]);
	if (tc_b->ta->change_ti != NULL)
		tc_b->ta->change_ti(tc_b->astate, &tablestate[tc_b->no.kidx]);

	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Destroys table specified by @ti.
 * Data layout (v0)(current):
 * Request: [ ip_fw3_opheader ]
 *
 * Returns 0 on success
 */
static int
destroy_table(struct ip_fw_chain *ch, struct tid_info *ti)
{
	struct namedobj_instance *ni;
	struct table_config *tc;

	IPFW_UH_WLOCK(ch);

	ni = CHAIN_TO_NI(ch);
	if ((tc = find_table(ni, ti)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}

	/* Do not permit destroying referenced tables */
	if (tc->no.refcnt > 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EBUSY);
	}

	IPFW_WLOCK(ch);
	unlink_table(ch, tc);
	IPFW_WUNLOCK(ch);

	/* Free obj index */
	if (ipfw_objhash_free_idx(ni, tc->no.kidx) != 0)
		printf("Error unlinking kidx %d from table %s\n",
		    tc->no.kidx, tc->tablename);

	IPFW_UH_WUNLOCK(ch);

	free_table_config(ni, tc);

	return (0);
}

static void
destroy_table_locked(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{

	unlink_table((struct ip_fw_chain *)arg, (struct table_config *)no);
	if (ipfw_objhash_free_idx(ni, no->kidx) != 0)
		printf("Error unlinking kidx %d from table %s\n",
		    no->kidx, no->name);
	free_table_config(ni, (struct table_config *)no);
}

/*
 * Shuts tables module down.
 */
void
ipfw_destroy_tables(struct ip_fw_chain *ch)
{

	/* Remove all tables from working set */
	IPFW_UH_WLOCK(ch);
	IPFW_WLOCK(ch);
	ipfw_objhash_foreach(CHAIN_TO_NI(ch), destroy_table_locked, ch);
	IPFW_WUNLOCK(ch);
	IPFW_UH_WUNLOCK(ch);

	/* Free pointers itself */
	free(ch->tablestate, M_IPFW);

	ipfw_table_algo_destroy(ch);

	ipfw_objhash_destroy(CHAIN_TO_NI(ch));
	free(CHAIN_TO_TCFG(ch), M_IPFW);
}

/*
 * Starts tables module.
 */
int
ipfw_init_tables(struct ip_fw_chain *ch)
{
	struct tables_config *tcfg;

	/* Allocate pointers */
	ch->tablestate = malloc(V_fw_tables_max * sizeof(struct table_info),
	    M_IPFW, M_WAITOK | M_ZERO);

	tcfg = malloc(sizeof(struct tables_config), M_IPFW, M_WAITOK | M_ZERO);
	tcfg->namehash = ipfw_objhash_create(V_fw_tables_max);
	ch->tblcfg = tcfg;

	ipfw_table_algo_init(ch);

	return (0);
}

/*
 * Grow tables index.
 *
 * Returns 0 on success.
 */
int
ipfw_resize_tables(struct ip_fw_chain *ch, unsigned int ntables)
{
	unsigned int ntables_old, tbl;
	struct namedobj_instance *ni;
	void *new_idx, *old_tablestate, *tablestate;
	struct table_info *ti;
	struct table_config *tc;
	int i, new_blocks;

	/* Check new value for validity */
	if (ntables > IPFW_TABLES_MAX)
		ntables = IPFW_TABLES_MAX;

	/* Allocate new pointers */
	tablestate = malloc(ntables * sizeof(struct table_info),
	    M_IPFW, M_WAITOK | M_ZERO);

	ipfw_objhash_bitmap_alloc(ntables, (void *)&new_idx, &new_blocks);

	IPFW_UH_WLOCK(ch);

	tbl = (ntables >= V_fw_tables_max) ? V_fw_tables_max : ntables;
	ni = CHAIN_TO_NI(ch);

	/* Temporary restrict decreasing max_tables */
	if (ntables < V_fw_tables_max) {

		/*
		 * FIXME: Check if we really can shrink
		 */
		IPFW_UH_WUNLOCK(ch);
		return (EINVAL);
	}

	/* Copy table info/indices */
	memcpy(tablestate, ch->tablestate, sizeof(struct table_info) * tbl);
	ipfw_objhash_bitmap_merge(ni, &new_idx, &new_blocks);

	IPFW_WLOCK(ch);

	/* Change pointers */
	old_tablestate = ch->tablestate;
	ch->tablestate = tablestate;
	ipfw_objhash_bitmap_swap(ni, &new_idx, &new_blocks);

	ntables_old = V_fw_tables_max;
	V_fw_tables_max = ntables;

	IPFW_WUNLOCK(ch);

	/* Notify all consumers that their @ti pointer has changed */
	ti = (struct table_info *)ch->tablestate;
	for (i = 0; i < tbl; i++, ti++) {
		if (ti->lookup == NULL)
			continue;
		tc = (struct table_config *)ipfw_objhash_lookup_kidx(ni, i);
		if (tc == NULL || tc->ta->change_ti == NULL)
			continue;

		tc->ta->change_ti(tc->astate, ti);
	}

	IPFW_UH_WUNLOCK(ch);

	/* Free old pointers */
	free(old_tablestate, M_IPFW);
	ipfw_objhash_bitmap_free(new_idx, new_blocks);

	return (0);
}

/*
 * Switch between "set 0" and "rule's set" table binding,
 * Check all ruleset bindings and permits changing
 * IFF each binding has both rule AND table in default set (set 0).
 *
 * Returns 0 on success.
 */
int
ipfw_switch_tables_namespace(struct ip_fw_chain *ch, unsigned int sets)
{
	struct namedobj_instance *ni;
	struct named_object *no;
	struct ip_fw *rule;
	ipfw_insn *cmd;
	int cmdlen, i, l;
	uint16_t kidx;
	uint8_t type;

	IPFW_UH_WLOCK(ch);

	if (V_fw_tables_sets == sets) {
		IPFW_UH_WUNLOCK(ch);
		return (0);
	}

	ni = CHAIN_TO_NI(ch);

	/*
	 * Scan all rules and examine tables opcodes.
	 */
	for (i = 0; i < ch->n_rules; i++) {
		rule = ch->map[i];

		l = rule->cmd_len;
		cmd = rule->cmd;
		cmdlen = 0;
		for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
			cmdlen = F_LEN(cmd);

			if (classify_table_opcode(cmd, &kidx, &type) != 0)
				continue;

			no = ipfw_objhash_lookup_kidx(ni, kidx);

			/* Check if both table object and rule has the set 0 */
			if (no->set != 0 || rule->set != 0) {
				IPFW_UH_WUNLOCK(ch);
				return (EBUSY);
			}

		}
	}
	V_fw_tables_sets = sets;

	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Lookup an IP @addr in table @tbl.
 * Stores found value in @val.
 *
 * Returns 1 if @addr was found.
 */
int
ipfw_lookup_table(struct ip_fw_chain *ch, uint16_t tbl, in_addr_t addr,
    uint32_t *val)
{
	struct table_info *ti;

	ti = KIDX_TO_TI(ch, tbl);

	return (ti->lookup(ti, &addr, sizeof(in_addr_t), val));
}

/*
 * Lookup an arbtrary key @paddr of legth @plen in table @tbl.
 * Stores found value in @val.
 *
 * Returns 1 if key was found.
 */
int
ipfw_lookup_table_extended(struct ip_fw_chain *ch, uint16_t tbl, uint16_t plen,
    void *paddr, uint32_t *val)
{
	struct table_info *ti;

	ti = KIDX_TO_TI(ch, tbl);

	return (ti->lookup(ti, paddr, plen, val));
}

/*
 * Info/List/dump support for tables.
 *
 */

/*
 * High-level 'get' cmds sysctl handlers
 */

/*
 * Lists all tables currently available in kernel.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ], size = ipfw_obj_lheader.size
 * Reply: [ ipfw_obj_lheader ipfw_xtable_info x N ]
 *
 * Returns 0 on success
 */
int
ipfw_list_tables(struct ip_fw_chain *ch, struct sockopt_data *sd)
{
	struct _ipfw_obj_lheader *olh;
	int error;

	olh = (struct _ipfw_obj_lheader *)ipfw_get_sopt_header(sd,sizeof(*olh));
	if (olh == NULL)
		return (EINVAL);
	if (sd->valsize < olh->size)
		return (EINVAL);

	IPFW_UH_RLOCK(ch);
	error = export_tables(ch, olh, sd);
	IPFW_UH_RUNLOCK(ch);

	return (error);
}

/*
 * Store table info to buffer provided by @sd.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_xtable_info(empty)]
 * Reply: [ ipfw_obj_header ipfw_xtable_info ]
 *
 * Returns 0 on success.
 */
int
ipfw_describe_table(struct ip_fw_chain *ch, struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	struct table_config *tc;
	struct tid_info ti;
	size_t sz;

	sz = sizeof(*oh) + sizeof(ipfw_xtable_info);
	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	if (oh == NULL)
		return (EINVAL);

	objheader_to_ti(oh, &ti);

	IPFW_UH_RLOCK(ch);
	if ((tc = find_table(CHAIN_TO_NI(ch), &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ESRCH);
	}

	export_table_info(ch, tc, (ipfw_xtable_info *)(oh + 1));
	IPFW_UH_RUNLOCK(ch);

	return (0);
}

/*
 * Modifies existing table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_xtable_info ]
 *
 * Returns 0 on success
 */
int
ipfw_modify_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	ipfw_xtable_info *i;
	char *tname;
	struct tid_info ti;
	struct namedobj_instance *ni;
	struct table_config *tc;

	if (sd->valsize != sizeof(*oh) + sizeof(ipfw_xtable_info))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)sd->kbuf;
	i = (ipfw_xtable_info *)(oh + 1);

	/*
	 * Verify user-supplied strings.
	 * Check for null-terminated/zero-length strings/
	 */
	tname = oh->ntlv.name;
	if (ipfw_check_table_name(tname) != 0)
		return (EINVAL);

	objheader_to_ti(oh, &ti);
	ti.type = i->type;

	IPFW_UH_WLOCK(ch);
	ni = CHAIN_TO_NI(ch);
	if ((tc = find_table(ni, &ti)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}

	/* Do not support any modifications for readonly tables */
	if ((tc->ta->flags & TA_FLAG_READONLY) != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EACCES);
	}

	if ((i->mflags & IPFW_TMFLAGS_FTYPE) != 0)
		tc->vftype = i->vftype;
	if ((i->mflags & IPFW_TMFLAGS_LIMIT) != 0)
		tc->limit = i->limit;
	if ((i->mflags & IPFW_TMFLAGS_LOCK) != 0)
		tc->locked = ((i->flags & IPFW_TGFLAGS_LOCKED) != 0);
	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Creates new table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_xtable_info ]
 *
 * Returns 0 on success
 */
int
ipfw_create_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	ipfw_xtable_info *i;
	char *tname, *aname;
	struct tid_info ti;
	struct namedobj_instance *ni;
	struct table_config *tc;

	if (sd->valsize != sizeof(*oh) + sizeof(ipfw_xtable_info))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)sd->kbuf;
	i = (ipfw_xtable_info *)(oh + 1);

	/*
	 * Verify user-supplied strings.
	 * Check for null-terminated/zero-length strings/
	 */
	tname = oh->ntlv.name;
	aname = i->algoname;
	if (ipfw_check_table_name(tname) != 0 ||
	    strnlen(aname, sizeof(i->algoname)) == sizeof(i->algoname))
		return (EINVAL);

	if (aname[0] == '\0') {
		/* Use default algorithm */
		aname = NULL;
	}

	objheader_to_ti(oh, &ti);
	ti.type = i->type;

	ni = CHAIN_TO_NI(ch);

	IPFW_UH_RLOCK(ch);
	if ((tc = find_table(ni, &ti)) != NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (EEXIST);
	}
	IPFW_UH_RUNLOCK(ch);

	return (create_table_internal(ch, &ti, aname, i, NULL, NULL, NULL, 0));
}

/*
 * Creates new table based on @ti and @aname.
 *
 * Relies on table name checking inside find_name_tlv()
 * Assume @aname to be checked and valid.
 * Stores allocated table config, used algo and kidx
 * inside @ptc, @pta and @pkidx (if non-NULL).
 * Reference created table if @compat is non-zero.
 *
 * Returns 0 on success.
 */
static int
create_table_internal(struct ip_fw_chain *ch, struct tid_info *ti,
    char *aname, ipfw_xtable_info *i, struct table_config **ptc,
    struct table_algo **pta, uint16_t *pkidx, int compat)
{
	struct namedobj_instance *ni;
	struct table_config *tc, *tc_new, *tmp;
	struct table_algo *ta;
	uint16_t kidx;

	ni = CHAIN_TO_NI(ch);

	ta = find_table_algo(CHAIN_TO_TCFG(ch), ti, aname);
	if (ta == NULL)
		return (ENOTSUP);

	tc = alloc_table_config(ch, ti, ta, aname, i->tflags, i->vtype);
	if (tc == NULL)
		return (ENOMEM);

	tc->vftype = i->vftype;
	tc->limit = i->limit;
	if (ta->flags & TA_FLAG_READONLY)
		tc->locked = 1;
	else
		tc->locked = (i->flags & IPFW_TGFLAGS_LOCKED) != 0;

	IPFW_UH_WLOCK(ch);

	/* Check if table has been already created */
	tc_new = find_table(ni, ti);
	if (tc_new != NULL) {

		/*
		 * Compat: do not fail if we're
		 * requesting to create existing table
		 * which has the same type / vtype
		 */
		if (compat == 0 || tc_new->no.type != tc->no.type ||
		    tc_new->vtype != tc->vtype) {
			IPFW_UH_WUNLOCK(ch);
			free_table_config(ni, tc);
			return (EEXIST);
		}

		/* Exchange tc and tc_new for proper refcounting & freeing */
		tmp = tc;
		tc = tc_new;
		tc_new = tmp;
	} else {
		/* New table */
		if (ipfw_objhash_alloc_idx(ni, &kidx) != 0) {
			IPFW_UH_WUNLOCK(ch);
			printf("Unable to allocate table index."
			    " Consider increasing net.inet.ip.fw.tables_max");
			free_table_config(ni, tc);
			return (EBUSY);
		}
		tc->no.kidx = kidx;

		IPFW_WLOCK(ch);
		link_table(ch, tc);
		IPFW_WUNLOCK(ch);
	}

	if (compat != 0)
		tc->no.refcnt++;
	if (ptc != NULL)
		*ptc = tc;
	if (pta != NULL)
		*pta = ta;
	if (pkidx != NULL)
		*pkidx = tc->no.kidx;

	IPFW_UH_WUNLOCK(ch);

	if (tc_new != NULL)
		free_table_config(ni, tc_new);

	return (0);
}

static void
ntlv_to_ti(ipfw_obj_ntlv *ntlv, struct tid_info *ti)
{

	memset(ti, 0, sizeof(struct tid_info));
	ti->set = ntlv->set;
	ti->uidx = ntlv->idx;
	ti->tlvs = ntlv;
	ti->tlen = ntlv->head.length;
}

static void
objheader_to_ti(struct _ipfw_obj_header *oh, struct tid_info *ti)
{

	ntlv_to_ti(&oh->ntlv, ti);
}

/*
 * Exports basic table info as name TLV.
 * Used inside dump_static_rules() to provide info
 * about all tables referenced by current ruleset.
 *
 * Returns 0 on success.
 */
int
ipfw_export_table_ntlv(struct ip_fw_chain *ch, uint16_t kidx,
    struct sockopt_data *sd)
{
	struct namedobj_instance *ni;
	struct named_object *no;
	ipfw_obj_ntlv *ntlv;

	ni = CHAIN_TO_NI(ch);

	no = ipfw_objhash_lookup_kidx(ni, kidx);
	KASSERT(no != NULL, ("invalid table kidx passed"));

	ntlv = (ipfw_obj_ntlv *)ipfw_get_sopt_space(sd, sizeof(*ntlv));
	if (ntlv == NULL)
		return (ENOMEM);

	ntlv->head.type = IPFW_TLV_TBL_NAME;
	ntlv->head.length = sizeof(*ntlv);
	ntlv->idx = no->kidx;
	strlcpy(ntlv->name, no->name, sizeof(ntlv->name));

	return (0);
}

/*
 * Marks every table kidx used in @rule with bit in @bmask.
 * Used to generate bitmask of referenced tables for given ruleset.
 * 
 * Returns number of newly-referenced tables.
 */
int
ipfw_mark_table_kidx(struct ip_fw_chain *chain, struct ip_fw *rule,
    uint32_t *bmask)
{
	int cmdlen, l, count;
	ipfw_insn *cmd;
	uint16_t kidx;
	uint8_t type;

	l = rule->cmd_len;
	cmd = rule->cmd;
	cmdlen = 0;
	count = 0;
	for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
		cmdlen = F_LEN(cmd);

		if (classify_table_opcode(cmd, &kidx, &type) != 0)
			continue;

		if ((bmask[kidx / 32] & (1 << (kidx % 32))) == 0)
			count++;

		bmask[kidx / 32] |= 1 << (kidx % 32);
	}

	return (count);
}

struct dump_args {
	struct table_info *ti;
	struct table_config *tc;
	struct sockopt_data *sd;
	uint32_t cnt;
	uint16_t uidx;
	int error;
	ipfw_table_entry *ent;
	uint32_t size;
	ipfw_obj_tentry tent;
};

static int
count_ext_entries(void *e, void *arg)
{
	struct dump_args *da;

	da = (struct dump_args *)arg;
	da->cnt++;

	return (0);
}

/*
 * Gets number of items from table either using
 * internal counter or calling algo callback for
 * externally-managed tables.
 *
 * Returns number of records.
 */
static uint32_t
table_get_count(struct ip_fw_chain *ch, struct table_config *tc)
{
	struct table_info *ti;
	struct table_algo *ta;
	struct dump_args da;

	ti = KIDX_TO_TI(ch, tc->no.kidx);
	ta = tc->ta;

	/* Use internal counter for self-managed tables */
	if ((ta->flags & TA_FLAG_READONLY) == 0)
		return (tc->count);

	/* Use callback to quickly get number of items */
	if ((ta->flags & TA_FLAG_EXTCOUNTER) != 0)
		return (ta->get_count(tc->astate, ti));

	/* Count number of iterms ourselves */
	memset(&da, 0, sizeof(da));
	ta->foreach(tc->astate, ti, count_ext_entries, &da);

	return (da.cnt);
}

/*
 * Exports table @tc info into standard ipfw_xtable_info format.
 */
static void
export_table_info(struct ip_fw_chain *ch, struct table_config *tc,
    ipfw_xtable_info *i)
{
	struct table_info *ti;
	struct table_algo *ta;
	
	i->type = tc->no.type;
	i->tflags = tc->tflags;
	i->vtype = tc->vtype;
	i->vftype = tc->vftype;
	i->set = tc->no.set;
	i->kidx = tc->no.kidx;
	i->refcnt = tc->no.refcnt;
	i->count = table_get_count(ch, tc);
	i->limit = tc->limit;
	i->flags |= (tc->locked != 0) ? IPFW_TGFLAGS_LOCKED : 0;
	i->size = tc->count * sizeof(ipfw_obj_tentry);
	i->size += sizeof(ipfw_obj_header) + sizeof(ipfw_xtable_info);
	strlcpy(i->tablename, tc->tablename, sizeof(i->tablename));
	ti = KIDX_TO_TI(ch, tc->no.kidx);
	ta = tc->ta;
	if (ta->print_config != NULL) {
		/* Use algo function to print table config to string */
		ta->print_config(tc->astate, ti, i->algoname,
		    sizeof(i->algoname));
	} else
		strlcpy(i->algoname, ta->name, sizeof(i->algoname));
	/* Dump algo-specific data, if possible */
	if (ta->dump_tinfo != NULL) {
		ta->dump_tinfo(tc->astate, ti, &i->ta_info);
		i->ta_info.flags |= IPFW_TATFLAGS_DATA;
	}
}

struct dump_table_args {
	struct ip_fw_chain *ch;
	struct sockopt_data *sd;
};

static void
export_table_internal(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	ipfw_xtable_info *i;
	struct dump_table_args *dta;

	dta = (struct dump_table_args *)arg;

	i = (ipfw_xtable_info *)ipfw_get_sopt_space(dta->sd, sizeof(*i));
	KASSERT(i != 0, ("previously checked buffer is not enough"));

	export_table_info(dta->ch, (struct table_config *)no, i);
}

/*
 * Export all tables as ipfw_xtable_info structures to
 * storage provided by @sd.
 *
 * If supplied buffer is too small, fills in required size
 * and returns ENOMEM.
 * Returns 0 on success.
 */
static int
export_tables(struct ip_fw_chain *ch, ipfw_obj_lheader *olh,
    struct sockopt_data *sd)
{
	uint32_t size;
	uint32_t count;
	struct dump_table_args dta;

	count = ipfw_objhash_count(CHAIN_TO_NI(ch));
	size = count * sizeof(ipfw_xtable_info) + sizeof(ipfw_obj_lheader);

	/* Fill in header regadless of buffer size */
	olh->count = count;
	olh->objsize = sizeof(ipfw_xtable_info);

	if (size > olh->size) {
		olh->size = size;
		return (ENOMEM);
	}

	olh->size = size;

	dta.ch = ch;
	dta.sd = sd;

	ipfw_objhash_foreach(CHAIN_TO_NI(ch), export_table_internal, &dta);

	return (0);
}

int
ipfw_dump_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	int error;

	switch (op3->version) {
	case 0:
		error = ipfw_dump_table_v0(ch, sd);
		break;
	case 1:
		error = ipfw_dump_table_v1(ch, sd);
		break;
	default:
		error = ENOTSUP;
	}

	return (error);
}

/*
 * Dumps all table data
 * Data layout (v1)(current):
 * Request: [ ipfw_obj_header ], size = ipfw_xtable_info.size
 * Reply: [ ipfw_obj_header ipfw_xtable_info ipfw_obj_tentry x N ]
 *
 * Returns 0 on success
 */
static int
ipfw_dump_table_v1(struct ip_fw_chain *ch, struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	ipfw_xtable_info *i;
	struct tid_info ti;
	struct table_config *tc;
	struct table_algo *ta;
	struct dump_args da;
	uint32_t sz;

	sz = sizeof(ipfw_obj_header) + sizeof(ipfw_xtable_info);
	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	if (oh == NULL)
		return (EINVAL);

	i = (ipfw_xtable_info *)(oh + 1);
	objheader_to_ti(oh, &ti);

	IPFW_UH_RLOCK(ch);
	if ((tc = find_table(CHAIN_TO_NI(ch), &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ESRCH);
	}
	export_table_info(ch, tc, i);

	if (sd->valsize < i->size) {

		/*
		 * Submitted buffer size is not enough.
		 * WE've already filled in @i structure with
		 * relevant table info including size, so we
		 * can return. Buffer will be flushed automatically.
		 */
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}

	/*
	 * Do the actual dump in eXtended format
	 */
	memset(&da, 0, sizeof(da));
	da.ti = KIDX_TO_TI(ch, tc->no.kidx);
	da.tc = tc;
	da.sd = sd;

	ta = tc->ta;

	ta->foreach(tc->astate, da.ti, dump_table_tentry, &da);
	IPFW_UH_RUNLOCK(ch);

	return (da.error);
}

/*
 * Dumps all table data
 * Data layout (version 0)(legacy):
 * Request: [ ipfw_xtable ], size = IP_FW_TABLE_XGETSIZE()
 * Reply: [ ipfw_xtable ipfw_table_xentry x N ]
 *
 * Returns 0 on success
 */
static int
ipfw_dump_table_v0(struct ip_fw_chain *ch, struct sockopt_data *sd)
{
	ipfw_xtable *xtbl;
	struct tid_info ti;
	struct table_config *tc;
	struct table_algo *ta;
	struct dump_args da;
	size_t sz, count;

	xtbl = (ipfw_xtable *)ipfw_get_sopt_header(sd, sizeof(ipfw_xtable));
	if (xtbl == NULL)
		return (EINVAL);

	memset(&ti, 0, sizeof(ti));
	ti.uidx = xtbl->tbl;
	
	IPFW_UH_RLOCK(ch);
	if ((tc = find_table(CHAIN_TO_NI(ch), &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (0);
	}
	count = table_get_count(ch, tc);
	sz = count * sizeof(ipfw_table_xentry) + sizeof(ipfw_xtable);

	xtbl->cnt = count;
	xtbl->size = sz;
	xtbl->type = tc->no.type;
	xtbl->tbl = ti.uidx;

	if (sd->valsize < sz) {

		/*
		 * Submitted buffer size is not enough.
		 * WE've already filled in @i structure with
		 * relevant table info including size, so we
		 * can return. Buffer will be flushed automatically.
		 */
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}

	/* Do the actual dump in eXtended format */
	memset(&da, 0, sizeof(da));
	da.ti = KIDX_TO_TI(ch, tc->no.kidx);
	da.tc = tc;
	da.sd = sd;

	ta = tc->ta;

	ta->foreach(tc->astate, da.ti, dump_table_xentry, &da);
	IPFW_UH_RUNLOCK(ch);

	return (0);
}

/*
 * Legacy IP_FW_TABLE_GETSIZE handler
 */
int
ipfw_count_table(struct ip_fw_chain *ch, struct tid_info *ti, uint32_t *cnt)
{
	struct table_config *tc;

	if ((tc = find_table(CHAIN_TO_NI(ch), ti)) == NULL)
		return (ESRCH);
	*cnt = table_get_count(ch, tc);
	return (0);
}

/*
 * Legacy IP_FW_TABLE_XGETSIZE handler
 */
int
ipfw_count_xtable(struct ip_fw_chain *ch, struct tid_info *ti, uint32_t *cnt)
{
	struct table_config *tc;
	uint32_t count;

	if ((tc = find_table(CHAIN_TO_NI(ch), ti)) == NULL) {
		*cnt = 0;
		return (0); /* 'table all list' requires success */
	}

	count = table_get_count(ch, tc);
	*cnt = count * sizeof(ipfw_table_xentry);
	if (count > 0)
		*cnt += sizeof(ipfw_xtable);
	return (0);
}

static int
dump_table_entry(void *e, void *arg)
{
	struct dump_args *da;
	struct table_config *tc;
	struct table_algo *ta;
	ipfw_table_entry *ent;
	int error;

	da = (struct dump_args *)arg;

	tc = da->tc;
	ta = tc->ta;

	/* Out of memory, returning */
	if (da->cnt == da->size)
		return (1);
	ent = da->ent++;
	ent->tbl = da->uidx;
	da->cnt++;

	error = ta->dump_tentry(tc->astate, da->ti, e, &da->tent);
	if (error != 0)
		return (error);

	ent->addr = da->tent.k.addr.s_addr;
	ent->masklen = da->tent.masklen;
	ent->value = da->tent.value;

	return (0);
}

/*
 * Dumps table in pre-8.1 legacy format.
 */
int
ipfw_dump_table_legacy(struct ip_fw_chain *ch, struct tid_info *ti,
    ipfw_table *tbl)
{
	struct table_config *tc;
	struct table_algo *ta;
	struct dump_args da;

	tbl->cnt = 0;

	if ((tc = find_table(CHAIN_TO_NI(ch), ti)) == NULL)
		return (0);	/* XXX: We should return ESRCH */

	ta = tc->ta;

	/* This dump format supports IPv4 only */
	if (tc->no.type != IPFW_TABLE_ADDR)
		return (0);

	memset(&da, 0, sizeof(da));
	da.ti = KIDX_TO_TI(ch, tc->no.kidx);
	da.tc = tc;
	da.ent = &tbl->ent[0];
	da.size = tbl->size;

	tbl->cnt = 0;
	ta->foreach(tc->astate, da.ti, dump_table_entry, &da);
	tbl->cnt = da.cnt;

	return (0);
}

/*
 * Dumps table entry in eXtended format (v1)(current).
 */
static int
dump_table_tentry(void *e, void *arg)
{
	struct dump_args *da;
	struct table_config *tc;
	struct table_algo *ta;
	ipfw_obj_tentry *tent;

	da = (struct dump_args *)arg;

	tc = da->tc;
	ta = tc->ta;

	tent = (ipfw_obj_tentry *)ipfw_get_sopt_space(da->sd, sizeof(*tent));
	/* Out of memory, returning */
	if (tent == NULL) {
		da->error = ENOMEM;
		return (1);
	}
	tent->head.length = sizeof(ipfw_obj_tentry);
	tent->idx = da->uidx;

	return (ta->dump_tentry(tc->astate, da->ti, e, tent));
}

/*
 * Dumps table entry in eXtended format (v0).
 */
static int
dump_table_xentry(void *e, void *arg)
{
	struct dump_args *da;
	struct table_config *tc;
	struct table_algo *ta;
	ipfw_table_xentry *xent;
	ipfw_obj_tentry *tent;
	int error;

	da = (struct dump_args *)arg;

	tc = da->tc;
	ta = tc->ta;

	xent = (ipfw_table_xentry *)ipfw_get_sopt_space(da->sd, sizeof(*xent));
	/* Out of memory, returning */
	if (xent == NULL)
		return (1);
	xent->len = sizeof(ipfw_table_xentry);
	xent->tbl = da->uidx;

	memset(&da->tent, 0, sizeof(da->tent));
	tent = &da->tent;
	error = ta->dump_tentry(tc->astate, da->ti, e, tent);
	if (error != 0)
		return (error);

	/* Convert current format to previous one */
	xent->masklen = tent->masklen;
	xent->value = tent->value;
	/* Apply some hacks */
	if (tc->no.type == IPFW_TABLE_ADDR && tent->subtype == AF_INET) {
		xent->k.addr6.s6_addr32[3] = tent->k.addr.s_addr;
		xent->flags = IPFW_TCF_INET;
	} else
		memcpy(&xent->k, &tent->k, sizeof(xent->k));

	return (0);
}

/*
 * Table algorithms
 */ 

/*
 * Finds algoritm by index, table type or supplied name.
 *
 * Returns pointer to algo or NULL.
 */
static struct table_algo *
find_table_algo(struct tables_config *tcfg, struct tid_info *ti, char *name)
{
	int i, l;
	struct table_algo *ta;

	if (ti->type > IPFW_TABLE_MAXTYPE)
		return (NULL);

	/* Search by index */
	if (ti->atype != 0) {
		if (ti->atype > tcfg->algo_count)
			return (NULL);
		return (tcfg->algo[ti->atype]);
	}

	if (name == NULL) {
		/* Return default algorithm for given type if set */
		return (tcfg->def_algo[ti->type]);
	}

	/* Search by name */
	/* TODO: better search */
	for (i = 1; i <= tcfg->algo_count; i++) {
		ta = tcfg->algo[i];

		/*
		 * One can supply additional algorithm
		 * parameters so we compare only the first word
		 * of supplied name:
		 * 'addr:chash hsize=32'
		 * '^^^^^^^^^'
		 *
		 */
		l = strlen(ta->name);
		if (strncmp(name, ta->name, l) != 0)
			continue;
		if (name[l] != '\0' && name[l] != ' ')
			continue;
		/* Check if we're requesting proper table type */
		if (ti->type != 0 && ti->type != ta->type)
			return (NULL);
		return (ta);
	}

	return (NULL);
}

/*
 * Register new table algo @ta.
 * Stores algo id inside @idx.
 *
 * Returns 0 on success.
 */
int
ipfw_add_table_algo(struct ip_fw_chain *ch, struct table_algo *ta, size_t size,
    int *idx)
{
	struct tables_config *tcfg;
	struct table_algo *ta_new;
	size_t sz;

	if (size > sizeof(struct table_algo))
		return (EINVAL);

	/* Check for the required on-stack size for add/del */
	sz = roundup2(ta->ta_buf_size, sizeof(void *));
	if (sz > TA_BUF_SZ)
		return (EINVAL);

	KASSERT(ta->type <= IPFW_TABLE_MAXTYPE,("Increase IPFW_TABLE_MAXTYPE"));

	/* Copy algorithm data to stable storage. */
	ta_new = malloc(sizeof(struct table_algo), M_IPFW, M_WAITOK | M_ZERO);
	memcpy(ta_new, ta, size);

	tcfg = CHAIN_TO_TCFG(ch);

	KASSERT(tcfg->algo_count < 255, ("Increase algo array size"));

	tcfg->algo[++tcfg->algo_count] = ta_new;
	ta_new->idx = tcfg->algo_count;

	/* Set algorithm as default one for given type */
	if ((ta_new->flags & TA_FLAG_DEFAULT) != 0 &&
	    tcfg->def_algo[ta_new->type] == NULL)
		tcfg->def_algo[ta_new->type] = ta_new;

	*idx = ta_new->idx;
	
	return (0);
}

/*
 * Unregisters table algo using @idx as id.
 * XXX: It is NOT safe to call this function in any place
 * other than ipfw instance destroy handler.
 */
void
ipfw_del_table_algo(struct ip_fw_chain *ch, int idx)
{
	struct tables_config *tcfg;
	struct table_algo *ta;

	tcfg = CHAIN_TO_TCFG(ch);

	KASSERT(idx <= tcfg->algo_count, ("algo idx %d out of range 1..%d",
	    idx, tcfg->algo_count));

	ta = tcfg->algo[idx];
	KASSERT(ta != NULL, ("algo idx %d is NULL", idx));

	if (tcfg->def_algo[ta->type] == ta)
		tcfg->def_algo[ta->type] = NULL;

	free(ta, M_IPFW);
}

/*
 * Lists all table algorithms currently available.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ], size = ipfw_obj_lheader.size
 * Reply: [ ipfw_obj_lheader ipfw_ta_info x N ]
 *
 * Returns 0 on success
 */
int
ipfw_list_table_algo(struct ip_fw_chain *ch, struct sockopt_data *sd)
{
	struct _ipfw_obj_lheader *olh;
	struct tables_config *tcfg;
	ipfw_ta_info *i;
	struct table_algo *ta;
	uint32_t count, n, size;

	olh = (struct _ipfw_obj_lheader *)ipfw_get_sopt_header(sd,sizeof(*olh));
	if (olh == NULL)
		return (EINVAL);
	if (sd->valsize < olh->size)
		return (EINVAL);

	IPFW_UH_RLOCK(ch);
	tcfg = CHAIN_TO_TCFG(ch);
	count = tcfg->algo_count;
	size = count * sizeof(ipfw_ta_info) + sizeof(ipfw_obj_lheader);

	/* Fill in header regadless of buffer size */
	olh->count = count;
	olh->objsize = sizeof(ipfw_ta_info);

	if (size > olh->size) {
		olh->size = size;
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}
	olh->size = size;

	for (n = 1; n <= count; n++) {
		i = (ipfw_ta_info *)ipfw_get_sopt_space(sd, sizeof(*i));
		KASSERT(i != 0, ("previously checked buffer is not enough"));
		ta = tcfg->algo[n];
		strlcpy(i->algoname, ta->name, sizeof(i->algoname));
		i->type = ta->type;
		i->refcnt = ta->refcnt;
	}

	IPFW_UH_RUNLOCK(ch);

	return (0);
}

/*
 * Tables rewriting code 
 */

/*
 * Determine table number and lookup type for @cmd.
 * Fill @tbl and @type with appropriate values.
 * Returns 0 for relevant opcodes, 1 otherwise.
 */
static int
classify_table_opcode(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{
	ipfw_insn_if *cmdif;
	int skip;
	uint16_t v;

	skip = 1;

	switch (cmd->opcode) {
	case O_IP_SRC_LOOKUP:
	case O_IP_DST_LOOKUP:
		/* Basic IPv4/IPv6 or u32 lookups */
		*puidx = cmd->arg1;
		/* Assume ADDR by default */
		*ptype = IPFW_TABLE_ADDR;
		skip = 0;
		
		if (F_LEN(cmd) > F_INSN_SIZE(ipfw_insn_u32)) {
			/*
			 * generic lookup. The key must be
			 * in 32bit big-endian format.
			 */
			v = ((ipfw_insn_u32 *)cmd)->d[1];
			switch (v) {
			case 0:
			case 1:
				/* IPv4 src/dst */
				break;
			case 2:
			case 3:
				/* src/dst port */
				*ptype = IPFW_TABLE_NUMBER;
				break;
			case 4:
				/* uid/gid */
				*ptype = IPFW_TABLE_NUMBER;
				break;
			case 5:
				/* jid */
				*ptype = IPFW_TABLE_NUMBER;
				break;
			case 6:
				/* dscp */
				*ptype = IPFW_TABLE_NUMBER;
				break;
			}
		}
		break;
	case O_XMIT:
	case O_RECV:
	case O_VIA:
		/* Interface table, possibly */
		cmdif = (ipfw_insn_if *)cmd;
		if (cmdif->name[0] != '\1')
			break;

		*ptype = IPFW_TABLE_INTERFACE;
		*puidx = cmdif->p.kidx;
		skip = 0;
		break;
	case O_IP_FLOW_LOOKUP:
		*puidx = cmd->arg1;
		*ptype = IPFW_TABLE_FLOW;
		skip = 0;
		break;
	}

	return (skip);
}

/*
 * Sets new table value for given opcode.
 * Assume the same opcodes as classify_table_opcode()
 */
static void
update_table_opcode(ipfw_insn *cmd, uint16_t idx)
{
	ipfw_insn_if *cmdif;

	switch (cmd->opcode) {
	case O_IP_SRC_LOOKUP:
	case O_IP_DST_LOOKUP:
		/* Basic IPv4/IPv6 or u32 lookups */
		cmd->arg1 = idx;
		break;
	case O_XMIT:
	case O_RECV:
	case O_VIA:
		/* Interface table, possibly */
		cmdif = (ipfw_insn_if *)cmd;
		cmdif->p.kidx = idx;
		break;
	case O_IP_FLOW_LOOKUP:
		cmd->arg1 = idx;
		break;
	}
}

/*
 * Checks table name for validity.
 * Enforce basic length checks, the rest
 * should be done in userland.
 *
 * Returns 0 if name is considered valid.
 */
int
ipfw_check_table_name(char *name)
{
	int nsize;
	ipfw_obj_ntlv *ntlv = NULL;

	nsize = sizeof(ntlv->name);

	if (strnlen(name, nsize) == nsize)
		return (EINVAL);

	if (name[0] == '\0')
		return (EINVAL);

	/*
	 * TODO: do some more complicated checks
	 */

	return (0);
}

/*
 * Find tablename TLV by @uid.
 * Check @tlvs for valid data inside.
 *
 * Returns pointer to found TLV or NULL.
 */
static ipfw_obj_ntlv *
find_name_tlv(void *tlvs, int len, uint16_t uidx)
{
	ipfw_obj_ntlv *ntlv;
	uintptr_t pa, pe;
	int l;

	pa = (uintptr_t)tlvs;
	pe = pa + len;
	l = 0;
	for (; pa < pe; pa += l) {
		ntlv = (ipfw_obj_ntlv *)pa;
		l = ntlv->head.length;

		if (l != sizeof(*ntlv))
			return (NULL);

		if (ntlv->head.type != IPFW_TLV_TBL_NAME)
			continue;

		if (ntlv->idx != uidx)
			continue;

		if (ipfw_check_table_name(ntlv->name) != 0)
			return (NULL);
		
		return (ntlv);
	}

	return (NULL);
}

/*
 * Finds table config based on either legacy index
 * or name in ntlv.
 * Note @ti structure contains unchecked data from userland.
 *
 * Returns pointer to table_config or NULL.
 */
static struct table_config *
find_table(struct namedobj_instance *ni, struct tid_info *ti)
{
	char *name, bname[16];
	struct named_object *no;
	ipfw_obj_ntlv *ntlv;
	uint32_t set;

	if (ti->tlvs != NULL) {
		ntlv = find_name_tlv(ti->tlvs, ti->tlen, ti->uidx);
		if (ntlv == NULL)
			return (NULL);
		name = ntlv->name;

		/*
		 * Use set provided by @ti instead of @ntlv one.
		 * This is needed due to different sets behavior
		 * controlled by V_fw_tables_sets.
		 */
		set = ti->set;
	} else {
		snprintf(bname, sizeof(bname), "%d", ti->uidx);
		name = bname;
		set = 0;
	}

	no = ipfw_objhash_lookup_name(ni, set, name);

	return ((struct table_config *)no);
}

/*
 * Allocate new table config structure using
 * specified @algo and @aname.
 *
 * Returns pointer to config or NULL.
 */
static struct table_config *
alloc_table_config(struct ip_fw_chain *ch, struct tid_info *ti,
    struct table_algo *ta, char *aname, uint8_t tflags, uint8_t vtype)
{
	char *name, bname[16];
	struct table_config *tc;
	int error;
	ipfw_obj_ntlv *ntlv;
	uint32_t set;

	if (ti->tlvs != NULL) {
		ntlv = find_name_tlv(ti->tlvs, ti->tlen, ti->uidx);
		if (ntlv == NULL)
			return (NULL);
		name = ntlv->name;
		set = ntlv->set;
	} else {
		snprintf(bname, sizeof(bname), "%d", ti->uidx);
		name = bname;
		set = 0;
	}

	tc = malloc(sizeof(struct table_config), M_IPFW, M_WAITOK | M_ZERO);
	tc->no.name = tc->tablename;
	tc->no.type = ta->type;
	tc->no.set = set;
	tc->tflags = tflags;
	tc->ta = ta;
	strlcpy(tc->tablename, name, sizeof(tc->tablename));
	tc->vtype = vtype;

	if (ti->tlvs == NULL) {
		tc->no.compat = 1;
		tc->no.uidx = ti->uidx;
	}

	/* Preallocate data structures for new tables */
	error = ta->init(ch, &tc->astate, &tc->ti, aname, tflags);
	if (error != 0) {
		free(tc, M_IPFW);
		return (NULL);
	}
	
	return (tc);
}

/*
 * Destroys table state and config.
 */
static void
free_table_config(struct namedobj_instance *ni, struct table_config *tc)
{

	KASSERT(tc->linked == 0, ("free() on linked config"));

	/*
	 * We're using ta without any locking/referencing.
	 * TODO: fix this if we're going to use unloadable algos.
	 */
	tc->ta->destroy(tc->astate, &tc->ti);
	free(tc, M_IPFW);
}

/*
 * Links @tc to @chain table named instance.
 * Sets appropriate type/states in @chain table info.
 */
static void
link_table(struct ip_fw_chain *ch, struct table_config *tc)
{
	struct namedobj_instance *ni;
	struct table_info *ti;
	uint16_t kidx;

	IPFW_UH_WLOCK_ASSERT(ch);
	IPFW_WLOCK_ASSERT(ch);

	ni = CHAIN_TO_NI(ch);
	kidx = tc->no.kidx;

	ipfw_objhash_add(ni, &tc->no);

	ti = KIDX_TO_TI(ch, kidx);
	*ti = tc->ti;

	/* Notify algo on real @ti address */
	if (tc->ta->change_ti != NULL)
		tc->ta->change_ti(tc->astate, ti);

	tc->linked = 1;
	tc->ta->refcnt++;
}

/*
 * Unlinks @tc from @chain table named instance.
 * Zeroes states in @chain and stores them in @tc.
 */
static void
unlink_table(struct ip_fw_chain *ch, struct table_config *tc)
{
	struct namedobj_instance *ni;
	struct table_info *ti;
	uint16_t kidx;

	IPFW_UH_WLOCK_ASSERT(ch);
	IPFW_WLOCK_ASSERT(ch);

	ni = CHAIN_TO_NI(ch);
	kidx = tc->no.kidx;

	/* Clear state. @ti copy is already saved inside @tc */
	ipfw_objhash_del(ni, &tc->no);
	ti = KIDX_TO_TI(ch, kidx);
	memset(ti, 0, sizeof(struct table_info));
	tc->linked = 0;
	tc->ta->refcnt--;

	/* Notify algo on real @ti address */
	if (tc->ta->change_ti != NULL)
		tc->ta->change_ti(tc->astate, NULL);
}

struct swap_table_args {
	int set;
	int new_set;
	int mv;
};

/*
 * Change set for each matching table.
 *
 * Ensure we dispatch each table once by setting/checking ochange
 * fields.
 */
static void
swap_table_set(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	struct table_config *tc;
	struct swap_table_args *sta;

	tc = (struct table_config *)no;
	sta = (struct swap_table_args *)arg;

	if (no->set != sta->set && (no->set != sta->new_set || sta->mv != 0))
		return;

	if (tc->ochanged != 0)
		return;

	tc->ochanged = 1;
	ipfw_objhash_del(ni, no);
	if (no->set == sta->set)
		no->set = sta->new_set;
	else
		no->set = sta->set;
	ipfw_objhash_add(ni, no);
}

/*
 * Cleans up ochange field for all tables.
 */
static void
clean_table_set_data(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	struct table_config *tc;
	struct swap_table_args *sta;

	tc = (struct table_config *)no;
	sta = (struct swap_table_args *)arg;

	tc->ochanged = 0;
}

/*
 * Swaps tables within two sets.
 */
void
ipfw_swap_tables_sets(struct ip_fw_chain *ch, uint32_t set,
    uint32_t new_set, int mv)
{
	struct swap_table_args sta;

	IPFW_UH_WLOCK_ASSERT(ch);

	sta.set = set;
	sta.new_set = new_set;
	sta.mv = mv;

	ipfw_objhash_foreach(CHAIN_TO_NI(ch), swap_table_set, &sta);
	ipfw_objhash_foreach(CHAIN_TO_NI(ch), clean_table_set_data, &sta);
}

/*
 * Move all tables which are reference by rules in @rr to set @new_set.
 * Makes sure that all relevant tables are referenced ONLLY by given rules.
 *
 * Retuns 0 on success,
 */
int
ipfw_move_tables_sets(struct ip_fw_chain *ch, ipfw_range_tlv *rt,
    uint32_t new_set)
{
	struct ip_fw *rule;
	struct table_config *tc;
	struct named_object *no;
	struct namedobj_instance *ni;
	int bad, i, l, cmdlen;
	uint16_t kidx;
	uint8_t type;
	ipfw_insn *cmd;

	IPFW_UH_WLOCK_ASSERT(ch);

	ni = CHAIN_TO_NI(ch);

	/* Stage 1: count number of references by given rules */
	for (i = 0; i < ch->n_rules - 1; i++) {
		rule = ch->map[i];
		if (ipfw_match_range(rule, rt) == 0)
			continue;

		l = rule->cmd_len;
		cmd = rule->cmd;
		cmdlen = 0;
		for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
			cmdlen = F_LEN(cmd);
			if (classify_table_opcode(cmd, &kidx, &type) != 0)
				continue;
			no = ipfw_objhash_lookup_kidx(ni, kidx);
			KASSERT(no != NULL, 
			    ("objhash lookup failed on index %d", kidx));
			tc = (struct table_config *)no;
			tc->ocount++;
		}

	}

	/* Stage 2: verify "ownership" */
	bad = 0;
	for (i = 0; i < ch->n_rules - 1; i++) {
		rule = ch->map[i];
		if (ipfw_match_range(rule, rt) == 0)
			continue;

		l = rule->cmd_len;
		cmd = rule->cmd;
		cmdlen = 0;
		for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
			cmdlen = F_LEN(cmd);
			if (classify_table_opcode(cmd, &kidx, &type) != 0)
				continue;
			no = ipfw_objhash_lookup_kidx(ni, kidx);
			KASSERT(no != NULL, 
			    ("objhash lookup failed on index %d", kidx));
			tc = (struct table_config *)no;
			if (tc->no.refcnt != tc->ocount) {

				/*
				 * Number of references differ:
				 * Other rule(s) are holding reference to given
				 * table, so it is not possible to change its set.
				 *
				 * Note that refcnt may account
				 * references to some going-to-be-added rules.
				 * Since we don't know their numbers (and event
				 * if they will be added) it is perfectly OK
				 * to return error here.
				 */
				bad = 1;
				break;
			}
		}

		if (bad != 0)
			break;
	}

	/* Stage 3: change set or cleanup */
	for (i = 0; i < ch->n_rules - 1; i++) {
		rule = ch->map[i];
		if (ipfw_match_range(rule, rt) == 0)
			continue;

		l = rule->cmd_len;
		cmd = rule->cmd;
		cmdlen = 0;
		for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
			cmdlen = F_LEN(cmd);
			if (classify_table_opcode(cmd, &kidx, &type) != 0)
				continue;
			no = ipfw_objhash_lookup_kidx(ni, kidx);
			KASSERT(no != NULL, 
			    ("objhash lookup failed on index %d", kidx));
			tc = (struct table_config *)no;

			tc->ocount = 0;
			if (bad != 0)
				continue;

			/* Actually change set. */
			ipfw_objhash_del(ni, no);
			no->set = new_set;
			ipfw_objhash_add(ni, no);
		}
	}

	return (bad);
}

/*
 * Finds and bumps refcount for tables referenced by given @rule.
 * Auto-creates non-existing tables.
 * Fills in @oib array with userland/kernel indexes.
 * First free oidx pointer is saved back in @oib.
 *
 * Returns 0 on success.
 */
static int
find_ref_rule_tables(struct ip_fw_chain *ch, struct ip_fw *rule,
    struct rule_check_info *ci, struct obj_idx **oib, struct tid_info *ti)
{
	struct table_config *tc;
	struct namedobj_instance *ni;
	struct named_object *no;
	int cmdlen, error, l, numnew;
	uint16_t kidx;
	ipfw_insn *cmd;
	struct obj_idx *pidx, *pidx_first, *p;

	pidx_first = *oib;
	pidx = pidx_first;
	l = rule->cmd_len;
	cmd = rule->cmd;
	cmdlen = 0;
	error = 0;
	numnew = 0;

	IPFW_UH_WLOCK(ch);
	ni = CHAIN_TO_NI(ch);

	/* Increase refcount on each existing referenced table. */
	for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
		cmdlen = F_LEN(cmd);

		if (classify_table_opcode(cmd, &ti->uidx, &ti->type) != 0)
			continue;

		pidx->uidx = ti->uidx;
		pidx->type = ti->type;

		if ((tc = find_table(ni, ti)) != NULL) {
			if (tc->no.type != ti->type) {
				/* Incompatible types */
				error = EINVAL;
				break;
			}

			/* Reference found table and save kidx */
			tc->no.refcnt++;
			pidx->kidx = tc->no.kidx;
			pidx++;
			continue;
		}

		/*
		 * Compability stuff for old clients:
		 * prepare to manually create non-existing tables.
		 */
		pidx++;
		numnew++;
	}

	if (error != 0) {
		/* Unref everything we have already done */
		for (p = *oib; p < pidx; p++) {
			if (p->kidx == 0)
				continue;

			/* Find & unref by existing idx */
			no = ipfw_objhash_lookup_kidx(ni, p->kidx);
			KASSERT(no != NULL, ("Ref'd table %d disappeared",
			    p->kidx));

			no->refcnt--;
		}
	}

	IPFW_UH_WUNLOCK(ch);

	if (numnew == 0) {
		*oib = pidx;
		return (error);
	}

	/*
	 * Compatibility stuff: do actual creation for non-existing,
	 * but referenced tables.
	 */
	for (p = pidx_first; p < pidx; p++) {
		if (p->kidx != 0)
			continue;

		ti->uidx = p->uidx;
		ti->type = p->type;
		ti->atype = 0;

		error = create_table_compat(ch, ti, NULL, NULL, &kidx);
		if (error == 0) {
			p->kidx = kidx;
			continue;
		}

		/* Error. We have to drop references */
		IPFW_UH_WLOCK(ch);
		for (p = pidx_first; p < pidx; p++) {
			if (p->kidx == 0)
				continue;

			/* Find & unref by existing idx */
			no = ipfw_objhash_lookup_kidx(ni, p->kidx);
			KASSERT(no != NULL, ("Ref'd table %d disappeared",
			    p->kidx));

			no->refcnt--;
		}
		IPFW_UH_WUNLOCK(ch);

		return (error);
	}

	*oib = pidx;

	return (error);
}

/*
 * Remove references from every table used in @rule.
 */
void
ipfw_unref_rule_tables(struct ip_fw_chain *chain, struct ip_fw *rule)
{
	int cmdlen, l;
	ipfw_insn *cmd;
	struct namedobj_instance *ni;
	struct named_object *no;
	uint16_t kidx;
	uint8_t type;

	IPFW_UH_WLOCK_ASSERT(chain);
	ni = CHAIN_TO_NI(chain);

	l = rule->cmd_len;
	cmd = rule->cmd;
	cmdlen = 0;
	for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
		cmdlen = F_LEN(cmd);

		if (classify_table_opcode(cmd, &kidx, &type) != 0)
			continue;

		no = ipfw_objhash_lookup_kidx(ni, kidx); 

		KASSERT(no != NULL, ("table id %d not found", kidx));
		KASSERT(no->type == type, ("wrong type %d (%d) for table id %d",
		    no->type, type, kidx));
		KASSERT(no->refcnt > 0, ("refcount for table %d is %d",
		    kidx, no->refcnt));

		no->refcnt--;
	}
}

/*
 * Compatibility function for old ipfw(8) binaries.
 * Rewrites table kernel indices with userland ones.
 * Convert tables matching '/^\d+$/' to their atoi() value.
 * Use number 65535 for other tables.
 *
 * Returns 0 on success.
 */
int
ipfw_rewrite_table_kidx(struct ip_fw_chain *chain, struct ip_fw_rule0 *rule)
{
	int cmdlen, error, l;
	ipfw_insn *cmd;
	uint16_t kidx, uidx;
	uint8_t type;
	struct named_object *no;
	struct namedobj_instance *ni;

	ni = CHAIN_TO_NI(chain);
	error = 0;

	l = rule->cmd_len;
	cmd = rule->cmd;
	cmdlen = 0;
	for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
		cmdlen = F_LEN(cmd);

		if (classify_table_opcode(cmd, &kidx, &type) != 0)
			continue;

		if ((no = ipfw_objhash_lookup_kidx(ni, kidx)) == NULL)
			return (1);

		uidx = no->uidx;
		if (no->compat == 0) {

			/*
			 * We are called via legacy opcode.
			 * Save error and show table as fake number
			 * not to make ipfw(8) hang.
			 */
			uidx = 65535;
			error = 2;
		}

		update_table_opcode(cmd, uidx);
	}

	return (error);
}

/*
 * Checks is opcode is referencing table of appropriate type.
 * Adds reference count for found table if true.
 * Rewrites user-supplied opcode values with kernel ones.
 *
 * Returns 0 on success and appropriate error code otherwise.
 */
int
ipfw_rewrite_table_uidx(struct ip_fw_chain *chain,
    struct rule_check_info *ci)
{
	int cmdlen, error, l;
	ipfw_insn *cmd;
	uint16_t uidx;
	uint8_t type;
	struct namedobj_instance *ni;
	struct obj_idx *p, *pidx_first, *pidx_last;
	struct tid_info ti;

	ni = CHAIN_TO_NI(chain);

	/*
	 * Prepare an array for storing opcode indices.
	 * Use stack allocation by default.
	 */
	if (ci->table_opcodes <= (sizeof(ci->obuf)/sizeof(ci->obuf[0]))) {
		/* Stack */
		pidx_first = ci->obuf;
	} else
		pidx_first = malloc(ci->table_opcodes * sizeof(struct obj_idx),
		    M_IPFW, M_WAITOK | M_ZERO);

	pidx_last = pidx_first;
	error = 0;
	type = 0;
	memset(&ti, 0, sizeof(ti));

	/*
	 * Use default set for looking up tables (old way) or
	 * use set rule is assigned to (new way).
	 */
	ti.set = (V_fw_tables_sets != 0) ? ci->krule->set : 0;
	if (ci->ctlv != NULL) {
		ti.tlvs = (void *)(ci->ctlv + 1);
		ti.tlen = ci->ctlv->head.length - sizeof(ipfw_obj_ctlv);
	}

	/* Reference all used tables */
	error = find_ref_rule_tables(chain, ci->krule, ci, &pidx_last, &ti);
	if (error != 0)
		goto free;

	IPFW_UH_WLOCK(chain);

	/* Perform rule rewrite */
	l = ci->krule->cmd_len;
	cmd = ci->krule->cmd;
	cmdlen = 0;
	p = pidx_first;
	for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
		cmdlen = F_LEN(cmd);
		if (classify_table_opcode(cmd, &uidx, &type) != 0)
			continue;
		update_table_opcode(cmd, p->kidx);
		p++;
	}

	IPFW_UH_WUNLOCK(chain);

free:
	if (pidx_first != ci->obuf)
		free(pidx_first, M_IPFW);

	return (error);
}

