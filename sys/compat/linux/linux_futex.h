/*	$NetBSD: linux_futex.h,v 1.2 2005/12/11 12:20:19 christos Exp $ */

/*-
 * Copyright (c) 2005 Emmanuel Dreyfus, all rights reserved.
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
 *	This product includes software developed by Emmanuel Dreyfus
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#define LINUX_FUTEX_WAIT	0
#define LINUX_FUTEX_WAKE	1
#define LINUX_FUTEX_FD		2
#define LINUX_FUTEX_REQUEUE	3
#define LINUX_FUTEX_CMP_REQUEUE	4
#define LINUX_FUTEX_WAKE_OP	5

#define FUTEX_OP_SET            0	/* *(int *)UADDR2 = OPARG; */
#define FUTEX_OP_ADD            1	/* *(int *)UADDR2 += OPARG; */
#define FUTEX_OP_OR             2	/* *(int *)UADDR2 |= OPARG; */
#define FUTEX_OP_ANDN           3	/* *(int *)UADDR2 &= ~OPARG; */
#define FUTEX_OP_XOR            4	/* *(int *)UADDR2 ^= OPARG; */

#define FUTEX_OP_OPARG_SHIFT    8	/* Use (1 << OPARG) instead of OPARG.  */

#define FUTEX_OP_CMP_EQ         0	/* if (oldval == CMPARG) wake */
#define FUTEX_OP_CMP_NE         1	/* if (oldval != CMPARG) wake */
#define FUTEX_OP_CMP_LT         2	/* if (oldval < CMPARG) wake */
#define FUTEX_OP_CMP_LE         3	/* if (oldval <= CMPARG) wake */
#define FUTEX_OP_CMP_GT         4	/* if (oldval > CMPARG) wake */
#define FUTEX_OP_CMP_GE         5	/* if (oldval >= CMPARG) wake */

#endif	/* !_LINUX_FUTEX_H */
