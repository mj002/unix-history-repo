/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Herb Hasler and Rick Macklem at The University of Guelph.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1989 Regents of the University of California.\n\
 All rights reserved.\n";
#endif not lint

#ifndef lint
static char sccsid[] = "@(#)mountd.c	5.23 (Berkeley) %G%";
#endif not lint

#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syslog.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#ifdef ISO
#include <netiso/iso.h>
#endif
#include <nfs/rpcv2.h>
#include <nfs/nfsv2.h>
#include "pathnames.h"
#ifdef DEBUG
#include <stdarg.h>
#endif

/*
 * Structures for keeping the mount list and export list
 */
struct mountlist {
	struct mountlist *ml_next;
	char	ml_host[RPCMNT_NAMELEN+1];
	char	ml_dirp[RPCMNT_PATHLEN+1];
};

struct dirlist {
	struct dirlist	*dp_left;
	struct dirlist	*dp_right;
	int		dp_flag;
	struct hostlist	*dp_hosts;	/* List of hosts this dir exported to */
	char		dp_dirp[1];	/* Actually malloc'd to size of dir */
};
/* dp_flag bits */
#define	DP_DEFSET	0x1

struct exportlist {
	struct exportlist *ex_next;
	struct dirlist	*ex_dirl;
	struct dirlist	*ex_defdir;
	int		ex_flag;
	fsid_t		ex_fs;
	char		*ex_fsdir;
};
/* ex_flag bits */
#define	EX_LINKED	0x1

struct netmsk {
	u_long	nt_net;
	u_long	nt_mask;
	char *nt_name;
};

union grouptypes {
	struct hostent *gt_hostent;
	struct netmsk	gt_net;
#ifdef ISO
	struct sockaddr_iso *gt_isoaddr;
#endif
};

struct grouplist {
	int gr_type;
	union grouptypes gr_ptr;
	struct grouplist *gr_next;
};
/* Group types */
#define	GT_NULL		0x0
#define	GT_HOST		0x1
#define	GT_NET		0x2
#define	GT_ISO		0x4

struct hostlist {
	struct grouplist *ht_grp;
	struct hostlist	 *ht_next;
};

/* Global defs */
int mntsrv(), umntall_each(), xdr_fhs(), xdr_mlist(), xdr_dir(), xdr_explist();
void get_exportlist(), send_umntall(), nextfield(), out_of_mem();
void get_mountlist(), add_mlist(), del_mlist(), free_exp(), free_grp();
void getexp_err(), hang_dirp(), add_dlist(), free_dir(), free_host();
void setnetgrent(), endnetgrent();
struct exportlist *ex_search(), *get_exp();
struct grouplist *get_grp();
char *realpath(), *add_expdir();
struct in_addr inet_makeaddr();
char *inet_ntoa();
struct dirlist *dirp_search();
struct hostlist *get_ht();
#ifdef ISO
struct iso_addr *iso_addr();
#endif
struct exportlist *exphead;
struct mountlist *mlhead;
struct grouplist *grphead;
char exname[MAXPATHLEN];
struct ucred def_anon = {
	(u_short) 1,
	(uid_t) -2,
	1,
	(gid_t) -2,
};
int root_only = 1;
int opt_flags;
/* Bits for above */
#define	OP_MAPROOT	0x01
#define	OP_MAPALL	0x02
#define	OP_KERB		0x04
#define	OP_MASK		0x08
#define	OP_NET		0x10
#define	OP_ISO		0x20
#define	OP_ALLDIRS	0x40

extern int errno;
#ifdef DEBUG
int debug = 1;
void	SYSLOG __P((int, const char *, ...));
#define syslog SYSLOG
#else
int debug = 0;
#endif

/*
 * Mountd server for NFS mount protocol as described in:
 * NFS: Network File System Protocol Specification, RFC1094, Appendix A
 * The optional arguments are the exports file name
 * default: _PATH_EXPORTS
 * and "-n" to allow nonroot mount.
 */
main(argc, argv)
	int argc;
	char **argv;
{
	SVCXPRT *transp;
	int c;
	extern int optind;
	extern char *optarg;

	while ((c = getopt(argc, argv, "n")) != EOF)
		switch (c) {
		case 'n':
			root_only = 0;
			break;
		default:
			fprintf(stderr, "Usage: mountd [-n] [export_file]\n");
			exit(1);
		};
	argc -= optind;
	argv += optind;
	grphead = (struct grouplist *)0;
	exphead = (struct exportlist *)0;
	mlhead = (struct mountlist *)0;
	if (argc == 1) {
		strncpy(exname, *argv, MAXPATHLEN-1);
		exname[MAXPATHLEN-1] = '\0';
	} else
		strcpy(exname, _PATH_EXPORTS);
	openlog("mountd:", LOG_PID, LOG_DAEMON);
	if (debug)
		fprintf(stderr,"Getting export list.\n");
	get_exportlist();
	if (debug)
		fprintf(stderr,"Getting mount list.\n");
	get_mountlist();
	if (debug)
		fprintf(stderr,"Here we go.\n");
	if (debug == 0) {
		daemon(0, 0);
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
	}
	signal(SIGHUP, get_exportlist);
	signal(SIGTERM, send_umntall);
	{ FILE *pidfile = fopen(_PATH_MOUNTDPID, "w");
	  if (pidfile != NULL) {
		fprintf(pidfile, "%d\n", getpid());
		fclose(pidfile);
	  }
	}
	if ((transp = svcudp_create(RPC_ANYSOCK)) == NULL) {
		syslog(LOG_ERR, "Can't create socket");
		exit(1);
	}
	pmap_unset(RPCPROG_MNT, RPCMNT_VER1);
	if (!svc_register(transp, RPCPROG_MNT, RPCMNT_VER1, mntsrv,
	    IPPROTO_UDP)) {
		syslog(LOG_ERR, "Can't register mount");
		exit(1);
	}
	svc_run();
	syslog(LOG_ERR, "Mountd died");
	exit(1);
}

/*
 * The mount rpc service
 */
mntsrv(rqstp, transp)
	register struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	register struct exportlist *ep;
	register struct dirlist *dp;
	nfsv2fh_t nfh;
	struct authunix_parms *ucr;
	struct stat stb;
	struct statfs fsb;
	struct hostent *hp;
	u_long saddr;
	char rpcpath[RPCMNT_PATHLEN+1], dirpath[MAXPATHLEN];
	int bad = ENOENT, omask, defset;
	uid_t uid = -2;

	/* Get authorization */
	switch (rqstp->rq_cred.oa_flavor) {
	case AUTH_UNIX:
		ucr = (struct authunix_parms *)rqstp->rq_clntcred;
		uid = ucr->aup_uid;
		break;
	case AUTH_NULL:
	default:
		break;
	}

	saddr = transp->xp_raddr.sin_addr.s_addr;
	hp = (struct hostent *)0;
	switch (rqstp->rq_proc) {
	case NULLPROC:
		if (!svc_sendreply(transp, xdr_void, (caddr_t)0))
			syslog(LOG_ERR, "Can't send reply");
		return;
	case RPCMNT_MOUNT:
		if ((uid != 0 && root_only) || uid == -2) {
			svcerr_weakauth(transp);
			return;
		}
		if (!svc_getargs(transp, xdr_dir, rpcpath)) {
			svcerr_decode(transp);
			return;
		}

		/*
		 * Get the real pathname and make sure it is a directory
		 * that exists.
		 */
		if (realpath(rpcpath, dirpath) == 0 ||
		    stat(dirpath, &stb) < 0 ||
		    (stb.st_mode & S_IFMT) != S_IFDIR ||
		    statfs(dirpath, &fsb) < 0) {
			chdir("/");	/* Just in case realpath doesn't */
			if (debug)
				fprintf(stderr, "stat failed on %s\n", dirpath);
			if (!svc_sendreply(transp, xdr_long, (caddr_t)&bad))
				syslog(LOG_ERR, "Can't send reply");
			return;
		}

		/* Check in the exports list */
		omask = sigblock(sigmask(SIGHUP));
		ep = ex_search(&fsb.f_fsid);
		defset = 0;
		if (ep && (chk_host(ep->ex_defdir, saddr, &defset) ||
		    ((dp = dirp_search(ep->ex_dirl, dirpath)) &&
		     chk_host(dp, saddr, &defset)) ||
		     (defset && scan_tree(ep->ex_defdir, saddr) == 0 &&
		      scan_tree(ep->ex_dirl, saddr) == 0))) {
			/* Get the file handle */
			bzero((caddr_t)&nfh, sizeof(nfh));
			if (getfh(dirpath, (fhandle_t *)&nfh) < 0) {
				bad = errno;
				syslog(LOG_ERR, "Can't get fh for %s", dirpath);
				if (!svc_sendreply(transp, xdr_long,
				    (caddr_t)&bad))
					syslog(LOG_ERR, "Can't send reply");
				sigsetmask(omask);
				return;
			}
			if (!svc_sendreply(transp, xdr_fhs, (caddr_t)&nfh))
				syslog(LOG_ERR, "Can't send reply");
			if (hp == NULL)
				hp = gethostbyaddr((caddr_t)&saddr,
				    sizeof(saddr), AF_INET);
			if (hp)
				add_mlist(hp->h_name, dirpath);
			else
				add_mlist(inet_ntoa(transp->xp_raddr.sin_addr),
					dirpath);
			if (debug)
				fprintf(stderr,"Mount successfull.\n");
		} else {
			bad = EACCES;
			if (!svc_sendreply(transp, xdr_long, (caddr_t)&bad))
				syslog(LOG_ERR, "Can't send reply");
		}
		sigsetmask(omask);
		return;
	case RPCMNT_DUMP:
		if (!svc_sendreply(transp, xdr_mlist, (caddr_t)0))
			syslog(LOG_ERR, "Can't send reply");
		return;
	case RPCMNT_UMOUNT:
		if ((uid != 0 && root_only) || uid == -2) {
			svcerr_weakauth(transp);
			return;
		}
		if (!svc_getargs(transp, xdr_dir, dirpath)) {
			svcerr_decode(transp);
			return;
		}
		if (!svc_sendreply(transp, xdr_void, (caddr_t)0))
			syslog(LOG_ERR, "Can't send reply");
		hp = gethostbyaddr((caddr_t)&saddr, sizeof(saddr), AF_INET);
		if (hp)
			del_mlist(hp->h_name, dirpath);
		del_mlist(inet_ntoa(transp->xp_raddr.sin_addr), dirpath);
		return;
	case RPCMNT_UMNTALL:
		if ((uid != 0 && root_only) || uid == -2) {
			svcerr_weakauth(transp);
			return;
		}
		if (!svc_sendreply(transp, xdr_void, (caddr_t)0))
			syslog(LOG_ERR, "Can't send reply");
		hp = gethostbyaddr((caddr_t)&saddr, sizeof(saddr), AF_INET);
		if (hp)
			del_mlist(hp->h_name, (char *)0);
		del_mlist(inet_ntoa(transp->xp_raddr.sin_addr), (char *)0);
		return;
	case RPCMNT_EXPORT:
		if (!svc_sendreply(transp, xdr_explist, (caddr_t)0))
			syslog(LOG_ERR, "Can't send reply");
		return;
	default:
		svcerr_noproc(transp);
		return;
	}
}

/*
 * Xdr conversion for a dirpath string
 */
xdr_dir(xdrsp, dirp)
	XDR *xdrsp;
	char *dirp;
{
	return (xdr_string(xdrsp, &dirp, RPCMNT_PATHLEN));
}

/*
 * Xdr routine to generate fhstatus
 */
xdr_fhs(xdrsp, nfh)
	XDR *xdrsp;
	nfsv2fh_t *nfh;
{
	int ok = 0;

	if (!xdr_long(xdrsp, &ok))
		return (0);
	return (xdr_opaque(xdrsp, (caddr_t)nfh, NFSX_FH));
}

xdr_mlist(xdrsp, cp)
	XDR *xdrsp;
	caddr_t cp;
{
	register struct mountlist *mlp;
	int true = 1;
	int false = 0;
	char *strp;

	mlp = mlhead;
	while (mlp) {
		if (!xdr_bool(xdrsp, &true))
			return (0);
		strp = &mlp->ml_host[0];
		if (!xdr_string(xdrsp, &strp, RPCMNT_NAMELEN))
			return (0);
		strp = &mlp->ml_dirp[0];
		if (!xdr_string(xdrsp, &strp, RPCMNT_PATHLEN))
			return (0);
		mlp = mlp->ml_next;
	}
	if (!xdr_bool(xdrsp, &false))
		return (0);
	return (1);
}

/*
 * Xdr conversion for export list
 */
xdr_explist(xdrsp, cp)
	XDR *xdrsp;
	caddr_t cp;
{
	register struct exportlist *ep;
	int false = 0;
	int omask;

	omask = sigblock(sigmask(SIGHUP));
	ep = exphead;
	while (ep) {
		if (put_exlist(ep->ex_dirl, xdrsp, ep->ex_defdir))
			goto errout;
		ep = ep->ex_next;
	}
	sigsetmask(omask);
	if (!xdr_bool(xdrsp, &false))
		return (0);
	return (1);
errout:
	sigsetmask(omask);
	return (0);
}

/*
 * Called from xdr_explist() to traverse the tree and export the
 * directory paths.
 */
put_exlist(dp, xdrsp, adp)
	register struct dirlist *dp;
	XDR *xdrsp;
	struct dirlist *adp;
{
	register struct grouplist *grp;
	register struct hostlist *hp;
	struct in_addr inaddr;
	int true = 1;
	int false = 0;
	int gotalldir = 0;
	char *strp;

	if (dp) {
		if (put_exlist(dp->dp_left, xdrsp, adp))
			return (1);
		if (!xdr_bool(xdrsp, &true))
			return (1);
		strp = dp->dp_dirp;
		if (!xdr_string(xdrsp, &strp, RPCMNT_PATHLEN))
			return (1);
		if (adp && !strcmp(dp->dp_dirp, adp->dp_dirp))
			gotalldir = 1;
		if ((dp->dp_flag & DP_DEFSET) == 0 &&
		    (gotalldir == 0 || (adp->dp_flag & DP_DEFSET) == 0)) {
			hp = dp->dp_hosts;
			while (hp) {
				grp = hp->ht_grp;
				if (grp->gr_type == GT_HOST) {
					if (!xdr_bool(xdrsp, &true))
						return (1);
					strp = grp->gr_ptr.gt_hostent->h_name;
					if (!xdr_string(xdrsp, &strp, 
					    RPCMNT_NAMELEN))
						return (1);
				} else if (grp->gr_type == GT_NET) {
					if (!xdr_bool(xdrsp, &true))
						return (1);
					strp = grp->gr_ptr.gt_net.nt_name;
					if (!xdr_string(xdrsp, &strp, 
					    RPCMNT_NAMELEN))
						return (1);
				}
				hp = hp->ht_next;
				if (gotalldir && hp == (struct hostlist *)0) {
					hp = adp->dp_hosts;
					gotalldir = 0;
				}
			}
		}
		if (!xdr_bool(xdrsp, &false))
			return (1);
		if (put_exlist(dp->dp_right, xdrsp, adp))
			return (1);
	}
	return (0);
}

#define LINESIZ	10240
char line[LINESIZ];
FILE *exp_file;

/*
 * Get the export list
 */
void
get_exportlist()
{
	register struct exportlist *ep, *ep2;
	register struct grouplist *grp, *tgrp;
	struct exportlist **epp;
	struct dirlist *dirhead;
	struct stat sb;
	struct statfs fsb, *fsp;
	struct hostent *hpe;
	struct ucred anon;
	struct ufs_args targs;
	char *cp, *endcp, *dirp, *hst, *usr, *dom, savedc;
	int len, has_host, exflags, got_nondir, dirplen, num, i, netgrp;

	/*
	 * First, get rid of the old list
	 */
	ep = exphead;
	while (ep) {
		ep2 = ep;
		ep = ep->ex_next;
		free_exp(ep2);
	}
	exphead = (struct exportlist *)0;

	grp = grphead;
	while (grp) {
		tgrp = grp;
		grp = grp->gr_next;
		free_grp(tgrp);
	}
	grphead = (struct grouplist *)0;

	/*
	 * And delete exports that are in the kernel for all local
	 * file systems.
	 * XXX: Should know how to handle all local exportable file systems
	 *      instead of just MOUNT_UFS.
	 */
	num = getmntinfo(&fsp, MNT_NOWAIT);
	for (i = 0; i < num; i++) {
		if (fsp->f_type == MOUNT_UFS) {
			targs.fspec = (char *)0;
			targs.exflags = MNT_DELEXPORT;
			if (mount(fsp->f_type, fsp->f_mntonname,
			    fsp->f_flags | MNT_UPDATE, (caddr_t)&targs) < 0)
				syslog(LOG_ERR, "Can't del exports %s",
				       fsp->f_mntonname);
		}
		fsp++;
	}

	/*
	 * Read in the exports file and build the list, calling
	 * mount() as we go along to push the export rules into the kernel.
	 */
	if ((exp_file = fopen(exname, "r")) == NULL) {
		syslog(LOG_ERR, "Can't open %s", exname);
		exit(2);
	}
	dirhead = (struct dirlist *)0;
	while (get_line()) {
		if (debug)
			fprintf(stderr,"Got line %s\n",line);
		cp = line;
		nextfield(&cp, &endcp);
		if (*cp == '#')
			goto nextline;

		/*
		 * Set defaults.
		 */
		has_host = FALSE;
		anon = def_anon;
		exflags = MNT_EXPORTED;
		got_nondir = 0;
		opt_flags = 0;
		ep = (struct exportlist *)0;

		/*
		 * Create new exports list entry
		 */
		len = endcp-cp;
		tgrp = grp = get_grp();
		while (len > 0) {
			if (len > RPCMNT_NAMELEN) {
			    getexp_err(ep, tgrp);
			    goto nextline;
			}
			if (*cp == '-') {
			    if (ep == (struct exportlist *)0) {
				getexp_err(ep, tgrp);
				goto nextline;
			    }
			    if (debug)
				fprintf(stderr, "doing opt %s\n", cp);
			    got_nondir = 1;
			    if (do_opt(&cp, &endcp, ep, grp, &has_host,
				&exflags, &anon)) {
				getexp_err(ep, tgrp);
				goto nextline;
			    }
			} else if (*cp == '/') {
			    savedc = *endcp;
			    *endcp = '\0';
			    if (stat(cp, &sb) >= 0 &&
				(sb.st_mode & S_IFMT) == S_IFDIR &&
				statfs(cp, &fsb) >= 0) {
				if (got_nondir) {
				    syslog(LOG_ERR, "Dirs must be first");
				    getexp_err(ep, tgrp);
				    goto nextline;
				}
				if (ep) {
				    if (ep->ex_fs.val[0] != fsb.f_fsid.val[0] ||
					ep->ex_fs.val[1] != fsb.f_fsid.val[1]) {
					getexp_err(ep, tgrp);
					goto nextline;
				    }
				} else {
				    /*
				     * See if this directory is already
				     * in the list.
				     */
				    ep = ex_search(&fsb.f_fsid);
				    if (ep == (struct exportlist *)0) {
					ep = get_exp();
					ep->ex_fs = fsb.f_fsid;
					ep->ex_fsdir = (char *)
					    malloc(strlen(fsb.f_mntonname) + 1);
					if (ep->ex_fsdir)
					    strcpy(ep->ex_fsdir,
						fsb.f_mntonname);
					else
					    out_of_mem();
					if (debug)
					  fprintf(stderr,
					      "Making new ep fs=0x%x,0x%x\n",
					      fsb.f_fsid.val[0],
					      fsb.f_fsid.val[1]);
				    } else if (debug)
					fprintf(stderr,
					    "Found ep fs=0x%x,0x%x\n",
					    fsb.f_fsid.val[0],
					    fsb.f_fsid.val[1]);
				}

				/*
				 * Add dirpath to export mount point.
				 */
				dirp = add_expdir(&dirhead, cp, len);
				dirplen = len;
			    } else {
				getexp_err(ep, tgrp);
				goto nextline;
			    }
			    *endcp = savedc;
			} else {
			    savedc = *endcp;
			    *endcp = '\0';
			    got_nondir = 1;
			    if (ep == (struct exportlist *)0) {
				getexp_err(ep, tgrp);
				goto nextline;
			    }

			    /*
			     * Get the host or netgroup.
			     */
			    setnetgrent(cp);
			    netgrp = getnetgrent(&hst, &usr, &dom);
			    do {
				if (has_host) {
				    grp->gr_next = get_grp();
				    grp = grp->gr_next;
				}
				if (netgrp) {
				    if (get_host(hst, grp)) {
					syslog(LOG_ERR, "Bad netgroup %s", cp);
					getexp_err(ep, tgrp);
					goto nextline;
				    }
				} else if (get_host(cp, grp)) {
				    getexp_err(ep, tgrp);
				    goto nextline;
				}
				has_host = TRUE;
			    } while (netgrp && getnetgrent(&hst, &usr, &dom));
			    endnetgrent();
			    *endcp = savedc;
			}
			cp = endcp;
			nextfield(&cp, &endcp);
			len = endcp - cp;
		}
		if (check_options(dirhead)) {
			getexp_err(ep, tgrp);
			goto nextline;
		}
		if (!has_host) {
			grp->gr_type = GT_HOST;
			if (debug)
				fprintf(stderr,"Adding a default entry\n");
			/* add a default group and make the grp list NULL */
			hpe = (struct hostent *)malloc(sizeof(struct hostent));
			if (hpe == (struct hostent *)0)
				out_of_mem();
			hpe->h_name = "Default";
			hpe->h_addrtype = AF_INET;
			hpe->h_length = sizeof (u_long);
			hpe->h_addr_list = (char **)0;
			grp->gr_ptr.gt_hostent = hpe;

		/*
		 * Don't allow a network export coincide with a list of
		 * host(s) on the same line.
		 */
		} else if ((opt_flags & OP_NET) && tgrp->gr_next) {
			getexp_err(ep, tgrp);
			goto nextline;
		}

		/*
		 * Loop through hosts, pushing the exports into the kernel.
		 * After loop, tgrp points to the start of the list and
		 * grp points to the last entry in the list.
		 */
		grp = tgrp;
		do {
		    if (do_mount(ep, grp, exflags, &anon, dirp,
			dirplen, &fsb)) {
			getexp_err(ep, tgrp);
			goto nextline;
		    }
		} while (grp->gr_next && (grp = grp->gr_next));

		/*
		 * Success. Update the data structures.
		 */
		if (has_host) {
			hang_dirp(dirhead, tgrp, ep, (opt_flags & OP_ALLDIRS));
			grp->gr_next = grphead;
			grphead = tgrp;
		} else {
			hang_dirp(dirhead, (struct grouplist *)0, ep,
			(opt_flags & OP_ALLDIRS));
			free_grp(grp);
		}
		dirhead = (struct dirlist *)0;
		if ((ep->ex_flag & EX_LINKED) == 0) {
			ep2 = exphead;
			epp = &exphead;

			/*
			 * Insert in the list in alphabetical order.
			 */
			while (ep2 && strcmp(ep2->ex_fsdir, ep->ex_fsdir) < 0) {
				epp = &ep2->ex_next;
				ep2 = ep2->ex_next;
			}
			if (ep2)
				ep->ex_next = ep2;
			*epp = ep;
			ep->ex_flag |= EX_LINKED;
		}
nextline:
		if (dirhead) {
			free_dir(dirhead);
			dirhead = (struct dirlist *)0;
		}
	}
	fclose(exp_file);
}

/*
 * Allocate an export list element
 */
struct exportlist *
get_exp()
{
	register struct exportlist *ep;

	ep = (struct exportlist *)malloc(sizeof (struct exportlist));
	if (ep == (struct exportlist *)0)
		out_of_mem();
	bzero((caddr_t)ep, sizeof (struct exportlist));
	return (ep);
}

/*
 * Allocate a group list element
 */
struct grouplist *
get_grp()
{
	register struct grouplist *gp;

	gp = (struct grouplist *)malloc(sizeof (struct grouplist));
	if (gp == (struct grouplist *)0)
		out_of_mem();
	bzero((caddr_t)gp, sizeof (struct grouplist));
	return (gp);
}

/*
 * Clean up upon an error in get_exportlist().
 */
void
getexp_err(ep, grp)
	struct exportlist *ep;
	struct grouplist *grp;
{
	struct grouplist *tgrp;

	syslog(LOG_ERR, "Bad exports list line %s", line);
	if (ep && ep->ex_next == (struct exportlist *)0)
		free_exp(ep);
	while (grp) {
		tgrp = grp;
		grp = grp->gr_next;
		free_grp(tgrp);
	}
}

/*
 * Search the export list for a matching fs.
 */
struct exportlist *
ex_search(fsid)
	fsid_t *fsid;
{
	register struct exportlist *ep;

	ep = exphead;
	while (ep) {
		if (ep->ex_fs.val[0] == fsid->val[0] &&
		    ep->ex_fs.val[1] == fsid->val[1])
			return (ep);
		ep = ep->ex_next;
	}
	return (ep);
}

/*
 * Add a directory path to the list.
 */
char *
add_expdir(dpp, cp, len)
	struct dirlist **dpp;
	char *cp;
	int len;
{
	register struct dirlist *dp;

	dp = (struct dirlist *)malloc(sizeof (struct dirlist) + len);
	dp->dp_left = *dpp;
	dp->dp_right = (struct dirlist *)0;
	dp->dp_flag = 0;
	dp->dp_hosts = (struct hostlist *)0;
	strcpy(dp->dp_dirp, cp);
	*dpp = dp;
	return (dp->dp_dirp);
}

/*
 * Hang the dir list element off the dirpath binary tree as required
 * and update the entry for host.
 */
void
hang_dirp(dp, grp, ep, alldirs)
	register struct dirlist *dp;
	struct grouplist *grp;
	struct exportlist *ep;
	int alldirs;
{
	register struct hostlist *hp;
	struct dirlist *dp2;

	if (alldirs) {
		if (ep->ex_defdir)
			free((caddr_t)dp);
		else
			ep->ex_defdir = dp;
		if (grp == (struct grouplist *)0)
			ep->ex_defdir->dp_flag |= DP_DEFSET;
		else while (grp) {
			hp = get_ht();
			hp->ht_grp = grp;
			hp->ht_next = ep->ex_defdir->dp_hosts;
			ep->ex_defdir->dp_hosts = hp;
			grp = grp->gr_next;
		}
	} else {

		/*
		 * Loop throught the directories adding them to the tree.
		 */
		while (dp) {
			dp2 = dp->dp_left;
			add_dlist(&ep->ex_dirl, dp, grp);
			dp = dp2;
		}
	}
}

/*
 * Traverse the binary tree either updating a node that is already there
 * for the new directory or adding the new node.
 */
void
add_dlist(dpp, newdp, grp)
	struct dirlist **dpp;
	struct dirlist *newdp;
	register struct grouplist *grp;
{
	register struct dirlist *dp;
	register struct hostlist *hp;
	int cmp;

	dp = *dpp;
	if (dp) {
		cmp = strcmp(dp->dp_dirp, newdp->dp_dirp);
		if (cmp > 0) {
			add_dlist(&dp->dp_left, newdp, grp);
			return;
		} else if (cmp < 0) {
			add_dlist(&dp->dp_right, newdp, grp);
			return;
		} else
			free((caddr_t)newdp);
	} else {
		dp = newdp;
		dp->dp_left = (struct dirlist *)0;
		*dpp = dp;
	}
	if (grp) {

		/*
		 * Hang all of the host(s) off of the directory point.
		 */
		do {
			hp = get_ht();
			hp->ht_grp = grp;
			hp->ht_next = dp->dp_hosts;
			dp->dp_hosts = hp;
			grp = grp->gr_next;
		} while (grp);
	} else
		dp->dp_flag |= DP_DEFSET;
}

/*
 * Search for a dirpath on the export point.
 */
struct dirlist *
dirp_search(dp, dirpath)
	register struct dirlist *dp;
	char *dirpath;
{
	register int cmp;

	if (dp) {
		cmp = strcmp(dp->dp_dirp, dirpath);
		if (cmp > 0)
			return (dirp_search(dp->dp_left, dirpath));
		else if (cmp < 0)
			return (dirp_search(dp->dp_right, dirpath));
		else
			return (dp);
	}
	return (dp);
}

/*
 * Scan for a host match in a directory tree.
 */
chk_host(dp, saddr, defsetp)
	struct dirlist *dp;
	u_long saddr;
	int *defsetp;
{
	register struct hostlist *hp;
	register struct grouplist *grp;
	register u_long **addrp;

	if (dp) {
		if (dp->dp_flag & DP_DEFSET)
			*defsetp = 1;
		hp = dp->dp_hosts;
		while (hp) {
			grp = hp->ht_grp;
			switch (grp->gr_type) {
			case GT_HOST:
			    addrp = (u_long **)
				grp->gr_ptr.gt_hostent->h_addr_list;
			    while (*addrp) {
				if (**addrp == saddr)
				    return (1);
				addrp++;
			    }
			    break;
			case GT_NET:
			    if ((saddr & grp->gr_ptr.gt_net.nt_mask) ==
				grp->gr_ptr.gt_net.nt_net)
				return (1);
			    break;
			};
			hp = hp->ht_next;
		}
	}
	return (0);
}

/*
 * Scan tree for a host that matches the address.
 */
scan_tree(dp, saddr)
	register struct dirlist *dp;
	u_long saddr;
{
	int defset;

	if (dp) {
		if (scan_tree(dp->dp_left, saddr))
			return (1);
		if (chk_host(dp, saddr, &defset))
			return (1);
		if (scan_tree(dp->dp_right, saddr))
			return (1);
	}
	return (0);
}

/*
 * Traverse the dirlist tree and free it up.
 */
void
free_dir(dp)
	register struct dirlist *dp;
{

	if (dp) {
		free_dir(dp->dp_left);
		free_dir(dp->dp_right);
		free_host(dp->dp_hosts);
		free((caddr_t)dp);
	}
}

/*
 * Parse the option string and update fields.
 * Option arguments may either be -<option>=<value> or
 * -<option> <value>
 */
do_opt(cpp, endcpp, ep, grp, has_hostp, exflagsp, cr)
	char **cpp, **endcpp;
	struct exportlist *ep;
	struct grouplist *grp;
	int *has_hostp;
	int *exflagsp;
	struct ucred *cr;
{
	register char *cpoptarg, *cpoptend;
	char *cp, *endcp, *cpopt, savedc, savedc2;
	int allflag, usedarg;

	cpopt = *cpp;
	cpopt++;
	cp = *endcpp;
	savedc = *cp;
	*cp = '\0';
	while (cpopt && *cpopt) {
		allflag = 1;
		usedarg = -2;
		if (cpoptend = index(cpopt, ',')) {
			*cpoptend++ = '\0';
			if (cpoptarg = index(cpopt, '='))
				*cpoptarg++ = '\0';
		} else {
			if (cpoptarg = index(cpopt, '='))
				*cpoptarg++ = '\0';
			else {
				*cp = savedc;
				nextfield(&cp, &endcp);
				**endcpp = '\0';
				if (endcp > cp && *cp != '-') {
					cpoptarg = cp;
					savedc2 = *endcp;
					*endcp = '\0';
					usedarg = 0;
				}
			}
		}
		if (!strcmp(cpopt, "ro") || !strcmp(cpopt, "o")) {
			*exflagsp |= MNT_EXRDONLY;
		} else if (cpoptarg && (!strcmp(cpopt, "maproot") ||
		    !(allflag = strcmp(cpopt, "mapall")) ||
		    !strcmp(cpopt, "root") || !strcmp(cpopt, "r"))) {
			usedarg++;
			parsecred(cpoptarg, cr);
			if (allflag == 0) {
				*exflagsp |= MNT_EXPORTANON;
				opt_flags |= OP_MAPALL;
			} else
				opt_flags |= OP_MAPROOT;
		} else if (!strcmp(cpopt, "kerb") || !strcmp(cpopt, "k")) {
			*exflagsp |= MNT_EXKERB;
			opt_flags |= OP_KERB;
		} else if (cpoptarg && (!strcmp(cpopt, "mask") ||
			!strcmp(cpopt, "m"))) {
			if (get_net(cpoptarg, &grp->gr_ptr.gt_net, 1)) {
				syslog(LOG_ERR, "Bad mask: %s", cpoptarg);
				return (1);
			}
			usedarg++;
			opt_flags |= OP_MASK;
		} else if (cpoptarg && (!strcmp(cpopt, "network") ||
			!strcmp(cpopt, "n"))) {
			if (grp->gr_type != GT_NULL) {
				syslog(LOG_ERR, "Network/host conflict");
				return (1);
			} else if (get_net(cpoptarg, &grp->gr_ptr.gt_net, 0)) {
				syslog(LOG_ERR, "Bad net: %s", cpoptarg);
				return (1);
			}
			grp->gr_type = GT_NET;
			*has_hostp = 1;
			usedarg++;
			opt_flags |= OP_NET;
		} else if (!strcmp(cpopt, "alldirs")) {
			opt_flags |= OP_ALLDIRS;
#ifdef ISO
		} else if (cpoptarg && !strcmp(cpopt, "iso")) {
			if (get_isoaddr(cpoptarg, grp)) {
				syslog(LOG_ERR, "Bad iso addr: %s", cpoptarg);
				return (1);
			}
			*has_hostp = 1;
			usedarg++;
			opt_flags |= OP_ISO;
#endif /* ISO */
		} else {
			syslog(LOG_ERR, "Bad opt %s", cpopt);
			return (1);
		}
		if (usedarg >= 0) {
			*endcp = savedc2;
			**endcpp = savedc;
			if (usedarg > 0) {
				*cpp = cp;
				*endcpp = endcp;
			}
			return (0);
		}
		cpopt = cpoptend;
	}
	**endcpp = savedc;
	return (0);
}

/*
 * Translate a character string to the corresponding list of network
 * addresses for a hostname.
 */
get_host(cp, grp)
	char *cp;
	register struct grouplist *grp;
{
	register struct hostent *hp, *nhp;
	register char **addrp, **naddrp;
	struct hostent t_host;
	int i;
	u_long saddr;
	char *aptr[2];

	if (grp->gr_type != GT_NULL)
		return (1);
	if ((hp = gethostbyname(cp)) == NULL) {
		if (isdigit(*cp)) {
			saddr = inet_addr(cp);
			if (saddr == -1) {
				syslog(LOG_ERR, "Inet_addr failed");
				return (1);
			}
			if ((hp = gethostbyaddr((caddr_t)&saddr, sizeof (saddr),
				AF_INET)) == NULL) {
				hp = &t_host;
				hp->h_name = cp;
				hp->h_addrtype = AF_INET;
				hp->h_length = sizeof (u_long);
				hp->h_addr_list = aptr;
				aptr[0] = (char *)&saddr;
				aptr[1] = (char *)0;
			}
		} else {
			syslog(LOG_ERR, "Gethostbyname failed");
			return (1);
		}
	}
	grp->gr_type = GT_HOST;
	nhp = grp->gr_ptr.gt_hostent = (struct hostent *)
		malloc(sizeof(struct hostent));
	if (nhp == (struct hostent *)0)
		out_of_mem();
	bcopy((caddr_t)hp, (caddr_t)nhp,
		sizeof(struct hostent));
	i = strlen(hp->h_name)+1;
	nhp->h_name = (char *)malloc(i);
	if (nhp->h_name == (char *)0)
		out_of_mem();
	bcopy(hp->h_name, nhp->h_name, i);
	addrp = hp->h_addr_list;
	i = 1;
	while (*addrp++)
		i++;
	naddrp = nhp->h_addr_list = (char **)
		malloc(i*sizeof(char *));
	if (naddrp == (char **)0)
		out_of_mem();
	addrp = hp->h_addr_list;
	while (*addrp) {
		*naddrp = (char *)
		    malloc(hp->h_length);
		if (*naddrp == (char *)0)
		    out_of_mem();
		bcopy(*addrp, *naddrp,
			hp->h_length);
		addrp++;
		naddrp++;
	}
	*naddrp = (char *)0;
	if (debug)
		fprintf(stderr, "got host %s\n", hp->h_name);
	return (0);
}

/*
 * Free up an exports list component
 */
void
free_exp(ep)
	register struct exportlist *ep;
{

	if (ep->ex_defdir) {
		free_host(ep->ex_defdir->dp_hosts);
		free((caddr_t)ep->ex_defdir);
	}
	if (ep->ex_fsdir)
		free(ep->ex_fsdir);
	free_dir(ep->ex_dirl);
	free((caddr_t)ep);
}

/*
 * Free hosts.
 */
void
free_host(hp)
	register struct hostlist *hp;
{
	register struct hostlist *hp2;

	while (hp) {
		hp2 = hp;
		hp = hp->ht_next;
		free((caddr_t)hp2);
	}
}

struct hostlist *
get_ht()
{
	register struct hostlist *hp;

	hp = (struct hostlist *)malloc(sizeof (struct hostlist));
	if (hp == (struct hostlist *)0)
		out_of_mem();
	hp->ht_next = (struct hostlist *)0;
	return (hp);
}

#ifdef ISO
/*
 * Translate an iso address.
 */
get_isoaddr(cp, grp)
	char *cp;
	struct grouplist *grp;
{
	struct iso_addr *isop;
	struct sockaddr_iso *isoaddr;

	if (grp->gr_type != GT_NULL)
		return (1);
	if ((isop = iso_addr(cp)) == NULL) {
		syslog(LOG_ERR,
		    "iso_addr failed, ignored");
		return (1);
	}
	isoaddr = (struct sockaddr_iso *)
	    malloc(sizeof (struct sockaddr_iso));
	if (isoaddr == (struct sockaddr_iso *)0)
		out_of_mem();
	bzero((caddr_t)isoaddr, sizeof (struct sockaddr_iso));
	bcopy((caddr_t)isop, (caddr_t)&isoaddr->siso_addr,
		sizeof (struct iso_addr));
	isoaddr->siso_len = sizeof (struct sockaddr_iso);
	isoaddr->siso_family = AF_ISO;
	grp->gr_type = GT_ISO;
	grp->gr_ptr.gt_isoaddr = isoaddr;
	return (0);
}
#endif	/* ISO */

/*
 * Out of memory, fatal
 */
void
out_of_mem()
{

	syslog(LOG_ERR, "Out of memory");
	exit(2);
}

/*
 * Do the mount syscall with the update flag to push the export info into
 * the kernel.
 */
do_mount(ep, grp, exflags, anoncrp, dirp, dirplen, fsb)
	struct exportlist *ep;
	struct grouplist *grp;
	int exflags;
	struct ucred *anoncrp;
	char *dirp;
	int dirplen;
	struct statfs *fsb;
{
	register char *cp = (char *)0;
	register u_long **addrp;
	int done;
	char savedc;
	struct sockaddr_in sin, imask;
	struct ufs_args args;
	u_long net;

	args.fspec = 0;
	args.exflags = exflags;
	args.anon = *anoncrp;
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_len = sizeof(sin);
	imask.sin_family = AF_INET;
	imask.sin_port = 0;
	imask.sin_len = sizeof(sin);
	if (grp->gr_type == GT_HOST)
		addrp = (u_long **)grp->gr_ptr.gt_hostent->h_addr_list;
	else
		addrp = (u_long **)0;
	done = FALSE;
	while (!done) {
		switch (grp->gr_type) {
		case GT_HOST:
			if (addrp)
				sin.sin_addr.s_addr = **addrp;
			else
				sin.sin_addr.s_addr = INADDR_ANY;
			args.saddr = (struct sockaddr *)&sin;
			args.slen = sizeof(sin);
			args.msklen = 0;
			break;
		case GT_NET:
			if (grp->gr_ptr.gt_net.nt_mask)
			    imask.sin_addr.s_addr = grp->gr_ptr.gt_net.nt_mask;
			else {
			    net = ntohl(grp->gr_ptr.gt_net.nt_net);
			    if (IN_CLASSA(net))
				imask.sin_addr.s_addr = inet_addr("255.0.0.0");
			    else if (IN_CLASSB(net))
				imask.sin_addr.s_addr =
				    inet_addr("255.255.0.0");
			    else
				imask.sin_addr.s_addr =
				    inet_addr("255.255.255.0");
			    grp->gr_ptr.gt_net.nt_mask = imask.sin_addr.s_addr;
			}
			sin.sin_addr.s_addr = grp->gr_ptr.gt_net.nt_net;
			args.saddr = (struct sockaddr *)&sin;
			args.slen = sizeof (sin);
			args.smask = (struct sockaddr *)&imask;
			args.msklen = sizeof (imask);
			break;
#ifdef ISO
		case GT_ISO:
			args.saddr = (struct sockaddr *)grp->gr_ptr.gt_isoaddr;
			args.slen = sizeof (struct sockaddr_iso);
			args.msklen = 0;
			break;
#endif	/* ISO */
		default:
			syslog(LOG_ERR, "Bad grouptype");
			if (cp)
				*cp = savedc;
			return (1);
		};

		/*
		 * XXX:
		 * Maybe I should just use the fsb->f_mntonname path instead
		 * of looping back up the dirp to the mount point??
		 * Also, needs to know how to export all types of local
		 * exportable file systems and not just MOUNT_UFS.
		 */
		while (mount(fsb->f_type, dirp,
		       fsb->f_flags | MNT_UPDATE, (caddr_t)&args) < 0) {
			if (cp)
				*cp-- = savedc;
			else
				cp = dirp + dirplen - 1;
			if (errno == EPERM) {
				syslog(LOG_ERR,
				   "Can't change attributes for %s.\n", dirp);
				return (1);
			}
			if (opt_flags & OP_ALLDIRS) {
				syslog(LOG_ERR, "Not root dir");
				return (1);
			}
			/* back up over the last component */
			while (*cp == '/' && cp > dirp)
				cp--;
			while (*(cp - 1) != '/' && cp > dirp)
				cp--;
			if (cp == dirp) {
				if (debug)
					fprintf(stderr,"mnt unsucc\n");
				syslog(LOG_ERR, "Can't export %s", dirp);
				return (1);
			}
			savedc = *cp;
			*cp = '\0';
		}
		if (addrp) {
			++addrp;
			if (*addrp == (u_long *)0)
				done = TRUE;
		} else
			done = TRUE;
	}
	if (cp)
		*cp = savedc;
	return (0);
}

/*
 * Translate a net address.
 */
get_net(cp, net, maskflg)
	char *cp;
	struct netmsk *net;
	int maskflg;
{
	register struct netent *np;
	register long netaddr;
	struct in_addr inetaddr, inetaddr2;
	char *name;

	if (np = getnetbyname(cp))
		inetaddr = inet_makeaddr(np->n_net, 0);
	else if (isdigit(*cp)) {
		if ((netaddr = inet_network(cp)) == -1)
			return (1);
		inetaddr = inet_makeaddr(netaddr, 0);
		/*
		 * Due to arbritrary subnet masks, you don't know how many
		 * bits to shift the address to make it into a network,
		 * however you do know how to make a network address into
		 * a host with host == 0 and then compare them.
		 * (What a pest)
		 */
		if (!maskflg) {
			setnetent(0);
			while (np = getnetent()) {
				inetaddr2 = inet_makeaddr(np->n_net, 0);
				if (inetaddr2.s_addr == inetaddr.s_addr)
					break;
			}
			endnetent();
		}
	} else
		return (1);
	if (maskflg)
		net->nt_mask = inetaddr.s_addr;
	else {
		if (np)
			name = np->n_name;
		else
			name = inet_ntoa(inetaddr);
		net->nt_name = (char *)malloc(strlen(name) + 1);
		if (net->nt_name == (char *)0)
			out_of_mem();
		strcpy(net->nt_name, name);
		net->nt_net = inetaddr.s_addr;
	}
	return (0);
}

/*
 * Parse out the next white space separated field
 */
void
nextfield(cp, endcp)
	char **cp;
	char **endcp;
{
	register char *p;

	p = *cp;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '\n' || *p == '\0')
		*cp = *endcp = p;
	else {
		*cp = p++;
		while (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\0')
			p++;
		*endcp = p;
	}
}

/*
 * Get an exports file line. Skip over blank lines and handle line
 * continuations.
 */
get_line()
{
	register char *p, *cp;
	register int len;
	int totlen, cont_line;

	/*
	 * Loop around ignoring blank lines and getting all continuation lines.
	 */
	p = line;
	totlen = 0;
	do {
		if (fgets(p, LINESIZ - totlen, exp_file) == NULL)
			return (0);
		len = strlen(p);
		cp = p + len - 1;
		cont_line = 0;
		while (cp >= p &&
		    (*cp == ' ' || *cp == '\t' || *cp == '\n' || *cp == '\\')) {
			if (*cp == '\\')
				cont_line = 1;
			cp--;
			len--;
		}
		*++cp = '\0';
		if (len > 0) {
			totlen += len;
			if (totlen >= LINESIZ) {
				syslog(LOG_ERR, "Exports line too long");
				exit(2);
			}
			p = cp;
		}
	} while (totlen == 0 || cont_line);
	return (1);
}

/*
 * Parse a description of a credential.
 */
parsecred(namelist, cr)
	char *namelist;
	register struct ucred *cr;
{
	register char *name;
	register int cnt;
	char *names;
	struct passwd *pw;
	struct group *gr;
	int ngroups, groups[NGROUPS + 1];

	/*
	 * Set up the unpriviledged user.
	 */
	cr->cr_ref = 1;
	cr->cr_uid = -2;
	cr->cr_groups[0] = -2;
	cr->cr_ngroups = 1;
	/*
	 * Get the user's password table entry.
	 */
	names = strsep(&namelist, " \t\n");
	name = strsep(&names, ":");
	if (isdigit(*name) || *name == '-')
		pw = getpwuid(atoi(name));
	else
		pw = getpwnam(name);
	/*
	 * Credentials specified as those of a user.
	 */
	if (names == NULL) {
		if (pw == NULL) {
			syslog(LOG_ERR, "Unknown user: %s\n", name);
			return;
		}
		cr->cr_uid = pw->pw_uid;
		ngroups = NGROUPS + 1;
		if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups))
			syslog(LOG_ERR, "Too many groups\n");
		/*
		 * Convert from int's to gid_t's and compress out duplicate
		 */
		cr->cr_ngroups = ngroups - 1;
		cr->cr_groups[0] = groups[0];
		for (cnt = 2; cnt < ngroups; cnt++)
			cr->cr_groups[cnt - 1] = groups[cnt];
		return;
	}
	/*
	 * Explicit credential specified as a colon separated list:
	 *	uid:gid:gid:...
	 */
	if (pw != NULL)
		cr->cr_uid = pw->pw_uid;
	else if (isdigit(*name) || *name == '-')
		cr->cr_uid = atoi(name);
	else {
		syslog(LOG_ERR, "Unknown user: %s\n", name);
		return;
	}
	cr->cr_ngroups = 0;
	while (names != NULL && *names != '\0' && cr->cr_ngroups < NGROUPS) {
		name = strsep(&names, ":");
		if (isdigit(*name) || *name == '-') {
			cr->cr_groups[cr->cr_ngroups++] = atoi(name);
		} else {
			if ((gr = getgrnam(name)) == NULL) {
				syslog(LOG_ERR, "Unknown group: %s\n", name);
				continue;
			}
			cr->cr_groups[cr->cr_ngroups++] = gr->gr_gid;
		}
	}
	if (names != NULL && *names != '\0' && cr->cr_ngroups == NGROUPS)
		syslog(LOG_ERR, "Too many groups\n");
}

#define	STRSIZ	(RPCMNT_NAMELEN+RPCMNT_PATHLEN+50)
/*
 * Routines that maintain the remote mounttab
 */
void
get_mountlist()
{
	register struct mountlist *mlp, **mlpp;
	register char *eos, *dirp;
	int len;
	char str[STRSIZ];
	FILE *mlfile;

	if ((mlfile = fopen(_PATH_RMOUNTLIST, "r")) == NULL) {
		syslog(LOG_ERR, "Can't open %s", _PATH_RMOUNTLIST);
		return;
	}
	mlpp = &mlhead;
	while (fgets(str, STRSIZ, mlfile) != NULL) {
		if ((dirp = index(str, '\t')) == NULL &&
		    (dirp = index(str, ' ')) == NULL)
			continue;
		mlp = (struct mountlist *)malloc(sizeof (*mlp));
		len = dirp-str;
		if (len > RPCMNT_NAMELEN)
			len = RPCMNT_NAMELEN;
		bcopy(str, mlp->ml_host, len);
		mlp->ml_host[len] = '\0';
		while (*dirp == '\t' || *dirp == ' ')
			dirp++;
		if ((eos = index(dirp, '\t')) == NULL &&
		    (eos = index(dirp, ' ')) == NULL &&
		    (eos = index(dirp, '\n')) == NULL)
			len = strlen(dirp);
		else
			len = eos-dirp;
		if (len > RPCMNT_PATHLEN)
			len = RPCMNT_PATHLEN;
		bcopy(dirp, mlp->ml_dirp, len);
		mlp->ml_dirp[len] = '\0';
		mlp->ml_next = (struct mountlist *)0;
		*mlpp = mlp;
		mlpp = &mlp->ml_next;
	}
	fclose(mlfile);
}

void
del_mlist(hostp, dirp)
	register char *hostp, *dirp;
{
	register struct mountlist *mlp, **mlpp;
	struct mountlist *mlp2;
	FILE *mlfile;
	int fnd = 0;

	mlpp = &mlhead;
	mlp = mlhead;
	while (mlp) {
		if (!strcmp(mlp->ml_host, hostp) &&
		    (!dirp || !strcmp(mlp->ml_dirp, dirp))) {
			fnd = 1;
			mlp2 = mlp;
			*mlpp = mlp = mlp->ml_next;
			free((caddr_t)mlp2);
		} else {
			mlpp = &mlp->ml_next;
			mlp = mlp->ml_next;
		}
	}
	if (fnd) {
		if ((mlfile = fopen(_PATH_RMOUNTLIST, "w")) == NULL) {
			syslog(LOG_ERR,"Can't update %s", _PATH_RMOUNTLIST);
			return;
		}
		mlp = mlhead;
		while (mlp) {
			fprintf(mlfile, "%s %s\n", mlp->ml_host, mlp->ml_dirp);
			mlp = mlp->ml_next;
		}
		fclose(mlfile);
	}
}

void
add_mlist(hostp, dirp)
	register char *hostp, *dirp;
{
	register struct mountlist *mlp, **mlpp;
	FILE *mlfile;

	mlpp = &mlhead;
	mlp = mlhead;
	while (mlp) {
		if (!strcmp(mlp->ml_host, hostp) && !strcmp(mlp->ml_dirp, dirp))
			return;
		mlpp = &mlp->ml_next;
		mlp = mlp->ml_next;
	}
	mlp = (struct mountlist *)malloc(sizeof (*mlp));
	strncpy(mlp->ml_host, hostp, RPCMNT_NAMELEN);
	mlp->ml_host[RPCMNT_NAMELEN] = '\0';
	strncpy(mlp->ml_dirp, dirp, RPCMNT_PATHLEN);
	mlp->ml_dirp[RPCMNT_PATHLEN] = '\0';
	mlp->ml_next = (struct mountlist *)0;
	*mlpp = mlp;
	if ((mlfile = fopen(_PATH_RMOUNTLIST, "a")) == NULL) {
		syslog(LOG_ERR, "Can't update %s", _PATH_RMOUNTLIST);
		return;
	}
	fprintf(mlfile, "%s %s\n", mlp->ml_host, mlp->ml_dirp);
	fclose(mlfile);
}

/*
 * This function is called via. SIGTERM when the system is going down.
 * It sends a broadcast RPCMNT_UMNTALL.
 */
void
send_umntall()
{
	(void) clnt_broadcast(RPCPROG_MNT, RPCMNT_VER1, RPCMNT_UMNTALL,
		xdr_void, (caddr_t)0, xdr_void, (caddr_t)0, umntall_each);
	exit(0);
}

umntall_each(resultsp, raddr)
	caddr_t resultsp;
	struct sockaddr_in *raddr;
{
	return (1);
}

/*
 * Free up a group list.
 */
void
free_grp(grp)
	register struct grouplist *grp;
{
	register char **addrp;

	if (grp->gr_type == GT_HOST) {
		if (grp->gr_ptr.gt_hostent->h_name) {
			addrp = grp->gr_ptr.gt_hostent->h_addr_list;
			while (addrp && *addrp)
				free(*addrp++);
			free((caddr_t)grp->gr_ptr.gt_hostent->h_addr_list);
			free(grp->gr_ptr.gt_hostent->h_name);
		}
		free((caddr_t)grp->gr_ptr.gt_hostent);
	} else if (grp->gr_type == GT_NET) {
		if (grp->gr_ptr.gt_net.nt_name)
			free(grp->gr_ptr.gt_net.nt_name);
	}
#ifdef ISO
	else if (grp->gr_type == GT_ISO)
		free((caddr_t)grp->gr_ptr.gt_isoaddr);
#endif
	free((caddr_t)grp);
}

/*
 * char *realpath(const char *path, char resolved_path[MAXPATHLEN])
 *
 * find the real name of path, by removing all ".", ".."
 * and symlink components.
 *
 * Jan-Simon Pendry, September 1991.
 */
char *
realpath(path, resolved)
	char *path;
	char resolved[MAXPATHLEN];
{
	int d = open(".", O_RDONLY);
	int rootd = 0;
	char *p, *q;
	struct stat stb;
	char wbuf[MAXPATHLEN];

	strcpy(resolved, path);

	if (d < 0)
		return 0;

loop:;
	q = strrchr(resolved, '/');
	if (q) {
		p = q + 1;
		if (q == resolved)
			q = "/";
		else {
			do
				--q;
			while (q > resolved && *q == '/');
			q[1] = '\0';
			q = resolved;
		}
		if (chdir(q) < 0)
			goto out;
	} else
		p = resolved;

	if (lstat(p, &stb) == 0) {
		if (S_ISLNK(stb.st_mode)) {
			int n = readlink(p, resolved, MAXPATHLEN);
			if (n < 0)
				goto out;
			resolved[n] = '\0';
			goto loop;
		}
		if (S_ISDIR(stb.st_mode)) {
			if (chdir(p) < 0)
				goto out;
			p = "";
		}
	}

	strcpy(wbuf, p);
	if (getcwd(resolved, MAXPATHLEN) == 0)
		goto out;
	if (resolved[0] == '/' && resolved[1] == '\0')
		rootd = 1;

	if (*wbuf) {
		if (strlen(resolved) + strlen(wbuf) + rootd + 1 > MAXPATHLEN) {
			errno = ENAMETOOLONG;
			goto out;
		}
		if (rootd == 0)
			strcat(resolved, "/");
		strcat(resolved, wbuf);
	}

	if (fchdir(d) < 0)
		goto out;
	(void) close(d);

	return resolved;

out:;
	(void) close(d);
	return 0;
}

#ifdef DEBUG
void
SYSLOG(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
#endif /* DEBUG */

/*
 * Check options for consistency.
 */
check_options(dp)
	struct dirlist *dp;
{

	if (dp == (struct dirlist *)0)
	    return (1);
	if ((opt_flags & (OP_MAPROOT | OP_MAPALL)) == (OP_MAPROOT | OP_MAPALL) ||
	    (opt_flags & (OP_MAPROOT | OP_KERB)) == (OP_MAPROOT | OP_KERB) ||
	    (opt_flags & (OP_MAPALL | OP_KERB)) == (OP_MAPALL | OP_KERB)) {
	    syslog(LOG_ERR, "-mapall, -maproot and -kerb mutually exclusive");
	    return (1);
	}
	if ((opt_flags & OP_MASK) && (opt_flags & OP_NET) == 0) {
	    syslog(LOG_ERR, "-mask requires -net");
	    return (1);
	}
	if ((opt_flags & (OP_NET | OP_ISO)) == (OP_NET | OP_ISO)) {
	    syslog(LOG_ERR, "-net and -iso mutually exclusive");
	    return (1);
	}
	if ((opt_flags & OP_ALLDIRS) && dp->dp_left) {
	    syslog(LOG_ERR, "-alldir has multiple directories");
	    return (1);
	}
	return (0);
}
