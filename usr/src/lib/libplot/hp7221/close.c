/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
static char sccsid[] = "@(#)close.c	5.2 (Berkeley) %G%";
#endif not lint

#include <signal.h>
#include "hp7221.h"

void
closepl()
{
	/* receive interupts */
	signal(SIGINT, SIG_IGN);
	printf( "v@}" );			/* Put pen away. */
	fflush( stdout );
}
