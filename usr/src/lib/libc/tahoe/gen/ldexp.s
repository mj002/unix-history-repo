/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 *
 * This code is derived from software contributed to Berkeley by
 * Computer Consoles Inc.
 */

#if defined(LIBC_SCCS) && !defined(lint)
	.asciz "@(#)ldexp.s	5.2 (Berkeley) %G%"
#endif /* LIBC_SCCS and not lint */

/*
 * double ldexp (value, exp)
 *	double value;
 *	int exp;
 *
 * Ldexp returns value*2**exp, if that result is in range.
 * If underflow occurs, it returns zero.  If overflow occurs,
 * it returns a value of appropriate sign and largest
 * possible magnitude.  In case of either overflow or underflow,
 * the external int "errno" is set to ERANGE.  Note that errno is
 * not modified if no error occurs, so if you intend to test it
 * after you use ldexp, you had better set it to something
 * other than ERANGE first (zero is a reasonable value to use).
 *
 * Constants
 */

/*
 * we can't include errno.h anymore, ANSI says that it defines errno.
 *
 * #include <errno.h>
 */
#define	ERANGE	34
#include <tahoemath/fp.h>

#include "DEFS.h"

ENTRY(ldexp, 0)
	movl	4(fp),r0	/* Fetch "value" */
	movl	8(fp),r1	

	andl3	$EXPMASK,r0,r2	/* r2 := shifted biased exponent */
	jeql	ld1		/* If it's zero, we're done */
	shar	$EXPSHIFT,r2,r2	/* shift to get value of exponent  */

	addl2	12(fp),r2	/* r2 := new biased exponent */
	jleq	under		/* if it's <= 0, we have an underflow */
	cmpl	r2,$256		/* Otherwise check if it's too big */
	jgeq	over		/* jump if overflow */
/*
*	Construct the result and return
*/
	andl2	$0!EXPMASK,r0	/* clear old exponent */
	shal 	$EXPSHIFT,r2,r2	/* Put the exponent back in the result */
	orl2	r2,r0
ld1:	ret
/*
*	Underflow
*/
under:	clrl	r0		/* Result is zero */
	clrl	r1
	jbr	err		/* Join general error code */
/*
*	Overflow
*/
over:	movl	huge0,r0	/* Largest possible floating magnitude */
	movl	huge1,r1
	jbc	$31,4(fp),err	/* Jump if argument was positive */
	orl2	$SIGNBIT,r0	/* If arg < 0, make result negative */

err:	movl	$ERANGE,_errno	/* Indicate range error */
	ret

	.data
	.globl	_errno		/* error flag */
huge0:	.word	0x7fff		/* The largest number that can */
	.word	0xffff		/*   be represented in a long floating */
huge1:	.word	0xffff		/*   number.  */
	.word	0xffff		
