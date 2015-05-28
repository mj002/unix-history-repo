/*-
 * Copyright (c) 2010 George V. Neville-Neil <gnn@freebsd.org>
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
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/pmc_mdep.h>

#define	MIPS24K_PMC_CAPS	(PMC_CAP_INTERRUPT | PMC_CAP_USER |     \
				 PMC_CAP_SYSTEM | PMC_CAP_EDGE |	\
				 PMC_CAP_THRESHOLD | PMC_CAP_READ |	\
				 PMC_CAP_WRITE | PMC_CAP_INVERT |	\
				 PMC_CAP_QUALIFIER)

#define MIPS24K_PMC_INTERRUPT_ENABLE      0x10 /* Enable interrupts */
#define MIPS24K_PMC_USER_ENABLE           0x08 /* Count in USER mode */
#define MIPS24K_PMC_SUPER_ENABLE          0x04 /* Count in SUPERVISOR mode */
#define MIPS24K_PMC_KERNEL_ENABLE         0x02 /* Count in KERNEL mode */
#define MIPS24K_PMC_ENABLE (MIPS24K_PMC_USER_ENABLE |	   \
			    MIPS24K_PMC_SUPER_ENABLE |	   \
			    MIPS24K_PMC_KERNEL_ENABLE)

#define MIPS24K_PMC_SELECT 5 /* Which bit position the event starts at. */

const struct mips_event_code_map mips_event_codes[] = {
	{ PMC_EV_MIPS24K_CYCLE, MIPS_CTR_ALL, 0},
	{ PMC_EV_MIPS24K_INSTR_EXECUTED, MIPS_CTR_ALL, 1},
	{ PMC_EV_MIPS24K_BRANCH_COMPLETED, MIPS_CTR_0, 2},
	{ PMC_EV_MIPS24K_BRANCH_MISPRED, MIPS_CTR_1, 2},
	{ PMC_EV_MIPS24K_RETURN, MIPS_CTR_0, 3},
	{ PMC_EV_MIPS24K_RETURN_MISPRED, MIPS_CTR_1, 3},
	{ PMC_EV_MIPS24K_RETURN_NOT_31, MIPS_CTR_0, 4},
	{ PMC_EV_MIPS24K_RETURN_NOTPRED, MIPS_CTR_1, 4},
	{ PMC_EV_MIPS24K_ITLB_ACCESS, MIPS_CTR_0, 5},
	{ PMC_EV_MIPS24K_ITLB_MISS, MIPS_CTR_1, 5},
	{ PMC_EV_MIPS24K_DTLB_ACCESS, MIPS_CTR_0, 6},
	{ PMC_EV_MIPS24K_DTLB_MISS, MIPS_CTR_1, 6},
	{ PMC_EV_MIPS24K_JTLB_IACCESS, MIPS_CTR_0, 7},
	{ PMC_EV_MIPS24K_JTLB_IMISS, MIPS_CTR_1, 7},
	{ PMC_EV_MIPS24K_JTLB_DACCESS, MIPS_CTR_0, 8},
	{ PMC_EV_MIPS24K_JTLB_DMISS, MIPS_CTR_1, 8},
	{ PMC_EV_MIPS24K_IC_FETCH, MIPS_CTR_0, 9},
	{ PMC_EV_MIPS24K_IC_MISS, MIPS_CTR_1, 9},
	{ PMC_EV_MIPS24K_DC_LOADSTORE, MIPS_CTR_0, 10},
	{ PMC_EV_MIPS24K_DC_WRITEBACK, MIPS_CTR_1, 10},
	{ PMC_EV_MIPS24K_DC_MISS, MIPS_CTR_ALL, 11},
	/* 12 reserved */
	{ PMC_EV_MIPS24K_STORE_MISS, MIPS_CTR_0, 13},
	{ PMC_EV_MIPS24K_LOAD_MISS, MIPS_CTR_1, 13},
	{ PMC_EV_MIPS24K_INTEGER_COMPLETED, MIPS_CTR_0, 14},
	{ PMC_EV_MIPS24K_FP_COMPLETED, MIPS_CTR_1, 14},
	{ PMC_EV_MIPS24K_LOAD_COMPLETED, MIPS_CTR_0, 15},
	{ PMC_EV_MIPS24K_STORE_COMPLETED, MIPS_CTR_1, 15},
	{ PMC_EV_MIPS24K_BARRIER_COMPLETED, MIPS_CTR_0, 16},
	{ PMC_EV_MIPS24K_MIPS16_COMPLETED, MIPS_CTR_1, 16},
	{ PMC_EV_MIPS24K_NOP_COMPLETED, MIPS_CTR_0, 17},
	{ PMC_EV_MIPS24K_INTEGER_MULDIV_COMPLETED, MIPS_CTR_1, 17},
	{ PMC_EV_MIPS24K_RF_STALL, MIPS_CTR_0, 18},
	{ PMC_EV_MIPS24K_INSTR_REFETCH, MIPS_CTR_1, 18},
	{ PMC_EV_MIPS24K_STORE_COND_COMPLETED, MIPS_CTR_0, 19},
	{ PMC_EV_MIPS24K_STORE_COND_FAILED, MIPS_CTR_1, 19},
	{ PMC_EV_MIPS24K_ICACHE_REQUESTS, MIPS_CTR_0, 20},
	{ PMC_EV_MIPS24K_ICACHE_HIT, MIPS_CTR_1, 20},
	{ PMC_EV_MIPS24K_L2_WRITEBACK, MIPS_CTR_0, 21},
	{ PMC_EV_MIPS24K_L2_ACCESS, MIPS_CTR_1, 21},
	{ PMC_EV_MIPS24K_L2_MISS, MIPS_CTR_0, 22},
	{ PMC_EV_MIPS24K_L2_ERR_CORRECTED, MIPS_CTR_1, 22},
	{ PMC_EV_MIPS24K_EXCEPTIONS, MIPS_CTR_0, 23},
	/* Event 23 on COP0 1/3 is undefined */
	{ PMC_EV_MIPS24K_RF_CYCLES_STALLED, MIPS_CTR_0, 24},
	{ PMC_EV_MIPS24K_IFU_CYCLES_STALLED, MIPS_CTR_0, 25},
	{ PMC_EV_MIPS24K_ALU_CYCLES_STALLED, MIPS_CTR_1, 25},
	/* Events 26 through 32 undefined or reserved to customers */
	{ PMC_EV_MIPS24K_UNCACHED_LOAD, MIPS_CTR_0, 33},
	{ PMC_EV_MIPS24K_UNCACHED_STORE, MIPS_CTR_1, 33},
	{ PMC_EV_MIPS24K_CP2_REG_TO_REG_COMPLETED, MIPS_CTR_0, 35},
	{ PMC_EV_MIPS24K_MFTC_COMPLETED, MIPS_CTR_1, 35},
	/* Event 36 reserved */
	{ PMC_EV_MIPS24K_IC_BLOCKED_CYCLES, MIPS_CTR_0, 37},
	{ PMC_EV_MIPS24K_DC_BLOCKED_CYCLES, MIPS_CTR_1, 37},
	{ PMC_EV_MIPS24K_L2_IMISS_STALL_CYCLES, MIPS_CTR_0, 38},
	{ PMC_EV_MIPS24K_L2_DMISS_STALL_CYCLES, MIPS_CTR_1, 38},
	{ PMC_EV_MIPS24K_DMISS_CYCLES, MIPS_CTR_0, 39},
	{ PMC_EV_MIPS24K_L2_MISS_CYCLES, MIPS_CTR_1, 39},
	{ PMC_EV_MIPS24K_UNCACHED_BLOCK_CYCLES, MIPS_CTR_0, 40},
	{ PMC_EV_MIPS24K_MDU_STALL_CYCLES, MIPS_CTR_0, 41},
	{ PMC_EV_MIPS24K_FPU_STALL_CYCLES, MIPS_CTR_1, 41},
	{ PMC_EV_MIPS24K_CP2_STALL_CYCLES, MIPS_CTR_0, 42},
	{ PMC_EV_MIPS24K_COREXTEND_STALL_CYCLES, MIPS_CTR_1, 42},
	{ PMC_EV_MIPS24K_ISPRAM_STALL_CYCLES, MIPS_CTR_0, 43},
	{ PMC_EV_MIPS24K_DSPRAM_STALL_CYCLES, MIPS_CTR_1, 43},
	{ PMC_EV_MIPS24K_CACHE_STALL_CYCLES, MIPS_CTR_0, 44},
	/* Event 44 undefined on 1/3 */
	{ PMC_EV_MIPS24K_LOAD_TO_USE_STALLS, MIPS_CTR_0, 45},
	{ PMC_EV_MIPS24K_BASE_MISPRED_STALLS, MIPS_CTR_1, 45},
	{ PMC_EV_MIPS24K_CPO_READ_STALLS, MIPS_CTR_0, 46},
	{ PMC_EV_MIPS24K_BRANCH_MISPRED_CYCLES, MIPS_CTR_1, 46},
	/* Event 47 reserved */
	{ PMC_EV_MIPS24K_IFETCH_BUFFER_FULL, MIPS_CTR_0, 48},
	{ PMC_EV_MIPS24K_FETCH_BUFFER_ALLOCATED, MIPS_CTR_1, 48},
	{ PMC_EV_MIPS24K_EJTAG_ITRIGGER, MIPS_CTR_0, 49},
	{ PMC_EV_MIPS24K_EJTAG_DTRIGGER, MIPS_CTR_1, 49},
	{ PMC_EV_MIPS24K_FSB_LT_QUARTER, MIPS_CTR_0, 50},
	{ PMC_EV_MIPS24K_FSB_QUARTER_TO_HALF, MIPS_CTR_1, 50},
	{ PMC_EV_MIPS24K_FSB_GT_HALF, MIPS_CTR_0, 51},
	{ PMC_EV_MIPS24K_FSB_FULL_PIPELINE_STALLS, MIPS_CTR_1, 51},
	{ PMC_EV_MIPS24K_LDQ_LT_QUARTER, MIPS_CTR_0, 52},
	{ PMC_EV_MIPS24K_LDQ_QUARTER_TO_HALF, MIPS_CTR_1, 52},
	{ PMC_EV_MIPS24K_LDQ_GT_HALF, MIPS_CTR_0, 53},
	{ PMC_EV_MIPS24K_LDQ_FULL_PIPELINE_STALLS, MIPS_CTR_1, 53},
	{ PMC_EV_MIPS24K_WBB_LT_QUARTER, MIPS_CTR_0, 54},
	{ PMC_EV_MIPS24K_WBB_QUARTER_TO_HALF, MIPS_CTR_1, 54},
	{ PMC_EV_MIPS24K_WBB_GT_HALF, MIPS_CTR_0, 55},
	{ PMC_EV_MIPS24K_WBB_FULL_PIPELINE_STALLS, MIPS_CTR_1, 55},
	/* Events 56-63 reserved */
	{ PMC_EV_MIPS24K_REQUEST_LATENCY, MIPS_CTR_0, 61},
	{ PMC_EV_MIPS24K_REQUEST_COUNT, MIPS_CTR_1, 61}

};

const int mips_event_codes_size =
	sizeof(mips_event_codes) / sizeof(mips_event_codes[0]);

struct mips_pmc_spec mips_pmc_spec = {
	.ps_cpuclass = PMC_CLASS_MIPS24K,
	.ps_cputype = PMC_CPU_MIPS_24K,
	.ps_capabilities = MIPS24K_PMC_CAPS,
	.ps_counter_width = 32
};

/*
 * Performance Count Register N
 */
uint64_t
mips_pmcn_read(unsigned int pmc)
{
	uint32_t reg = 0;

	KASSERT(pmc < mips_npmcs, ("[mips24k,%d] illegal PMC number %d",
				   __LINE__, pmc));

	/* The counter value is the next value after the control register. */
	switch (pmc) {
	case 0:
		reg = mips_rd_perfcnt1();
		break;
	case 1:
		reg = mips_rd_perfcnt3();
		break;
	default:
		return 0;
	}
	return (reg);
}

uint64_t
mips_pmcn_write(unsigned int pmc, uint64_t reg)
{

	KASSERT(pmc < mips_npmcs, ("[mips24k,%d] illegal PMC number %d",
				   __LINE__, pmc));

	switch (pmc) {
	case 0:
		mips_wr_perfcnt1(reg);
		break;
	case 1:
		mips_wr_perfcnt3(reg);
		break;
	default:
		return 0;
	}
	return (reg);
}

uint32_t
mips_get_perfctl(int cpu, int ri, uint32_t event, uint32_t caps)
{
	uint32_t config;

	config = event;

	config <<= MIPS24K_PMC_SELECT;

	if (caps & PMC_CAP_SYSTEM)
		config |= (MIPS24K_PMC_SUPER_ENABLE |
			   MIPS24K_PMC_KERNEL_ENABLE);
	if (caps & PMC_CAP_USER)
		config |= MIPS24K_PMC_USER_ENABLE;
	if ((caps & (PMC_CAP_USER | PMC_CAP_SYSTEM)) == 0)
		config |= MIPS24K_PMC_ENABLE;
	if (caps & PMC_CAP_INTERRUPT)
		config |= MIPS24K_PMC_INTERRUPT_ENABLE;

	PMCDBG2(MDP,ALL,2,"mips24k-get_perfctl ri=%d -> config=0x%x", ri, config);

	return (config);
}
