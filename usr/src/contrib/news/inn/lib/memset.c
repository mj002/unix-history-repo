/*  $Revision: 1.2 $
**
**  This file has been modified to get it to compile more easily
**  on pre-4.4BSD systems.  Rich $alz, June 1991.
*/
#define const /* NULL */
#define void char
#define size_t int
#define NULL 0

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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

#if 0
#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)memset.c	5.5 (Berkeley) 5/15/90";
#endif /* LIBC_SCCS and not lint */

#include <string.h>
#include <sys/stdc.h>
#endif

void *
memset(dst, c, n)
	void *dst;
	register int c;
	register size_t n;
{

	if (n != 0) {
		register char *d = dst;

		do
			*d++ = c;
		while (--n != 0);
	}
	return (dst);
}
