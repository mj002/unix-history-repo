#ifndef lint
static char sccsid[] = "@(#)strout.c	5.1 (Berkeley) %G%";
#endif not lint

#include	<stdio.h>

_strout(count, string, adjust, file, fillch)
register char *string;
register count;
int adjust;
register FILE *file;
{
	while (adjust < 0) {
		if (*string=='-' && fillch=='0') {
			putc(*string++, file);
			count--;
		}
		putc(fillch, file);
		adjust++;
	}
	while (--count>=0)
		putc(*string++, file);
	while (adjust) {
		putc(fillch, file);
		adjust--;
	}
}
