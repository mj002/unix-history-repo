/* @(#) $Header: /tcpdump/master/tcpdump/ieee802_11.h,v 1.3 2001/06/14 09:50:01 guy Exp $ (LBL) */
/*
 * Copyright (c) 2001
 *	Fortress Technologies
 *      Charlie Lenahan ( clenahan@fortresstech.com )
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#define IEEE802_11_FC_LEN	2

#define T_MGMT 0x0  /* management */
#define T_CTRL 0x1  /* control */
#define T_DATA 0x2 /* data */
#define T_RESV 0x3  /* reserved */

#define ST_ASSOC_REQUEST   	0x0 
#define ST_ASSOC_RESPONSE 	0x1 
#define ST_REASSOC_REQUEST   	0x2 
#define ST_REASSOC_RESPONSE  	0x3 
#define ST_PROBE_REQUEST   	0x4 
#define ST_PROBE_RESPONSE   	0x5 
/* RESERVED 			0x6  */
/* RESERVED 			0x7  */
#define ST_BEACON   		0x8 
#define ST_ATIM			0x9
#define ST_DISASSOC		0xA
#define ST_AUTH			0xB
#define ST_DEAUTH		0xC
/* RESERVED 			0xD  */
/* RESERVED 			0xE  */
/* RESERVED 			0xF  */


#define CTRL_PS_POLL	0xA 
#define CTRL_RTS	0xB
#define CTRL_CTS	0xC
#define CTRL_ACK	0xD
#define CTRL_CF_END	0xE
#define CTRL_END_ACK	0xF

/*
 * Bits in the frame control field.
 */
#define FC_VERSION(fc)		((fc) & 0x3)
#define FC_TYPE(fc)		(((fc) >> 2) & 0x3)
#define FC_SUBTYPE(fc)		(((fc) >> 4) & 0xF)
#define FC_TO_DS(fc)		((fc) & 0x0100)
#define FC_FROM_DS(fc)		((fc) & 0x0200)
#define FC_MORE_FLAG(fc)	((fc) & 0x0400)
#define FC_RETRY(fc)		((fc) & 0x0800)
#define FC_POWER_MGMT(fc)	((fc) & 0x1000)
#define FC_MORE_DATA(fc)	((fc) & 0x2000)
#define FC_WEP(fc)		((fc) & 0x4000)
#define FC_ORDER(fc)		((fc) & 0x8000)

struct mgmt_header_t {
	u_int16_t	fc;
	u_int16_t 	duration;
	u_int8_t	da[6];
	u_int8_t	sa[6];
	u_int8_t	bssid[6];
	u_int16_t	seq_ctrl;
};

#define MGMT_HEADER_LEN	(2+2+6+6+6+2)

#define CAPABILITY_ESS(cap)	((cap) & 0x0001)
#define CAPABILITY_IBSS(cap)	((cap) & 0x0002)
#define CAPABILITY_CFP(cap)	((cap) & 0x0004)
#define CAPABILITY_CFP_REQ(cap)	((cap) & 0x0008)
#define CAPABILITY_PRIVACY(cap)	((cap) & 0x0010)

struct ssid_t {
	u_int8_t	element_id;
	u_int8_t	length;
	u_char		ssid[33];  /* 32 + 1 for null */
} ;

struct rates_t {
	u_int8_t	element_id;
	u_int8_t	length;
	u_int8_t	rate[8];
};

struct challenge_t {
	u_int8_t	element_id;
	u_int8_t	length;
	u_int8_t	text[254]; /* 1-253 + 1 for null */
};
struct fh_t {
	u_int8_t	element_id;
	u_int8_t	length;
	u_int16_t	dwell_time;
	u_int8_t	hop_set;
	u_int8_t 	hop_pattern;
	u_int8_t	hop_index;
};

struct ds_t {
	u_int8_t	element_id;
	u_int8_t	length;
	u_int8_t	channel;
};

struct cf_t {
	u_int8_t	element_id;
	u_int8_t	length;
	u_int8_t	count;
	u_int8_t	period;
	u_int16_t	max_duration;
	u_int16_t	dur_remaing;
};

struct tim_t {
	u_int8_t	element_id;
	u_int8_t	length;
	u_int8_t	count;
	u_int8_t	period;
	u_int8_t	bitmap_control;
	u_int8_t	bitmap[251];
};

#define E_SSID 		0
#define E_RATES 	1
#define E_FH	 	2
#define E_DS 		3
#define E_CF	 	4
#define E_TIM	 	5
#define E_IBSS 		6
/* reserved 		7 */
/* reserved 		8 */
/* reserved 		9 */
/* reserved 		10 */
/* reserved 		11 */
/* reserved 		12 */
/* reserved 		13 */
/* reserved 		14 */
/* reserved 		15 */
/* reserved 		16 */

#define E_CHALLENGE 	16	
/* reserved 		17 */
/* reserved 		18 */
/* reserved 		19 */
/* reserved 		16 */
/* reserved 		16 */


struct mgmt_body_t {
	u_int8_t   	timestamp[8];
	u_int16_t  	beacon_interval;
	u_int16_t 	listen_interval;
	u_int16_t 	status_code;
	u_int16_t 	aid;
	u_char		ap[6];
	u_int16_t	reason_code;
	u_int16_t	auth_alg;
	u_int16_t	auth_trans_seq_num;
	struct challenge_t  challenge;
	u_int16_t	capability_info;
	struct ssid_t	ssid;
	struct rates_t 	rates;
	struct ds_t	ds;
	struct cf_t	cf;
	struct fh_t	fh;
	struct tim_t	tim;
};

struct ctrl_rts_t {
	u_int16_t	fc;
	u_int16_t	duration;
	u_int8_t	ra[6];
	u_int8_t	ta[6];
	u_int8_t	fcs[4];
};

#define CTRL_RTS_LEN	(2+2+6+6+4)

struct ctrl_cts_t {
	u_int16_t	fc;
	u_int16_t	duration;
	u_int8_t	ra[6];
	u_int8_t	fcs[4];
};

#define CTRL_CTS_LEN	(2+2+6+4)

struct ctrl_ack_t {
	u_int16_t	fc;
	u_int16_t	duration;
	u_int8_t	ra[6];
	u_int8_t	fcs[4];
};

#define CTRL_ACK_LEN	(2+2+6+4)

struct ctrl_ps_poll_t {
	u_int16_t	fc;
	u_int16_t	aid;
	u_int8_t	bssid[6];
	u_int8_t	ta[6];
	u_int8_t	fcs[4];
};

#define CTRL_PS_POLL_LEN	(2+2+6+6+4)

struct ctrl_end_t {
	u_int16_t	fc;
	u_int16_t	duration;
	u_int8_t	ra[6];
	u_int8_t	bssid[6];
	u_int8_t	fcs[4];
};

#define CTRL_END_LEN	(2+2+6+6+4)

struct ctrl_end_ack_t {
	u_int16_t	fc;
	u_int16_t	duration;
	u_int8_t	ra[6];
	u_int8_t	bssid[6];
	u_int8_t	fcs[4];
};

#define CTRL_END_ACK_LEN	(2+2+6+6+4)

#define IV_IV(iv)	((iv) & 0xFFFFFF)
#define IV_PAD(iv)	(((iv) >> 24) & 0x3F)
#define IV_KEYID(iv)	(((iv) >> 30) & 0x03)
