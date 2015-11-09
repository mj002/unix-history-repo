/*-
 * Copyright (c) 2015 Andrew Turner
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/pcpu.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/intr.h>
#include <machine/smp.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_cpu.h>
#include <dev/psci/psci.h>

static int running_cpus;

int
platform_mp_probe(void)
{

	return (mp_ncpus > 1);
}

static boolean_t
virt_maxid(u_int id, phandle_t node, u_int addr_cells, pcell_t *reg)
{

	if (mp_maxid < id)
		mp_maxid = id;

	return (true);
}

void
platform_mp_setmaxid(void)
{

	mp_maxid = PCPU_GET(cpuid);
	mp_ncpus = ofw_cpu_early_foreach(virt_maxid, true);
	if (mp_ncpus < 1)
		mp_ncpus = 1;
	mp_ncpus = MIN(mp_ncpus, MAXCPU);
}

static boolean_t
virt_start_ap(u_int id, phandle_t node, u_int addr_cells, pcell_t *reg)
{
	int err;

	if (running_cpus >= mp_ncpus)
		return (false);
	running_cpus++;

	err = psci_cpu_on(*reg, pmap_kextract((vm_offset_t)mpentry), id);

	if (err != PSCI_RETVAL_SUCCESS)
		return (false);

	return (true);
}

void
platform_mp_start_ap(void)
{

	ofw_cpu_early_foreach(virt_start_ap, true);
}

void
platform_mp_init_secondary(void)
{

	arm_pic_init_secondary();
}

void
platform_ipi_send(cpuset_t cpus, u_int ipi)
{

	pic_ipi_send(cpus, ipi);
}
