/*-
 * Copyright (c) 2007 Robert N. M. Watson
 * Copyright (c) 2015 Allan Jude <allanjude@freebsd.org>
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <errno.h>
#include <libprocstat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

/*
 * Walk the stack trace provided by the kernel and reduce it to what we
 * actually want to print.  This involves stripping true instruction pointers,
 * frame numbers, and carriage returns as generated by stack(9).  If -kk is
 * specified, print the function and offset, otherwise just the function.
 */
enum trace_state { TS_FRAMENUM, TS_PC, TS_AT, TS_FUNC, TS_OFF };

static enum trace_state
kstack_nextstate(enum trace_state ts)
{

	switch (ts) {
	case TS_FRAMENUM:
		return (TS_PC);

	case TS_PC:
		return (TS_AT);

	case TS_AT:
		return (TS_FUNC);

	case TS_FUNC:
		return (TS_OFF);

	case TS_OFF:
		return TS_FRAMENUM;

	default:
		errx(-1, "kstack_nextstate");
	}
}

static void
kstack_cleanup(const char *old, char *new, int kflag)
{
	enum trace_state old_ts, ts;
	const char *cp_old;
	char *cp_new;

	ts = TS_FRAMENUM;
	for (cp_old = old, cp_new = new; *cp_old != '\0'; cp_old++) {
		switch (*cp_old) {
		case ' ':
		case '\n':
		case '+':
			old_ts = ts;
			ts = kstack_nextstate(old_ts);
			if (old_ts == TS_OFF) {
				*cp_new = ' ';
				cp_new++;
			}
			if (kflag > 1 && old_ts == TS_FUNC) {
				*cp_new = '+';
				cp_new++;
			}
			continue;
		}
		if (ts == TS_FUNC || (kflag > 1 && ts == TS_OFF)) {
			*cp_new = *cp_old;
			cp_new++;
		}
	}
	*cp_new = '\0';
}

static void
kstack_cleanup_encoded(const char *old, char *new, int kflag)
{
	enum trace_state old_ts, ts;
	const char *cp_old;
	char *cp_new, *cp_loop, *cp_tofree, *cp_line;

	ts = TS_FRAMENUM;
	if (kflag == 1) {
		for (cp_old = old, cp_new = new; *cp_old != '\0'; cp_old++) {
			switch (*cp_old) {
			case '\n':
				*cp_new = *cp_old;
				cp_new++;
			case ' ':
			case '+':
				old_ts = ts;
				ts = kstack_nextstate(old_ts);
				continue;
			}
			if (ts == TS_FUNC) {
				*cp_new = *cp_old;
				cp_new++;
			}
		}
		*cp_new = '\0';
		cp_tofree = cp_loop = strdup(new);
	} else
		cp_tofree = cp_loop = strdup(old);
        while ((cp_line = strsep(&cp_loop, "\n")) != NULL) {
		if (strlen(cp_line) != 0 && *cp_line != 127)
			xo_emit("{le:token/%s}", cp_line);
	}
	free(cp_tofree);
}

/*
 * Sort threads by tid.
 */
static int
kinfo_kstack_compare(const void *a, const void *b)
{

        return ((const struct kinfo_kstack *)a)->kkst_tid -
            ((const struct kinfo_kstack *)b)->kkst_tid;
}

static void
kinfo_kstack_sort(struct kinfo_kstack *kkstp, int count)
{

        qsort(kkstp, count, sizeof(*kkstp), kinfo_kstack_compare);
}


void
procstat_kstack(struct procstat *procstat, struct kinfo_proc *kipp)
{
	struct kinfo_kstack *kkstp, *kkstp_free;
	struct kinfo_proc *kip, *kip_free;
	char trace[KKST_MAXLEN], encoded_trace[KKST_MAXLEN];
	unsigned int i, j;
	unsigned int kip_count, kstk_count;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %6s %-19s %-19s %-29s}\n", "PID", "TID", "COMM",
		    "TDNAME", "KSTACK");

	kkstp = kkstp_free = procstat_getkstack(procstat, kipp, &kstk_count);
	if (kkstp == NULL)
		return;

	/*
	 * We need to re-query for thread information, so don't use *kipp.
	 */
	kip = kip_free = procstat_getprocs(procstat,
	    KERN_PROC_PID | KERN_PROC_INC_THREAD, kipp->ki_pid, &kip_count);

	if (kip == NULL) {
		procstat_freekstack(procstat, kkstp_free);
		return;
	}

	kinfo_kstack_sort(kkstp, kstk_count);
	for (i = 0; i < kstk_count; i++) {
		kkstp = &kkstp_free[i];

		/*
		 * Look up the specific thread using its tid so we can
		 * display the per-thread command line.
		 */
		kipp = NULL;
		for (j = 0; j < kip_count; j++) {
			kipp = &kip_free[j];
			if (kkstp->kkst_tid == kipp->ki_tid)
				break;
		}
		if (kipp == NULL)
			continue;

		xo_emit("{k:process_id/%5d/%d} ", kipp->ki_pid);
		xo_emit("{:thread_id/%6d/%d} ", kkstp->kkst_tid);
		xo_emit("{:command/%-19s/%s} ", kipp->ki_comm);
		xo_emit("{:thread_name/%-19s/%s} ",
                    kinfo_proc_thread_name(kipp));

		switch (kkstp->kkst_state) {
		case KKST_STATE_RUNNING:
			xo_emit("{:state/%-29s/%s}\n", "<running>");
			continue;

		case KKST_STATE_SWAPPED:
			xo_emit("{:state/%-29s/%s}\n", "<swapped>");
			continue;

		case KKST_STATE_STACKOK:
			break;

		default:
			xo_emit("{:state/%-29s/%s}\n", "<unknown>");
			continue;
		}

		/*
		 * The kernel generates a trace with carriage returns between
		 * entries, but for a more compact view, we convert carriage
		 * returns to spaces.
		 */
		kstack_cleanup(kkstp->kkst_trace, trace,
		    (procstat_opts & PS_OPT_VERBOSE) != 0 ? 2 : 1);
		xo_open_list("trace");
		kstack_cleanup_encoded(kkstp->kkst_trace, encoded_trace,
		    (procstat_opts & PS_OPT_VERBOSE) != 0 ? 2 : 1);
		xo_close_list("trace");
		xo_emit("{d:trace/%-29s}\n", trace);
	}
	procstat_freekstack(procstat, kkstp_free);
	procstat_freeprocs(procstat, kip_free);
}
