/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)ttoutput.c	3.8 (Berkeley) %G%";
#endif /* not lint */

#include "ww.h"
#include "tt.h"
#include <sys/errno.h>

/*
 * Buffered output package.
 * We need this because stdio fails on non-blocking writes.
 */

ttflush()
{
	register char *p;
	register n;
	extern errno;

	wwnflush++;
	for (p = tt_ob; p < tt_obp;) {
		wwnwr++;
		n = write(1, p, tt_obp - p);
		if (n < 0) {
			wwnwre++;
			if (errno != EWOULDBLOCK) {
				/* can't deal with this */
				p = tt_obp;
			}
		} else if (n == 0) {
			/* what to do? */
			wwnwrz++;
		} else {
			wwnwrc += n;
			p += n;
		}
	}
	tt_obp = tt_ob;
}

ttputs(s)
register char *s;
{
	while (*s)
		ttputc(*s++);
}

ttwrite(s, n)
	register char *s;
	register n;
{
	switch (n) {
	case 0:
		break;
	case 1:
		ttputc(*s);
		break;
	case 2:
		if (tt_obe - tt_obp < 2)
			(*tt.tt_flush)();
		*tt_obp++ = *s++;
		*tt_obp++ = *s;
		break;
	case 3:
		if (tt_obe - tt_obp < 3)
			(*tt.tt_flush)();
		*tt_obp++ = *s++;
		*tt_obp++ = *s++;
		*tt_obp++ = *s;
		break;
	case 4:
		if (tt_obe - tt_obp < 4)
			(*tt.tt_flush)();
		*tt_obp++ = *s++;
		*tt_obp++ = *s++;
		*tt_obp++ = *s++;
		*tt_obp++ = *s;
		break;
	case 5:
		if (tt_obe - tt_obp < 5)
			(*tt.tt_flush)();
		*tt_obp++ = *s++;
		*tt_obp++ = *s++;
		*tt_obp++ = *s++;
		*tt_obp++ = *s++;
		*tt_obp++ = *s;
		break;
	default:
		while (n > 0) {
			register m;

			while ((m = tt_obe - tt_obp) == 0)
				(*tt.tt_flush)();
			if ((m = tt_obe - tt_obp) > n)
				m = n;
			bcopy(s, tt_obp, m);
			tt_obp += m;
			s += m;
			n -= m;
		}
	}
}
