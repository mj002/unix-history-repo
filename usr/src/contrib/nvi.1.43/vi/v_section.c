/*-
 * Copyright (c) 1992, 1993, 1994
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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
 */

#ifndef lint
static char sccsid[] = "@(#)v_section.c	9.1 (Berkeley) 11/9/94";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include "compat.h"
#include <db.h>
#include <regex.h>

#include "vi.h"
#include "vcmd.h"

/*
 * !!!
 * In historic vi, the section commands ignored empty lines, unlike the
 * paragraph commands, which was probably okay.  However, they also moved
 * to the start of the last line when there where no more sections instead
 * of the end of the last line like the paragraph commands.  I've changed
 * the latter behavior to match the paragraph commands.
 *
 * In historic vi, a section was defined as the first character(s) of the
 * line matching, which could be followed by anything.  This implementation
 * follows that historic practice.
 *
 * !!!
 * The historic vi documentation (USD:15-10) claimed:
 *	The section commands interpret a preceding count as a different
 *	window size in which to redraw the screen at the new location,
 *	and this window size is the base size for newly drawn windows
 *	until another size is specified.  This is very useful if you are
 *	on a slow terminal ...
 *
 * I can't get the 4BSD vi to do this, it just beeps at me.  For now, a
 * count to the section commands simply repeats the command.
 */

/*
 * v_sectionf -- [count]]]
 *	Move forward count sections/functions.
 *
 * !!!
 * Using ]] as a motion command was a bit special, historically.  It could
 * match } as well as the usual { and section values.  If it matched a { or
 * a section, it did NOT include the matched line.  If it matched a }, it
 * did include the line.  No clue why.
 */
int
v_sectionf(sp, vp)
	SCR *sp;
	VICMDARG *vp;
{
	recno_t cnt, lno;
	size_t len;
	char *p, *list, *lp;

	/* Get the macro list. */
	if ((list = O_STR(sp, O_SECTIONS)) == NULL)
		return (1);

	/*
	 * !!!
	 * If the starting cursor position is at or before any non-blank
	 * characters in the line, i.e. the movement is cutting all of the
	 * line's text, the buffer is in line mode.  It's a lot easier to
	 * check here, because we know that the end is going to be the start
	 * or end of a line.
	 */
	if (ISMOTION(vp))
		if (vp->m_start.cno == 0)
			F_SET(vp, VM_LMODE);
		else {
			vp->m_stop = vp->m_start;
			vp->m_stop.cno = 0;
			if (nonblank(sp, vp->m_stop.lno, &vp->m_stop.cno))
				return (1);
			if (vp->m_start.cno <= vp->m_stop.cno)
				F_SET(vp, VM_LMODE);
		}

	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	for (lno = vp->m_start.lno;
	    (p = file_gline(sp, ++lno, &len)) != NULL;) {
		if (len == 0)
			continue;
		if (p[0] == '{' || ISMOTION(vp) && p[0] == '}') {
			if (!--cnt) {
				if (p[0] == '{')
					goto adjust1;
				goto adjust2;
			}
			continue;
		}
		/*
		 * !!!
		 * Historic documentation (USD:15-11, 4.2) said that formfeed
		 * characters (^L) in the first column delimited sections.
		 * The historic code mentions formfeed characters, but never
		 * implements them.  Seems reasonable, do it.
		 */
		if (p[0] == '\014') {
			if (!--cnt)
				goto adjust1;
			continue;
		}
		if (p[0] != '.' || len < 2)
			continue;
		for (lp = list; *lp != '\0'; lp += 2 * sizeof(*lp))
			if (lp[0] == p[1] &&
			    (lp[1] == ' ' && len == 2 || lp[1] == p[2]) &&
			    !--cnt) {
				/*
				 * !!!
				 * If not cutting this line, adjust to the end
				 * of the previous one.  Otherwise, position to
				 * column 0.
				 */
adjust1:			if (ISMOTION(vp))
					goto ret1;

adjust2:			vp->m_stop.lno = lno;
				vp->m_stop.cno = 0;
				goto ret2;
			}
	}

	/* If moving forward, reached EOF, check to see if we started there. */
	if (vp->m_start.lno == lno - 1) {
		v_eof(sp, NULL);
		return (1);
	}

ret1:	if (file_gline(sp, --lno, &len) == NULL)
		return (1);
	vp->m_stop.lno = lno;
	vp->m_stop.cno = len ? len - 1 : 0;

	/*
	 * Non-motion commands go to the end of the range.  Delete and
	 * yank stay at the start of the range.  Ignore others.
	 */
ret2:	if (ISMOTION(vp)) {
		vp->m_final = vp->m_start;
		if (F_ISSET(vp, VM_LMODE))
			vp->m_final.cno = 0;
	} else
		vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_sectionb -- [count][[
 *	Move backward count sections/functions.
 */
int
v_sectionb(sp, vp)
	SCR *sp;
	VICMDARG *vp;
{
	size_t len;
	recno_t cnt, lno;
	char *p, *list, *lp;

	/* An empty file or starting from line 1 is always illegal. */
	if (vp->m_start.lno <= 1) {
		v_sof(sp, NULL);
		return (1);
	}

	/* Get the macro list. */
	if ((list = O_STR(sp, O_SECTIONS)) == NULL)
		return (1);

	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	for (lno = vp->m_start.lno;
	    (p = file_gline(sp, --lno, &len)) != NULL;) {
		if (len == 0)
			continue;
		if (p[0] == '{') {
			if (!--cnt)
				goto adjust1;
			continue;
		}
		/*
		 * !!!
		 * Historic documentation (USD:15-11, 4.2) said that formfeed
		 * characters (^L) in the first column delimited sections.
		 * The historic code mentions formfeed characters, but never
		 * implements them.  Seems reasonable, do it.
		 */
		if (p[0] == '\014') {
			if (!--cnt)
				goto adjust1;
			continue;
		}
		if (p[0] != '.' || len < 2)
			continue;
		for (lp = list; *lp != '\0'; lp += 2 * sizeof(*lp))
			if (lp[0] == p[1] &&
			    (lp[1] == ' ' && len == 2 || lp[1] == p[2]) &&
			    !--cnt) {
adjust1:			vp->m_stop.lno = lno;
				vp->m_stop.cno = 0;
				goto ret1;
			}
	}

	/*
	 * If moving backward, reached SOF, which is a movement sink.
	 * We already checked for starting there.
	 */
	vp->m_stop.lno = 1;
	vp->m_stop.cno = 0;

	/*
	 * All commands move to the end of the range.
	 *
	 * !!!
	 * Historic practice is the section cut was in line mode if it started
	 * from column 0 and was in the backward direction.  Otherwise, left
	 * motion commands adjust the starting point to the character before
	 * the current one.  What makes this worse is that if it cut to line
	 * mode it also went to the first non-<blank>.
	 */
ret1:	if (vp->m_start.cno == 0) {
		F_CLR(vp, VM_RCM_MASK);
		F_SET(vp, VM_RCM_SETFNB);

		--vp->m_start.lno;
		F_SET(vp, VM_LMODE);
	} else
		--vp->m_start.cno;

	vp->m_final = vp->m_stop;
	return (0);
}
