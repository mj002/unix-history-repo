/*
 * Copyright (c) 1980 Regents of the University of California.
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
static char sccsid[] = "@(#)move.c	5.4 (Berkeley) 6/1/90";
#endif /* not lint */

# include	"trek.h"

/*
**  Move Under Warp or Impulse Power
**
**	`Ramflag' is set if we are to be allowed to ram stars,
**	Klingons, etc.  This is passed from warp(), which gets it from
**	either play() or ram().  Course is the course (0 -> 360) at
**	which we want to move.  `Speed' is the speed we
**	want to go, and `time' is the expected time.  It
**	can get cut short if a long range tractor beam is to occur.  We
**	cut short the move so that the user doesn't get docked time and
**	energy for distance which he didn't travel.
**
**	We check the course through the current quadrant to see that he
**	doesn't run into anything.  After that, though, space sort of
**	bends around him.  Note that this puts us in the awkward posi-
**	tion of being able to be dropped into a sector which is com-
**	pletely surrounded by stars.  Oh Well.
**
**	If the SINS (Space Inertial Navigation System) is out, we ran-
**	domize the course accordingly before ever starting to move.
**	We will still move in a straight line.
**
**	Note that if your computer is out, you ram things anyway.  In
**	other words, if your computer and sins are both out, you're in
**	potentially very bad shape.
**
**	Klingons get a chance to zap you as you leave the quadrant.
**	By the way, they also try to follow you (heh heh).
**
**	Return value is the actual amount of time used.
**
**
**	Uses trace flag 4.
*/

double move(ramflag, course, time, speed)
int	ramflag;
int	course;
double	time;
double	speed;
{
	double			angle;
	double			x, y, dx, dy;
	register int		ix, iy;
	double			bigger;
	int			n;
	register int		i;
	double			dist;
	double			sectsize;
	double			xn;
	double			evtime;

#	ifdef xTRACE
	if (Trace)
		printf("move: ramflag %d course %d time %.2f speed %.2f\n",
			ramflag, course, time, speed);
#	endif
	sectsize = NSECTS;
	/* initialize delta factors for move */
	angle = course * 0.0174532925;
	if (damaged(SINS))
		angle += Param.navigcrud[1] * (franf() - 0.5);
	else
		if (Ship.sinsbad)
			angle += Param.navigcrud[0] * (franf() - 0.5);
	dx = -cos(angle);
	dy = sin(angle);
	bigger = fabs(dx);
	dist = fabs(dy);
	if (dist > bigger)
		bigger = dist;
	dx /= bigger;
	dy /= bigger;

	/* check for long range tractor beams */
	/****  TEMPORARY CODE == DEBUGGING  ****/
	evtime = Now.eventptr[E_LRTB]->date - Now.date;
#	ifdef xTRACE
	if (Trace)
		printf("E.ep = %u, ->evcode = %d, ->date = %.2f, evtime = %.2f\n",
			Now.eventptr[E_LRTB], Now.eventptr[E_LRTB]->evcode,
			Now.eventptr[E_LRTB]->date, evtime);
#	endif
	if (time > evtime && Etc.nkling < 3)
	{
		/* then we got a LRTB */
		evtime += 0.005;
		time = evtime;
	}
	else
		evtime = -1.0e50;
	dist = time * speed;

	/* move within quadrant */
	Sect[Ship.sectx][Ship.secty] = EMPTY;
	x = Ship.sectx + 0.5;
	y = Ship.secty + 0.5;
	xn = NSECTS * dist * bigger;
	n = xn + 0.5;
#	ifdef xTRACE
	if (Trace)
		printf("dx = %.2f, dy = %.2f, xn = %.2f, n = %d\n", dx, dy, xn, n);
#	endif
	Move.free = 0;

	for (i = 0; i < n; i++)
	{
		ix = (x += dx);
		iy = (y += dy);
#		ifdef xTRACE
		if (Trace)
			printf("ix = %d, x = %.2f, iy = %d, y = %.2f\n", ix, x, iy, y);
#		endif
		if (x < 0.0 || y < 0.0 || x >= sectsize || y >= sectsize)
		{
			/* enter new quadrant */
			dx = Ship.quadx * NSECTS + Ship.sectx + dx * xn;
			dy = Ship.quady * NSECTS + Ship.secty + dy * xn;
			if (dx < 0.0)
				ix = -1;
			else
				ix = dx + 0.5;
			if (dy < 0.0)
				iy = -1;
			else
				iy = dy + 0.5;
#			ifdef xTRACE
			if (Trace)
				printf("New quad: ix = %d, iy = %d\n", ix, iy);
#			endif
			Ship.sectx = x;
			Ship.secty = y;
			compkldist(0);
			Move.newquad = 2;
			attack(0);
			checkcond();
			Ship.quadx = ix / NSECTS;
			Ship.quady = iy / NSECTS;
			Ship.sectx = ix % NSECTS;
			Ship.secty = iy % NSECTS;
			if (ix < 0 || Ship.quadx >= NQUADS || iy < 0 || Ship.quady >= NQUADS)
				if (!damaged(COMPUTER))
				{
					dumpme(0);
				}
				else
					lose(L_NEGENB);
			initquad(0);
			n = 0;
			break;
		}
		if (Sect[ix][iy] != EMPTY)
		{
			/* we just hit something */
			if (!damaged(COMPUTER) && ramflag <= 0)
			{
				ix = x - dx;
				iy = y - dy;
				printf("Computer reports navigation error; %s stopped at %d,%d\n",
					Ship.shipname, ix, iy);
				Ship.energy -= Param.stopengy * speed;
				break;
			}
			/* test for a black hole */
			if (Sect[ix][iy] == HOLE)
			{
				/* get dumped elsewhere in the galaxy */
				dumpme(1);
				initquad(0);
				n = 0;
				break;
			}
			ram(ix, iy);
			break;
		}
	}
	if (n > 0)
	{
		dx = Ship.sectx - ix;
		dy = Ship.secty - iy;
		dist = sqrt(dx * dx + dy * dy) / NSECTS;
		time = dist / speed;
		if (evtime > time)
			time = evtime;		/* spring the LRTB trap */
		Ship.sectx = ix;
		Ship.secty = iy;
	}
	Sect[Ship.sectx][Ship.secty] = Ship.ship;
	compkldist(0);
	return (time);
}
