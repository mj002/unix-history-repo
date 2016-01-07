/*-
 * Copyright (c) 1983, 1988, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)inet.c	8.5 (Berkeley) 5/24/95";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <net/route.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_carp.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif /* INET6 */
#include <netinet/in_pcb.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/pim_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_seq.h>
#define	TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "netstat.h"

char	*inetname(struct in_addr *);
void	inetprint(struct in_addr *, int, const char *, int);
#ifdef INET6
static int udp_done, tcp_done, sdp_done;
#endif /* INET6 */

static int
pcblist_sysctl(int proto, const char *name, char **bufp, int istcp __unused)
{
	const char *mibvar;
	char *buf;
	size_t len;

	switch (proto) {
	case IPPROTO_TCP:
		mibvar = "net.inet.tcp.pcblist";
		break;
	case IPPROTO_UDP:
		mibvar = "net.inet.udp.pcblist";
		break;
	case IPPROTO_DIVERT:
		mibvar = "net.inet.divert.pcblist";
		break;
	default:
		mibvar = "net.inet.raw.pcblist";
		break;
	}
	if (strncmp(name, "sdp", 3) == 0)
		mibvar = "net.inet.sdp.pcblist";
	len = 0;
	if (sysctlbyname(mibvar, 0, &len, 0, 0) < 0) {
		if (errno != ENOENT)
			warn("sysctl: %s", mibvar);
		return (0);
	}
	if ((buf = malloc(len)) == 0) {
		warnx("malloc %lu bytes", (u_long)len);
		return (0);
	}
	if (sysctlbyname(mibvar, buf, &len, 0, 0) < 0) {
		warn("sysctl: %s", mibvar);
		free(buf);
		return (0);
	}
	*bufp = buf;
	return (1);
}

/*
 * Copied directly from uipc_socket2.c.  We leave out some fields that are in
 * nested structures that aren't used to avoid extra work.
 */
static void
sbtoxsockbuf(struct sockbuf *sb, struct xsockbuf *xsb)
{
	xsb->sb_cc = sb->sb_cc;
	xsb->sb_hiwat = sb->sb_hiwat;
	xsb->sb_mbcnt = sb->sb_mbcnt;
	xsb->sb_mcnt = sb->sb_mcnt;
	xsb->sb_ccnt = sb->sb_ccnt;
	xsb->sb_mbmax = sb->sb_mbmax;
	xsb->sb_lowat = sb->sb_lowat;
	xsb->sb_flags = sb->sb_flags;
	xsb->sb_timeo = sb->sb_timeo;
}

int
sotoxsocket(struct socket *so, struct xsocket *xso)
{
	struct protosw proto;
	struct domain domain;

	bzero(xso, sizeof *xso);
	xso->xso_len = sizeof *xso;
	xso->xso_so = so;
	xso->so_type = so->so_type;
	xso->so_options = so->so_options;
	xso->so_linger = so->so_linger;
	xso->so_state = so->so_state;
	xso->so_pcb = so->so_pcb;
	if (kread((uintptr_t)so->so_proto, &proto, sizeof(proto)) != 0)
		return (-1);
	xso->xso_protocol = proto.pr_protocol;
	if (kread((uintptr_t)proto.pr_domain, &domain, sizeof(domain)) != 0)
		return (-1);
	xso->xso_family = domain.dom_family;
	xso->so_qlen = so->so_qlen;
	xso->so_incqlen = so->so_incqlen;
	xso->so_qlimit = so->so_qlimit;
	xso->so_timeo = so->so_timeo;
	xso->so_error = so->so_error;
	xso->so_oobmark = so->so_oobmark;
	sbtoxsockbuf(&so->so_snd, &xso->so_snd);
	sbtoxsockbuf(&so->so_rcv, &xso->so_rcv);
	return (0);
}

static int
pcblist_kvm(u_long off, char **bufp, int istcp)
{
	struct inpcbinfo pcbinfo;
	struct inpcbhead listhead;
	struct inpcb *inp;
	struct xinpcb xi;
	struct xinpgen xig;
	struct xtcpcb xt;
	struct socket so;
	struct xsocket *xso;
	char *buf, *p;
	size_t len;

	if (off == 0)
		return (0);
	kread(off, &pcbinfo, sizeof(pcbinfo));
	if (istcp)
		len = 2 * sizeof(xig) +
		    (pcbinfo.ipi_count + pcbinfo.ipi_count / 8) *
		    sizeof(struct xtcpcb);
	else
		len = 2 * sizeof(xig) +
		    (pcbinfo.ipi_count + pcbinfo.ipi_count / 8) *
		    sizeof(struct xinpcb);
	if ((buf = malloc(len)) == 0) {
		warnx("malloc %lu bytes", (u_long)len);
		return (0);
	}
	p = buf;

#define	COPYOUT(obj, size) do {						\
	if (len < (size)) {						\
		warnx("buffer size exceeded");				\
		goto fail;						\
	}								\
	bcopy((obj), p, (size));					\
	len -= (size);							\
	p += (size);							\
} while (0)

#define	KREAD(off, buf, len) do {					\
	if (kread((uintptr_t)(off), (buf), (len)) != 0)			\
		goto fail;						\
} while (0)

	/* Write out header. */
	xig.xig_len = sizeof xig;
	xig.xig_count = pcbinfo.ipi_count;
	xig.xig_gen = pcbinfo.ipi_gencnt;
	xig.xig_sogen = 0;
	COPYOUT(&xig, sizeof xig);

	/* Walk the PCB list. */
	xt.xt_len = sizeof xt;
	xi.xi_len = sizeof xi;
	if (istcp)
		xso = &xt.xt_socket;
	else
		xso = &xi.xi_socket;
	KREAD(pcbinfo.ipi_listhead, &listhead, sizeof(listhead));
	LIST_FOREACH(inp, &listhead, inp_list) {
		if (istcp) {
			KREAD(inp, &xt.xt_inp, sizeof(*inp));
			inp = &xt.xt_inp;
		} else {
			KREAD(inp, &xi.xi_inp, sizeof(*inp));
			inp = &xi.xi_inp;
		}

		if (inp->inp_gencnt > pcbinfo.ipi_gencnt)
			continue;

		if (istcp) {
			if (inp->inp_ppcb == NULL)
				bzero(&xt.xt_tp, sizeof xt.xt_tp);
			else if (inp->inp_flags & INP_TIMEWAIT) {
				bzero(&xt.xt_tp, sizeof xt.xt_tp);
				xt.xt_tp.t_state = TCPS_TIME_WAIT;
			} else
				KREAD(inp->inp_ppcb, &xt.xt_tp,
				    sizeof xt.xt_tp);
		}
		if (inp->inp_socket) {
			KREAD(inp->inp_socket, &so, sizeof(so));
			if (sotoxsocket(&so, xso) != 0)
				goto fail;
		} else {
			bzero(xso, sizeof(*xso));
			if (istcp)
				xso->xso_protocol = IPPROTO_TCP;
		}
		if (istcp)
			COPYOUT(&xt, sizeof xt);
		else
			COPYOUT(&xi, sizeof xi);
	}

	/* Reread the pcbinfo and write out the footer. */
	kread(off, &pcbinfo, sizeof(pcbinfo));
	xig.xig_count = pcbinfo.ipi_count;
	xig.xig_gen = pcbinfo.ipi_gencnt;
	COPYOUT(&xig, sizeof xig);

	*bufp = buf;
	return (1);

fail:
	free(buf);
	return (0);
#undef COPYOUT
#undef KREAD
}

/*
 * Print a summary of connections related to an Internet
 * protocol.  For TCP, also give state of connection.
 * Listening processes (aflag) are suppressed unless the
 * -a (all) flag is specified.
 */
void
protopr(u_long off, const char *name, int af1, int proto)
{
	int istcp;
	static int first = 1;
	char *buf;
	const char *vchar;
	struct tcpcb *tp = NULL;
	struct inpcb *inp;
	struct xinpgen *xig, *oxig;
	struct xsocket *so;
	struct xtcp_timer *timer;

	istcp = 0;
	switch (proto) {
	case IPPROTO_TCP:
#ifdef INET6
		if (strncmp(name, "sdp", 3) != 0) {
			if (tcp_done != 0)
				return;
			else
				tcp_done = 1;
		} else {
			if (sdp_done != 0)
				return;
			else
				sdp_done = 1;
		}
#endif
		istcp = 1;
		break;
	case IPPROTO_UDP:
#ifdef INET6
		if (udp_done != 0)
			return;
		else
			udp_done = 1;
#endif
		break;
	}
	if (live) {
		if (!pcblist_sysctl(proto, name, &buf, istcp))
			return;
	} else {
		if (!pcblist_kvm(off, &buf, istcp))
			return;
	}

	oxig = xig = (struct xinpgen *)buf;
	for (xig = (struct xinpgen *)((char *)xig + xig->xig_len);
	     xig->xig_len > sizeof(struct xinpgen);
	     xig = (struct xinpgen *)((char *)xig + xig->xig_len)) {
		if (istcp) {
			timer = &((struct xtcpcb *)xig)->xt_timer;
			tp = &((struct xtcpcb *)xig)->xt_tp;
			inp = &((struct xtcpcb *)xig)->xt_inp;
			so = &((struct xtcpcb *)xig)->xt_socket;
		} else {
			inp = &((struct xinpcb *)xig)->xi_inp;
			so = &((struct xinpcb *)xig)->xi_socket;
			timer = NULL;
		}

		/* Ignore sockets for protocols other than the desired one. */
		if (so->xso_protocol != proto)
			continue;

		/* Ignore PCBs which were freed during copyout. */
		if (inp->inp_gencnt > oxig->xig_gen)
			continue;

		if ((af1 == AF_INET && (inp->inp_vflag & INP_IPV4) == 0)
#ifdef INET6
		    || (af1 == AF_INET6 && (inp->inp_vflag & INP_IPV6) == 0)
#endif /* INET6 */
		    || (af1 == AF_UNSPEC && ((inp->inp_vflag & INP_IPV4) == 0
#ifdef INET6
					  && (inp->inp_vflag & INP_IPV6) == 0
#endif /* INET6 */
			))
		    )
			continue;
		if (!aflag &&
		    (
		     (istcp && tp->t_state == TCPS_LISTEN)
		     || (af1 == AF_INET &&
		      inet_lnaof(inp->inp_laddr) == INADDR_ANY)
#ifdef INET6
		     || (af1 == AF_INET6 &&
			 IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
#endif /* INET6 */
		     || (af1 == AF_UNSPEC &&
			 (((inp->inp_vflag & INP_IPV4) != 0 &&
			   inet_lnaof(inp->inp_laddr) == INADDR_ANY)
#ifdef INET6
			  || ((inp->inp_vflag & INP_IPV6) != 0 &&
			      IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
#endif
			  ))
		     ))
			continue;

		if (first) {
			if (!Lflag) {
				printf("Active Internet connections");
				if (aflag)
					printf(" (including servers)");
			} else
				printf(
	"Current listen queue sizes (qlen/incqlen/maxqlen)");
			putchar('\n');
			if (Aflag)
				printf("%-*s ", 2 * (int)sizeof(void *), "Tcpcb");
			if (Lflag)
				printf((Aflag && !Wflag) ?
				    "%-5.5s %-14.14s %-18.18s" :
				    "%-5.5s %-14.14s %-22.22s",
				    "Proto", "Listen", "Local Address");
			else if (Tflag)
				printf((Aflag && !Wflag) ?
			    "%-5.5s %-6.6s %-6.6s %-6.6s %-18.18s %s" :
			    "%-5.5s %-6.6s %-6.6s %-6.6s %-22.22s %s",
				    "Proto", "Rexmit", "OOORcv", "0-win",
				    "Local Address", "Foreign Address");
			else {
				printf((Aflag && !Wflag) ? 
				       "%-5.5s %-6.6s %-6.6s %-18.18s %-18.18s" :
				       "%-5.5s %-6.6s %-6.6s %-22.22s %-22.22s",
				       "Proto", "Recv-Q", "Send-Q",
				       "Local Address", "Foreign Address");
				if (!xflag && !Rflag)
					printf(" (state)");
			}
			if (xflag) {
				printf(" %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s %-6.6s",
				       "R-MBUF", "S-MBUF", "R-CLUS", 
				       "S-CLUS", "R-HIWA", "S-HIWA", 
				       "R-LOWA", "S-LOWA", "R-BCNT", 
				       "S-BCNT", "R-BMAX", "S-BMAX");
				printf(" %7.7s %7.7s %7.7s %7.7s %7.7s %7.7s",
				       "rexmt", "persist", "keep",
				       "2msl", "delack", "rcvtime");
			} else if (Rflag) {
				printf ("  %8.8s %5.5s",
				    "flowid", "ftype");
			}
			putchar('\n');
			first = 0;
		}
		if (Lflag && so->so_qlimit == 0)
			continue;
		if (Aflag) {
			if (istcp)
				printf("%*lx ", 2 * (int)sizeof(void *), (u_long)inp->inp_ppcb);
			else
				printf("%*lx ", 2 * (int)sizeof(void *), (u_long)so->so_pcb);
		}
#ifdef INET6
		if ((inp->inp_vflag & INP_IPV6) != 0)
			vchar = ((inp->inp_vflag & INP_IPV4) != 0) ?
			    "46" : "6 ";
		else
#endif
		vchar = ((inp->inp_vflag & INP_IPV4) != 0) ?
		    "4 " : "  ";
		if (istcp && (tp->t_flags & TF_TOE) != 0)
			printf("%-3.3s%-2.2s ", "toe", vchar);
		else
			printf("%-3.3s%-2.2s ", name, vchar);
		if (Lflag) {
			char buf1[15];

			snprintf(buf1, 15, "%d/%d/%d", so->so_qlen,
			    so->so_incqlen, so->so_qlimit);
			printf("%-14.14s ", buf1);
		} else if (Tflag) {
			if (istcp)
				printf("%6u %6u %6u ", tp->t_sndrexmitpack,
				       tp->t_rcvoopack, tp->t_sndzerowin);
		} else {
			printf("%6u %6u ", so->so_rcv.sb_cc, so->so_snd.sb_cc);
		}
		if (numeric_port) {
			if (inp->inp_vflag & INP_IPV4) {
				inetprint(&inp->inp_laddr, (int)inp->inp_lport,
				    name, 1);
				if (!Lflag)
					inetprint(&inp->inp_faddr,
					    (int)inp->inp_fport, name, 1);
			}
#ifdef INET6
			else if (inp->inp_vflag & INP_IPV6) {
				inet6print(&inp->in6p_laddr,
				    (int)inp->inp_lport, name, 1);
				if (!Lflag)
					inet6print(&inp->in6p_faddr,
					    (int)inp->inp_fport, name, 1);
			} /* else nothing printed now */
#endif /* INET6 */
		} else if (inp->inp_flags & INP_ANONPORT) {
			if (inp->inp_vflag & INP_IPV4) {
				inetprint(&inp->inp_laddr, (int)inp->inp_lport,
				    name, 1);
				if (!Lflag)
					inetprint(&inp->inp_faddr,
					    (int)inp->inp_fport, name, 0);
			}
#ifdef INET6
			else if (inp->inp_vflag & INP_IPV6) {
				inet6print(&inp->in6p_laddr,
				    (int)inp->inp_lport, name, 1);
				if (!Lflag)
					inet6print(&inp->in6p_faddr,
					    (int)inp->inp_fport, name, 0);
			} /* else nothing printed now */
#endif /* INET6 */
		} else {
			if (inp->inp_vflag & INP_IPV4) {
				inetprint(&inp->inp_laddr, (int)inp->inp_lport,
				    name, 0);
				if (!Lflag)
					inetprint(&inp->inp_faddr,
					    (int)inp->inp_fport, name,
					    inp->inp_lport != inp->inp_fport);
			}
#ifdef INET6
			else if (inp->inp_vflag & INP_IPV6) {
				inet6print(&inp->in6p_laddr,
				    (int)inp->inp_lport, name, 0);
				if (!Lflag)
					inet6print(&inp->in6p_faddr,
					    (int)inp->inp_fport, name,
					    inp->inp_lport != inp->inp_fport);
			} /* else nothing printed now */
#endif /* INET6 */
		}
		if (xflag) {
			printf("%6u %6u %6u %6u %6u %6u %6u %6u %6u %6u %6u %6u",
			       so->so_rcv.sb_mcnt, so->so_snd.sb_mcnt,
			       so->so_rcv.sb_ccnt, so->so_snd.sb_ccnt,
			       so->so_rcv.sb_hiwat, so->so_snd.sb_hiwat,
			       so->so_rcv.sb_lowat, so->so_snd.sb_lowat,
			       so->so_rcv.sb_mbcnt, so->so_snd.sb_mbcnt,
			       so->so_rcv.sb_mbmax, so->so_snd.sb_mbmax);
			if (timer != NULL)
				printf(" %4d.%02d %4d.%02d %4d.%02d %4d.%02d %4d.%02d %4d.%02d",
				    timer->tt_rexmt / 1000, (timer->tt_rexmt % 1000) / 10,
				    timer->tt_persist / 1000, (timer->tt_persist % 1000) / 10,
				    timer->tt_keep / 1000, (timer->tt_keep % 1000) / 10,
				    timer->tt_2msl / 1000, (timer->tt_2msl % 1000) / 10,
				    timer->tt_delack / 1000, (timer->tt_delack % 1000) / 10,
				    timer->t_rcvtime / 1000, (timer->t_rcvtime % 1000) / 10);
		}
		if (istcp && !Lflag && !xflag && !Tflag && !Rflag) {
			if (tp->t_state < 0 || tp->t_state >= TCP_NSTATES)
				printf("%d", tp->t_state);
			else {
				printf("%s", tcpstates[tp->t_state]);
#if defined(TF_NEEDSYN) && defined(TF_NEEDFIN)
				/* Show T/TCP `hidden state' */
				if (tp->t_flags & (TF_NEEDSYN|TF_NEEDFIN))
					putchar('*');
#endif /* defined(TF_NEEDSYN) && defined(TF_NEEDFIN) */
			}
		}
		if (Rflag) {
			printf(" %08x %5d",
			    inp->inp_flowid,
			    inp->inp_flowtype);
		}
		putchar('\n');
	}
	if (xig != oxig && xig->xig_gen != oxig->xig_gen) {
		if (oxig->xig_count > xig->xig_count) {
			printf("Some %s sockets may have been deleted.\n",
			    name);
		} else if (oxig->xig_count < xig->xig_count) {
			printf("Some %s sockets may have been created.\n",
			    name);
		} else {
			printf(
	"Some %s sockets may have been created or deleted.\n",
			    name);
		}
	}
	free(buf);
}

/*
 * Dump TCP statistics structure.
 */
void
tcp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct tcpstat tcpstat;

#ifdef INET6
	if (tcp_done != 0)
		return;
	else
		tcp_done = 1;
#endif

	if (fetch_stats("net.inet.tcp.stats", off, &tcpstat,
	    sizeof(tcpstat), kread_counters) != 0)
		return;

	printf ("%s:\n", name);

#define	p(f, m) if (tcpstat.f || sflag <= 1)				\
	printf(m, (uintmax_t )tcpstat.f, plural(tcpstat.f))

#define	p1a(f, m) if (tcpstat.f || sflag <= 1)				\
	printf(m, (uintmax_t )tcpstat.f)

#define	p2(f1, f2, m) if (tcpstat.f1 || tcpstat.f2 || sflag <= 1)	\
	printf(m, (uintmax_t )tcpstat.f1, plural(tcpstat.f1),		\
	    (uintmax_t )tcpstat.f2, plural(tcpstat.f2))

#define	p2a(f1, f2, m) if (tcpstat.f1 || tcpstat.f2 || sflag <= 1)	\
	printf(m, (uintmax_t )tcpstat.f1, plural(tcpstat.f1),		\
	    (uintmax_t )tcpstat.f2)

#define	p3(f, m) if (tcpstat.f || sflag <= 1)				\
	printf(m, (uintmax_t )tcpstat.f, pluralies(tcpstat.f))

	p(tcps_sndtotal, "\t%ju packet%s sent\n");
	p2(tcps_sndpack,tcps_sndbyte, "\t\t%ju data packet%s (%ju byte%s)\n");
	p2(tcps_sndrexmitpack, tcps_sndrexmitbyte,
	    "\t\t%ju data packet%s (%ju byte%s) retransmitted\n");
	p(tcps_sndrexmitbad,
	    "\t\t%ju data packet%s unnecessarily retransmitted\n");
	p(tcps_mturesent, "\t\t%ju resend%s initiated by MTU discovery\n");
	p2a(tcps_sndacks, tcps_delack,
	    "\t\t%ju ack-only packet%s (%ju delayed)\n");
	p(tcps_sndurg, "\t\t%ju URG only packet%s\n");
	p(tcps_sndprobe, "\t\t%ju window probe packet%s\n");
	p(tcps_sndwinup, "\t\t%ju window update packet%s\n");
	p(tcps_sndctrl, "\t\t%ju control packet%s\n");
	p(tcps_rcvtotal, "\t%ju packet%s received\n");
	p2(tcps_rcvackpack, tcps_rcvackbyte,
	    "\t\t%ju ack%s (for %ju byte%s)\n");
	p(tcps_rcvdupack, "\t\t%ju duplicate ack%s\n");
	p(tcps_rcvacktoomuch, "\t\t%ju ack%s for unsent data\n");
	p2(tcps_rcvpack, tcps_rcvbyte,
	    "\t\t%ju packet%s (%ju byte%s) received in-sequence\n");
	p2(tcps_rcvduppack, tcps_rcvdupbyte,
	    "\t\t%ju completely duplicate packet%s (%ju byte%s)\n");
	p(tcps_pawsdrop, "\t\t%ju old duplicate packet%s\n");
	p2(tcps_rcvpartduppack, tcps_rcvpartdupbyte,
	    "\t\t%ju packet%s with some dup. data (%ju byte%s duped)\n");
	p2(tcps_rcvoopack, tcps_rcvoobyte,
	    "\t\t%ju out-of-order packet%s (%ju byte%s)\n");
	p2(tcps_rcvpackafterwin, tcps_rcvbyteafterwin,
	    "\t\t%ju packet%s (%ju byte%s) of data after window\n");
	p(tcps_rcvwinprobe, "\t\t%ju window probe%s\n");
	p(tcps_rcvwinupd, "\t\t%ju window update packet%s\n");
	p(tcps_rcvafterclose, "\t\t%ju packet%s received after close\n");
	p(tcps_rcvbadsum, "\t\t%ju discarded for bad checksum%s\n");
	p(tcps_rcvbadoff, "\t\t%ju discarded for bad header offset field%s\n");
	p1a(tcps_rcvshort, "\t\t%ju discarded because packet too short\n");
	p1a(tcps_rcvmemdrop, "\t\t%ju discarded due to memory problems\n");
	p(tcps_connattempt, "\t%ju connection request%s\n");
	p(tcps_accepts, "\t%ju connection accept%s\n");
	p(tcps_badsyn, "\t%ju bad connection attempt%s\n");
	p(tcps_listendrop, "\t%ju listen queue overflow%s\n");
	p(tcps_badrst, "\t%ju ignored RSTs in the window%s\n");
	p(tcps_connects, "\t%ju connection%s established (including accepts)\n");
	p2(tcps_closed, tcps_drops,
	    "\t%ju connection%s closed (including %ju drop%s)\n");
	p(tcps_cachedrtt, "\t\t%ju connection%s updated cached RTT on close\n");
	p(tcps_cachedrttvar,
	    "\t\t%ju connection%s updated cached RTT variance on close\n");
	p(tcps_cachedssthresh,
	    "\t\t%ju connection%s updated cached ssthresh on close\n");
	p(tcps_conndrops, "\t%ju embryonic connection%s dropped\n");
	p2(tcps_rttupdated, tcps_segstimed,
	    "\t%ju segment%s updated rtt (of %ju attempt%s)\n");
	p(tcps_rexmttimeo, "\t%ju retransmit timeout%s\n");
	p(tcps_timeoutdrop, "\t\t%ju connection%s dropped by rexmit timeout\n");
	p(tcps_persisttimeo, "\t%ju persist timeout%s\n");
	p(tcps_persistdrop, "\t\t%ju connection%s dropped by persist timeout\n");
	p(tcps_finwait2_drops,
	    "\t%ju Connection%s (fin_wait_2) dropped because of timeout\n");
	p(tcps_keeptimeo, "\t%ju keepalive timeout%s\n");
	p(tcps_keepprobe, "\t\t%ju keepalive probe%s sent\n");
	p(tcps_keepdrops, "\t\t%ju connection%s dropped by keepalive\n");
	p(tcps_predack, "\t%ju correct ACK header prediction%s\n");
	p(tcps_preddat, "\t%ju correct data packet header prediction%s\n");

	p3(tcps_sc_added, "\t%ju syncache entr%s added\n");
	p1a(tcps_sc_retransmitted, "\t\t%ju retransmitted\n");
	p1a(tcps_sc_dupsyn, "\t\t%ju dupsyn\n");
	p1a(tcps_sc_dropped, "\t\t%ju dropped\n");
	p1a(tcps_sc_completed, "\t\t%ju completed\n");
	p1a(tcps_sc_bucketoverflow, "\t\t%ju bucket overflow\n");
	p1a(tcps_sc_cacheoverflow, "\t\t%ju cache overflow\n");
	p1a(tcps_sc_reset, "\t\t%ju reset\n");
	p1a(tcps_sc_stale, "\t\t%ju stale\n");
	p1a(tcps_sc_aborted, "\t\t%ju aborted\n");
	p1a(tcps_sc_badack, "\t\t%ju badack\n");
	p1a(tcps_sc_unreach, "\t\t%ju unreach\n");
	p(tcps_sc_zonefail, "\t\t%ju zone failure%s\n");
	p(tcps_sc_sendcookie, "\t%ju cookie%s sent\n");
	p(tcps_sc_recvcookie, "\t%ju cookie%s received\n");

	p3(tcps_hc_added, "\t%ju hostcache entr%s added\n");
	p1a(tcps_hc_bucketoverflow, "\t\t%ju bucket overflow\n");

	p(tcps_sack_recovery_episode, "\t%ju SACK recovery episode%s\n");
	p(tcps_sack_rexmits,
	    "\t%ju segment rexmit%s in SACK recovery episodes\n");
	p(tcps_sack_rexmit_bytes,
	    "\t%ju byte rexmit%s in SACK recovery episodes\n");
	p(tcps_sack_rcv_blocks,
	    "\t%ju SACK option%s (SACK blocks) received\n");
	p(tcps_sack_send_blocks, "\t%ju SACK option%s (SACK blocks) sent\n");
	p1a(tcps_sack_sboverflow, "\t%ju SACK scoreboard overflow\n");

	p(tcps_ecn_ce, "\t%ju packet%s with ECN CE bit set\n");
	p(tcps_ecn_ect0, "\t%ju packet%s with ECN ECT(0) bit set\n");
	p(tcps_ecn_ect1, "\t%ju packet%s with ECN ECT(1) bit set\n");
	p(tcps_ecn_shs, "\t%ju successful ECN handshake%s\n");
	p(tcps_ecn_rcwnd, "\t%ju time%s ECN reduced the congestion window\n");

	p(tcps_sig_rcvgoodsig,
	     "\t%ju packet%s with valid tcp-md5 signature received\n");
	p(tcps_sig_rcvbadsig,
	     "\t%ju packet%s with invalid tcp-md5 signature received\n");
	p(tcps_sig_err_buildsig,
	     "\t%ju packet%s with tcp-md5 signature mismatch\n");
	p(tcps_sig_err_sigopt,
	     "\t%ju packet%s with unexpected tcp-md5 signature received\n");
	p(tcps_sig_err_nosigopt,
	     "\t%ju packet%s without expected tcp-md5 signature received\n");
#undef p
#undef p1a
#undef p2
#undef p2a
#undef p3
}

/*
 * Dump UDP statistics structure.
 */
void
udp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct udpstat udpstat;
	uint64_t delivered;

#ifdef INET6
	if (udp_done != 0)
		return;
	else
		udp_done = 1;
#endif

	if (fetch_stats("net.inet.udp.stats", off, &udpstat,
	    sizeof(udpstat), kread_counters) != 0)
		return;

	printf("%s:\n", name);
#define	p(f, m) if (udpstat.f || sflag <= 1) \
    printf("\t%ju " m, (uintmax_t)udpstat.f, plural(udpstat.f))
#define	p1a(f, m) if (udpstat.f || sflag <= 1) \
    printf("\t%ju " m, (uintmax_t)udpstat.f)
	p(udps_ipackets, "datagram%s received\n");
	p1a(udps_hdrops, "with incomplete header\n");
	p1a(udps_badlen, "with bad data length field\n");
	p1a(udps_badsum, "with bad checksum\n");
	p1a(udps_nosum, "with no checksum\n");
	p1a(udps_noport, "dropped due to no socket\n");
	p(udps_noportbcast,
	    "broadcast/multicast datagram%s undelivered\n");
	p1a(udps_fullsock, "dropped due to full socket buffers\n");
	p1a(udpps_pcbhashmiss, "not for hashed pcb\n");
	delivered = udpstat.udps_ipackets -
		    udpstat.udps_hdrops -
		    udpstat.udps_badlen -
		    udpstat.udps_badsum -
		    udpstat.udps_noport -
		    udpstat.udps_noportbcast -
		    udpstat.udps_fullsock;
	if (delivered || sflag <= 1)
		printf("\t%ju delivered\n", (uint64_t)delivered);
	p(udps_opackets, "datagram%s output\n");
	/* the next statistic is cumulative in udps_noportbcast */
	p(udps_filtermcast,
	    "time%s multicast source filter matched\n");
#undef p
#undef p1a
}

/*
 * Dump CARP statistics structure.
 */
void
carp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct carpstats carpstat;

	if (fetch_stats("net.inet.carp.stats", off, &carpstat,
	    sizeof(carpstat), kread_counters) != 0)
		return;

	printf("%s:\n", name);

#define	p(f, m) if (carpstat.f || sflag <= 1) \
	printf(m, (uintmax_t)carpstat.f, plural(carpstat.f))
#define	p2(f, m) if (carpstat.f || sflag <= 1) \
	printf(m, (uintmax_t)carpstat.f)

	p(carps_ipackets, "\t%ju packet%s received (IPv4)\n");
	p(carps_ipackets6, "\t%ju packet%s received (IPv6)\n");
	p(carps_badttl, "\t\t%ju packet%s discarded for wrong TTL\n");
	p(carps_hdrops, "\t\t%ju packet%s shorter than header\n");
	p(carps_badsum, "\t\t%ju discarded for bad checksum%s\n");
	p(carps_badver,	"\t\t%ju discarded packet%s with a bad version\n");
	p2(carps_badlen, "\t\t%ju discarded because packet too short\n");
	p2(carps_badauth, "\t\t%ju discarded for bad authentication\n");
	p2(carps_badvhid, "\t\t%ju discarded for bad vhid\n");
	p2(carps_badaddrs, "\t\t%ju discarded because of a bad address list\n");
	p(carps_opackets, "\t%ju packet%s sent (IPv4)\n");
	p(carps_opackets6, "\t%ju packet%s sent (IPv6)\n");
	p2(carps_onomem, "\t\t%ju send failed due to mbuf memory error\n");
#if notyet
	p(carps_ostates, "\t\t%s state update%s sent\n");
#endif
#undef p
#undef p2
}

/*
 * Dump IP statistics structure.
 */
void
ip_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct ipstat ipstat;

	if (fetch_stats("net.inet.ip.stats", off, &ipstat,
	    sizeof(ipstat), kread_counters) != 0)
		return;

	printf("%s:\n", name);

#define	p(f, m) if (ipstat.f || sflag <= 1) \
    printf(m, (uintmax_t )ipstat.f, plural(ipstat.f))
#define	p1a(f, m) if (ipstat.f || sflag <= 1) \
    printf(m, (uintmax_t )ipstat.f)

	p(ips_total, "\t%ju total packet%s received\n");
	p(ips_badsum, "\t%ju bad header checksum%s\n");
	p1a(ips_toosmall, "\t%ju with size smaller than minimum\n");
	p1a(ips_tooshort, "\t%ju with data size < data length\n");
	p1a(ips_toolong, "\t%ju with ip length > max ip packet size\n");
	p1a(ips_badhlen, "\t%ju with header length < data size\n");
	p1a(ips_badlen, "\t%ju with data length < header length\n");
	p1a(ips_badoptions, "\t%ju with bad options\n");
	p1a(ips_badvers, "\t%ju with incorrect version number\n");
	p(ips_fragments, "\t%ju fragment%s received\n");
	p(ips_fragdropped, "\t%ju fragment%s dropped (dup or out of space)\n");
	p(ips_fragtimeout, "\t%ju fragment%s dropped after timeout\n");
	p(ips_reassembled, "\t%ju packet%s reassembled ok\n");
	p(ips_delivered, "\t%ju packet%s for this host\n");
	p(ips_noproto, "\t%ju packet%s for unknown/unsupported protocol\n");
	p(ips_forward, "\t%ju packet%s forwarded");
	p(ips_fastforward, " (%ju packet%s fast forwarded)");
	if (ipstat.ips_forward || sflag <= 1)
		putchar('\n');
	p(ips_cantforward, "\t%ju packet%s not forwardable\n");
	p(ips_notmember,
	    "\t%ju packet%s received for unknown multicast group\n");
	p(ips_redirectsent, "\t%ju redirect%s sent\n");
	p(ips_localout, "\t%ju packet%s sent from this host\n");
	p(ips_rawout, "\t%ju packet%s sent with fabricated ip header\n");
	p(ips_odropped,
	    "\t%ju output packet%s dropped due to no bufs, etc.\n");
	p(ips_noroute, "\t%ju output packet%s discarded due to no route\n");
	p(ips_fragmented, "\t%ju output datagram%s fragmented\n");
	p(ips_ofragments, "\t%ju fragment%s created\n");
	p(ips_cantfrag, "\t%ju datagram%s that can't be fragmented\n");
	p(ips_nogif, "\t%ju tunneling packet%s that can't find gif\n");
	p(ips_badaddr, "\t%ju datagram%s with bad address in header\n");
#undef p
#undef p1a
}

/*
 * Dump ARP statistics structure.
 */
void
arp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct arpstat arpstat;

	if (fetch_stats("net.link.ether.arp.stats", off, &arpstat,
	    sizeof(arpstat), kread_counters) != 0)
		return;

	printf("%s:\n", name);

#define	p(f, m) if (arpstat.f || sflag <= 1) \
    printf("\t%ju " m, (uintmax_t)arpstat.f, plural(arpstat.f))
#define	p2(f, m) if (arpstat.f || sflag <= 1) \
    printf("\t%ju " m, (uintmax_t)arpstat.f, pluralies(arpstat.f))

	p(txrequests, "ARP request%s sent\n");
	p2(txreplies, "ARP repl%s sent\n");
	p(rxrequests, "ARP request%s received\n");
	p2(rxreplies, "ARP repl%s received\n");
	p(received, "ARP packet%s received\n");
	p(dropped, "total packet%s dropped due to no ARP entry\n");
	p(timeouts, "ARP entry%s timed out\n");
	p(dupips, "Duplicate IP%s seen\n");
#undef p
#undef p2
}



static	const char *icmpnames[ICMP_MAXTYPE + 1] = {
	"echo reply",			/* RFC 792 */
	"#1",
	"#2",
	"destination unreachable",	/* RFC 792 */
	"source quench",		/* RFC 792 */
	"routing redirect",		/* RFC 792 */
	"#6",
	"#7",
	"echo",				/* RFC 792 */
	"router advertisement",		/* RFC 1256 */
	"router solicitation",		/* RFC 1256 */
	"time exceeded",		/* RFC 792 */
	"parameter problem",		/* RFC 792 */
	"time stamp",			/* RFC 792 */
	"time stamp reply",		/* RFC 792 */
	"information request",		/* RFC 792 */
	"information request reply",	/* RFC 792 */
	"address mask request",		/* RFC 950 */
	"address mask reply",		/* RFC 950 */
	"#19",
	"#20",
	"#21",
	"#22",
	"#23",
	"#24",
	"#25",
	"#26",
	"#27",
	"#28",
	"#29",
	"icmp traceroute",		/* RFC 1393 */
	"datagram conversion error",	/* RFC 1475 */
	"mobile host redirect",
	"IPv6 where-are-you",
	"IPv6 i-am-here",
	"mobile registration req",
	"mobile registration reply",
	"domain name request",		/* RFC 1788 */
	"domain name reply",		/* RFC 1788 */
	"icmp SKIP",
	"icmp photuris",		/* RFC 2521 */
};

/*
 * Dump ICMP statistics.
 */
void
icmp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct icmpstat icmpstat;
	size_t len;
	int i, first;

	if (fetch_stats("net.inet.icmp.stats", off, &icmpstat,
	    sizeof(icmpstat), kread_counters) != 0)
		return;

	printf("%s:\n", name);

#define	p(f, m) if (icmpstat.f || sflag <= 1) \
    printf(m, icmpstat.f, plural(icmpstat.f))
#define	p1a(f, m) if (icmpstat.f || sflag <= 1) \
    printf(m, icmpstat.f)
#define	p2(f, m) if (icmpstat.f || sflag <= 1) \
    printf(m, icmpstat.f, plurales(icmpstat.f))

	p(icps_error, "\t%lu call%s to icmp_error\n");
	p(icps_oldicmp,
	    "\t%lu error%s not generated in response to an icmp message\n");
	for (first = 1, i = 0; i < ICMP_MAXTYPE + 1; i++)
		if (icmpstat.icps_outhist[i] != 0) {
			if (first) {
				printf("\tOutput histogram:\n");
				first = 0;
			}
			if (icmpnames[i] != NULL)
				printf("\t\t%s: %lu\n", icmpnames[i],
					icmpstat.icps_outhist[i]);
			else
				printf("\t\tunknown ICMP #%d: %lu\n", i,
					icmpstat.icps_outhist[i]);
		}
	p(icps_badcode, "\t%lu message%s with bad code fields\n");
	p(icps_tooshort, "\t%lu message%s less than the minimum length\n");
	p(icps_checksum, "\t%lu message%s with bad checksum\n");
	p(icps_badlen, "\t%lu message%s with bad length\n");
	p1a(icps_bmcastecho, "\t%lu multicast echo requests ignored\n");
	p1a(icps_bmcasttstamp, "\t%lu multicast timestamp requests ignored\n");
	for (first = 1, i = 0; i < ICMP_MAXTYPE + 1; i++)
		if (icmpstat.icps_inhist[i] != 0) {
			if (first) {
				printf("\tInput histogram:\n");
				first = 0;
			}
			if (icmpnames[i] != NULL)
				printf("\t\t%s: %lu\n", icmpnames[i],
				    icmpstat.icps_inhist[i]);
			else
				printf("\t\tunknown ICMP #%d: %lu\n", i,
				    icmpstat.icps_inhist[i]);
		}
	p(icps_reflect, "\t%lu message response%s generated\n");
	p2(icps_badaddr, "\t%lu invalid return address%s\n");
	p(icps_noroute, "\t%lu no return route%s\n");
#undef p
#undef p1a
#undef p2
	if (live) {
		len = sizeof i;
		if (sysctlbyname("net.inet.icmp.maskrepl", &i, &len, NULL, 0) <
		    0)
			return;
		printf("\tICMP address mask responses are %sabled\n",
		    i ? "en" : "dis");
	}
}

#ifndef BURN_BRIDGES
/*
 * Dump IGMP statistics structure (pre 8.x kernel).
 */
static void
igmp_stats_live_old(const char *name)
{
	struct oigmpstat oigmpstat, zerostat;
	size_t len = sizeof(oigmpstat);

	if (zflag)
		memset(&zerostat, 0, len);
	if (sysctlbyname("net.inet.igmp.stats", &oigmpstat, &len,
	    zflag ? &zerostat : NULL, zflag ? len : 0) < 0) {
		warn("sysctl: net.inet.igmp.stats");
		return;
	}

	printf("%s:\n", name);

#define	p(f, m) if (oigmpstat.f || sflag <= 1) \
    printf(m, oigmpstat.f, plural(oigmpstat.f))
#define	py(f, m) if (oigmpstat.f || sflag <= 1) \
    printf(m, oigmpstat.f, oigmpstat.f != 1 ? "ies" : "y")
	p(igps_rcv_total, "\t%u message%s received\n");
	p(igps_rcv_tooshort, "\t%u message%s received with too few bytes\n");
	p(igps_rcv_badsum, "\t%u message%s received with bad checksum\n");
	py(igps_rcv_queries, "\t%u membership quer%s received\n");
	py(igps_rcv_badqueries,
	    "\t%u membership quer%s received with invalid field(s)\n");
	p(igps_rcv_reports, "\t%u membership report%s received\n");
	p(igps_rcv_badreports,
	    "\t%u membership report%s received with invalid field(s)\n");
	p(igps_rcv_ourreports,
"\t%u membership report%s received for groups to which we belong\n");
        p(igps_snd_reports, "\t%u membership report%s sent\n");
#undef p
#undef py
}
#endif /* !BURN_BRIDGES */

/*
 * Dump IGMP statistics structure.
 */
void
igmp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct igmpstat igmpstat;

	if (fetch_stats("net.inet.igmp.stats", 0, &igmpstat,
	    sizeof(igmpstat), kread) != 0)
		return;

	if (igmpstat.igps_version != IGPS_VERSION_3) {
		warnx("%s: version mismatch (%d != %d)", __func__,
		    igmpstat.igps_version, IGPS_VERSION_3);
	}
	if (igmpstat.igps_len != IGPS_VERSION3_LEN) {
		warnx("%s: size mismatch (%d != %d)", __func__,
		    igmpstat.igps_len, IGPS_VERSION3_LEN);
	}

	printf("%s:\n", name);

#define	p64(f, m) if (igmpstat.f || sflag <= 1) \
    printf(m, (uintmax_t) igmpstat.f, plural(igmpstat.f))
#define	py64(f, m) if (igmpstat.f || sflag <= 1) \
    printf(m, (uintmax_t) igmpstat.f, pluralies(igmpstat.f))
	p64(igps_rcv_total, "\t%ju message%s received\n");
	p64(igps_rcv_tooshort, "\t%ju message%s received with too few bytes\n");
	p64(igps_rcv_badttl, "\t%ju message%s received with wrong TTL\n");
	p64(igps_rcv_badsum, "\t%ju message%s received with bad checksum\n");
	py64(igps_rcv_v1v2_queries, "\t%ju V1/V2 membership quer%s received\n");
	py64(igps_rcv_v3_queries, "\t%ju V3 membership quer%s received\n");
	py64(igps_rcv_badqueries,
	    "\t%ju membership quer%s received with invalid field(s)\n");
	py64(igps_rcv_gen_queries, "\t%ju general quer%s received\n");
	py64(igps_rcv_group_queries, "\t%ju group quer%s received\n");
	py64(igps_rcv_gsr_queries, "\t%ju group-source quer%s received\n");
	py64(igps_drop_gsr_queries, "\t%ju group-source quer%s dropped\n");
	p64(igps_rcv_reports, "\t%ju membership report%s received\n");
	p64(igps_rcv_badreports,
	    "\t%ju membership report%s received with invalid field(s)\n");
	p64(igps_rcv_ourreports,
"\t%ju membership report%s received for groups to which we belong\n");
        p64(igps_rcv_nora, "\t%ju V3 report%s received without Router Alert\n");
        p64(igps_snd_reports, "\t%ju membership report%s sent\n");
#undef p64
#undef py64
}

/*
 * Dump PIM statistics structure.
 */
void
pim_stats(u_long off __unused, const char *name, int af1 __unused,
    int proto __unused)
{
	struct pimstat pimstat;

	if (fetch_stats("net.inet.pim.stats", off, &pimstat,
	    sizeof(pimstat), kread_counters) != 0)
		return;

	printf("%s:\n", name);

#define	p(f, m) if (pimstat.f || sflag <= 1) \
    printf(m, (uintmax_t)pimstat.f, plural(pimstat.f))
#define	py(f, m) if (pimstat.f || sflag <= 1) \
    printf(m, (uintmax_t)pimstat.f, pimstat.f != 1 ? "ies" : "y")
	p(pims_rcv_total_msgs, "\t%ju message%s received\n");
	p(pims_rcv_total_bytes, "\t%ju byte%s received\n");
	p(pims_rcv_tooshort, "\t%ju message%s received with too few bytes\n");
        p(pims_rcv_badsum, "\t%ju message%s received with bad checksum\n");
	p(pims_rcv_badversion, "\t%ju message%s received with bad version\n");
	p(pims_rcv_registers_msgs, "\t%ju data register message%s received\n");
	p(pims_rcv_registers_bytes, "\t%ju data register byte%s received\n");
	p(pims_rcv_registers_wrongiif,
	    "\t%ju data register message%s received on wrong iif\n");
	p(pims_rcv_badregisters, "\t%ju bad register%s received\n");
	p(pims_snd_registers_msgs, "\t%ju data register message%s sent\n");
	p(pims_snd_registers_bytes, "\t%ju data register byte%s sent\n");
#undef p
#undef py
}

/*
 * Pretty print an Internet address (net address + port).
 */
void
inetprint(struct in_addr *in, int port, const char *proto, int num_port)
{
	struct servent *sp = 0;
	char line[80], *cp;
	int width;

	if (Wflag)
	    sprintf(line, "%s.", inetname(in));
	else
	    sprintf(line, "%.*s.", (Aflag && !num_port) ? 12 : 16, inetname(in));
	cp = strchr(line, '\0');
	if (!num_port && port)
		sp = getservbyport((int)port, proto);
	if (sp || port == 0)
		sprintf(cp, "%.15s ", sp ? sp->s_name : "*");
	else
		sprintf(cp, "%d ", ntohs((u_short)port));
	width = (Aflag && !Wflag) ? 18 : 22;
	if (Wflag)
	    printf("%-*s ", width, line);
	else
	    printf("%-*.*s ", width, width, line);
}

/*
 * Construct an Internet address representation.
 * If numeric_addr has been supplied, give
 * numeric value, otherwise try for symbolic name.
 */
char *
inetname(struct in_addr *inp)
{
	char *cp;
	static char line[MAXHOSTNAMELEN];
	struct hostent *hp;
	struct netent *np;

	cp = 0;
	if (!numeric_addr && inp->s_addr != INADDR_ANY) {
		int net = inet_netof(*inp);
		int lna = inet_lnaof(*inp);

		if (lna == INADDR_ANY) {
			np = getnetbyaddr(net, AF_INET);
			if (np)
				cp = np->n_name;
		}
		if (cp == 0) {
			hp = gethostbyaddr((char *)inp, sizeof (*inp), AF_INET);
			if (hp) {
				cp = hp->h_name;
				trimdomain(cp, strlen(cp));
			}
		}
	}
	if (inp->s_addr == INADDR_ANY)
		strcpy(line, "*");
	else if (cp) {
		strlcpy(line, cp, sizeof(line));
	} else {
		inp->s_addr = ntohl(inp->s_addr);
#define	C(x)	((u_int)((x) & 0xff))
		sprintf(line, "%u.%u.%u.%u", C(inp->s_addr >> 24),
		    C(inp->s_addr >> 16), C(inp->s_addr >> 8), C(inp->s_addr));
	}
	return (line);
}
