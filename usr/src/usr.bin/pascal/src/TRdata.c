/*-
 * Copyright (c) 1980 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)TRdata.c	5.2 (Berkeley) %G%";
#endif /* not lint */

#include "whoami.h"
#include "0.h"
#ifdef	PI1
#ifdef	DEBUG
char	*trnames[] =
{
	0,
	"MINUS",
	"MOD",
	"DIV",
	"DIVD",
	"MULT",
	"ADD",
	"SUB",
	"EQ",
	"NE",
	"LT",
	"GT",
	"LE",
	"GE",
	"NOT",
	"AND",
	"OR",
	"ASGN",
	"PLUS",
	"IN",
	"LISTPP",
	"PDEC",
	"FDEC",
	"PVAL",
	"PVAR",
	"PFUNC",
	"PPROC",
	"NIL",
	"STRNG",
	"CSTRNG",
	"PLUSC",
	"MINUSC",
	"ID",
	"INT",
	"FINT",
	"CINT",
	"CFINT",
	"TYPTR",
	"TYPACK",
	"TYSCAL",
	"TYRANG",
	"TYARY",
	"TYFILE",
	"TYSET",
	"TYREC",
	"TYFIELD",
	"TYVARPT",
	"TYVARNT",
	"CSTAT",
	"BLOCK",
	"BSTL",
	"LABEL",
	"PCALL",
	"FCALL",
	"CASE",
	"WITH",
	"WHILE",
	"REPEAT",
	"FORU",
	"FORD",
	"GOTO",
	"IF",
	"ASRT",
	"CSET",
	"RANG",
	"VAR",
	"ARGL",
	"ARY",
	"FIELD",
	"PTR",
	"WEXP",
	"PROG",
	"BINT",
	"CBINT",
	"IFEL",
	"IFX",
	"TYID",
	"COPSTR",
	"BOTTLE",
	"RFIELD",
	"FLDLST",
	"LAST"
};
#endif
#endif

#ifndef PC
#ifndef OBJ
char	*trdesc[] =
{
	0,
	"dp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dpp",
	"dp",
	"dpp",
	"dpp",
	"npp",
	"dp",
	"dpp",
	"pp",
	"n\"pp",
	"n\"pp",
	"pp",
	"pp",
	"pp",
	"p",
	"d",
	"dp",
	"p",
	"p",
	"p",
	"p",
	"dp",
	"dp",
	"p",
	"p",
	"np",
	"np",
	"np",
	"npp",
	"npp",
	"np",
	"np",
	"np",
	"pp",
	"nppp",
	"npp",
	"npp",
	"np",
	"np",
	"n\"p",
	"n\"p",
	"n\"p",
	"npp",
	"npp",
	"npp",
	"npp",
	"nppp",
	"nppp",
	"n\"",
	"nppp",
	"np",
	"dp",
	"pp",
	"n\"p",
	"p",
	"p",
	"pp",
	"",
	"ppp",
	"n\"pp",
	"dp",
	"p",
	"nppp",
	"nppp",
	"np",
	"s",
	"nnnnn",
	"npp",
	"npp",
	"x"
};
#endif
#endif
char	*opnames[] =
{
	0,
	"unary -",
	"mod",
	"div",
	"/",
	"*",
	"+",
	"-",
	"=",
	"<>",
	"<",
	">",
	"<=",
	">=",
	"not",
	"and",
	"or",
	":=",
	"unary +",
	"in"
};
