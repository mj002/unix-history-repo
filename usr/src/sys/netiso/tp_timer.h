/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)tp_timer.h	7.6 (Berkeley) %G%
 */

/***********************************************************
		Copyright IBM Corporation 1987

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of IBM not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

IBM DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
IBM BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

/*
 * ARGO Project, Computer Sciences Dept., University of Wisconsin - Madison
 */
/* 
 * ARGO TP
 *
 * $Header: tp_timer.h,v 5.1 88/10/12 12:21:41 root Exp $
 * $Source: /usr/argo/sys/netiso/RCS/tp_timer.h,v $
 *
 * ARGO TP
 * The callout structures used by the tp timers.
 */

#ifndef __TP_CALLOUT__
#define __TP_CALLOUT__

/* C timers - one per tpcb, generally cancelled */

struct	Ecallarg {
	u_int	c_arg1;
	u_int	c_arg2;
	int		c_arg3;
};

struct	Ccallout {
	int	c_time;		/* incremental time */
};

#define SET_DELACK(t) {\
    (t)->tp_flags |= TPF_DELACK; \
    if ((t)->tp_fasttimeo == 0)\
	{ (t)->tp_fasttimeo = tp_ftimeolist; tp_ftimeolist = (t); } }

#endif __TP_CALLOUT__
