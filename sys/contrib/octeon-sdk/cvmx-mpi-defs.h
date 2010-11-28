/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Networks (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Networks nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * cvmx-mpi-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon mpi.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_MPI_TYPEDEFS_H__
#define __CVMX_MPI_TYPEDEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_MPI_CFG CVMX_MPI_CFG_FUNC()
static inline uint64_t CVMX_MPI_CFG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN50XX)))
		cvmx_warn("CVMX_MPI_CFG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070000001000ull);
}
#else
#define CVMX_MPI_CFG (CVMX_ADD_IO_SEG(0x0001070000001000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_MPI_DATX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 8))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 8))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 8)))))
		cvmx_warn("CVMX_MPI_DATX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x0001070000001080ull) + ((offset) & 15) * 8;
}
#else
#define CVMX_MPI_DATX(offset) (CVMX_ADD_IO_SEG(0x0001070000001080ull) + ((offset) & 15) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_MPI_STS CVMX_MPI_STS_FUNC()
static inline uint64_t CVMX_MPI_STS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN50XX)))
		cvmx_warn("CVMX_MPI_STS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070000001008ull);
}
#else
#define CVMX_MPI_STS (CVMX_ADD_IO_SEG(0x0001070000001008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_MPI_TX CVMX_MPI_TX_FUNC()
static inline uint64_t CVMX_MPI_TX_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN50XX)))
		cvmx_warn("CVMX_MPI_TX not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070000001010ull);
}
#else
#define CVMX_MPI_TX (CVMX_ADD_IO_SEG(0x0001070000001010ull))
#endif

/**
 * cvmx_mpi_cfg
 */
union cvmx_mpi_cfg
{
	uint64_t u64;
	struct cvmx_mpi_cfg_s
	{
#if __BYTE_ORDER == __BIG_ENDIAN
	uint64_t reserved_29_63               : 35;
	uint64_t clkdiv                       : 13; /**< Fsclk = Feclk / (2 * CLKDIV)
                                                         CLKDIV = Feclk / (2 * Fsclk) */
	uint64_t reserved_12_15               : 4;
	uint64_t cslate                       : 1;  /**< If 0, MPI_CS asserts 1/2 SCLK before transaction
                                                            1, MPI_CS assert coincident with transaction
                                                         NOTE: only used if CSENA == 1 */
	uint64_t tritx                        : 1;  /**< If 0, MPI_TX pin is driven when slave is not
                                                               expected to be driving
                                                            1, MPI_TX pin is tristated when not transmitting
                                                         NOTE: only used when WIREOR==1 */
	uint64_t idleclks                     : 2;  /**< Guarantee IDLECLKS idle sclk cycles between
                                                         commands. */
	uint64_t cshi                         : 1;  /**< If 0, CS is low asserted
                                                         1, CS is high asserted */
	uint64_t csena                        : 1;  /**< If 0, the MPI_CS is a GPIO, not used by MPI_TX
                                                         1, CS is driven per MPI_TX intruction */
	uint64_t int_ena                      : 1;  /**< If 0, polling is required
                                                         1, MPI engine interrupts X end of transaction */
	uint64_t lsbfirst                     : 1;  /**< If 0, shift MSB first
                                                         1, shift LSB first */
	uint64_t wireor                       : 1;  /**< If 0, MPI_TX and MPI_RX are separate wires (SPI)
                                                               MPI_TX pin is always driven
                                                            1, MPI_TX/RX is all from MPI_TX pin (MPI)
                                                               MPI_TX pin is tristated when not transmitting
                                                         NOTE: if WIREOR==1, MPI_RX pin is not used by the
                                                               MPI engine */
	uint64_t clk_cont                     : 1;  /**< If 0, clock idles to value given by IDLELO after
                                                            completion of MPI transaction
                                                         1, clock never idles, requires CS deassertion
                                                            assertion between commands */
	uint64_t idlelo                       : 1;  /**< If 0, MPI_CLK idles high, 1st transition is hi->lo
                                                         1, MPI_CLK idles low, 1st transition is lo->hi */
	uint64_t enable                       : 1;  /**< If 0, all MPI pins are GPIOs
                                                         1, MPI_CLK, MPI_CS, and MPI_TX are driven */
#else
	uint64_t enable                       : 1;
	uint64_t idlelo                       : 1;
	uint64_t clk_cont                     : 1;
	uint64_t wireor                       : 1;
	uint64_t lsbfirst                     : 1;
	uint64_t int_ena                      : 1;
	uint64_t csena                        : 1;
	uint64_t cshi                         : 1;
	uint64_t idleclks                     : 2;
	uint64_t tritx                        : 1;
	uint64_t cslate                       : 1;
	uint64_t reserved_12_15               : 4;
	uint64_t clkdiv                       : 13;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_mpi_cfg_s                 cn30xx;
	struct cvmx_mpi_cfg_cn31xx
	{
#if __BYTE_ORDER == __BIG_ENDIAN
	uint64_t reserved_29_63               : 35;
	uint64_t clkdiv                       : 13; /**< Fsclk = Feclk / (2 * CLKDIV)
                                                         CLKDIV = Feclk / (2 * Fsclk) */
	uint64_t reserved_11_15               : 5;
	uint64_t tritx                        : 1;  /**< If 0, MPI_TX pin is driven when slave is not
                                                               expected to be driving
                                                            1, MPI_TX pin is tristated when not transmitting
                                                         NOTE: only used when WIREOR==1 */
	uint64_t idleclks                     : 2;  /**< Guarantee IDLECLKS idle sclk cycles between
                                                         commands. */
	uint64_t cshi                         : 1;  /**< If 0, CS is low asserted
                                                         1, CS is high asserted */
	uint64_t csena                        : 1;  /**< If 0, the MPI_CS is a GPIO, not used by MPI_TX
                                                         1, CS is driven per MPI_TX intruction */
	uint64_t int_ena                      : 1;  /**< If 0, polling is required
                                                         1, MPI engine interrupts X end of transaction */
	uint64_t lsbfirst                     : 1;  /**< If 0, shift MSB first
                                                         1, shift LSB first */
	uint64_t wireor                       : 1;  /**< If 0, MPI_TX and MPI_RX are separate wires (SPI)
                                                               MPI_TX pin is always driven
                                                            1, MPI_TX/RX is all from MPI_TX pin (MPI)
                                                               MPI_TX pin is tristated when not transmitting
                                                         NOTE: if WIREOR==1, MPI_RX pin is not used by the
                                                               MPI engine */
	uint64_t clk_cont                     : 1;  /**< If 0, clock idles to value given by IDLELO after
                                                            completion of MPI transaction
                                                         1, clock never idles, requires CS deassertion
                                                            assertion between commands */
	uint64_t idlelo                       : 1;  /**< If 0, MPI_CLK idles high, 1st transition is hi->lo
                                                         1, MPI_CLK idles low, 1st transition is lo->hi */
	uint64_t enable                       : 1;  /**< If 0, all MPI pins are GPIOs
                                                         1, MPI_CLK, MPI_CS, and MPI_TX are driven */
#else
	uint64_t enable                       : 1;
	uint64_t idlelo                       : 1;
	uint64_t clk_cont                     : 1;
	uint64_t wireor                       : 1;
	uint64_t lsbfirst                     : 1;
	uint64_t int_ena                      : 1;
	uint64_t csena                        : 1;
	uint64_t cshi                         : 1;
	uint64_t idleclks                     : 2;
	uint64_t tritx                        : 1;
	uint64_t reserved_11_15               : 5;
	uint64_t clkdiv                       : 13;
	uint64_t reserved_29_63               : 35;
#endif
	} cn31xx;
	struct cvmx_mpi_cfg_s                 cn50xx;
};
typedef union cvmx_mpi_cfg cvmx_mpi_cfg_t;

/**
 * cvmx_mpi_dat#
 */
union cvmx_mpi_datx
{
	uint64_t u64;
	struct cvmx_mpi_datx_s
	{
#if __BYTE_ORDER == __BIG_ENDIAN
	uint64_t reserved_8_63                : 56;
	uint64_t data                         : 8;  /**< Data to transmit/received */
#else
	uint64_t data                         : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_mpi_datx_s                cn30xx;
	struct cvmx_mpi_datx_s                cn31xx;
	struct cvmx_mpi_datx_s                cn50xx;
};
typedef union cvmx_mpi_datx cvmx_mpi_datx_t;

/**
 * cvmx_mpi_sts
 */
union cvmx_mpi_sts
{
	uint64_t u64;
	struct cvmx_mpi_sts_s
	{
#if __BYTE_ORDER == __BIG_ENDIAN
	uint64_t reserved_13_63               : 51;
	uint64_t rxnum                        : 5;  /**< Number of bytes written for transaction */
	uint64_t reserved_1_7                 : 7;
	uint64_t busy                         : 1;  /**< If 0, no MPI transaction in progress
                                                         1, MPI engine is processing a transaction */
#else
	uint64_t busy                         : 1;
	uint64_t reserved_1_7                 : 7;
	uint64_t rxnum                        : 5;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_mpi_sts_s                 cn30xx;
	struct cvmx_mpi_sts_s                 cn31xx;
	struct cvmx_mpi_sts_s                 cn50xx;
};
typedef union cvmx_mpi_sts cvmx_mpi_sts_t;

/**
 * cvmx_mpi_tx
 */
union cvmx_mpi_tx
{
	uint64_t u64;
	struct cvmx_mpi_tx_s
	{
#if __BYTE_ORDER == __BIG_ENDIAN
	uint64_t reserved_17_63               : 47;
	uint64_t leavecs                      : 1;  /**< If 0, deassert CS after transaction is done
                                                         1, leave CS asserted after transactrion is done */
	uint64_t reserved_13_15               : 3;
	uint64_t txnum                        : 5;  /**< Number of bytes to transmit */
	uint64_t reserved_5_7                 : 3;
	uint64_t totnum                       : 5;  /**< Number of bytes to shift (transmit + receive) */
#else
	uint64_t totnum                       : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t txnum                        : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t leavecs                      : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_mpi_tx_s                  cn30xx;
	struct cvmx_mpi_tx_s                  cn31xx;
	struct cvmx_mpi_tx_s                  cn50xx;
};
typedef union cvmx_mpi_tx cvmx_mpi_tx_t;

#endif
