/*-
 * This module is believed to contain source code proprietary to AT&T.
 * Use and redistribution is subject to the Berkeley Software License
 * Agreement and your Software Agreement with AT&T (Western Electric).
 *
 *	@(#)tab300.c	4.2 (Berkeley) 4/18/91
 */

#define INCH 240
/*
 * DASI 300 10 pitch
 * nroff driving tables
 * width and code tables
 */

struct {
	int bset;
	int breset;
	int Hor;
	int Vert;
	int Newline;
	int Char;
	int Em;
	int Halfline;
	int Adj;
	char *twinit;
	char *twrest;
	char *twnl;
	char *hlr;
	char *hlf;
	char *flr;
	char *bdon;
	char *bdoff;
	char *ploton;
	char *plotoff;
	char *up;
	char *down;
	char *right;
	char *left;
	char *codetab[256-32];
	int zzz;
	} t = {
/*bset*/	0,
/*breset*/	0177420,
/*Hor*/		INCH/60,
/*Vert*/	INCH/48,
/*Newline*/	INCH/6,
/*Char*/	INCH/10,
/*Em*/		INCH/10,
/*Halfline*/	INCH/12,
/*Adj*/		INCH/10,
/*twinit*/	"\007",
/*twrest*/	"\007",
/*twnl*/	"\015\n",
/*hlr*/		"\006\013\013\013\013\006",
/*hlf*/		"\006\012\012\012\012\006",
/*flr*/		"\013",
/*bdon*/	"",
/*bdoff*/	"",
/*ploton*/	"\006",
/*plotoff*/	"\033\006",
/*up*/		"\013",
/*down*/	"\n",
/*right*/	" ",
/*left*/	"\b",
/*codetab*/
#include "code.300"
