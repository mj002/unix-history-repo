/*
 * Copyright 1988 by the Massachusetts Institute of Technology.
 * For copying and distribution information, please see the file
 * <Copyright.MIT>.
 *
 *	from: conf-bsdapollo.h,v 4.1 89/01/24 14:26:22 jtkohl Exp $
 *	$Id: conf-bsdapollo.h,v 1.1.1.1 1994/09/30 14:49:53 csgr Exp $
 */

#define BSDUNIX
#define BITS32
#define BIG
#define MSBFIRST
#define DES_SHIFT_SHIFT
/*
 * As of SR10, the C compiler claims to be __STDC__, but doesn't support
 * const.  Sigh.
 */
#define const


