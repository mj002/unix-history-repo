/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
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
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
static char sccsid[] = "@(#)ectype.c	3.3 (Berkeley) %G%";
#endif /* not lint */

#include "ectype.h"

char	ectype[] = {
/* 0x00 */
    E_SPACE,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0x10 */
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0x20 */
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0x30 */
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0x40 */
    E_SPACE,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
/* 0x50 */
    E_PRINT|E_PUNCT,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
/* 0x60 */
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
/* 0x70 */
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
    E_PRINT|E_PUNCT,
/* 0x80 */
    0x00,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0x90 */
    0x00,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0xA0 */
    0x00,
    E_PRINT|E_PUNCT,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    E_PRINT|E_LOWER,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0xB0 */
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0xC0 */
    E_PRINT|E_PUNCT,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0xD0 */
    E_PRINT|E_PUNCT,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0xE0 */
    E_PRINT|E_PUNCT,
    0x00,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    E_PRINT|E_UPPER,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
/* 0xF0 */
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    E_PRINT|E_DIGIT,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
};
