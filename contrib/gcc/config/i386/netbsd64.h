/* Definitions of target machine for GNU compiler,
   for x86-64/ELF NetBSD systems.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Contributed by Wasabi Systems, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      NETBSD_OS_CPP_BUILTINS_ELF();		\
      if (TARGET_64BIT)				\
	NETBSD_OS_CPP_BUILTINS_LP64();		\
    }						\
  while (0)


/* Extra specs needed for NetBSD/x86-64 ELF.  */

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS			\
  { "netbsd_cpp_spec", NETBSD_CPP_SPEC },	\
  { "netbsd_link_spec", NETBSD_LINK_SPEC_ELF },	\
  { "netbsd_entry_point", NETBSD_ENTRY_POINT },


/* Provide a LINK_SPEC appropriate for a NetBSD/x86-64 ELF target.  */

#undef LINK_SPEC
#define LINK_SPEC \
  "%{m32:-m elf_i386} \
   %{m64:-m elf_x86_64} \
   %(netbsd_link_spec)"

#define NETBSD_ENTRY_POINT "_start"


/* Provide a CPP_SPEC appropriate for NetBSD.  */

#undef CPP_SPEC
#define CPP_SPEC "%(netbsd_cpp_spec)"


/* Output assembler code to FILE to call the profiler.  */

#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE, LABELNO)				\
{									\
  if (TARGET_64BIT && flag_pic)						\
    fprintf (FILE, "\tcall *__mcount@PLT\n");				\
  else if (flag_pic)							\
    fprintf (FILE, "\tcall *__mcount@PLT\n");				\
  else									\
    fprintf (FILE, "\tcall __mcount\n");				\
}

/* Attempt to enable execute permissions on the stack.  */
#define TRANSFER_FROM_TRAMPOLINE NETBSD_ENABLE_EXECUTE_STACK

#define TARGET_VERSION fprintf (stderr, " (NetBSD/x86_64 ELF)");
