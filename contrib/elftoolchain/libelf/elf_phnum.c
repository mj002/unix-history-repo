/*-
 * Copyright (c) 2006,2008 Joseph Koshy
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
 */

#include <ar.h>
#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_phnum.c 3174 2015-03-27 17:13:41Z emaste $");

static int
_libelf_getphdrnum(Elf *e, size_t *phnum)
{
	void *eh;
	int ec;

	if (e == NULL || e->e_kind != ELF_K_ELF ||
	    ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (-1);
	}

	if ((eh = _libelf_ehdr(e, ec, 0)) == NULL)
		return (-1);

	*phnum = e->e_u.e_elf.e_nphdr;

	return (0);
}

int
elf_getphdrnum(Elf *e, size_t *phnum)
{
	return (_libelf_getphdrnum(e, phnum));
}

/* Deprecated API */
int
elf_getphnum(Elf *e, size_t *phnum)
{
	return (_libelf_getphdrnum(e, phnum) >= 0);
}
