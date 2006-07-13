/*-
 * Copyright (c) 2003, Jeffrey Roberson <jeff@freebsd.org>
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
 *
 * $FreeBSD$
 *
 */

#ifndef _SYS_THR_H_
#define	_SYS_THR_H_

#include <posix4/sched.h>

struct thr_sched_param {
	int	policy;
	struct	sched_param	param;
};

/* Create the thread in the suspended state. */
#define	THR_SUSPENDED		0x0001
/* Create the system scope thread. */
#define	THR_SYSTEM_SCOPE	0x0002

struct thr_param {
    void	(*start_func)(void *);	/* thread entry function. */
    void	*arg;			/* argument for entry function. */
    char	*stack_base;		/* stack base address. */
    size_t	stack_size;		/* stack size. */
    char	*tls_base;		/* tls base address. */
    size_t	tls_size;		/* tls size. */
    long	*child_tid;		/* address to store new TID. */
    long	*parent_tid;		/* parent accesses the new TID here. */
    int		flags;			/* thread flags. */
    struct thr_sched_param *sched_param;	/* POSIX scheduler parameters .*/
    long	sched_param_size;	/* scheduler parameter size */
    void	*spare[2];		/* TODO: cpu affinity mask etc. */
};

/* 
 * See pthread_*
 */
#ifndef _KERNEL

int thr_create(ucontext_t *ctx, long *id, int flags);
int thr_new(struct thr_param *param, int param_size);
int thr_self(long *id);
void thr_exit(long *state);
int thr_kill(long id, int sig);
int thr_suspend(const struct timespec *timeout);
int thr_wake(long id);
int thr_set_name(long id, const char *name);
int thr_setscheduler(long id, int policy, const struct sched_param *param,
	int param_size);
int thr_getscheduler(long id, int *policy, struct sched_param *param,
	int param_size);
int thr_setschedparam(long id, const struct sched_param *param,
	int param_size);
#endif /* !_KERNEL */

#endif /* ! _SYS_THR_H_ */
