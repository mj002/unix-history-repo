/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
static char sccsid[] = "@(#)close.c	5.1 (Berkeley) %G%";
#endif not lint

#include "imp.h"
#include "imPcodes.h"

closepl(){
	putch(imP_ENDPAGE);
	fflush(stdout);
}
