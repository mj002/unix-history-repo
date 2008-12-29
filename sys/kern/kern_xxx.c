/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_xxx.c	8.2 (Berkeley) 11/14/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/vimage.h>


#if defined(COMPAT_43)

#ifndef _SYS_SYSPROTO_H_
struct gethostname_args {
	char	*hostname;
	u_int	len;
};
#endif
/* ARGSUSED */
int
ogethostname(td, uap)
	struct thread *td;
	struct gethostname_args *uap;
{
	int name[2];
	size_t len = uap->len;

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	return (userland_sysctl(td, name, 2, uap->hostname, &len,
	    1, 0, 0, 0, 0));
}

#ifndef _SYS_SYSPROTO_H_
struct sethostname_args {
	char	*hostname;
	u_int	len;
};
#endif
/* ARGSUSED */
int
osethostname(td, uap)
	struct thread *td;
	register struct sethostname_args *uap;
{
	int name[2];
	int error;

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	return (userland_sysctl(td, name, 2, 0, 0, 0, uap->hostname,
	    uap->len, 0, 0));
}

#ifndef _SYS_SYSPROTO_H_
struct ogethostid_args {
	int	dummy;
};
#endif
/* ARGSUSED */
int
ogethostid(td, uap)
	struct thread *td;
	struct ogethostid_args *uap;
{

	*(long *)(td->td_retval) = hostid;
	return (0);
}
#endif /* COMPAT_43 */

#ifdef COMPAT_43
#ifndef _SYS_SYSPROTO_H_
struct osethostid_args {
	long	hostid;
};
#endif
/* ARGSUSED */
int
osethostid(td, uap)
	struct thread *td;
	struct osethostid_args *uap;
{
	int error;

	error = priv_check(td, PRIV_SETHOSTID);
	if (error)
		return (error);
	mtx_lock(&Giant);
	hostid = uap->hostid;
	mtx_unlock(&Giant);
	return (0);
}

int
oquota(td, uap)
	struct thread *td;
	struct oquota_args *uap;
{

	return (ENOSYS);
}
#endif /* COMPAT_43 */

#ifdef COMPAT_FREEBSD4
/*
 * This is the FreeBSD-1.1 compatible uname(2) interface.  These days it is
 * done in libc as a wrapper around a bunch of sysctl's.  This must maintain
 * the old 1.1 binary ABI.
 */
#if SYS_NMLN != 32
#error "FreeBSD-1.1 uname syscall has been broken"
#endif
#ifndef _SYS_SYSPROTO_H_
struct uname_args {
	struct utsname  *name;
};
#endif
/* ARGSUSED */
int
freebsd4_uname(struct thread *td, struct freebsd4_uname_args *uap)
{
	int name[2], error;
	size_t len;
	char *s, *us;

	name[0] = CTL_KERN;
	name[1] = KERN_OSTYPE;
	len = sizeof (uap->name->sysname);
	error = userland_sysctl(td, name, 2, uap->name->sysname, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->sysname + sizeof(uap->name->sysname) - 1, 0);

	name[1] = KERN_HOSTNAME;
	len = sizeof uap->name->nodename;
	error = userland_sysctl(td, name, 2, uap->name->nodename, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->nodename + sizeof(uap->name->nodename) - 1, 0);

	name[1] = KERN_OSRELEASE;
	len = sizeof uap->name->release;
	error = userland_sysctl(td, name, 2, uap->name->release, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->release + sizeof(uap->name->release) - 1, 0);

/*
	name = KERN_VERSION;
	len = sizeof uap->name->version;
	error = userland_sysctl(td, name, 2, uap->name->version, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->version + sizeof(uap->name->version) - 1, 0);
*/

/*
 * this stupid hackery to make the version field look like FreeBSD 1.1
 */
	for(s = version; *s && *s != '#'; s++);

	for(us = uap->name->version; *s && *s != ':'; s++) {
		error = subyte( us++, *s);
		if (error)
			return (error);
	}
	error = subyte( us++, 0);
	if (error)
		return (error);

	name[0] = CTL_HW;
	name[1] = HW_MACHINE;
	len = sizeof uap->name->machine;
	error = userland_sysctl(td, name, 2, uap->name->machine, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->machine + sizeof(uap->name->machine) - 1, 0);
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getdomainname_args {
	char    *domainname;
	int     len;
};
#endif
/* ARGSUSED */
int
freebsd4_getdomainname(struct thread *td,
    struct freebsd4_getdomainname_args *uap)
{
	int name[2];
	size_t len = uap->len;

	name[0] = CTL_KERN;
	name[1] = KERN_NISDOMAINNAME;
	return (userland_sysctl(td, name, 2, uap->domainname, &len,
	    1, 0, 0, 0, 0));
}

#ifndef _SYS_SYSPROTO_H_
struct setdomainname_args {
	char    *domainname;
	int     len;
};
#endif
/* ARGSUSED */
int
freebsd4_setdomainname(struct thread *td,
    struct freebsd4_setdomainname_args *uap)
{
	int name[2];

	name[0] = CTL_KERN;
	name[1] = KERN_NISDOMAINNAME;
	return (userland_sysctl(td, name, 2, 0, 0, 0, uap->domainname,
	    uap->len, 0, 0));
}
#endif /* COMPAT_FREEBSD4 */
