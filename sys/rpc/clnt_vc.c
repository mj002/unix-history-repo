/*	$NetBSD: clnt_vc.c,v 1.4 2000/07/14 08:40:42 fvdl Exp $	*/

/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid2 = "@(#)clnt_tcp.c 1.37 87/10/05 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)clnt_tcp.c	2.2 88/08/01 4.0 RPCSRC";
static char sccsid3[] = "@(#)clnt_vc.c 1.19 89/03/16 Copyr 1988 Sun Micro";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
 
/*
 * clnt_tcp.c, Implements a TCP/IP based, client side RPC.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * TCP based RPC supports 'batched calls'.
 * A sequence of calls may be batched-up in a send buffer.  The rpc call
 * return immediately to the client even though the call was not necessarily
 * sent.  The batching occurs if the results' xdr routine is NULL (0) AND
 * the rpc timeout value is zero (see clnt.h, rpc).
 *
 * Clients should NOT casually batch calls that in fact return results; that is,
 * the server side should be aware that a call is batched and not produce any
 * return message.  Batched calls that produce many result messages can
 * deadlock (netlock) the client and the server....
 *
 * Now go hang yourself.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <rpc/rpc.h>
#include <rpc/rpc_com.h>

#define MCALL_MSG_SIZE 24

struct cmessage {
        struct cmsghdr cmsg;
        struct cmsgcred cmcred;
};

static enum clnt_stat clnt_vc_call(CLIENT *, rpcproc_t, xdrproc_t, void *,
    xdrproc_t, void *, struct timeval);
static void clnt_vc_geterr(CLIENT *, struct rpc_err *);
static bool_t clnt_vc_freeres(CLIENT *, xdrproc_t, void *);
static void clnt_vc_abort(CLIENT *);
static bool_t clnt_vc_control(CLIENT *, u_int, void *);
static void clnt_vc_destroy(CLIENT *);
static bool_t time_not_ok(struct timeval *);
static void clnt_vc_soupcall(struct socket *so, void *arg, int waitflag);

static struct clnt_ops clnt_vc_ops = {
	.cl_call =	clnt_vc_call,
	.cl_abort =	clnt_vc_abort,
	.cl_geterr =	clnt_vc_geterr,
	.cl_freeres =	clnt_vc_freeres,
	.cl_destroy =	clnt_vc_destroy,
	.cl_control =	clnt_vc_control
};

/*
 * A pending RPC request which awaits a reply.
 */
struct ct_request {
	TAILQ_ENTRY(ct_request) cr_link;
	uint32_t		cr_xid;		/* XID of request */
	struct mbuf		*cr_mrep;	/* reply received by upcall */
	int			cr_error;	/* any error from upcall */
};

TAILQ_HEAD(ct_request_list, ct_request);

struct ct_data {
	struct mtx	ct_lock;
	struct socket	*ct_socket;	/* connection socket */
	bool_t		ct_closeit;	/* close it on destroy */
	struct timeval	ct_wait;	/* wait interval in milliseconds */
	struct sockaddr_storage	ct_addr; /* remote addr */
	struct rpc_err	ct_error;
	uint32_t	ct_xid;
	char		ct_mcallc[MCALL_MSG_SIZE]; /* marshalled callmsg */
	size_t		ct_mpos;	/* pos after marshal */
	const char	*ct_waitchan;
	int		ct_waitflag;
	struct mbuf	*ct_record;	/* current reply record */
	size_t		ct_record_resid; /* how much left of reply to read */
	bool_t		ct_record_eor;	 /* true if reading last fragment */
	struct ct_request_list ct_pending;
};

static const char clnt_vc_errstr[] = "%s : %s";
static const char clnt_vc_str[] = "clnt_vc_create";
static const char clnt_read_vc_str[] = "read_vc";
static const char __no_mem_str[] = "out of memory";

/*
 * Create a client handle for a connection.
 * Default options are set, which the user can change using clnt_control()'s.
 * The rpc/vc package does buffering similar to stdio, so the client
 * must pick send and receive buffer sizes, 0 => use the default.
 * NB: fd is copied into a private area.
 * NB: The rpch->cl_auth is set null authentication. Caller may wish to
 * set this something more useful.
 *
 * fd should be an open socket
 */
CLIENT *
clnt_vc_create(
	struct socket *so,		/* open file descriptor */
	struct sockaddr *raddr,		/* servers address */
	const rpcprog_t prog,		/* program number */
	const rpcvers_t vers,		/* version number */
	size_t sendsz,			/* buffer recv size */
	size_t recvsz)			/* buffer send size */
{
	CLIENT *cl;			/* client handle */
	struct ct_data *ct = NULL;	/* client handle */
	struct timeval now;
	struct rpc_msg call_msg;
	static uint32_t disrupt;
	struct __rpc_sockinfo si;
	XDR xdrs;
	int error;

	if (disrupt == 0)
		disrupt = (uint32_t)(long)raddr;

	cl = (CLIENT *)mem_alloc(sizeof (*cl));
	ct = (struct ct_data *)mem_alloc(sizeof (*ct));

	mtx_init(&ct->ct_lock, "ct->ct_lock", NULL, MTX_DEF);

	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0) {
		error = soconnect(so, raddr, curthread);
		if (error) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = error;
			goto err;
		}
	}

	if (!__rpc_socket2sockinfo(so, &si))
		goto err;

	ct->ct_closeit = FALSE;

	/*
	 * Set up private data struct
	 */
	ct->ct_socket = so;
	ct->ct_wait.tv_sec = -1;
	ct->ct_wait.tv_usec = -1;
	memcpy(&ct->ct_addr, raddr, raddr->sa_len);

	/*
	 * Initialize call message
	 */
	getmicrotime(&now);
	ct->ct_xid = ((uint32_t)++disrupt) ^ __RPC_GETXID(&now);
	call_msg.rm_xid = ct->ct_xid;
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = (uint32_t)prog;
	call_msg.rm_call.cb_vers = (uint32_t)vers;

	/*
	 * pre-serialize the static part of the call msg and stash it away
	 */
	xdrmem_create(&xdrs, ct->ct_mcallc, MCALL_MSG_SIZE,
	    XDR_ENCODE);
	if (! xdr_callhdr(&xdrs, &call_msg)) {
		if (ct->ct_closeit) {
			soclose(ct->ct_socket);
		}
		goto err;
	}
	ct->ct_mpos = XDR_GETPOS(&xdrs);
	XDR_DESTROY(&xdrs);
	ct->ct_waitchan = "rpcrecv";
	ct->ct_waitflag = 0;

	/*
	 * Create a client handle which uses xdrrec for serialization
	 * and authnone for authentication.
	 */
	cl->cl_ops = &clnt_vc_ops;
	cl->cl_private = ct;
	cl->cl_auth = authnone_create();
	sendsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)sendsz);
	recvsz = __rpc_get_t_size(si.si_af, si.si_proto, (int)recvsz);

	SOCKBUF_LOCK(&ct->ct_socket->so_rcv);
	ct->ct_socket->so_upcallarg = ct;
	ct->ct_socket->so_upcall = clnt_vc_soupcall;
	ct->ct_socket->so_rcv.sb_flags |= SB_UPCALL;
	SOCKBUF_UNLOCK(&ct->ct_socket->so_rcv);

	ct->ct_record = NULL;
	ct->ct_record_resid = 0;
	TAILQ_INIT(&ct->ct_pending);
	return (cl);

err:
	if (cl) {
		if (ct) {
			mem_free(ct, sizeof (struct ct_data));
		}
		if (cl)
			mem_free(cl, sizeof (CLIENT));
	}
	return ((CLIENT *)NULL);
}

static enum clnt_stat
clnt_vc_call(
	CLIENT *cl,
	rpcproc_t proc,
	xdrproc_t xdr_args,
	void *args_ptr,
	xdrproc_t xdr_results,
	void *results_ptr,
	struct timeval utimeout)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;
	XDR xdrs;
	struct rpc_msg reply_msg;
	bool_t ok;
	int nrefreshes = 2;		/* number of times to refresh cred */
	struct timeval timeout;
	uint32_t xid;
	struct mbuf *mreq = NULL;
	struct ct_request cr;
	int error;

	mtx_lock(&ct->ct_lock);

	cr.cr_mrep = NULL;
	cr.cr_error = 0;

	if (ct->ct_wait.tv_usec == -1) {
		timeout = utimeout;	/* use supplied timeout */
	} else {
		timeout = ct->ct_wait;	/* use default timeout */
	}

call_again:
	mtx_assert(&ct->ct_lock, MA_OWNED);

	ct->ct_xid++;
	xid = ct->ct_xid;

	mtx_unlock(&ct->ct_lock);

	/*
	 * Leave space to pre-pend the record mark.
	 */
	MGETHDR(mreq, M_WAIT, MT_DATA);
	MCLGET(mreq, M_WAIT);
	mreq->m_len = 0;
	mreq->m_data += sizeof(uint32_t);
	m_append(mreq, ct->ct_mpos, ct->ct_mcallc);

	/*
	 * The XID is the first thing in the request.
	 */
	*mtod(mreq, uint32_t *) = htonl(xid);

	xdrmbuf_create(&xdrs, mreq, XDR_ENCODE);

	ct->ct_error.re_status = RPC_SUCCESS;

	if ((! XDR_PUTINT32(&xdrs, &proc)) ||
	    (! AUTH_MARSHALL(cl->cl_auth, &xdrs)) ||
	    (! (*xdr_args)(&xdrs, args_ptr))) {
		if (ct->ct_error.re_status == RPC_SUCCESS)
			ct->ct_error.re_status = RPC_CANTENCODEARGS;
		m_freem(mreq);
		return (ct->ct_error.re_status);
	}
	m_fixhdr(mreq);

	/*
	 * Prepend a record marker containing the packet length.
	 */
	M_PREPEND(mreq, sizeof(uint32_t), M_WAIT);
	*mtod(mreq, uint32_t *) =
		htonl(0x80000000 | (mreq->m_pkthdr.len - sizeof(uint32_t)));

	cr.cr_xid = xid;
	mtx_lock(&ct->ct_lock);
	TAILQ_INSERT_TAIL(&ct->ct_pending, &cr, cr_link);
	mtx_unlock(&ct->ct_lock);

	/*
	 * sosend consumes mreq.
	 */
	error = sosend(ct->ct_socket, NULL, NULL, mreq, NULL, 0, curthread);
	mreq = NULL;

	reply_msg.acpted_rply.ar_verf = _null_auth;
	reply_msg.acpted_rply.ar_results.where = results_ptr;
	reply_msg.acpted_rply.ar_results.proc = xdr_results;

	mtx_lock(&ct->ct_lock);

	if (error) {
		TAILQ_REMOVE(&ct->ct_pending, &cr, cr_link);

		ct->ct_error.re_errno = error;
		ct->ct_error.re_status = RPC_CANTSEND;
		goto out;
	}

	/*
	 * Check to see if we got an upcall while waiting for the
	 * lock. In both these cases, the request has been removed
	 * from ct->ct_pending.
	 */
	if (cr.cr_error) {
		ct->ct_error.re_errno = cr.cr_error;
		ct->ct_error.re_status = RPC_CANTRECV;
		goto out;
	}
	if (cr.cr_mrep) {
		goto got_reply;
	}

	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		if (cr.cr_xid)
			TAILQ_REMOVE(&ct->ct_pending, &cr, cr_link);
		ct->ct_error.re_status = RPC_TIMEDOUT;
		goto out;
	}

	error = msleep(&cr, &ct->ct_lock, ct->ct_waitflag, ct->ct_waitchan,
	    tvtohz(&timeout));

	if (error) {
		/*
		 * The sleep returned an error so our request is still
		 * on the list. Turn the error code into an
		 * appropriate client status.
		 */
		if (cr.cr_xid)
			TAILQ_REMOVE(&ct->ct_pending, &cr, cr_link);
		ct->ct_error.re_errno = error;
		switch (error) {
		case EINTR:
			ct->ct_error.re_status = RPC_INTR;
			break;
		case EWOULDBLOCK:
			ct->ct_error.re_status = RPC_TIMEDOUT;
			break;
		default:
			ct->ct_error.re_status = RPC_CANTRECV;
		}
		goto out;
	} else {
		/*
		 * We were woken up by the upcall.  If the
		 * upcall had a receive error, report that,
		 * otherwise we have a reply.
		 */
		if (cr.cr_error) {
			ct->ct_error.re_errno = cr.cr_error;
			ct->ct_error.re_status = RPC_CANTRECV;
			goto out;
		}
	}

got_reply:
	/*
	 * Now decode and validate the response. We need to drop the
	 * lock since xdr_replymsg may end up sleeping in malloc.
	 */
	mtx_unlock(&ct->ct_lock);

	xdrmbuf_create(&xdrs, cr.cr_mrep, XDR_DECODE);
	ok = xdr_replymsg(&xdrs, &reply_msg);
	XDR_DESTROY(&xdrs);
	cr.cr_mrep = NULL;

	mtx_lock(&ct->ct_lock);

	if (ok) {
		if ((reply_msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
			(reply_msg.acpted_rply.ar_stat == SUCCESS))
			ct->ct_error.re_status = RPC_SUCCESS;
		else
			_seterr_reply(&reply_msg, &(ct->ct_error));

		if (ct->ct_error.re_status == RPC_SUCCESS) {
			if (! AUTH_VALIDATE(cl->cl_auth,
					    &reply_msg.acpted_rply.ar_verf)) {
				ct->ct_error.re_status = RPC_AUTHERROR;
				ct->ct_error.re_why = AUTH_INVALIDRESP;
			}
			if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
				xdrs.x_op = XDR_FREE;
				(void) xdr_opaque_auth(&xdrs,
					&(reply_msg.acpted_rply.ar_verf));
			}
		}		/* end successful completion */
		/*
		 * If unsuccesful AND error is an authentication error
		 * then refresh credentials and try again, else break
		 */
		else if (ct->ct_error.re_status == RPC_AUTHERROR)
			/* maybe our credentials need to be refreshed ... */
			if (nrefreshes > 0 &&
			    AUTH_REFRESH(cl->cl_auth, &reply_msg)) {
				nrefreshes--;
				goto call_again;
			}
		/* end of unsuccessful completion */
	}	/* end of valid reply message */
	else {
		ct->ct_error.re_status = RPC_CANTDECODERES;
	}
out:
	mtx_assert(&ct->ct_lock, MA_OWNED);

	if (mreq)
		m_freem(mreq);
	if (cr.cr_mrep)
		m_freem(cr.cr_mrep);

	mtx_unlock(&ct->ct_lock);
	return (ct->ct_error.re_status);
}

static void
clnt_vc_geterr(CLIENT *cl, struct rpc_err *errp)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;

	*errp = ct->ct_error;
}

static bool_t
clnt_vc_freeres(CLIENT *cl, xdrproc_t xdr_res, void *res_ptr)
{
	XDR xdrs;
	bool_t dummy;

	xdrs.x_op = XDR_FREE;
	dummy = (*xdr_res)(&xdrs, res_ptr);

	return (dummy);
}

/*ARGSUSED*/
static void
clnt_vc_abort(CLIENT *cl)
{
}

static bool_t
clnt_vc_control(CLIENT *cl, u_int request, void *info)
{
	struct ct_data *ct = (struct ct_data *)cl->cl_private;
	void *infop = info;

	mtx_lock(&ct->ct_lock);

	switch (request) {
	case CLSET_FD_CLOSE:
		ct->ct_closeit = TRUE;
		mtx_unlock(&ct->ct_lock);
		return (TRUE);
	case CLSET_FD_NCLOSE:
		ct->ct_closeit = FALSE;
		mtx_unlock(&ct->ct_lock);
		return (TRUE);
	default:
		break;
	}

	/* for other requests which use info */
	if (info == NULL) {
		mtx_unlock(&ct->ct_lock);
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			mtx_unlock(&ct->ct_lock);
			return (FALSE);
		}
		ct->ct_wait = *(struct timeval *)infop;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)infop = ct->ct_wait;
		break;
	case CLGET_SERVER_ADDR:
		(void) memcpy(info, &ct->ct_addr, (size_t)ct->ct_addr.ss_len);
		break;
	case CLGET_SVC_ADDR:
		/*
		 * Slightly different semantics to userland - we use
		 * sockaddr instead of netbuf.
		 */
		memcpy(info, &ct->ct_addr, ct->ct_addr.ss_len);
		break;
	case CLSET_SVC_ADDR:		/* set to new address */
		mtx_unlock(&ct->ct_lock);
		return (FALSE);
	case CLGET_XID:
		*(uint32_t *)info = ct->ct_xid;
		break;
	case CLSET_XID:
		/* This will set the xid of the NEXT call */
		/* decrement by 1 as clnt_vc_call() increments once */
		ct->ct_xid = *(uint32_t *)info - 1;
		break;
	case CLGET_VERS:
		/*
		 * This RELIES on the information that, in the call body,
		 * the version number field is the fifth field from the
		 * begining of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(uint32_t *)info =
		    ntohl(*(uint32_t *)(void *)(ct->ct_mcallc +
		    4 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_VERS:
		*(uint32_t *)(void *)(ct->ct_mcallc +
		    4 * BYTES_PER_XDR_UNIT) =
		    htonl(*(uint32_t *)info);
		break;

	case CLGET_PROG:
		/*
		 * This RELIES on the information that, in the call body,
		 * the program number field is the fourth field from the
		 * begining of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(uint32_t *)info =
		    ntohl(*(uint32_t *)(void *)(ct->ct_mcallc +
		    3 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_PROG:
		*(uint32_t *)(void *)(ct->ct_mcallc +
		    3 * BYTES_PER_XDR_UNIT) =
		    htonl(*(uint32_t *)info);
		break;

	case CLSET_WAITCHAN:
		ct->ct_waitchan = *(const char **)info;
		break;

	case CLGET_WAITCHAN:
		*(const char **) info = ct->ct_waitchan;
		break;

	case CLSET_INTERRUPTIBLE:
		if (*(int *) info)
			ct->ct_waitflag = PCATCH;
		else
			ct->ct_waitflag = 0;
		break;

	case CLGET_INTERRUPTIBLE:
		if (ct->ct_waitflag)
			*(int *) info = TRUE;
		else
			*(int *) info = FALSE;
		break;

	default:
		mtx_unlock(&ct->ct_lock);
		return (FALSE);
	}

	mtx_unlock(&ct->ct_lock);
	return (TRUE);
}

static void
clnt_vc_destroy(CLIENT *cl)
{
	struct ct_data *ct = (struct ct_data *) cl->cl_private;
	struct socket *so = NULL;

	mtx_lock(&ct->ct_lock);

	if (ct->ct_socket) {
		SOCKBUF_LOCK(&ct->ct_socket->so_rcv);
		ct->ct_socket->so_upcallarg = NULL;
		ct->ct_socket->so_upcall = NULL;
		ct->ct_socket->so_rcv.sb_flags &= ~SB_UPCALL;
		SOCKBUF_UNLOCK(&ct->ct_socket->so_rcv);

		KASSERT(!TAILQ_FIRST(&ct->ct_pending),
		    ("Destroying RPC client with pending RPC requests"));

		if (ct->ct_closeit) {
			so = ct->ct_socket;
		}
	}

	mtx_unlock(&ct->ct_lock);

	mtx_destroy(&ct->ct_lock);
	if (so) {
		soshutdown(so, SHUT_WR);
		soclose(so);
	}
	mem_free(ct, sizeof(struct ct_data));
	mem_free(cl, sizeof(CLIENT));
}

/*
 * Make sure that the time is not garbage.   -1 value is disallowed.
 * Note this is different from time_not_ok in clnt_dg.c
 */
static bool_t
time_not_ok(struct timeval *t)
{
	return (t->tv_sec <= -1 || t->tv_sec > 100000000 ||
		t->tv_usec <= -1 || t->tv_usec > 1000000);
}

void
clnt_vc_soupcall(struct socket *so, void *arg, int waitflag)
{
	struct ct_data *ct = (struct ct_data *) arg;
	struct uio uio;
	struct mbuf *m;
	struct ct_request *cr;
	int error, rcvflag, foundreq;
	uint32_t xid, header;

	uio.uio_td = curthread;
	do {
		/*
		 * If ct_record_resid is zero, we are waiting for a
		 * record mark.
		 */
		if (ct->ct_record_resid == 0) {
			bool_t do_read;

			/*
			 * Make sure there is either a whole record
			 * mark in the buffer or there is some other
			 * error condition
			 */
			do_read = FALSE;
			SOCKBUF_LOCK(&so->so_rcv);
			if (so->so_rcv.sb_cc >= sizeof(uint32_t)
			    || (so->so_rcv.sb_state & SBS_CANTRCVMORE)
			    || so->so_error)
				do_read = TRUE;
			SOCKBUF_UNLOCK(&so->so_rcv);

			if (!do_read)
				return;

			uio.uio_resid = sizeof(uint32_t);
			m = NULL;
			rcvflag = MSG_DONTWAIT | MSG_SOCALLBCK;
			error = soreceive(so, NULL, &uio, &m, NULL, &rcvflag);

			if (error == EWOULDBLOCK)
				break;
			
			/*
			 * If there was an error, wake up all pending
			 * requests.
			 */
			if (error || uio.uio_resid > 0) {
			wakeup_all:
				mtx_lock(&ct->ct_lock);
				if (!error) {
					/*
					 * We must have got EOF trying
					 * to read from the stream.
					 */
					error = ECONNRESET;
				}
				ct->ct_error.re_status = RPC_CANTRECV;
				ct->ct_error.re_errno = error;
				TAILQ_FOREACH(cr, &ct->ct_pending, cr_link) {
					cr->cr_error = error;
					wakeup(cr);
				}
				TAILQ_INIT(&ct->ct_pending);
				mtx_unlock(&ct->ct_lock);
				break;
			}
			memcpy(&header, mtod(m, uint32_t *), sizeof(uint32_t));
			header = ntohl(header);
			ct->ct_record = NULL;
			ct->ct_record_resid = header & 0x7fffffff;
			ct->ct_record_eor = ((header & 0x80000000) != 0);
			m_freem(m);
		} else {
			/*
			 * We have the record mark. Read as much as
			 * the socket has buffered up to the end of
			 * this record.
			 */
			uio.uio_resid = ct->ct_record_resid;
			m = NULL;
			rcvflag = MSG_DONTWAIT | MSG_SOCALLBCK;
			error = soreceive(so, NULL, &uio, &m, NULL, &rcvflag);

			if (error == EWOULDBLOCK)
				break;

			if (error || uio.uio_resid == ct->ct_record_resid)
				goto wakeup_all;

			/*
			 * If we have part of the record already,
			 * chain this bit onto the end.
			 */
			if (ct->ct_record)
				m_last(ct->ct_record)->m_next = m;
			else
				ct->ct_record = m;

			ct->ct_record_resid = uio.uio_resid;

			/*
			 * If we have the entire record, see if we can
			 * match it to a request.
			 */
			if (ct->ct_record_resid == 0
			    && ct->ct_record_eor) {
				/*
				 * The XID is in the first uint32_t of
				 * the reply.
				 */
				ct->ct_record =
					m_pullup(ct->ct_record, sizeof(xid));
				if (!ct->ct_record)
					break;
				memcpy(&xid,
				    mtod(ct->ct_record, uint32_t *),
				    sizeof(uint32_t));
				xid = ntohl(xid);

				mtx_lock(&ct->ct_lock);
				foundreq = 0;
				TAILQ_FOREACH(cr, &ct->ct_pending, cr_link) {
					if (cr->cr_xid == xid) {
						/*
						 * This one
						 * matches. We snip it
						 * out of the pending
						 * list and leave the
						 * reply mbuf in
						 * cr->cr_mrep. Set
						 * the XID to zero so
						 * that clnt_vc_call
						 * can know not to
						 * repeat the
						 * TAILQ_REMOVE.
						 */
						TAILQ_REMOVE(&ct->ct_pending,
						    cr, cr_link);
						cr->cr_xid = 0;
						cr->cr_mrep = ct->ct_record;
						cr->cr_error = 0;
						foundreq = 1;
						wakeup(cr);
						break;
					}
				}
				mtx_unlock(&ct->ct_lock);

				if (!foundreq)
					m_freem(ct->ct_record);
				ct->ct_record = NULL;
			}
		}
	} while (m);
}
