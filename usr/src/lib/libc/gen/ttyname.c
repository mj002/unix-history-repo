/*
 * Copyright (c) 1988 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)ttyname.c	5.6 (Berkeley) 6/1/90";
#endif /* LIBC_SCCS and not lint */

#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sgtty.h>
#include <paths.h>

char *
ttyname(fd)
	int fd;
{
	register struct direct *dirp;
	register DIR *dp;
	struct stat sb1, sb2;
	struct sgttyb ttyb;
	static char buf[sizeof(_PATH_DEV) + MAXNAMLEN] = _PATH_DEV;
	char *rval, *strcpy();

	if (ioctl(fd, TIOCGETP, &ttyb) < 0)
		return(NULL);
	if (fstat(fd, &sb1) < 0 || (sb1.st_mode&S_IFMT) != S_IFCHR)
		return(NULL);
	if ((dp = opendir(_PATH_DEV)) == NULL)
		return(NULL);
	for (rval = NULL; dirp = readdir(dp);) {
		if (dirp->d_ino != sb1.st_ino)
			continue;
		(void)strcpy(buf + sizeof(_PATH_DEV) - 1, dirp->d_name);
		if (stat(buf, &sb2) < 0 || sb1.st_dev != sb2.st_dev ||
		    sb1.st_ino != sb2.st_ino)
			continue;
		closedir(dp);
		rval = buf;
		break;
	}
	closedir(dp);
	return(rval);
}
