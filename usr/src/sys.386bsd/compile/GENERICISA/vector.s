/*
 * AT/386
 * Interrupt vector routines
 * Generated by config program
 */ 

#include	"i386/isa/isa.h"
#include	"i386/isa/icu.h"

#define	VEC(name)	.align 4; .globl _V/**/name; _V/**/name:

	.globl	_hardclock
VEC(clk)
	INTR(0, _highmask, 0)
	call	_hardclock 
	INTREXIT1


	.globl _wdintr, _wd0mask
	.data
_wd0mask:	.long 0
	.text
VEC(wd0)
	INTR(0, _biomask, 1)
	call	_wdintr
	INTREXIT2


	.globl _fdintr, _fd0mask
	.data
_fd0mask:	.long 0
	.text
VEC(fd0)
	INTR(0, _biomask, 2)
	call	_fdintr
	INTREXIT1


	.globl _asintr, _as0mask
	.data
_as0mask:	.long 0
	.text
VEC(as0)
	INTR(0, _biomask, 3)
	call	_asintr
	INTREXIT2


	.globl _pcrint, _pc0mask
	.data
_pc0mask:	.long 0
	.text
VEC(pc0)
	INTR(0, _ttymask, 4)
	call	_pcrint
	INTREXIT1


	.globl _npxintr, _npx0mask
	.data
_npx0mask:	.long 0
	.text
VEC(npx0)
	INTR(0, _npx0mask, 5)
	call	_npxintr
	INTREXIT2


	.globl _comintr, _com1mask
	.data
_com1mask:	.long 0
	.text
VEC(com1)
	INTR(1, _ttymask, 6)
	call	_comintr
	INTREXIT1


	.globl _comintr, _com2mask
	.data
_com2mask:	.long 0
	.text
VEC(com2)
	INTR(2, _ttymask, 7)
	call	_comintr
	INTREXIT1


	.globl _weintr, _we0mask
	.data
_we0mask:	.long 0
	.text
VEC(we0)
	INTR(0, _netmask, 8)
	call	_weintr
	INTREXIT1


	.globl _neintr, _ne0mask
	.data
_ne0mask:	.long 0
	.text
VEC(ne0)
	INTR(0, _netmask, 9)
	call	_neintr
	INTREXIT1


	.globl _ecintr, _ec0mask
	.data
_ec0mask:	.long 0
	.text
VEC(ec0)
	INTR(0, _netmask, 10)
	call	_ecintr
	INTREXIT1


	.globl _isintr, _is0mask
	.data
_is0mask:	.long 0
	.text
VEC(is0)
	INTR(0, _netmask, 11)
	call	_isintr
	INTREXIT2


	.globl _wtintr, _wt0mask
	.data
_wt0mask:	.long 0
	.text
VEC(wt0)
	INTR(0, _biomask, 12)
	call	_wtintr
	INTREXIT1


