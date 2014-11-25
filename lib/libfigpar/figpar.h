/*-
 * Copyright (c) 2002-2014 Devin Teske <dteske@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _FIGPAR_H_
#define _FIGPAR_H_

#include <sys/types.h>

/*
 * Union for storing various types of data in a single common container.
 */
union fp_cfgvalue {
	void		*data;		/* Pointer to NUL-terminated string */
	char		*str;		/* Pointer to NUL-terminated string */
	char		**strarray;	/* Pointer to an array of strings */
	int32_t		num;		/* Signed 32-bit integer value */
	uint32_t	u_num;		/* Unsigned 32-bit integer value */ 
	uint32_t	boolean:1;	/* Boolean integer value (0 or 1) */
};

/*
 * Option types (based on above cfgvalue union)
 */
enum fp_cfgtype {
	FP_TYPE_NONE		= 0x0000, /* for directives with no value */
	FP_TYPE_BOOL		= 0x0001, /* boolean */
	FP_TYPE_INT		= 0x0002, /* signed 32 bit integer */
	FP_TYPE_UINT		= 0x0004, /* unsigned 32 bit integer */
	FP_TYPE_STR		= 0x0008, /* string pointer */
	FP_TYPE_STRARRAY	= 0x0010, /* string array pointer */
	FP_TYPE_DATA1		= 0x0020, /* void data type-1 (whatever) */
	FP_TYPE_DATA2		= 0x0040, /* void data type-2 (whatever) */
	FP_TYPE_DATA3		= 0x0080, /* void data type-3 (whatever) */
	FP_TYPE_RESERVED1	= 0x0100, /* reserved data type-1 (future) */
	FP_TYPE_RESERVED2	= 0x0200, /* reserved data type-2 (future) */
	FP_TYPE_RESERVED3	= 0x0400, /* reserved data type-3 (future) */
};

/*
 * Options to parse_config() for processing_options bitmask
 */
#define FP_BREAK_ON_EQUALS	0x0001	/* stop reading directive at `=' */
#define FP_BREAK_ON_SEMICOLON	0x0002	/* `;' starts a new line */
#define FP_CASE_SENSITIVE	0x0004	/* directives are case sensitive */
#define FP_REQUIRE_EQUALS	0x0008	/* assignment directives only */
#define FP_STRICT_EQUALS	0x0010	/* `=' must be part of directive */

/*
 * Anatomy of a config file option
 */
struct fp_config {
	enum fp_cfgtype		type;		/* Option value type */
	const char		*directive;	/* config file keyword */
	union fp_cfgvalue	value;		/* NB: set by action */

	/*
	 * Function pointer; action to be taken when the directive is found
	 */
	int (*action)(struct fp_config *option, uint32_t line, char *directive,
	    char *value);
};
extern struct fp_config fp_dummy_config;

__BEGIN_DECLS
int			 parse_config(struct fp_config _options[],
			    const char *_path,
			    int (*_unknown)(struct fp_config *_option,
			    uint32_t _line, char *_directive, char *_value),
			    uint16_t _processing_options);
struct fp_config	*get_config_option(struct fp_config _options[],
			    const char *_directive);
__END_DECLS

#endif /* _FIGPAR_H_ */
