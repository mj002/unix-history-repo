/*
 * System call switch table.
 *
 * DO NOT EDIT-- this file is automatically generated.
 * $FreeBSD$
 * created from FreeBSD: src/sys/i386/ibcs2/syscalls.isc,v 1.6 2003/12/24 00:14:08 peter Exp 
 */

#include <sys/param.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <i386/ibcs2/ibcs2_types.h>
#include <i386/ibcs2/ibcs2_signal.h>
#include <i386/ibcs2/ibcs2_proto.h>
#include <i386/ibcs2/ibcs2_xenix.h>

#define AS(name) (sizeof(struct name) / sizeof(register_t))

/* The casts are bogus but will do for now. */
struct sysent isc_sysent[] = {
	{ 0, (sy_call_t *)nosys },			/* 0 = nosys */
	{ 0, (sy_call_t *)nosys },			/* 1 = isc_setostype */
	{ AS(ibcs2_rename_args), (sy_call_t *)ibcs2_rename },	/* 2 = ibcs2_rename */
	{ AS(ibcs2_sigaction_args), (sy_call_t *)ibcs2_sigaction },	/* 3 = ibcs2_sigaction */
	{ AS(ibcs2_sigprocmask_args), (sy_call_t *)ibcs2_sigprocmask },	/* 4 = ibcs2_sigprocmask */
	{ AS(ibcs2_sigpending_args), (sy_call_t *)ibcs2_sigpending },	/* 5 = ibcs2_sigpending */
	{ AS(getgroups_args), (sy_call_t *)getgroups },	/* 6 = getgroups */
	{ AS(setgroups_args), (sy_call_t *)setgroups },	/* 7 = setgroups */
	{ AS(ibcs2_pathconf_args), (sy_call_t *)ibcs2_pathconf },	/* 8 = ibcs2_pathconf */
	{ AS(ibcs2_fpathconf_args), (sy_call_t *)ibcs2_fpathconf },	/* 9 = ibcs2_fpathconf */
	{ 0, (sy_call_t *)nosys },			/* 10 = nosys */
	{ AS(ibcs2_wait_args), (sy_call_t *)ibcs2_wait },	/* 11 = ibcs2_wait */
	{ 0, (sy_call_t *)setsid },			/* 12 = setsid */
	{ 0, (sy_call_t *)getpid },			/* 13 = getpid */
	{ 0, (sy_call_t *)nosys },			/* 14 = isc_adduser */
	{ 0, (sy_call_t *)nosys },			/* 15 = isc_setuser */
	{ AS(ibcs2_sysconf_args), (sy_call_t *)ibcs2_sysconf },	/* 16 = ibcs2_sysconf */
	{ AS(ibcs2_sigsuspend_args), (sy_call_t *)ibcs2_sigsuspend },	/* 17 = ibcs2_sigsuspend */
	{ AS(ibcs2_symlink_args), (sy_call_t *)ibcs2_symlink },	/* 18 = ibcs2_symlink */
	{ AS(ibcs2_readlink_args), (sy_call_t *)ibcs2_readlink },	/* 19 = ibcs2_readlink */
	{ 0, (sy_call_t *)nosys },			/* 20 = isc_getmajor */
};
