/* ===-- multc3.c - Implement __multc3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __multc3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"
#include "int_math.h"

/* Returns: the product of a + ib and c + id */

COMPILER_RT_ABI long double _Complex
__multc3(long double __a, long double __b, long double __c, long double __d)
{
    long double __ac = __a * __c;
    long double __bd = __b * __d;
    long double __ad = __a * __d;
    long double __bc = __b * __c;
    long double _Complex z;
    __real__ z = __ac - __bd;
    __imag__ z = __ad + __bc;
    if (crt_isnan(__real__ z) && crt_isnan(__imag__ z))
    {
        int __recalc = 0;
        if (crt_isinf(__a) || crt_isinf(__b))
        {
            __a = crt_copysignl(crt_isinf(__a) ? 1 : 0, __a);
            __b = crt_copysignl(crt_isinf(__b) ? 1 : 0, __b);
            if (crt_isnan(__c))
                __c = crt_copysignl(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysignl(0, __d);
            __recalc = 1;
        }
        if (crt_isinf(__c) || crt_isinf(__d))
        {
            __c = crt_copysignl(crt_isinf(__c) ? 1 : 0, __c);
            __d = crt_copysignl(crt_isinf(__d) ? 1 : 0, __d);
            if (crt_isnan(__a))
                __a = crt_copysignl(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysignl(0, __b);
            __recalc = 1;
        }
        if (!__recalc && (crt_isinf(__ac) || crt_isinf(__bd) ||
                          crt_isinf(__ad) || crt_isinf(__bc)))
        {
            if (crt_isnan(__a))
                __a = crt_copysignl(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysignl(0, __b);
            if (crt_isnan(__c))
                __c = crt_copysignl(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysignl(0, __d);
            __recalc = 1;
        }
        if (__recalc)
        {
            __real__ z = CRT_INFINITY * (__a * __c - __b * __d);
            __imag__ z = CRT_INFINITY * (__a * __d + __b * __c);
        }
    }
    return z;
}
