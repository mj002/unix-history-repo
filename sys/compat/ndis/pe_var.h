/*
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _PE_VAR_H_
#define _PE_VAR_H_

/*
 *  Image Format
 */

#define IMAGE_DOS_SIGNATURE                 0x5A4D      /* MZ */
#define IMAGE_OS2_SIGNATURE                 0x454E      /* NE */
#define IMAGE_OS2_SIGNATURE_LE              0x454C      /* LE */
#define IMAGE_VXD_SIGNATURE                 0x454C      /* LE */
#define IMAGE_NT_SIGNATURE                  0x00004550  /* PE00 */

/*
 * All PE files have one of these, just so if you attempt to
 * run them, they'll print out a message telling you they can
 * only be run in Windows.
 */

struct image_dos_header {
	uint16_t	idh_magic;	/* Magic number */
	uint16_t	idh_cblp;	/* Bytes on last page of file */
	uint16_t	idh_cp;		/* Pages in file */
	uint16_t	idh_crlc;	/* Relocations */
	uint16_t	idh_cparhdr;	/* Size of header in paragraphs */
	uint16_t	idh_minalloc;	/* Minimum extra paragraphs needed */
	uint16_t	idh_maxalloc;	/* Maximum extra paragraphs needed */
	uint16_t	idh_ss;		/* Initial (relative) SS value */
	uint16_t	idh_sp;		/* Initial SP value */
	uint16_t	idh_csum;	/* Checksum */
	uint16_t	idh_ip;		/* Initial IP value */
	uint16_t	idh_cs;		/* Initial (relative) CS value */
	uint16_t	idh_lfarlc;	/* File address of relocation table */
	uint16_t	idh_ovno;	/* Overlay number */
	uint16_t	idh_rsvd1[4];	/* Reserved words */
	uint16_t	idh_oemid;	/* OEM identifier (for idh_oeminfo) */
	uint16_t	idh_oeminfo;	/* OEM information; oemid specific */
	uint16_t	idh_rsvd2[10];	/* Reserved words */
	uint32_t	idh_lfanew;	/* File address of new exe header */
};

typedef struct image_dos_header image_dos_header;

/*
 * File header format.
 */

struct image_file_header {
	uint16_t	ifh_machine;		/* Machine type */
	uint16_t	ifh_numsections;	/* # of sections */
	uint32_t	ifh_timestamp;		/* Date/time stamp */
	uint32_t	ifh_symtblptr;		/* Offset to symbol table */
	uint32_t	ifh_numsyms; 		/* # of symbols */
	uint16_t	ifh_optionalhdrlen;	/* Size of optional header */
	uint16_t	ifh_characteristics;	/* Characteristics */
};

typedef struct image_file_header image_file_header;

/* Machine types */

#define IMAGE_FILE_MACHINE_UNKNOWN      0
#define IMAGE_FILE_MACHINE_I860         0x014d
#define IMAGE_FILE_MACHINE_I386         0x014c
#define IMAGE_FILE_MACHINE_R3000        0x0162
#define IMAGE_FILE_MACHINE_R4000        0x0166
#define IMAGE_FILE_MACHINE_R10000       0x0168
#define IMAGE_FILE_MACHINE_WCEMIPSV2    0x0169
#define IMAGE_FILE_MACHINE_ALPHA        0x0184
#define IMAGE_FILE_MACHINE_SH3          0x01a2
#define IMAGE_FILE_MACHINE_SH3DSP       0x01a3
#define IMAGE_FILE_MACHINE_SH3E         0x01a4
#define IMAGE_FILE_MACHINE_SH4          0x01a6
#define IMAGE_FILE_MACHINE_SH5          0x01a8
#define IMAGE_FILE_MACHINE_ARM          0x01c0
#define IMAGE_FILE_MACHINE_THUMB        0x01c2
#define IMAGE_FILE_MACHINE_AM33         0x01d3
#define IMAGE_FILE_MACHINE_POWERPC      0x01f0
#define IMAGE_FILE_MACHINE_POWERPCFP    0x01f1
#define IMAGE_FILE_MACHINE_IA64         0x0200
#define IMAGE_FILE_MACHINE_MIPS16       0x0266
#define IMAGE_FILE_MACHINE_ALPHA64      0x0284
#define IMAGE_FILE_MACHINE_MIPSFPU      0x0366
#define IMAGE_FILE_MACHINE_MIPSFPU16    0x0466
#define IMAGE_FILE_MACHINE_AXP64        IMAGE_FILE_MACHINE_ALPHA64
#define IMAGE_FILE_MACHINE_TRICORE      0x0520
#define IMAGE_FILE_MACHINE_CEF          0x0cef
#define IMAGE_FILE_MACHINE_EBC          0x0ebc
#define IMAGE_FILE_MACHINE_AMD64        0x8664
#define IMAGE_FILE_MACHINE_M32R         0x9041
#define IMAGE_FILE_MACHINE_CEE          0xc0ee

/* Characteristics */

#define IMAGE_FILE_RELOCS_STRIPPED      0x0001 /* No relocation info */
#define IMAGE_FILE_EXECUTABLE_IMAGE     0x0002
#define IMAGE_FILE_LINE_NUMS_STRIPPED   0x0004
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED  0x0008
#define IMAGE_FILE_AGGRESIVE_WS_TRIM    0x0010
#define IMAGE_FILE_LARGE_ADDRESS_AWARE  0x0020
#define IMAGE_FILE_16BIT_MACHINE        0x0040
#define IMAGE_FILE_BYTES_REVERSED_LO    0x0080
#define IMAGE_FILE_32BIT_MACHINE        0x0100
#define IMAGE_FILE_DEBUG_STRIPPED       0x0200
#define IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP      0x0400
#define IMAGE_FILE_NET_RUN_FROM_SWAP    0x0800
#define IMAGE_FILE_SYSTEM               0x1000
#define IMAGE_FILE_DLL                  0x2000
#define IMAGE_FILE_UP_SYSTEM_ONLY       0x4000
#define IMAGE_FILE_BYTES_REVERSED_HI    0x8000

#define IMAGE_SIZEOF_FILE_HEADER             20

/*
 * Directory format.
 */

struct image_data_directory {
	uint32_t		idd_vaddr;	/* virtual address */
	uint32_t		idd_size;	/* size */
};

typedef struct image_data_directory image_data_directory;

#define IMAGE_DIRECTORY_ENTRIES_MAX    16

/*
 * Optional header format.
 */

struct image_optional_header {

	/* Standard fields */

	uint16_t	ioh_magic;
	uint8_t		ioh_linkerver_major;
	uint8_t		ioh_linkerver_minor;
	uint32_t	ioh_codesize;
	uint32_t	ioh_datasize;
	uint32_t	ioh_bsssize;
	uint32_t	ioh_entryaddr;
	uint32_t	ioh_codebaseaddr;
	uint32_t	ioh_databaseaddr;

	/* NT-specific fields */

	uint32_t	ioh_imagebase;
	uint32_t	ioh_sectalign;
	uint32_t	ioh_filealign;
	uint16_t	ioh_osver_major;
	uint16_t	ioh_osver_minor;
	uint16_t	ioh_imagever_major;
	uint16_t	ioh_imagever_minor;
	uint16_t	ioh_subsys_major;
	uint16_t	ioh_subsys_minor;
	uint32_t	ioh_win32ver;
	uint32_t	ioh_imagesize;
	uint32_t	ioh_headersize;
	uint32_t	ioh_csum;
	uint16_t	ioh_subsys;
	uint16_t	ioh_dll_characteristics;
	uint32_t	ioh_stackreservesize;
	uint32_t	ioh_stackcommitsize;
	uint32_t	ioh_heapreservesize;
	uint32_t	ioh_heapcommitsize;
	uint16_t	ioh_loaderflags;
	uint32_t	ioh_rva_size_cnt;
	image_data_directory	ioh_datadir[IMAGE_DIRECTORY_ENTRIES_MAX];
};

typedef struct image_optional_header image_optional_header;

struct image_nt_header {
	uint32_t		inh_signature;
	image_file_header	inh_filehdr;
	image_optional_header	inh_optionalhdr;
};

typedef struct image_nt_header image_nt_header;

/* Directory Entries */

#define IMAGE_DIRECTORY_ENTRY_EXPORT         0   /* Export Directory */
#define IMAGE_DIRECTORY_ENTRY_IMPORT         1   /* Import Directory */
#define IMAGE_DIRECTORY_ENTRY_RESOURCE       2   /* Resource Directory */
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION      3   /* Exception Directory */
#define IMAGE_DIRECTORY_ENTRY_SECURITY       4   /* Security Directory */
#define IMAGE_DIRECTORY_ENTRY_BASERELOC      5   /* Base Relocation Table */
#define IMAGE_DIRECTORY_ENTRY_DEBUG          6   /* Debug Directory */
#define IMAGE_DIRECTORY_ENTRY_COPYRIGHT      7   /* Description String */
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR      8   /* Machine Value (MIPS GP) */
#define IMAGE_DIRECTORY_ENTRY_TLS            9   /* TLS Directory */
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG   10   /* Load Configuration Directory */
#define IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT  11   /* Bound Import Directory in headers */
#define IMAGE_DIRECTORY_ENTRY_IAT           12   /* Import Address Table */
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT      13
#define IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR    14

/*
 * Section header format.
 */

#define IMAGE_SHORT_NAME_LEN			8

struct image_section_header {
	uint8_t		ish_name[IMAGE_SHORT_NAME_LEN];
	union {
		uint32_t	ish_paddr;
		uint32_t	ish_vsize;
	} ish_misc;
	uint32_t	ish_vaddr;
	uint32_t	ish_rawdatasize;
	uint32_t	ish_rawdataaddr;
	uint32_t	ish_relocaddr;
	uint32_t	ish_linenumaddr;
	uint16_t	ish_numrelocs;
	uint16_t	ish_numlinenums;
	uint32_t	ish_characteristics;
};

typedef struct image_section_header image_section_header;

#define IMAGE_SIZEOF_SECTION_HEADER          40

/*
 * Import format
 */

struct image_import_by_name {
	uint16_t	iibn_hint;
	u_int8_t	iibn_name[1];
};

#define IMAGE_ORDINAL_FLAG 0x80000000
#define IMAGE_ORDINAL(Ordinal) (Ordinal & 0xffff)

struct image_import_descriptor {
	uint32_t	iid_import_name_table_addr;
	uint32_t	iid_timestamp;
	uint32_t	iid_forwardchain;
	uint32_t	iid_nameaddr;
	uint32_t	iid_import_address_table_addr;
};

typedef struct image_import_descriptor image_import_descriptor;

struct image_base_reloc {
	uint32_t	ibr_vaddr;
	uint32_t	ibr_blocksize;
	uint16_t	ibr_rel[1];
};

typedef struct image_base_reloc image_base_reloc;

#define IMR_RELTYPE(x)		((x >> 12) & 0xF)
#define IMR_RELOFFSET(x)	(x & 0xFFF)

/* generic relocation types */
#define IMAGE_REL_BASED_ABSOLUTE                0
#define IMAGE_REL_BASED_HIGH                    1
#define IMAGE_REL_BASED_LOW                     2
#define IMAGE_REL_BASED_HIGHLOW                 3
#define IMAGE_REL_BASED_HIGHADJ                 4
#define IMAGE_REL_BASED_MIPS_JMPADDR            5
#define IMAGE_REL_BASED_SECTION                 6
#define IMAGE_REL_BASED_REL                     7
#define IMAGE_REL_BASED_MIPS_JMPADDR16          9
#define IMAGE_REL_BASED_IA64_IMM64              9 /* yes, 9 too */
#define IMAGE_REL_BASED_DIR64                   10
#define IMAGE_REL_BASED_HIGH3ADJ                11


struct image_patch_table {
	char		*ipt_name;
	void		(*ipt_func)(void);
};

typedef struct image_patch_table image_patch_table;

__BEGIN_DECLS
extern int pe_get_dos_header(vm_offset_t, image_dos_header *);
extern int pe_is_nt_image(vm_offset_t);
extern int pe_get_optional_header(vm_offset_t, image_optional_header *);
extern int pe_get_file_header(vm_offset_t, image_file_header *);
extern int pe_get_section_header(vm_offset_t, image_section_header *);
extern int pe_numsections(vm_offset_t);
extern vm_offset_t pe_imagebase(vm_offset_t);
extern vm_offset_t pe_directory_offset(vm_offset_t, uint32_t);
extern vm_offset_t pe_translate_addr (vm_offset_t, uint32_t);
extern int pe_get_section(vm_offset_t, image_section_header *, const char *);
extern int pe_relocate(vm_offset_t);
extern int pe_get_import_descriptor(vm_offset_t, image_import_descriptor *, char *);
extern int pe_patch_imports(vm_offset_t, char *, image_patch_table *);
__END_DECLS

#endif /* _PE_VAR_H_ */
