/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/bwi/bwiphy.h,v 1.1 2007/09/08 06:15:54 sephe Exp $
 * $FreeBSD$
 */

#ifndef _BWI_PHY_H
#define _BWI_PHY_H

struct bwi_gains {
	int16_t	tbl_gain1;
	int16_t	tbl_gain2;
	int16_t	phy_gain;
};

int		bwi_phy_attach(struct bwi_mac *);
void		bwi_phy_clear_state(struct bwi_phy *);

int		bwi_phy_calibrate(struct bwi_mac *);
void		bwi_phy_set_bbp_atten(struct bwi_mac *, uint16_t);

void		bwi_set_gains(struct bwi_mac *, const struct bwi_gains *);
int16_t		bwi_nrssi_read(struct bwi_mac *, uint16_t);
void		bwi_nrssi_write(struct bwi_mac *, uint16_t, int16_t);

uint16_t	bwi_phy_read(struct bwi_mac *, uint16_t);
void		bwi_phy_write(struct bwi_mac *, uint16_t, uint16_t);

static __inline void
bwi_phy_init(struct bwi_mac *_mac)
{
	_mac->mac_phy.phy_init(_mac);
}

#define PHY_WRITE(mac, ctrl, val)	bwi_phy_write((mac), (ctrl), (val))
#define PHY_READ(mac, ctrl)		bwi_phy_read((mac), (ctrl))

#define PHY_SETBITS(mac, ctrl, bits)		\
	PHY_WRITE((mac), (ctrl), PHY_READ((mac), (ctrl)) | (bits))
#define PHY_CLRBITS(mac, ctrl, bits)		\
	PHY_WRITE((mac), (ctrl), PHY_READ((mac), (ctrl)) & ~(bits))
#define PHY_FILT_SETBITS(mac, ctrl, filt, bits)	\
	PHY_WRITE((mac), (ctrl), (PHY_READ((mac), (ctrl)) & (filt)) | (bits))

#define BWI_PHYR_NRSSI_THR_11B		0x020
#define BWI_PHYR_BBP_ATTEN		0x060
#define BWI_PHYR_TBL_CTRL_11A		0x072
#define BWI_PHYR_TBL_DATA_LO_11A	0x073
#define BWI_PHYR_TBL_DATA_HI_11A	0x074
#define BWI_PHYR_TBL_CTRL_11G		0x472
#define BWI_PHYR_TBL_DATA_LO_11G	0x473
#define BWI_PHYR_TBL_DATA_HI_11G	0x474
#define BWI_PHYR_NRSSI_THR_11G		0x48a
#define BWI_PHYR_NRSSI_CTRL		0x803
#define BWI_PHYR_NRSSI_DATA		0x804
#define BWI_PHYR_RF_LO			0x810

/*
 * PHY Tables
 */
/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/FineFrequency
 * G PHY
 */
#define BWI_PHY_FREQ_11G_REV1 \
	0x0089,	0x02e9,	0x0409,	0x04e9,	0x05a9,	0x0669,	0x0709,	0x0789,	\
	0x0829,	0x08a9,	0x0929,	0x0989,	0x0a09,	0x0a69,	0x0ac9,	0x0b29,	\
	0x0ba9,	0x0be9,	0x0c49,	0x0ca9,	0x0d09,	0x0d69,	0x0da9,	0x0e09,	\
	0x0e69,	0x0ea9,	0x0f09,	0x0f49,	0x0fa9,	0x0fe9,	0x1029,	0x1089,	\
	0x10c9,	0x1109,	0x1169,	0x11a9,	0x11e9,	0x1229,	0x1289,	0x12c9,	\
	0x1309,	0x1349,	0x1389,	0x13c9,	0x1409,	0x1449,	0x14a9,	0x14e9,	\
	0x1529,	0x1569,	0x15a9,	0x15e9,	0x1629,	0x1669,	0x16a9,	0x16e8,	\
	0x1728,	0x1768,	0x17a8,	0x17e8,	0x1828,	0x1868,	0x18a8,	0x18e8,	\
	0x1928,	0x1968,	0x19a8,	0x19e8,	0x1a28,	0x1a68,	0x1aa8,	0x1ae8,	\
	0x1b28,	0x1b68,	0x1ba8,	0x1be8,	0x1c28,	0x1c68,	0x1ca8,	0x1ce8,	\
	0x1d28,	0x1d68,	0x1dc8,	0x1e08,	0x1e48,	0x1e88,	0x1ec8,	0x1f08,	\
	0x1f48,	0x1f88,	0x1fe8,	0x2028,	0x2068,	0x20a8,	0x2108,	0x2148,	\
	0x2188,	0x21c8,	0x2228,	0x2268,	0x22c8,	0x2308,	0x2348,	0x23a8,	\
	0x23e8,	0x2448,	0x24a8,	0x24e8,	0x2548,	0x25a8,	0x2608,	0x2668,	\
	0x26c8,	0x2728,	0x2787,	0x27e7,	0x2847,	0x28c7,	0x2947,	0x29a7,	\
	0x2a27,	0x2ac7,	0x2b47,	0x2be7,	0x2ca7,	0x2d67,	0x2e47,	0x2f67,	\
	0x3247,	0x3526,	0x3646,	0x3726,	0x3806,	0x38a6,	0x3946,	0x39e6,	\
	0x3a66,	0x3ae6,	0x3b66,	0x3bc6,	0x3c45,	0x3ca5,	0x3d05,	0x3d85,	\
	0x3de5,	0x3e45,	0x3ea5,	0x3ee5,	0x3f45,	0x3fa5,	0x4005,	0x4045,	\
	0x40a5,	0x40e5,	0x4145,	0x4185,	0x41e5,	0x4225,	0x4265,	0x42c5,	\
	0x4305,	0x4345,	0x43a5,	0x43e5,	0x4424,	0x4464,	0x44c4,	0x4504,	\
	0x4544,	0x4584,	0x45c4,	0x4604,	0x4644,	0x46a4,	0x46e4,	0x4724,	\
	0x4764,	0x47a4,	0x47e4,	0x4824,	0x4864,	0x48a4,	0x48e4,	0x4924,	\
	0x4964,	0x49a4,	0x49e4,	0x4a24,	0x4a64,	0x4aa4,	0x4ae4,	0x4b23,	\
	0x4b63,	0x4ba3,	0x4be3,	0x4c23,	0x4c63,	0x4ca3,	0x4ce3,	0x4d23,	\
	0x4d63,	0x4da3,	0x4de3,	0x4e23,	0x4e63,	0x4ea3,	0x4ee3,	0x4f23,	\
	0x4f63,	0x4fc3,	0x5003,	0x5043,	0x5083,	0x50c3,	0x5103,	0x5143,	\
	0x5183,	0x51e2,	0x5222,	0x5262,	0x52a2,	0x52e2,	0x5342,	0x5382,	\
	0x53c2,	0x5402,	0x5462,	0x54a2,	0x5502,	0x5542,	0x55a2,	0x55e2,	\
	0x5642,	0x5682,	0x56e2,	0x5722,	0x5782,	0x57e1,	0x5841,	0x58a1,	\
	0x5901,	0x5961,	0x59c1,	0x5a21,	0x5aa1,	0x5b01,	0x5b81,	0x5be1,	\
	0x5c61,	0x5d01,	0x5d80,	0x5e20,	0x5ee0,	0x5fa0,	0x6080,	0x61c0

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/noise_table
 */
/* G PHY Revision 1 */
#define BWI_PHY_NOISE_11G_REV1 \
	0x013c,	0x01f5,	0x031a,	0x0631,	0x0001,	0x0001,	0x0001,	0x0001
/* G PHY generic */
#define BWI_PHY_NOISE_11G \
	0x5484, 0x3c40, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/rotor_table
 * G PHY Revision 1
 */
#define BWI_PHY_ROTOR_11G_REV1 \
	0xfeb93ffd, 0xfec63ffd, 0xfed23ffd, 0xfedf3ffd,	\
	0xfeec3ffe, 0xfef83ffe, 0xff053ffe, 0xff113ffe,	\
	0xff1e3ffe, 0xff2a3fff, 0xff373fff, 0xff443fff,	\
	0xff503fff, 0xff5d3fff, 0xff693fff, 0xff763fff,	\
	0xff824000, 0xff8f4000, 0xff9b4000, 0xffa84000,	\
	0xffb54000, 0xffc14000, 0xffce4000, 0xffda4000,	\
	0xffe74000, 0xfff34000, 0x00004000, 0x000d4000,	\
	0x00194000, 0x00264000, 0x00324000, 0x003f4000,	\
	0x004b4000, 0x00584000, 0x00654000, 0x00714000,	\
	0x007e4000, 0x008a3fff, 0x00973fff, 0x00a33fff,	\
	0x00b03fff, 0x00bc3fff, 0x00c93fff, 0x00d63fff,	\
	0x00e23ffe, 0x00ef3ffe, 0x00fb3ffe, 0x01083ffe,	\
	0x01143ffe, 0x01213ffd, 0x012e3ffd, 0x013a3ffd,	\
	0x01473ffd

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/noise_scale_table
 */
/* G PHY Revision [0,2] */
#define BWI_PHY_NOISE_SCALE_11G_REV2 \
	0x6c77,	0x5162,	0x3b40,	0x3335,	0x2f2d,	0x2a2a,	0x2527,	0x1f21,	\
	0x1a1d,	0x1719,	0x1616,	0x1414,	0x1414,	0x1400,	0x1414,	0x1614,	\
	0x1716,	0x1a19,	0x1f1d,	0x2521,	0x2a27,	0x2f2a,	0x332d,	0x3b35,	\
	0x5140,	0x6c62,	0x0077
/* G PHY Revsion 7 */
#define BWI_PHY_NOISE_SCALE_11G_REV7 \
	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	\
	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa400,	0xa4a4,	0xa4a4,	\
	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	\
	0xa4a4,	0xa4a4,	0x00a4
/* G PHY generic */
#define BWI_PHY_NOISE_SCALE_11G \
	0xd8dd,	0xcbd4,	0xbcc0,	0xb6b7,	0xb2b0,	0xadad,	0xa7a9,	0x9fa1,	\
	0x969b,	0x9195,	0x8f8f,	0x8a8a,	0x8a8a,	0x8a00,	0x8a8a,	0x8f8a,	\
	0x918f,	0x9695,	0x9f9b,	0xa7a1,	0xada9,	0xb2ad,	0xb6b0,	0xbcb7,	\
	0xcbc0,	0xd8d4,	0x00dd

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/sigma_square_table
 */
/* G PHY Revision 2 */
#define BWI_PHY_SIGMA_SQ_11G_REV2 \
	0x007a,	0x0075,	0x0071,	0x006c,	0x0067,	0x0063,	0x005e,	0x0059,	\
	0x0054,	0x0050,	0x004b,	0x0046,	0x0042,	0x003d,	0x003d,	0x003d,	\
	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	\
	0x003d,	0x003d,	0x0000,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	\
	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	\
	0x0042,	0x0046,	0x004b,	0x0050,	0x0054,	0x0059,	0x005e,	0x0063,	\
	0x0067,	0x006c,	0x0071,	0x0075,	0x007a
/* G PHY Revision (2,7] */
#define BWI_PHY_SIGMA_SQ_11G_REV7 \
	0x00de,	0x00dc,	0x00da,	0x00d8,	0x00d6,	0x00d4,	0x00d2,	0x00cf,	\
	0x00cd,	0x00ca,	0x00c7,	0x00c4,	0x00c1,	0x00be,	0x00be,	0x00be,	\
	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	\
	0x00be,	0x00be,	0x0000,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	\
	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	\
	0x00c1,	0x00c4,	0x00c7,	0x00ca,	0x00cd,	0x00cf,	0x00d2,	0x00d4,	\
	0x00d6,	0x00d8,	0x00da,	0x00dc,	0x00de

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/retard_table
 * G PHY
 */
#define BWI_PHY_DELAY_11G_REV1 \
	0xdb93cb87, 0xd666cf64, 0xd1fdd358, 0xcda6d826,	\
	0xca38dd9f, 0xc729e2b4, 0xc469e88e, 0xc26aee2b,	\
	0xc0def46c, 0xc073fa62, 0xc01d00d5, 0xc0760743,	\
	0xc1560d1e, 0xc2e51369, 0xc4ed18ff, 0xc7ac1ed7,	\
	0xcb2823b2, 0xcefa28d9, 0xd2f62d3f, 0xd7bb3197,	\
	0xdce53568, 0xe1fe3875, 0xe7d13b35, 0xed663d35,	\
	0xf39b3ec4, 0xf98e3fa7, 0x00004000, 0x06723fa7,	\
	0x0c653ec4, 0x129a3d35, 0x182f3b35, 0x1e023875,	\
	0x231b3568, 0x28453197, 0x2d0a2d3f, 0x310628d9,	\
	0x34d823b2, 0x38541ed7, 0x3b1318ff, 0x3d1b1369,	\
	0x3eaa0d1e, 0x3f8a0743, 0x3fe300d5, 0x3f8dfa62,	\
	0x3f22f46c, 0x3d96ee2b, 0x3b97e88e, 0x38d7e2b4,	\
	0x35c8dd9f, 0x325ad826, 0x2e03d358, 0x299acf64,	\
	0x246dcb87

#endif	/* !_BWI_PHY_H */
