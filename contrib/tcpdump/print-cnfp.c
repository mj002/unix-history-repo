/*	$OpenBSD: print-cnfp.c,v 1.2 1998/06/25 20:26:59 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Cisco NetFlow protocol */

#define NETDISSECT_REWORKED
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <tcpdump-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"

#include "tcp.h"
#include "ipproto.h"

struct nfhdr {
	uint32_t	ver_cnt;	/* version [15], and # of records */
	uint32_t	msys_uptime;
	uint32_t	utc_sec;
	uint32_t	utc_nsec;
	uint32_t	sequence;	/* v5 flow sequence number */
	uint32_t	reserved;	/* v5 only */
};

struct nfrec {
	struct in_addr	src_ina;
	struct in_addr	dst_ina;
	struct in_addr	nhop_ina;
	uint32_t	ifaces;		/* src,dst ifaces */
	uint32_t	packets;
	uint32_t	octets;
	uint32_t	start_time;	/* sys_uptime value */
	uint32_t	last_time;	/* sys_uptime value */
	uint32_t	ports;		/* src,dst ports */
	uint32_t	proto_tos;	/* proto, tos, pad, flags(v5) */
	uint32_t	asses;		/* v1: flags; v5: src,dst AS */
	uint32_t	masks;		/* src,dst addr prefix; v6: encaps */
	struct in_addr	peer_nexthop;	/* v6: IP address of the nexthop within the peer (FIB)*/
};

void
cnfp_print(netdissect_options *ndo,
           const u_char *cp, const u_char *bp _U_)
{
	register const struct nfhdr *nh;
	register const struct nfrec *nr;
	struct protoent *pent;
	int nrecs, ver;
#if 0
	time_t t;
#endif

	nh = (const struct nfhdr *)cp;

	if ((const u_char *)(nh + 1) > ndo->ndo_snapend)
		return;

	nrecs = EXTRACT_32BITS(&nh->ver_cnt) & 0xffff;
	ver = (EXTRACT_32BITS(&nh->ver_cnt) & 0xffff0000) >> 16;
#if 0
	/*
	 * This is seconds since the UN*X epoch, and is followed by
	 * nanoseconds.  XXX - format it, rather than just dumping the
	 * raw seconds-since-the-Epoch.
	 */
	t = EXTRACT_32BITS(&nh->utc_sec);
#endif

	ND_PRINT((ndo, "NetFlow v%x, %u.%03u uptime, %u.%09u, ", ver,
	       EXTRACT_32BITS(&nh->msys_uptime)/1000,
	       EXTRACT_32BITS(&nh->msys_uptime)%1000,
	       EXTRACT_32BITS(&nh->utc_sec), EXTRACT_32BITS(&nh->utc_nsec)));

	if (ver == 5 || ver == 6) {
		ND_PRINT((ndo, "#%u, ", EXTRACT_32BITS(&nh->sequence)));
		nr = (const struct nfrec *)&nh[1];
		ndo->ndo_snaplen -= 24;
	} else {
		nr = (const struct nfrec *)&nh->sequence;
		ndo->ndo_snaplen -= 16;
	}

	ND_PRINT((ndo, "%2u recs", nrecs));

	for (; nrecs-- && (const u_char *)(nr + 1) <= ndo->ndo_snapend; nr++) {
		char buf[20];
		char asbuf[20];

		ND_PRINT((ndo, "\n  started %u.%03u, last %u.%03u",
		       EXTRACT_32BITS(&nr->start_time)/1000,
		       EXTRACT_32BITS(&nr->start_time)%1000,
		       EXTRACT_32BITS(&nr->last_time)/1000,
		       EXTRACT_32BITS(&nr->last_time)%1000));

		asbuf[0] = buf[0] = '\0';
		if (ver == 5 || ver == 6) {
			snprintf(buf, sizeof(buf), "/%u",
				 (EXTRACT_32BITS(&nr->masks) >> 24) & 0xff);
			snprintf(asbuf, sizeof(asbuf), ":%u",
				 (EXTRACT_32BITS(&nr->asses) >> 16) & 0xffff);
		}
		ND_PRINT((ndo, "\n    %s%s%s:%u ", intoa(nr->src_ina.s_addr), buf, asbuf,
			EXTRACT_32BITS(&nr->ports) >> 16));

		if (ver == 5 || ver ==6) {
			snprintf(buf, sizeof(buf), "/%d",
				 (EXTRACT_32BITS(&nr->masks) >> 16) & 0xff);
			snprintf(asbuf, sizeof(asbuf), ":%u",
				 EXTRACT_32BITS(&nr->asses) & 0xffff);
		}
		ND_PRINT((ndo, "> %s%s%s:%u ", intoa(nr->dst_ina.s_addr), buf, asbuf,
			EXTRACT_32BITS(&nr->ports) & 0xffff));

		ND_PRINT((ndo, ">> %s\n    ", intoa(nr->nhop_ina.s_addr)));

		pent = getprotobynumber((EXTRACT_32BITS(&nr->proto_tos) >> 8) & 0xff);
		if (!pent || ndo->ndo_nflag)
			ND_PRINT((ndo, "%u ",
			       (EXTRACT_32BITS(&nr->proto_tos) >> 8) & 0xff));
		else
			ND_PRINT((ndo, "%s ", pent->p_name));

		/* tcp flags for tcp only */
		if (pent && pent->p_proto == IPPROTO_TCP) {
			int flags;
			if (ver == 1)
				flags = (EXTRACT_32BITS(&nr->asses) >> 24) & 0xff;
			else
				flags = (EXTRACT_32BITS(&nr->proto_tos) >> 16) & 0xff;
			ND_PRINT((ndo, "%s%s%s%s%s%s%s",
				flags & TH_FIN  ? "F" : "",
				flags & TH_SYN  ? "S" : "",
				flags & TH_RST  ? "R" : "",
				flags & TH_PUSH ? "P" : "",
				flags & TH_ACK  ? "A" : "",
				flags & TH_URG  ? "U" : "",
				flags           ? " " : ""));
		}

		buf[0]='\0';
		if (ver == 6) {
			snprintf(buf, sizeof(buf), "(%u<>%u encaps)",
				 (EXTRACT_32BITS(&nr->masks) >> 8) & 0xff,
				 (EXTRACT_32BITS(&nr->masks)) & 0xff);
		}
		ND_PRINT((ndo, "tos %u, %u (%u octets) %s",
		       EXTRACT_32BITS(&nr->proto_tos) & 0xff,
		       EXTRACT_32BITS(&nr->packets),
		       EXTRACT_32BITS(&nr->octets), buf));
	}
}
