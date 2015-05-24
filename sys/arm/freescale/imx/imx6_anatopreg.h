/*-
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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

#ifndef	IMX6_ANATOPREG_H
#define	IMX6_ANATOPREG_H

#define	IMX6_ANALOG_CCM_PLL_ARM				0x000
#define	IMX6_ANALOG_CCM_PLL_ARM_SET			0x004
#define	IMX6_ANALOG_CCM_PLL_ARM_CLR			0x008
#define	IMX6_ANALOG_CCM_PLL_ARM_TOG			0x00C
#define	  IMX6_ANALOG_CCM_PLL_ARM_DIV_MASK		  0x7F
#define	  IMX6_ANALOG_CCM_PLL_ARM_LOCK			  (1U << 31)
#define	  IMX6_ANALOG_CCM_PLL_ARM_BYPASS		  (1 << 16)
#define	  IMX6_ANALOG_CCM_PLL_ARM_CLK_SRC_MASK		  (0x03 << 16)
#define	IMX6_ANALOG_CCM_PLL_USB1			0x010
#define	IMX6_ANALOG_CCM_PLL_USB1_SET			0x014
#define	IMX6_ANALOG_CCM_PLL_USB1_CLR			0x018
#define	IMX6_ANALOG_CCM_PLL_USB1_TOG			0x01C
#define	   IMX6_ANALOG_CCM_PLL_USB_LOCK			  (1U << 31)
#define	   IMX6_ANALOG_CCM_PLL_USB_BYPASS		  (1 << 16)
#define	   IMX6_ANALOG_CCM_PLL_USB_ENABLE		  (1 << 13)
#define	   IMX6_ANALOG_CCM_PLL_USB_POWER		  (1 << 12)
#define	   IMX6_ANALOG_CCM_PLL_USB_EN_USB_CLKS		  (1 <<  6)
#define	IMX6_ANALOG_CCM_PLL_USB2			0x020
#define	IMX6_ANALOG_CCM_PLL_USB2_SET			0x024
#define	IMX6_ANALOG_CCM_PLL_USB2_CLR			0x028
#define	IMX6_ANALOG_CCM_PLL_USB2_TOG			0x02C
#define	IMX6_ANALOG_CCM_PLL_SYS				0x030
#define	IMX6_ANALOG_CCM_PLL_SYS_SET			0x034
#define	IMX6_ANALOG_CCM_PLL_SYS_CLR			0x038
#define	IMX6_ANALOG_CCM_PLL_SYS_TOG			0x03C
#define	IMX6_ANALOG_CCM_PLL_SYS_SS			0x040
#define	IMX6_ANALOG_CCM_PLL_SYS_NUM			0x050
#define	IMX6_ANALOG_CCM_PLL_SYS_DENOM			0x060
#define	IMX6_ANALOG_CCM_PLL_AUDIO			0x070
#define	   IMX6_ANALOG_CCM_PLL_AUDIO_ENABLE		  (1 << 13)
#define	   IMX6_ANALOG_CCM_PLL_AUDIO_DIV_SELECT_SHIFT	  0
#define	   IMX6_ANALOG_CCM_PLL_AUDIO_DIV_SELECT_MASK	  0x7f
#define	IMX6_ANALOG_CCM_PLL_AUDIO_SET			0x074
#define	IMX6_ANALOG_CCM_PLL_AUDIO_CLR			0x078
#define	IMX6_ANALOG_CCM_PLL_AUDIO_TOG			0x07C
#define	IMX6_ANALOG_CCM_PLL_AUDIO_NUM			0x080
#define	IMX6_ANALOG_CCM_PLL_AUDIO_DENOM			0x090
#define	IMX6_ANALOG_CCM_PLL_VIDEO			0x0A0
#define	IMX6_ANALOG_CCM_PLL_VIDEO_SET			0x0A4
#define	IMX6_ANALOG_CCM_PLL_VIDEO_CLR			0x0A8
#define	IMX6_ANALOG_CCM_PLL_VIDEO_TOG			0x0AC
#define	IMX6_ANALOG_CCM_PLL_VIDEO_NUM			0x0B0
#define	IMX6_ANALOG_CCM_PLL_VIDEO_DENOM			0x0C0
#define	IMX6_ANALOG_CCM_PLL_MLB				0x0D0
#define	IMX6_ANALOG_CCM_PLL_MLB_SET			0x0D4
#define	IMX6_ANALOG_CCM_PLL_MLB_CLR			0x0D8
#define	IMX6_ANALOG_CCM_PLL_MLB_TOG			0x0DC
#define	IMX6_ANALOG_CCM_PLL_ENET			0x0E0
#define	IMX6_ANALOG_CCM_PLL_ENET_SET			0x0E4
#define	IMX6_ANALOG_CCM_PLL_ENET_CLR			0x0E8
#define	IMX6_ANALOG_CCM_PLL_ENET_TOG			0x0EC
#define	IMX6_ANALOG_CCM_PFD_480				0x0F0
#define	IMX6_ANALOG_CCM_PFD_480_SET			0x0F4
#define	IMX6_ANALOG_CCM_PFD_480_CLR			0x0F8
#define	IMX6_ANALOG_CCM_PFD_480_TOG			0x0FC
#define	IMX6_ANALOG_CCM_PFD_528				0x100
#define	IMX6_ANALOG_CCM_PFD_528_SET			0x104
#define	IMX6_ANALOG_CCM_PFD_528_CLR			0x108
#define	IMX6_ANALOG_CCM_PFD_528_TOG			0x10C

#define	IMX6_ANALOG_PMU_REG_CORE			0x140
#define	  IMX6_ANALOG_PMU_REG2_TARG_SHIFT		  18
#define	  IMX6_ANALOG_PMU_REG2_TARG_MASK		   \
    (0x1f << IMX6_ANALOG_PMU_REG2_TARG_SHIFT)
#define	  IMX6_ANALOG_PMU_REG1_TARG_SHIFT		   9
#define	  IMX6_ANALOG_PMU_REG1_TARG_MASK		   \
    (0x1f << IMX6_ANALOG_PMU_REG1_TARG_SHIFT)
#define	  IMX6_ANALOG_PMU_REG0_TARG_SHIFT		   0
#define	  IMX6_ANALOG_PMU_REG0_TARG_MASK		   \
    (0x1f << IMX6_ANALOG_PMU_REG0_TARG_SHIFT)

#define	IMX6_ANALOG_PMU_MISC0				0x150
#define	IMX6_ANALOG_PMU_MISC0_SET			0x154
#define	IMX6_ANALOG_PMU_MISC0_CLR			0x158
#define	IMX6_ANALOG_PMU_MISC0_TOG			0x15C
#define	  IMX6_ANALOG_PMU_MISC0_SELFBIASOFF		  (1 << 3)

#define	IMX6_ANALOG_PMU_MISC1				0x160
#define	IMX6_ANALOG_PMU_MISC1_SET			0x164
#define	IMX6_ANALOG_PMU_MISC1_CLR			0x168
#define	IMX6_ANALOG_PMU_MISC1_TOG			0x16C
#define	  IMX6_ANALOG_PMU_MISC1_IRQ_TEMPSENSE		  (1 << 29)

#define	IMX6_ANALOG_PMU_MISC2				0x170
#define	IMX6_ANALOG_PMU_MISC2_SET			0x174
#define	IMX6_ANALOG_PMU_MISC2_CLR			0x178
#define	IMX6_ANALOG_PMU_MISC2_TOG			0x17C

/*
 * Note that the ANALOG_CCM_MISCn registers are the same as the PMU_MISCn
 * registers; some bits conceptually belong to the PMU and some to the CCM.
 */
#define	IMX6_ANALOG_CCM_MISC0				IMX6_ANALOG_PMU_MISC0
#define	IMX6_ANALOG_CCM_MISC0_SET			IMX6_ANALOG_PMU_MISC0_SET
#define	IMX6_ANALOG_CCM_MISC0_CLR			IMX6_ANALOG_PMU_MISC0_CLR
#define	IMX6_ANALOG_CCM_MISC0_TOG			IMX6_ANALOG_PMU_MISC0_TOG

#define	IMX6_ANALOG_CCM_MISC2				IMX6_ANALOG_PMU_MISC2
#define	IMX6_ANALOG_CCM_MISC2_SET			IMX6_ANALOG_PMU_MISC2_SET
#define	IMX6_ANALOG_CCM_MISC2_CLR			IMX6_ANALOG_PMU_MISC2_CLR
#define	IMX6_ANALOG_CCM_MISC2_TOG			IMX6_ANALOG_PMU_MISC2_TOG

#define	IMX6_ANALOG_TEMPMON_TEMPSENSE0			0x180
#define	IMX6_ANALOG_TEMPMON_TEMPSENSE0_SET		0x184
#define	IMX6_ANALOG_TEMPMON_TEMPSENSE0_CLR		0x188
#define	IMX6_ANALOG_TEMPMON_TEMPSENSE0_TOG		0x18C
#define	IMX6_ANALOG_TEMPMON_TEMPSENSE0_TOG		0x18C
#define	  IMX6_ANALOG_TEMPMON_TEMPSENSE0_ALARM_SHIFT	 20
#define	  IMX6_ANALOG_TEMPMON_TEMPSENSE0_ALARM_MASK	  \
    (0xfff << IMX6_ANALOG_TEMPMON_TEMPSENSE0_ALARM_SHIFT)
#define	  IMX6_ANALOG_TEMPMON_TEMPSENSE0_TEMP_CNT_SHIFT	  8
#define	  IMX6_ANALOG_TEMPMON_TEMPSENSE0_TEMP_CNT_MASK	  \
    (0xfff << IMX6_ANALOG_TEMPMON_TEMPSENSE0_TEMP_CNT_SHIFT)
#define	  IMX6_ANALOG_TEMPMON_TEMPSENSE0_VALID		0x4
#define	  IMX6_ANALOG_TEMPMON_TEMPSENSE0_MEASURE	0x2
#define	  IMX6_ANALOG_TEMPMON_TEMPSENSE0_POWER_DOWN	0x1

#define	IMX6_ANALOG_TEMPMON_TEMPSENSE1			0x190
#define	IMX6_ANALOG_TEMPMON_TEMPSENSE1_SET		0x194
#define	IMX6_ANALOG_TEMPMON_TEMPSENSE1_CLR		0x198
#define	IMX6_ANALOG_TEMPMON_TEMPSENSE1_TOG		0x19C

#define	IMX6_ANALOG_USB1_VBUS_DETECT			0x1A0
#define	IMX6_ANALOG_USB1_VBUS_DETECT_SET		0x1A4
#define	IMX6_ANALOG_USB1_VBUS_DETECT_CLR		0x1A8
#define	IMX6_ANALOG_USB1_VBUS_DETECT_TOG		0x1AC
#define	IMX6_ANALOG_USB1_CHRG_DETECT			0x1B0
#define	IMX6_ANALOG_USB1_CHRG_DETECT_SET		0x1B4
#define	IMX6_ANALOG_USB1_CHRG_DETECT_CLR		0x1B8
#define	IMX6_ANALOG_USB1_CHRG_DETECT_TOG		0x1BC
#define	  IMX6_ANALOG_USB_CHRG_DETECT_N_ENABLE		  (1 << 20) /* EN_B */
#define	  IMX6_ANALOG_USB_CHRG_DETECT_N_CHK_CHRG	  (1 << 19) /* CHK_CHRG_B */
#define	  IMX6_ANALOG_USB_CHRG_DETECT_CHK_CONTACT	  (1 << 18)
#define	IMX6_ANALOG_USB1_VBUS_DETECT_STAT		0x1C0
#define	IMX6_ANALOG_USB1_CHRG_DETECT_STAT		0x1D0
#define	IMX6_ANALOG_USB1_MISC				0x1F0
#define	IMX6_ANALOG_USB1_MISC_SET			0x1F4
#define	IMX6_ANALOG_USB1_MISC_CLR			0x1F8
#define	IMX6_ANALOG_USB1_MISC_TOG			0x1FC
#define	IMX6_ANALOG_USB2_VBUS_DETECT			0x200
#define	IMX6_ANALOG_USB2_VBUS_DETECT_SET		0x204
#define	IMX6_ANALOG_USB2_VBUS_DETECT_CLR		0x208
#define	IMX6_ANALOG_USB2_VBUS_DETECT_TOG		0x20C
#define	IMX6_ANALOG_USB2_CHRG_DETECT			0x210
#define	IMX6_ANALOG_USB2_CHRG_DETECT_SET		0x214
#define	IMX6_ANALOG_USB2_CHRG_DETECT_CLR		0x218
#define	IMX6_ANALOG_USB2_CHRG_DETECT_TOG		0x21C
#define	IMX6_ANALOG_USB2_VBUS_DETECT_STAT		0x220
#define	IMX6_ANALOG_USB2_CHRG_DETECT_STAT		0x230
#define	IMX6_ANALOG_USB2_MISC				0x250
#define	IMX6_ANALOG_USB2_MISC_SET			0x254
#define	IMX6_ANALOG_USB2_MISC_CLR			0x258
#define	IMX6_ANALOG_USB2_MISC_TOG			0x25C
#define	IMX6_ANALOG_DIGPROG				0x260
#define	IMX6_ANALOG_DIGPROG_SL				0x280
#define	  IMX6_ANALOG_DIGPROG_SOCTYPE_SHIFT		  16
#define	  IMX6_ANALOG_DIGPROG_SOCTYPE_MASK		  \
    (0xff << IMX6_ANALOG_DIGPROG_SOCTYPE_SHIFT)

#endif
