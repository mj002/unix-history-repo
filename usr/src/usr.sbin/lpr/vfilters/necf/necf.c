/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1983 Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)necf.c	5.5 (Berkeley) 6/1/90";
#endif /* not lint */

#include <stdio.h>
#include <sgtty.h>

#define PAGESIZE	66

main()
{
	extern char *rindex();
	char line[256];
	register char c, *cp;
	register lnumber;

#ifdef SHEETFEEDER
	printf("\033=\033\033\033O\f");
#else
	printf("\033=");
#endif
	lnumber = 0;
	while (fgets(line, sizeof(line), stdin) != NULL) {
#ifdef SHEETFEEDER
		if (lnumber == PAGESIZE-1) {
			putchar('\f');
			lnumber = 0;
		}
		if (lnumber >= 2) {
#endif
#ifdef TTY
			if ((cp = rindex(line, '\n')) != NULL)
				*cp = '\r';
#endif
			printf("%s", line);
#ifdef SHEETFEEDER
		}
		lnumber++;
#endif
	}
	fflush (stdout);
}
