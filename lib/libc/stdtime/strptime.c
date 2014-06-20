/*-
 * Copyright (c) 1994 Powerdog Industries.  All rights reserved.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY POWERDOG INDUSTRIES ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE POWERDOG INDUSTRIES BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Powerdog Industries.
 */

#include <sys/cdefs.h>
#ifndef lint
#ifndef NOID
static char copyright[] __unused =
"@(#) Copyright (c) 1994 Powerdog Industries.  All rights reserved.";
static char sccsid[] __unused = "@(#)strptime.c	0.1 (Powerdog) 94/03/27";
#endif /* !defined NOID */
#endif /* not lint */
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "un-namespace.h"
#include "libc_private.h"
#include "timelocal.h"

static char * _strptime(const char *, const char *, struct tm *, int *, locale_t);

#define asizeof(a)	(sizeof (a) / sizeof ((a)[0]))

static char *
_strptime(const char *buf, const char *fmt, struct tm *tm, int *GMTp,
		locale_t locale)
{
	char	c;
	const char *ptr;
	int	i,
		len;
	int Ealternative, Oalternative;
	struct lc_time_T *tptr = __get_current_time_locale(locale);

	ptr = fmt;
	while (*ptr != 0) {
		if (*buf == 0)
			break;

		c = *ptr++;

		if (c != '%') {
			if (isspace_l((unsigned char)c, locale))
				while (*buf != 0 && 
				       isspace_l((unsigned char)*buf, locale))
					buf++;
			else if (c != *buf++)
				return 0;
			continue;
		}

		Ealternative = 0;
		Oalternative = 0;
label:
		c = *ptr++;
		switch (c) {
		case 0:
		case '%':
			if (*buf++ != '%')
				return 0;
			break;

		case '+':
			buf = _strptime(buf, tptr->date_fmt, tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'C':
			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			/* XXX This will break for 3-digit centuries. */
			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit_l((unsigned char)*buf, locale); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i < 19)
				return 0;

			tm->tm_year = i * 100 - 1900;
			break;

		case 'c':
			buf = _strptime(buf, tptr->c_fmt, tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'D':
			buf = _strptime(buf, "%m/%d/%y", tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'E':
			if (Ealternative || Oalternative)
				break;
			Ealternative++;
			goto label;

		case 'O':
			if (Ealternative || Oalternative)
				break;
			Oalternative++;
			goto label;

		case 'F':
			buf = _strptime(buf, "%Y-%m-%d", tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'R':
			buf = _strptime(buf, "%H:%M", tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'r':
			buf = _strptime(buf, tptr->ampm_fmt, tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'T':
			buf = _strptime(buf, "%H:%M:%S", tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'X':
			buf = _strptime(buf, tptr->X_fmt, tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'x':
			buf = _strptime(buf, tptr->x_fmt, tm, GMTp, locale);
			if (buf == 0)
				return 0;
			break;

		case 'j':
			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			len = 3;
			for (i = 0; len && *buf != 0 &&
			     isdigit_l((unsigned char)*buf, locale); buf++){
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i < 1 || i > 366)
				return 0;

			tm->tm_yday = i - 1;
			break;

		case 'M':
		case 'S':
			if (*buf == 0 ||
				isspace_l((unsigned char)*buf, locale))
				break;

			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			len = 2;
			for (i = 0; len && *buf != 0 &&
				isdigit_l((unsigned char)*buf, locale); buf++){
				i *= 10;
				i += *buf - '0';
				len--;
			}

			if (c == 'M') {
				if (i > 59)
					return 0;
				tm->tm_min = i;
			} else {
				if (i > 60)
					return 0;
				tm->tm_sec = i;
			}

			if (*buf != 0 &&
				isspace_l((unsigned char)*buf, locale))
				while (*ptr != 0 &&
				       !isspace_l((unsigned char)*ptr, locale))
					ptr++;
			break;

		case 'H':
		case 'I':
		case 'k':
		case 'l':
			/*
			 * Of these, %l is the only specifier explicitly
			 * documented as not being zero-padded.  However,
			 * there is no harm in allowing zero-padding.
			 *
			 * XXX The %l specifier may gobble one too many
			 * digits if used incorrectly.
			 */
			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit_l((unsigned char)*buf, locale); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (c == 'H' || c == 'k') {
				if (i > 23)
					return 0;
			} else if (i > 12)
				return 0;

			tm->tm_hour = i;

			if (*buf != 0 &&
			    isspace_l((unsigned char)*buf, locale))
				while (*ptr != 0 &&
				       !isspace_l((unsigned char)*ptr, locale))
					ptr++;
			break;

		case 'p':
			/*
			 * XXX This is bogus if parsed before hour-related
			 * specifiers.
			 */
			len = strlen(tptr->am);
			if (strncasecmp_l(buf, tptr->am, len, locale) == 0) {
				if (tm->tm_hour > 12)
					return 0;
				if (tm->tm_hour == 12)
					tm->tm_hour = 0;
				buf += len;
				break;
			}

			len = strlen(tptr->pm);
			if (strncasecmp_l(buf, tptr->pm, len, locale) == 0) {
				if (tm->tm_hour > 12)
					return 0;
				if (tm->tm_hour != 12)
					tm->tm_hour += 12;
				buf += len;
				break;
			}

			return 0;

		case 'A':
		case 'a':
			for (i = 0; i < asizeof(tptr->weekday); i++) {
				len = strlen(tptr->weekday[i]);
				if (strncasecmp_l(buf, tptr->weekday[i],
						len, locale) == 0)
					break;
				len = strlen(tptr->wday[i]);
				if (strncasecmp_l(buf, tptr->wday[i],
						len, locale) == 0)
					break;
			}
			if (i == asizeof(tptr->weekday))
				return 0;

			tm->tm_wday = i;
			buf += len;
			break;

		case 'U':
		case 'W':
			/*
			 * XXX This is bogus, as we can not assume any valid
			 * information present in the tm structure at this
			 * point to calculate a real value, so just check the
			 * range for now.
			 */
			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit_l((unsigned char)*buf, locale); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i > 53)
				return 0;

			if (*buf != 0 &&
			    isspace_l((unsigned char)*buf, locale))
				while (*ptr != 0 &&
				       !isspace_l((unsigned char)*ptr, locale))
					ptr++;
			break;

		case 'w':
			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			i = *buf - '0';
			if (i > 6)
				return 0;

			tm->tm_wday = i;

			if (*buf != 0 &&
			    isspace_l((unsigned char)*buf, locale))
				while (*ptr != 0 &&
				       !isspace_l((unsigned char)*ptr, locale))
					ptr++;
			break;

		case 'd':
		case 'e':
			/*
			 * The %e specifier is explicitly documented as not
			 * being zero-padded but there is no harm in allowing
			 * such padding.
			 *
			 * XXX The %e specifier may gobble one too many
			 * digits if used incorrectly.
			 */
			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit_l((unsigned char)*buf, locale); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i > 31)
				return 0;

			tm->tm_mday = i;

			if (*buf != 0 &&
			    isspace_l((unsigned char)*buf, locale))
				while (*ptr != 0 &&
				       !isspace_l((unsigned char)*ptr, locale))
					ptr++;
			break;

		case 'B':
		case 'b':
		case 'h':
			for (i = 0; i < asizeof(tptr->month); i++) {
				if (Oalternative) {
					if (c == 'B') {
						len = strlen(tptr->alt_month[i]);
						if (strncasecmp_l(buf,
								tptr->alt_month[i],
								len, locale) == 0)
							break;
					}
				} else {
					len = strlen(tptr->month[i]);
					if (strncasecmp_l(buf, tptr->month[i],
							len, locale) == 0)
						break;
				}
			}
			/*
			 * Try the abbreviated month name if the full name
			 * wasn't found and Oalternative was not requested.
			 */
			if (i == asizeof(tptr->month) && !Oalternative) {
				for (i = 0; i < asizeof(tptr->month); i++) {
					len = strlen(tptr->mon[i]);
					if (strncasecmp_l(buf, tptr->mon[i],
							len, locale) == 0)
						break;
				}
			}
			if (i == asizeof(tptr->month))
				return 0;

			tm->tm_mon = i;
			buf += len;
			break;

		case 'm':
			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit_l((unsigned char)*buf, locale); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i < 1 || i > 12)
				return 0;

			tm->tm_mon = i - 1;

			if (*buf != 0 &&
			    isspace_l((unsigned char)*buf, locale))
				while (*ptr != 0 &&
				       !isspace_l((unsigned char)*ptr, locale))
					ptr++;
			break;

		case 's':
			{
			char *cp;
			int sverrno;
			long n;
			time_t t;

			sverrno = errno;
			errno = 0;
			n = strtol_l(buf, &cp, 10, locale);
			if (errno == ERANGE || (long)(t = n) != n) {
				errno = sverrno;
				return 0;
			}
			errno = sverrno;
			buf = cp;
			gmtime_r(&t, tm);
			*GMTp = 1;
			}
			break;

		case 'Y':
		case 'y':
			if (*buf == 0 ||
			    isspace_l((unsigned char)*buf, locale))
				break;

			if (!isdigit_l((unsigned char)*buf, locale))
				return 0;

			len = (c == 'Y') ? 4 : 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit_l((unsigned char)*buf, locale); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (c == 'Y')
				i -= 1900;
			if (c == 'y' && i < 69)
				i += 100;
			if (i < 0)
				return 0;

			tm->tm_year = i;

			if (*buf != 0 &&
			    isspace_l((unsigned char)*buf, locale))
				while (*ptr != 0 &&
				       !isspace_l((unsigned char)*ptr, locale))
					ptr++;
			break;

		case 'Z':
			{
			const char *cp;
			char *zonestr;

			for (cp = buf; *cp &&
			     isupper_l((unsigned char)*cp, locale); ++cp) {
				/*empty*/}
			if (cp - buf) {
				zonestr = alloca(cp - buf + 1);
				strncpy(zonestr, buf, cp - buf);
				zonestr[cp - buf] = '\0';
				tzset();
				if (0 == strcmp(zonestr, "GMT")) {
				    *GMTp = 1;
				} else if (0 == strcmp(zonestr, tzname[0])) {
				    tm->tm_isdst = 0;
				} else if (0 == strcmp(zonestr, tzname[1])) {
				    tm->tm_isdst = 1;
				} else {
				    return 0;
				}
				buf += cp - buf;
			}
			}
			break;

		case 'z':
			{
			int sign = 1;

			if (*buf != '+') {
				if (*buf == '-')
					sign = -1;
				else
					return 0;
			}

			buf++;
			i = 0;
			for (len = 4; len > 0; len--) {
				if (isdigit_l((unsigned char)*buf, locale)) {
					i *= 10;
					i += *buf - '0';
					buf++;
				} else
					return 0;
			}

			tm->tm_hour -= sign * (i / 100);
			tm->tm_min  -= sign * (i % 100);
			*GMTp = 1;
			}
			break;
		}
	}
	return (char *)buf;
}


char *
strptime_l(const char * __restrict buf, const char * __restrict fmt,
    struct tm * __restrict tm, locale_t loc)
{
	char *ret;
	int gmt;
	FIX_LOCALE(loc);

	gmt = 0;
	ret = _strptime(buf, fmt, tm, &gmt, loc);
	if (ret && gmt) {
		time_t t = timegm(tm);
		localtime_r(&t, tm);
	}

	return (ret);
}
char *
strptime(const char * __restrict buf, const char * __restrict fmt,
    struct tm * __restrict tm)
{
	return strptime_l(buf, fmt, tm, __get_locale());
}
