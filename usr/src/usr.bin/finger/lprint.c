/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Tony Nardo of the Johns Hopkins University/Applied Physics Lab.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
static char sccsid[] = "@(#)lprint.c	5.1 (Berkeley) %G%";
#endif /* not lint */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <tzfile.h>
#include <stdio.h>
#include "finger.h"
#include "pathnames.h"

#define	LINE_LEN	80
#define	TAB_LEN		8		/* 8 spaces between tabs */
#define	_PATH_PLAN	".plan"
#define	_PATH_PROJECT	".project"

lflag_print()
{
	extern PERSON *head;
	extern int pplan;
	register PERSON *pn;

	for (pn = head;;) {
		if (pn->info == PRINTED) {
			if (!(pn = pn->next))
				break;
			continue;
		}
		lprint(pn);
		if (!pplan) {
			(void)show_text(pn->dir, _PATH_PROJECT,
			    "Project:");
			if (!show_text(pn->dir, _PATH_PLAN, "Plan:"))
				(void)printf("No Plan.\n");
		}
		if (!(pn = pn->next))
			break;
		putchar('\n');
	}
}

lprint(pn)
	register PERSON *pn;
{
	extern time_t now;
	register struct tm *delta;
	register PERSON *p;
	register int cpr, len, maxlen;
	int oddfield;
	time_t time();
	char *t, *ctime();

	/*
	 * long format --
	 *	login name
	 *	real name
	 *	home directory
	 *	shell
	 *	office, office phone, home phone if available
	 */
	(void)printf("Login: %-15s\t\t\tName: %s\nDirectory: %-25s",
	    pn->name, pn->realname, pn->dir);
	(void)printf("\tShell: %-s\n", *pn->shell ? pn->shell : _PATH_BSHELL);

	/*
	 * try and print office, office phone, and home phone on one line;
	 * if that fails, do line filling so it looks nice.
	 */
#define	OFFICE_TAG		"Office"
#define	OFFICE_PHONE_TAG	"Office Phone"
	oddfield = 0;
	if (pn->office && pn->officephone &&
	    strlen(pn->office) + strlen(pn->officephone) +
	    sizeof(OFFICE_TAG) + 2 <= 5 * TAB_LEN) {
		(void)sprintf(tbuf, "%s: %s, %s", OFFICE_TAG, pn->office,
		    pn->officephone);
		oddfield = demi_print(tbuf, oddfield);
	} else {
		if (pn->office) {
			(void)sprintf(tbuf, "%s: %s", OFFICE_TAG, pn->office);
			oddfield = demi_print(tbuf, oddfield);
		}
		if (pn->officephone) {
			(void)sprintf(tbuf, "%s: %s", OFFICE_PHONE_TAG,
			    pn->officephone);
			oddfield = demi_print(tbuf, oddfield);
		}
	}
	if (pn->homephone) {
		(void)sprintf(tbuf, "%s: %s", "Home Phone", pn->homephone);
		oddfield = demi_print(tbuf, oddfield);
	}
	if (oddfield)
		putchar('\n');

	/*
	 * long format con't: if logged in
	 *	terminal
	 *	idle time
	 *	if messages allowed
	 *	where logged in from
	 */
	if (pn->info == LOGGEDIN) {
		/* find out longest device name for this user for formatting */
		for (maxlen = -1, p = pn; p; p = p->next)
			if (!strcmp(p->name, pn->name) &&
			    (len = strlen(p->tty)) > maxlen)
				maxlen = len;
		/* find rest of entries for user */
		for (p = pn; p; p = p->next) {
			if (strcmp(p->name, pn->name))
				continue;
			p->info = PRINTED;
			cpr = printf("On since %16.16s on %s",
			    ctime(&p->loginat), p->tty);
			/*
			 * idle time is tough; if have one, print a comma,
			 * then spaces to pad out the device name, then the
			 * idle time.  Follow with a comma if a remote login.
			 */
			delta = gmtime(&p->idletime);
			if (delta->tm_yday || delta->tm_hour || delta->tm_min) {
				cpr += printf("%-*s idle ",
				    maxlen - strlen(p->tty) + 1, ",");
				if (delta->tm_yday > 0) {
					cpr += printf("%d day%s ",
					   delta->tm_yday,
					   delta->tm_yday == 1 ? "" : "s");
				}
				cpr += printf("%d:%02d",
				    delta->tm_hour, delta->tm_min);
				if (*p->host) {
					putchar(',');
					++cpr;
				}
			}
			if (!p->writable)
				cpr += printf(" (messages off)");
			if (*p->host) {
				if (LINE_LEN < (cpr + 6 +
				    strlen(p->host))) {
					(void)printf("\n   ");
				}
				(void)printf(" from %s", p->host);
			}
			(void)putchar('\n');
		}
	}
	/*
	 * long format con't: if not logged in
	 *	when last logged in
	 */
	else if (!pn->loginat)
		(void)printf("Never logged in.\n");
	else {
		t = ctime(&pn->loginat);
		if (now - pn->loginat > SECSPERDAY * DAYSPERNYEAR / 2)
			cpr = printf("Last login %10.10s, %4.4s on %s",
			    t, t + 20, pn->tty);
		else
			cpr = printf("Last login %16.16s on %s", t, pn->tty);
		if (*pn->host) {
			if (LINE_LEN < (cpr + 6 + strlen(pn->host)))
				(void)printf("\n   ");
			(void)printf(" from %s", pn->host);
		}
		putchar('\n');
	}
	/*
	 * long format con't:
	 *	mail status
	 */
	chkmail(pn);
}

demi_print(str, oddfield)
	char *str;
	int oddfield;
{
	static int lenlast;
	int lenthis, maxlen;

	lenthis = strlen(str);
	if (oddfield) {
		/*
		 * We left off on an odd number of fields.  If we haven't
		 * crossed the midpoint of the screen, and we have room for
		 * the next field, print it on the same line; otherwise,
		 * print it on a new line.
		 *
		 * Note: we insist on having the right hand fields start
		 * no less than 5 tabs out.
		 */
		maxlen = 5 * TAB_LEN;
		if (maxlen < lenlast)
			maxlen = lenlast;
		if (((((maxlen / TAB_LEN) + 1) * TAB_LEN) +
		    lenthis) <= LINE_LEN) {
			while(lenlast < (4 * TAB_LEN)) {
				putchar('\t');
				lenlast += TAB_LEN;
			}
			(void)printf("\t%s\n", str);	/* force one tab */
		} else {
			(void)printf("\n%s", str);	/* go to next line */
			oddfield = !oddfield;	/* this'll be undone below */
		}
	} else
		(void)printf("%s", str);
	oddfield = !oddfield;			/* toggle odd/even marker */
	lenlast = lenthis;
	return(oddfield);
}

show_text(directory, file_name, header)
	char *directory, *file_name, *header;
{
	register int fd, n;

	(void)sprintf(tbuf, "%s/%s", directory, file_name);
	if ((fd = open(tbuf, O_RDONLY, 0)) < 0)
		return(0);
	(void)printf("%s\n", header);
	(void)fflush(stdout);
	while ((n = read(fd, tbuf, sizeof(tbuf))) > 0)
		if (write(1, tbuf, n) != n)
			break;
	(void)close(fd);
	return(1);
}

chkmail(pn)
	PERSON *pn;
{
	register char *date;
	struct stat sb;

	/*
	 * build path of user's mail box and get stats; if missing
	 * or empty, no mail.
	 */
	(void)sprintf(tbuf, "%s/%s", _PATH_MAILDIR, pn->name);
	if (stat(tbuf, &sb) < 0 || !sb.st_size) {
		(void)printf("No unread mail.\n");
		return;
	}

	/*
	 * if access time matches the modification time then we know
	 * that new mail was received but we haven't a clue as to when
	 * it was last read.
	 */
	date = ctime(&sb.st_ctime);
	if (sb.st_atime == sb.st_ctime)
		(void)printf("New mail received %16.16s.\n", date);
	else if (sb.st_atime > sb.st_ctime) {
		date = ctime(&sb.st_atime);
		(void)printf("No new mail, last read %16.16s.\n", date);
	} else {
		(void)printf("Mail received %16.16s; ", date);
		date = ctime(&sb.st_atime);
		(void)printf("last read %16.16s.\n", date);
	}
}
