/*
 * Copyright (c) 1983 Eric P. Allman
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)sendmail.h	8.89 (Berkeley) %G%
 */

/*
**  SENDMAIL.H -- Global definitions for sendmail.
*/

# ifdef _DEFINE
# define EXTERN
# ifndef lint
static char SmailSccsId[] =	"@(#)sendmail.h	8.89		%G%";
# endif
# else /*  _DEFINE */
# define EXTERN extern
# endif /* _DEFINE */

# include <unistd.h>
# include <stddef.h>
# include <stdlib.h>
# include <stdio.h>
# include <ctype.h>
# include <setjmp.h>
# include <sysexits.h>
# include <string.h>
# include <time.h>
# include <errno.h>

# include "conf.h"
# include "conf.h"
# include "useful.h"

# ifdef LOG
# include <syslog.h>
# endif /* LOG */

# ifdef DAEMON
# include <sys/socket.h>
# endif
# ifdef NETUNIX
# include <sys/un.h>
# endif
# ifdef NETINET
# include <netinet/in.h>
# endif
# ifdef NETISO
# include <netiso/iso.h>
# endif
# ifdef NETNS
# include <netns/ns.h>
# endif
# ifdef NETX25
# include <netccitt/x25.h>
# endif




/*
**  Data structure for bit maps.
**
**	Each bit in this map can be referenced by an ascii character.
**	This is 256 possible bits, or 32 8-bit bytes.
*/

#define BITMAPBYTES	32	/* number of bytes in a bit map */
#define BYTEBITS	8	/* number of bits in a byte */

/* internal macros */
#define _BITWORD(bit)	(bit / (BYTEBITS * sizeof (int)))
#define _BITBIT(bit)	(1 << (bit % (BYTEBITS * sizeof (int))))

typedef int	BITMAP[BITMAPBYTES / sizeof (int)];

/* test bit number N */
#define bitnset(bit, map)	((map)[_BITWORD(bit)] & _BITBIT(bit))

/* set bit number N */
#define setbitn(bit, map)	(map)[_BITWORD(bit)] |= _BITBIT(bit)

/* clear bit number N */
#define clrbitn(bit, map)	(map)[_BITWORD(bit)] &= ~_BITBIT(bit)

/* clear an entire bit map */
#define clrbitmap(map)		bzero((char *) map, BITMAPBYTES)
/*
**  Address structure.
**	Addresses are stored internally in this structure.
**
**	Each address is on two chains and in one tree.
**		The q_next chain is used to link together addresses
**		  for one mailer (and is rooted in a mailer).
**		The q_chain chain is used to maintain a list of
**		  addresses originating from one call to sendto, and
**		  is used primarily for printing messages.
**		The q_alias, q_sibling, and q_child tree maintains
**		  a complete tree of the aliases.  q_alias points to
**		  the parent -- obviously, there can be several, and
**		  so this points to "one" of them.  Ditto for q_sibling.
*/

struct address
{
	char		*q_paddr;	/* the printname for the address */
	char		*q_user;	/* user name */
	char		*q_ruser;	/* real user name, or NULL if q_user */
	char		*q_host;	/* host name */
	struct mailer	*q_mailer;	/* mailer to use */
	u_long		q_flags;	/* status flags, see below */
	uid_t		q_uid;		/* user-id of receiver (if known) */
	gid_t		q_gid;		/* group-id of receiver (if known) */
	char		*q_home;	/* home dir (local mailer only) */
	char		*q_fullname;	/* full name if known */
	char		*q_fullname;	/* full name of this person */
	struct address	*q_next;	/* chain */
	struct address	*q_alias;	/* parent in alias tree */
	struct address	*q_sibling;	/* sibling in alias tree */
	struct address	*q_child;	/* child in alias tree */
};

typedef struct address ADDRESS;

# define QDONTSEND	0x00000001	/* don't send to this address */
# define QBADADDR	0x00000002	/* this address is verified bad */
# define QGOODUID	0x00000004	/* the q_uid q_gid fields are good */
# define QPRIMARY	0x00000008	/* set from argv */
# define QQUEUEUP	0x00000010	/* queue for later transmission */
# define QSENT		0x00000020	/* has been successfully delivered */
# define QNOTREMOTE	0x00000040	/* address not for remote forwarding */
# define QSELFREF	0x00000080	/* this address references itself */
# define QVERIFIED	0x00000100	/* verified, but not expanded */
# define QREPORT	0x00000200	/* report this addr in return message */
# define QBOGUSSHELL	0x00000400	/* user has no valid shell listed */
# define QUNSAFEADDR	0x00000800	/* address aquired via unsafe path */
# define QPINGONSUCCESS	0x00001000	/* give return on successful delivery */
# define QPINGONFAILURE	0x00002000	/* give return on failure */
# define QPINGONDELAY	0x00004000	/* give return on message delay */
# define QHAS_RET_PARAM	0x00008000	/* RCPT command had RET argument */
# define QRET_HDRS	0x00010000	/* don't return message body */
# define QRELAYED	0x00020000	/* relayed to non-DSN aware mailer */

# define NULLADDR	((ADDRESS *) NULL)
# define QPSEUDO	000040	/* only on the list for verification */
/*
**  Mailer definition structure.
**	Every mailer known to the system is declared in this
**	structure.  It defines the pathname of the mailer, some
**	flags associated with it, and the argument vector to
**	pass to it.  The flags are defined in conf.c
**
**	The argument vector is expanded before actual use.  All
**	words except the first are passed through the macro
**	processor.
*/

struct mailer
{
	char	*m_name;	/* symbolic name of this mailer */
	char	*m_mailer;	/* pathname of the mailer to use */
	char	*m_mtatype;	/* type of this MTA */
	char	*m_addrtype;	/* type for addresses */
	char	*m_diagtype;	/* type for diagnostics */
	BITMAP	m_flags;	/* status flags, see below */
	short	m_mno;		/* mailer number internally */
	char	**m_argv;	/* template argument vector */
	short	m_sh_rwset;	/* rewrite set: sender header addresses */
	short	m_se_rwset;	/* rewrite set: sender envelope addresses */
	short	m_rh_rwset;	/* rewrite set: recipient header addresses */
	short	m_re_rwset;	/* rewrite set: recipient envelope addresses */
	char	*m_eol;		/* end of line string */
	long	m_maxsize;	/* size limit on message to this mailer */
	int	m_linelimit;	/* max # characters per line */
	char	*m_execdir;	/* directory to chdir to before execv */
	uid_t	m_uid;		/* UID to run as */
	gid_t	m_gid;		/* GID to run as */
	char	*m_defcharset;	/* default character set */
};

typedef struct mailer	MAILER;

/* bits for m_flags */
# define M_ESMTP	'a'	/* run Extended SMTP protocol */
# define M_ALIASABLE	'A'	/* user can be LHS of an alias */
# define M_BLANKEND	'b'	/* ensure blank line at end of message */
# define M_NOCOMMENT	'c'	/* don't include comment part of address */
# define M_CANONICAL	'C'	/* make addresses canonical "u@dom" */
# define M_NOBRACKET	'd'	/* never angle bracket envelope route-addrs */
		/*	'D'	/* CF: include Date: */
# define M_EXPENSIVE	'e'	/* it costs to use this mailer.... */
# define M_ESCFROM	'E'	/* escape From lines to >From */
# define M_FOPT		'f'	/* mailer takes picky -f flag */
		/*	'F'	/* CF: include From: or Resent-From: */
# define M_NO_NULL_FROM	'g'	/* sender of errors should be $g */
# define M_HST_UPPER	'h'	/* preserve host case distinction */
# define M_PREHEAD	'H'	/* MAIL11V3: preview headers */
# define M_UDBENVELOPE	'i'	/* do udbsender rewriting on envelope */
# define M_INTERNAL	'I'	/* SMTP to another sendmail site */
# define M_NOLOOPCHECK	'k'	/* don't check for loops in HELO command */
# define M_LOCALMAILER	'l'	/* delivery is to this host */
# define M_LIMITS	'L'	/* must enforce SMTP line limits */
# define M_MUSER	'm'	/* can handle multiple users at once */
		/*	'M'	/* CF: include Message-Id: */
# define M_NHDR		'n'	/* don't insert From line */
# define M_MANYSTATUS	'N'	/* MAIL11V3: DATA returns multi-status */
# define M_RUNASRCPT	'o'	/* always run mailer as recipient */
# define M_FROMPATH	'p'	/* use reverse-path in MAIL FROM: */
		/*	'P'	/* CF: include Return-Path: */
# define M_ROPT		'r'	/* mailer takes picky -r flag */
# define M_SECURE_PORT	'R'	/* try to send on a reserved TCP port */
# define M_STRIPQ	's'	/* strip quote chars from user/host */
# define M_SPECIFIC_UID	'S'	/* run as specific uid/gid */
# define M_USR_UPPER	'u'	/* preserve user case distinction */
# define M_UGLYUUCP	'U'	/* this wants an ugly UUCP from line */
		/*	'V'	/* UIUC: !-relativize all addresses */
# define M_HASPWENT	'w'	/* check for /etc/passwd entry */
		/*	'x'	/* CF: include Full-Name: */
# define M_XDOT		'X'	/* use hidden-dot algorithm */
# define M_TRYRULESET5	'5'	/* use ruleset 5 after local aliasing */
# define M_7BITS	'7'	/* use 7-bit path */
# define M_8BITS	'8'	/* force "just send 8" behaviour */
# define M_CHECKINCLUDE	':'	/* check for :include: files */
# define M_CHECKPROG	'|'	/* check for |program addresses */
# define M_CHECKFILE	'/'	/* check for /file addresses */
# define M_CHECKUDB	'@'	/* user can be user database key */

EXTERN MAILER	*Mailer[MAXMAILERS+1];

EXTERN MAILER	*LocalMailer;		/* ptr to local mailer */
EXTERN MAILER	*ProgMailer;		/* ptr to program mailer */
EXTERN MAILER	*FileMailer;		/* ptr to *file* mailer */
EXTERN MAILER	*InclMailer;		/* ptr to *include* mailer */
/*
**  Header structure.
**	This structure is used internally to store header items.
*/

struct header
{
	char		*h_field;	/* the name of the field */
	char		*h_value;	/* the value of that field */
	struct header	*h_link;	/* the next header */
	u_short		h_flags;	/* status bits, see below */
	BITMAP		h_mflags;	/* m_flags bits needed */
};

typedef struct header	HDR;

/*
**  Header information structure.
**	Defined in conf.c, this struct declares the header fields
**	that have some magic meaning.
*/

struct hdrinfo
{
	char	*hi_field;	/* the name of the field */
	u_short	hi_flags;	/* status bits, see below */
};

extern struct hdrinfo	HdrInfo[];

/* bits for h_flags and hi_flags */
# define H_EOH		0x0001	/* this field terminates header */
# define H_RCPT		0x0002	/* contains recipient addresses */
# define H_DEFAULT	0x0004	/* if another value is found, drop this */
# define H_RESENT	0x0008	/* this address is a "Resent-..." address */
# define H_CHECK	0x0010	/* check h_mflags against m_flags */
# define H_ACHECK	0x0020	/* ditto, but always (not just default) */
# define H_FORCE	0x0040	/* force this field, even if default */
# define H_TRACE	0x0080	/* this field contains trace information */
# define H_FROM		0x0100	/* this is a from-type field */
# define H_VALID	0x0200	/* this field has a validated value */
# define H_RECEIPTTO	0x0400	/* this field has return receipt info */
# define H_ERRORSTO	0x0800	/* this field has error address info */
# define H_CTE		0x1000	/* this field is a content-transfer-encoding */
# define H_CTYPE	0x2000	/* this is a content-type field */
/*
**  Information about currently open connections to mailers, or to
**  hosts that we have looked up recently.
*/

# define MCI		struct mailer_con_info

MCI
{
	short		mci_flags;	/* flag bits, see below */
	short		mci_errno;	/* error number on last connection */
	short		mci_herrno;	/* h_errno from last DNS lookup */
	short		mci_exitstat;	/* exit status from last connection */
	short		mci_state;	/* SMTP state */
	long		mci_maxsize;	/* max size this server will accept */
	FILE		*mci_in;	/* input side of connection */
	FILE		*mci_out;	/* output side of connection */
	int		mci_pid;	/* process id of subordinate proc */
	char		*mci_phase;	/* SMTP phase string */
	struct mailer	*mci_mailer;	/* ptr to the mailer for this conn */
	char		*mci_host;	/* host name */
	char		*mci_status;	/* DSN status to be copied to addrs */
	time_t		mci_lastuse;	/* last usage time */
};


/* flag bits */
#define MCIF_VALID	0x0001		/* this entry is valid */
#define MCIF_TEMP	0x0002		/* don't cache this connection */
#define MCIF_CACHED	0x0004		/* currently in open cache */
#define MCIF_ESMTP	0x0008		/* this host speaks ESMTP */
#define MCIF_EXPN	0x0010		/* EXPN command supported */
#define MCIF_SIZE	0x0020		/* SIZE option supported */
#define MCIF_8BITMIME	0x0040		/* BODY=8BITMIME supported */
#define MCIF_7BIT	0x0080		/* strip this message to 7 bits */
#define MCIF_MULTSTAT	0x0100		/* MAIL11V3: handles MULT status */
#define MCIF_INHEADER	0x0200		/* currently outputing header */
#define MCIF_CVT8TO7	0x0400		/* convert from 8 to 7 bits */
#define MCIF_DSN	0x0800		/* DSN extension supported */

/* states */
#define MCIS_CLOSED	0		/* no traffic on this connection */
#define MCIS_OPENING	1		/* sending initial protocol */
#define MCIS_OPEN	2		/* open, initial protocol sent */
#define MCIS_ACTIVE	3		/* message being sent */
#define MCIS_QUITING	4		/* running quit protocol */
#define MCIS_SSD	5		/* service shutting down */
#define MCIS_ERROR	6		/* I/O error on connection */
/*
**  Envelope structure.
**	This structure defines the message itself.  There is usually
**	only one of these -- for the message that we originally read
**	and which is our primary interest -- but other envelopes can
**	be generated during processing.  For example, error messages
**	will have their own envelope.
*/

# define ENVELOPE	struct envelope

ENVELOPE
{
	HDR		*e_header;	/* head of header list */
	long		e_msgpriority;	/* adjusted priority of this message */
	time_t		e_ctime;	/* time message appeared in the queue */
	char		*e_to;		/* the target person */
	char		*e_receiptto;	/* return receipt address */
	ADDRESS		e_from;		/* the person it is from */
	char		*e_sender;	/* e_from.q_paddr w comments stripped */
	char		**e_fromdomain;	/* the domain part of the sender */
	ADDRESS		*e_returnto;	/* place to return the message to */
	ADDRESS		*e_sendqueue;	/* list of message recipients */
	ADDRESS		*e_errorqueue;	/* the queue for error responses */
	long		e_msgsize;	/* size of the message in bytes */
	long		e_flags;	/* flags, see below */
	int		e_nrcpts;	/* number of recipients */
	short		e_class;	/* msg class (priority, junk, etc.) */
	short		e_hopcount;	/* number of times processed */
	short		e_nsent;	/* number of sends since checkpoint */
	short		e_sendmode;	/* message send mode */
	short		e_errormode;	/* error return mode */
	short		e_timeoutclass;	/* message timeout class */
	int		(*e_puthdr)__P((MCI *, HDR *, ENVELOPE *));
					/* function to put header of message */
	int		(*e_putbody)__P((MCI *, ENVELOPE *, char *));
					/* function to put body of message */
	struct envelope	*e_parent;	/* the message this one encloses */
	struct envelope *e_sibling;	/* the next envelope of interest */
	char		*e_bodytype;	/* type of message body */
	char		*e_df;		/* location of temp file */
	FILE		*e_dfp;		/* temporary file */
	char		*e_id;		/* code for this entry in queue */
	FILE		*e_xfp;		/* transcript file */
	FILE		*e_lockfp;	/* the lock file for this message */
	FILE		*e_qfp;		/* queue control file */
	char		*e_message;	/* error message */
	char		*e_statmsg;	/* stat msg (changes per delivery) */
	char		*e_msgboundary;	/* MIME-style message part boundary */
	char		*e_origrcpt;	/* original recipient (one only) */
	char		*e_envid;	/* envelope id from MAIL FROM: line */
	time_t		e_dtime;	/* time of last delivery attempt */
	int		e_ntries;	/* number of delivery attempts */
	dev_t		e_dfdev;	/* df file's device, for crash recov */
	ino_t		e_dfino;	/* df file's ino, for crash recovery */
	char		*e_macro[256];	/* macro definitions */
};

/* values for e_flags */
#define EF_OLDSTYLE	0x0000001	/* use spaces (not commas) in hdrs */
#define EF_INQUEUE	0x0000002	/* this message is fully queued */
#define EF_NORETURN	0x0000004	/* don't return the message on error */
#define EF_CLRQUEUE	0x0000008	/* disk copy is no longer needed */
#define EF_SENDRECEIPT	0x0000010	/* send a return receipt */
#define EF_FATALERRS	0x0000020	/* fatal errors occured */
#define EF_KEEPQUEUE	0x0000040	/* keep queue files always */
#define EF_RESPONSE	0x0000080	/* this is an error or return receipt */
#define EF_RESENT	0x0000100	/* this message is being forwarded */
#define EF_VRFYONLY	0x0000200	/* verify only (don't expand aliases) */
#define EF_WARNING	0x0000400	/* warning message has been sent */
#define EF_QUEUERUN	0x0000800	/* this envelope is from queue */
#define EF_GLOBALERRS	0x0001000	/* treat errors as global */
#define EF_PM_NOTIFY	0x0002000	/* send return mail to postmaster */
#define EF_METOO	0x0004000	/* send to me too */
#define EF_LOGSENDER	0x0008000	/* need to log the sender */
#define EF_NORECEIPT	0x0010000	/* suppress all return-receipts */
#define EF_HAS8BIT	0x0020000	/* at least one 8-bit char in body */
#define EF_NL_NOT_EOL	0x0040000	/* don't accept raw NL as EOLine */
#define EF_CRLF_NOT_EOL	0x0080000	/* don't accept CR-LF as EOLine */

EXTERN ENVELOPE	*CurEnv;	/* envelope currently being processed */
/*
**  Message priority classes.
**
**	The message class is read directly from the Priority: header
**	field in the message.
**
**	CurEnv->e_msgpriority is the number of bytes in the message plus
**	the creation time (so that jobs ``tend'' to be ordered correctly),
**	adjusted by the message class, the number of recipients, and the
**	amount of time the message has been sitting around.  This number
**	is used to order the queue.  Higher values mean LOWER priority.
**
**	Each priority class point is worth WkClassFact priority points;
**	each recipient is worth WkRecipFact priority points.  Each time
**	we reprocess a message the priority is adjusted by WkTimeFact.
**	WkTimeFact should normally decrease the priority so that jobs
**	that have historically failed will be run later; thanks go to
**	Jay Lepreau at Utah for pointing out the error in my thinking.
**
**	The "class" is this number, unadjusted by the age or size of
**	this message.  Classes with negative representations will have
**	error messages thrown away if they are not local.
*/

struct priority
{
	char	*pri_name;	/* external name of priority */
	int	pri_val;	/* internal value for same */
};

EXTERN struct priority	Priorities[MAXPRIORITIES];
EXTERN int		NumPriorities;	/* pointer into Priorities */
/*
**  Rewrite rules.
*/

struct rewrite
{
	char	**r_lhs;	/* pattern match */
	char	**r_rhs;	/* substitution value */
	struct rewrite	*r_next;/* next in chain */
};

EXTERN struct rewrite	*RewriteRules[MAXRWSETS];

/*
**  Special characters in rewriting rules.
**	These are used internally only.
**	The COND* rules are actually used in macros rather than in
**		rewriting rules, but are given here because they
**		cannot conflict.
*/

/* left hand side items */
# define MATCHZANY	0220	/* match zero or more tokens */
# define MATCHANY	0221	/* match one or more tokens */
# define MATCHONE	0222	/* match exactly one token */
# define MATCHCLASS	0223	/* match one token in a class */
# define MATCHNCLASS	0224	/* match anything not in class */
# define MATCHREPL	0225	/* replacement on RHS for above */
# define MATCHLOOKUP	'\035'	/* look up and replace a sequence */
# define MATCHELOOKUP	'\036'	/* end of the sequence */

/* right hand side items */
# define CANONNET	0226	/* canonical net, next token */
# define CANONHOST	0227	/* canonical host, next token */
# define CANONUSER	0230	/* canonical user, next N tokens */
# define CALLSUBR	0231	/* call another rewriting set */

/* conditionals in macros */
# define CONDIF		0232	/* conditional if-then */
# define CONDELSE	0233	/* conditional else */
# define CONDFI		0234	/* conditional fi */

/* bracket characters for host name lookup */
# define HOSTBEGIN	0235	/* hostname lookup begin */
# define HOSTEND	0236	/* hostname lookup end */

/* bracket characters for generalized lookup */
# define LOOKUPBEGIN	0205	/* generalized lookup begin */
# define LOOKUPEND	0206	/* generalized lookup end */

/* macro substitution character */
# define MACROEXPAND	0201	/* macro expansion */
# define MACRODEXPAND	0202	/* deferred macro expansion */

/* to make the code clearer */
# define MATCHZERO	CANONHOST

/* external <==> internal mapping table */
struct metamac
{
	char	metaname;	/* external code (after $) */
	u_char	metaval;	/* internal code (as above) */
};
/*
**  Name canonification short circuit.
**
**	If the name server for a host is down, the process of trying to
**	canonify the name can hang.  This is similar to (but alas, not
**	identical to) looking up the name for delivery.  This stab type
**	caches the result of the name server lookup so we don't hang
**	multiple times.
*/

#define NAMECANON	struct _namecanon

NAMECANON
{
	short		nc_errno;	/* cached errno */
	short		nc_herrno;	/* cached h_errno */
	short		nc_stat;	/* cached exit status code */
	short		nc_flags;	/* flag bits */
	char		*nc_cname;	/* the canonical name */
};

/* values for nc_flags */
#define NCF_VALID	0x0001	/* entry valid */
/*
**  Mapping functions
**
**	These allow arbitrary mappings in the config file.  The idea
**	(albeit not the implementation) comes from IDA sendmail.
*/

# define MAPCLASS	struct _mapclass
# define MAP		struct _map
# define MAXMAPACTIONS	3		/* size of map_actions array */


/*
**  An actual map.
*/

MAP
{
	MAPCLASS	*map_class;	/* the class of this map */
	char		*map_mname;	/* name of this map */
	int		map_mflags;	/* flags, see below */
	char		*map_file;	/* the (nominal) filename */
	ARBPTR_T	map_db1;	/* the open database ptr */
	ARBPTR_T	map_db2;	/* an "extra" database pointer */
	char		*map_keycolnm;	/* key column name */
	char		*map_valcolnm;	/* value column name */
	u_char		map_keycolno;	/* key column number */
	u_char		map_valcolno;	/* value column number */
	char		map_coldelim;	/* column delimiter */
	char		*map_app;	/* to append to successful matches */
	char		*map_domain;	/* the (nominal) NIS domain */
	char		*map_rebuild;	/* program to run to do auto-rebuild */
	time_t		map_mtime;	/* last database modification time */
	MAP		*map_stack[MAXMAPSTACK];   /* list for stacked maps */
	short		map_return[MAXMAPACTIONS]; /* return bitmaps for stacked maps */
};

/* bit values for map_flags */
# define MF_VALID	0x0001		/* this entry is valid */
# define MF_INCLNULL	0x0002		/* include null byte in key */
# define MF_OPTIONAL	0x0004		/* don't complain if map not found */
# define MF_NOFOLDCASE	0x0008		/* don't fold case in keys */
# define MF_MATCHONLY	0x0010		/* don't use the map value */
# define MF_OPEN	0x0020		/* this entry is open */
# define MF_WRITABLE	0x0040		/* open for writing */
# define MF_ALIAS	0x0080		/* this is an alias file */
# define MF_TRY0NULL	0x0100		/* try with no null byte */
# define MF_TRY1NULL	0x0200		/* try with the null byte */
# define MF_LOCKED	0x0400		/* this map is currently locked */
# define MF_ALIASWAIT	0x0800		/* alias map in aliaswait state */
# define MF_IMPL_HASH	0x1000		/* implicit: underlying hash database */
# define MF_IMPL_NDBM	0x2000		/* implicit: underlying NDBM database */
# define MF_UNSAFEDB	0x4000		/* this map is world writable */

/* indices for map_actions */
# define MA_NOTFOUND	0		/* member map returned "not found" */
# define MA_UNAVAIL	1		/* member map is not available */
# define MA_TRYAGAIN	2		/* member map returns temp failure */

/*
**  The class of a map -- essentially the functions to call
*/

MAPCLASS
{
	char	*map_cname;		/* name of this map class */
	char	*map_ext;		/* extension for database file */
	short	map_cflags;		/* flag bits, see below */
	bool	(*map_parse)__P((MAP *, char *));
					/* argument parsing function */
	char	*(*map_lookup)__P((MAP *, char *, char **, int *));
					/* lookup function */
	void	(*map_store)__P((MAP *, char *, char *));
					/* store function */
	bool	(*map_open)__P((MAP *, int));
					/* open function */
	void	(*map_close)__P((MAP *));
					/* close function */
};

/* bit values for map_cflags */
#define MCF_ALIASOK	0x0001		/* can be used for aliases */
#define MCF_ALIASONLY	0x0002		/* usable only for aliases */
#define MCF_REBUILDABLE	0x0004		/* can rebuild alias files */
#define MCF_OPTFILE	0x0008		/* file name is optional */
/*
**  Symbol table definitions
*/

struct symtab
{
	char		*s_name;	/* name to be entered */
	char		s_type;		/* general type (see below) */
	struct symtab	*s_next;	/* pointer to next in chain */
	union
	{
		BITMAP		sv_class;	/* bit-map of word classes */
		ADDRESS		*sv_addr;	/* pointer to address header */
		MAILER		*sv_mailer;	/* pointer to mailer */
		char		*sv_alias;	/* alias */
		MAPCLASS	sv_mapclass;	/* mapping function class */
		MAP		sv_map;		/* mapping function */
		char		*sv_hostsig;	/* host signature */
		MCI		sv_mci;		/* mailer connection info */
		NAMECANON	sv_namecanon;	/* canonical name cache */
		int		sv_macro;	/* macro name => id mapping */
		int		sv_ruleset;	/* ruleset index */
	}	s_value;
};

typedef struct symtab	STAB;

/* symbol types */
# define ST_UNDEF	0	/* undefined type */
# define ST_CLASS	1	/* class map */
# define ST_ADDRESS	2	/* an address in parsed format */
# define ST_MAILER	3	/* a mailer header */
# define ST_ALIAS	4	/* an alias */
# define ST_MAPCLASS	5	/* mapping function class */
# define ST_MAP		6	/* mapping function */
# define ST_HOSTSIG	7	/* host signature */
# define ST_NAMECANON	8	/* cached canonical name */
# define ST_MACRO	9	/* macro name to id mapping */
# define ST_RULESET	10	/* ruleset index */
# define ST_MCI		16	/* mailer connection info (offset) */

# define s_class	s_value.sv_class
# define s_address	s_value.sv_addr
# define s_mailer	s_value.sv_mailer
# define s_alias	s_value.sv_alias
# define s_mci		s_value.sv_mci

extern STAB		*stab __P((char *, int, int));
extern void		stabapply __P((void (*)(STAB *, int), int));

/* opcodes to stab */
# define ST_FIND	0	/* find entry */
# define ST_ENTER	1	/* enter if not there */
/*
**  STRUCT EVENT -- event queue.
**
**	Maintained in sorted order.
**
**	We store the pid of the process that set this event to insure
**	that when we fork we will not take events intended for the parent.
*/

struct event
{
	time_t		ev_time;	/* time of the function call */
	void		(*ev_func)__P((int));
					/* function to call */
	int		ev_arg;		/* argument to ev_func */
	int		ev_pid;		/* pid that set this event */
	struct event	*ev_link;	/* link to next item */
};

typedef struct event	EVENT;

EXTERN EVENT	*EventQueue;		/* head of event queue */
/*
**  Operation, send, error, and MIME modes
**
**	The operation mode describes the basic operation of sendmail.
**	This can be set from the command line, and is "send mail" by
**	default.
**
**	The send mode tells how to send mail.  It can be set in the
**	configuration file.  It's setting determines how quickly the
**	mail will be delivered versus the load on your system.  If the
**	-v (verbose) flag is given, it will be forced to SM_DELIVER
**	mode.
**
**	The error mode tells how to return errors.
*/

EXTERN char	OpMode;		/* operation mode, see below */

#define MD_DELIVER	'm'		/* be a mail sender */
#define MD_SMTP		's'		/* run SMTP on standard input */
#define MD_ARPAFTP	'a'		/* obsolete ARPANET mode (Grey Book) */
#define MD_DAEMON	'd'		/* run as a daemon */
#define MD_VERIFY	'v'		/* verify: don't collect or deliver */
#define MD_TEST		't'		/* test mode: resolve addrs only */
#define MD_INITALIAS	'i'		/* initialize alias database */
#define MD_PRINT	'p'		/* print the queue */
#define MD_FREEZE	'z'		/* freeze the configuration file */


/* values for e_sendmode -- send modes */
#define SM_DELIVER	'i'		/* interactive delivery */
#define SM_QUICKD	'j'		/* deliver w/o queueing */
#define SM_FORK		'b'		/* deliver in background */
#define SM_QUEUE	'q'		/* queue, don't deliver */
#define SM_VERIFY	'v'		/* verify only (used internally) */

/* used only as a parameter to sendall */
#define SM_DEFAULT	'\0'		/* unspecified, use SendMode */


/* values for e_errormode -- error handling modes */
#define EM_PRINT	'p'		/* print errors */
#define EM_MAIL		'm'		/* mail back errors */
#define EM_WRITE	'w'		/* write back errors */
#define EM_BERKNET	'e'		/* special berknet processing */
#define EM_QUIET	'q'		/* don't print messages (stat only) */


/* MIME processing mode */
EXTERN int	MimeMode;

/* bit values for MimeMode */
#define MM_CVTMIME	0x0001		/* convert 8 to 7 bit MIME */
#define MM_PASS8BIT	0x0002		/* just send 8 bit data blind */
#define MM_MIME8BIT	0x0004		/* convert 8-bit data to MIME */

/* queue sorting order algorithm */
EXTERN int	QueueSortOrder;

#define QS_BYPRIORITY	0		/* sort by message priority */
#define QS_BYHOST	1		/* sort by first host name */
/*
**  Additional definitions
*/


/*
**  Privacy flags
**	These are bit values for the PrivacyFlags word.
*/

#define PRIV_PUBLIC		0	/* what have I got to hide? */
#define PRIV_NEEDMAILHELO	0x0001	/* insist on HELO for MAIL, at least */
#define PRIV_NEEDEXPNHELO	0x0002	/* insist on HELO for EXPN */
#define PRIV_NEEDVRFYHELO	0x0004	/* insist on HELO for VRFY */
#define PRIV_NOEXPN		0x0008	/* disallow EXPN command entirely */
#define PRIV_NOVRFY		0x0010	/* disallow VRFY command entirely */
#define PRIV_AUTHWARNINGS	0x0020	/* flag possible authorization probs */
#define PRIV_NORECEIPTS		0x0040	/* disallow return receipts */
#define PRIV_RESTRICTMAILQ	0x1000	/* restrict mailq command */
#define PRIV_RESTRICTQRUN	0x2000	/* restrict queue run */
#define PRIV_GOAWAY		0x0fff	/* don't give no info, anyway, anyhow */

/* struct defining such things */
struct prival
{
	char	*pv_name;	/* name of privacy flag */
	int	pv_flag;	/* numeric level */
};


/*
**  Flags passed to remotename, parseaddr, allocaddr, and buildaddr.
*/

#define RF_SENDERADDR		0x001	/* this is a sender address */
#define RF_HEADERADDR		0x002	/* this is a header address */
#define RF_CANONICAL		0x004	/* strip comment information */
#define RF_ADDDOMAIN		0x008	/* OK to do domain extension */
#define RF_COPYPARSE		0x010	/* copy parsed user & host */
#define RF_COPYPADDR		0x020	/* copy print address */
#define RF_COPYALL		(RF_COPYPARSE|RF_COPYPADDR)
#define RF_COPYNONE		0


/*
**  Flags passed to safefile.
*/

#define SFF_ANYFILE		0	/* no special restrictions */
#define SFF_MUSTOWN		0x0001	/* user must own this file */
#define SFF_NOSLINK		0x0002	/* file cannot be a symbolic link */
#define SFF_ROOTOK		0x0004	/* ok for root to own this file */
#define SFF_NOPATHCHECK		0x0010	/* don't bother checking dir path */


/*
**  Regular UNIX sockaddrs are too small to handle ISO addresses, so
**  we are forced to declare a supertype here.
*/

union bigsockaddr
{
	struct sockaddr		sa;	/* general version */
#ifdef NETUNIX
	struct sockaddr_un	sunix;	/* UNIX family */
#endif
#ifdef NETINET
	struct sockaddr_in	sin;	/* INET family */
#endif
#ifdef NETISO
	struct sockaddr_iso	siso;	/* ISO family */
#endif
#ifdef NETNS
	struct sockaddr_ns	sns;	/* XNS family */
#endif
#ifdef NETX25
	struct sockaddr_x25	sx25;	/* X.25 family */
#endif
};

#define SOCKADDR	union bigsockaddr


/*
**  Vendor codes
**
**	Vendors can customize sendmail to add special behaviour,
**	generally for back compatibility.  Ideally, this should
**	be set up in the .cf file using the "V" command.  However,
**	it's quite reasonable for some vendors to want the default
**	be their old version; this can be set using
**		-DVENDOR_DEFAULT=VENDOR_xxx
**	in the Makefile.
**
**	Vendors should apply to sendmail@CS.Berkeley.EDU for
**	unique vendor codes.
*/

#define VENDOR_BERKELEY	1	/* Berkeley-native configuration file */
#define VENDOR_SUN	2	/* Sun-native configuration file */

EXTERN int	VendorCode;	/* vendor-specific operation enhancements */
/*
**  Global variables.
*/

EXTERN bool	FromFlag;	/* if set, "From" person is explicit */
EXTERN bool	MeToo;		/* send to the sender also */
EXTERN bool	IgnrDot;	/* don't let dot end messages */
EXTERN bool	SaveFrom;	/* save leading "From" lines */
EXTERN bool	Verbose;	/* set if blow-by-blow desired */
EXTERN bool	GrabTo;		/* if set, get recipients from msg */
EXTERN bool	SuprErrs;	/* set if we are suppressing errors */
EXTERN bool	HoldErrs;	/* only output errors to transcript */
EXTERN bool	NoConnect;	/* don't connect to non-local mailers */
EXTERN bool	SuperSafe;	/* be extra careful, even if expensive */
EXTERN bool	ForkQueueRuns;	/* fork for each job when running the queue */
EXTERN bool	AutoRebuild;	/* auto-rebuild the alias database as needed */
EXTERN bool	CheckAliases;	/* parse addresses during newaliases */
EXTERN bool	NoAlias;	/* suppress aliasing */
EXTERN bool	UseNameServer;	/* using DNS -- interpret h_errno & MX RRs */
EXTERN bool	UseHesiod;	/* using Hesiod -- interpret Hesiod errors */
EXTERN bool	SevenBitInput;	/* force 7-bit data on input */
EXTERN bool	HasEightBits;	/* has at least one eight bit input byte */
EXTERN time_t	SafeAlias;	/* interval to wait until @:@ in alias file */
EXTERN FILE	*InChannel;	/* input connection */
EXTERN FILE	*OutChannel;	/* output connection */
EXTERN uid_t	RealUid;	/* when Daemon, real uid of caller */
EXTERN gid_t	RealGid;	/* when Daemon, real gid of caller */
EXTERN uid_t	DefUid;		/* default uid to run as */
EXTERN gid_t	DefGid;		/* default gid to run as */
EXTERN char	*DefUser;	/* default user to run as (from DefUid) */
EXTERN int	OldUmask;	/* umask when sendmail starts up */
EXTERN int	Errors;		/* set if errors (local to single pass) */
EXTERN int	ExitStat;	/* exit status code */
EXTERN int	LineNumber;	/* line number in current input */
EXTERN int	LogLevel;	/* level of logging to perform */
EXTERN int	FileMode;	/* mode on files */
EXTERN int	QueueLA;	/* load average starting forced queueing */
EXTERN int	RefuseLA;	/* load average refusing connections are */
EXTERN int	CurrentLA;	/* current load average */
EXTERN long	QueueFactor;	/* slope of queue function */
EXTERN time_t	QueueIntvl;	/* intervals between running the queue */
EXTERN char	*HelpFile;	/* location of SMTP help file */
EXTERN char	*ErrMsgFile;	/* file to prepend to all error messages */
EXTERN char	*StatFile;	/* location of statistics summary */
EXTERN char	*QueueDir;	/* location of queue directory */
EXTERN char	*FileName;	/* name to print on error messages */
EXTERN char	*SmtpPhase;	/* current phase in SMTP processing */
EXTERN char	*MyHostName;	/* name of this host for SMTP messages */
EXTERN char	*RealHostName;	/* name of host we are talking to */
EXTERN SOCKADDR RealHostAddr;	/* address of host we are talking to */
EXTERN char	*CurHostName;	/* current host we are dealing with */
EXTERN jmp_buf	TopFrame;	/* branch-to-top-of-loop-on-error frame */
EXTERN bool	QuickAbort;	/*  .... but only if we want a quick abort */
EXTERN bool	LogUsrErrs;	/* syslog user errors (e.g., SMTP RCPT cmd) */
EXTERN bool	SendMIMEErrors;	/* send error messages in MIME format */
EXTERN bool	MatchGecos;	/* look for user names in gecos field */
EXTERN bool	UseErrorsTo;	/* use Errors-To: header (back compat) */
EXTERN bool	TryNullMXList;	/* if we are the best MX, try host directly */
EXTERN bool	InChild;	/* true if running in an SMTP subprocess */
EXTERN bool	DisConnected;	/* running with OutChannel redirected to xf */
EXTERN char	SpaceSub;	/* substitution for <lwsp> */
EXTERN int	PrivacyFlags;	/* privacy flags */
EXTERN char	*ConfFile;	/* location of configuration file [conf.c] */
extern char	*PidFile;	/* location of proc id file [conf.c] */
extern ADDRESS	NullAddress;	/* a null (template) address [main.c] */
EXTERN long	WkClassFact;	/* multiplier for message class -> priority */
EXTERN long	WkRecipFact;	/* multiplier for # of recipients -> priority */
EXTERN long	WkTimeFact;	/* priority offset each time this job is run */
EXTERN char	*UdbSpec;	/* user database source spec */
EXTERN int	MaxHopCount;	/* max # of hops until bounce */
EXTERN int	ConfigLevel;	/* config file level */
EXTERN char	*TimeZoneSpec;	/* override time zone specification */
EXTERN char	*ForwardPath;	/* path to search for .forward files */
EXTERN long	MinBlocksFree;	/* min # of blocks free on queue fs */
EXTERN char	*FallBackMX;	/* fall back MX host */
EXTERN long	MaxMessageSize;	/* advertised max size we will accept */
EXTERN time_t	MaxHostStatAge;	/* max age of cached host status info */
EXTERN time_t	MinQueueAge;	/* min delivery interval */
EXTERN time_t	DialDelay;	/* delay between dial-on-demand tries */
EXTERN char	*ServiceSwitchFile;	/* backup service switch */
EXTERN char	*DefaultCharSet;	/* default character set for MIME */
EXTERN int	DeliveryNiceness;	/* how nice to be during delivery */
EXTERN char	*PostMasterCopy;	/* address to get errs cc's */
EXTERN int	CheckpointInterval;	/* queue file checkpoint interval */
EXTERN bool	DontPruneRoutes;	/* don't prune source routes */
EXTERN bool	BrokenSmtpPeers;	/* peers can't handle 2-line greeting */
EXTERN int	MaxMciCache;		/* maximum entries in MCI cache */
EXTERN time_t	MciCacheTimeout;	/* maximum idle time on connections */
EXTERN char	*QueueLimitRecipient;	/* limit queue runs to this recipient */
EXTERN char	*QueueLimitSender;	/* limit queue runs to this sender */
EXTERN char	*QueueLimitId;		/* limit queue runs to this id */
EXTERN FILE	*TrafficLogFile;	/* file in which to log all traffic */
extern int	errno;


/*
**  Timeouts
**
**	Indicated values are the MINIMUM per RFC 1123 section 5.3.2.
*/

EXTERN struct
{
			/* RFC 1123-specified timeouts [minimum value] */
	time_t	to_initial;	/* initial greeting timeout [5m] */
	time_t	to_mail;	/* MAIL command [5m] */
	time_t	to_rcpt;	/* RCPT command [5m] */
	time_t	to_datainit;	/* DATA initiation [2m] */
	time_t	to_datablock;	/* DATA block [3m] */
	time_t	to_datafinal;	/* DATA completion [10m] */
	time_t	to_nextcommand;	/* next command [5m] */
			/* following timeouts are not mentioned in RFC 1123 */
	time_t	to_rset;	/* RSET command */
	time_t	to_helo;	/* HELO command */
	time_t	to_quit;	/* QUIT command */
	time_t	to_miscshort;	/* misc short commands (NOOP, VERB, etc) */
	time_t	to_ident;	/* IDENT protocol requests */
	time_t	to_fileopen;	/* opening :include: and .forward files */
			/* following are per message */
	time_t	to_q_return[MAXTOCLASS];	/* queue return timeouts */
	time_t	to_q_warning[MAXTOCLASS];	/* queue warning timeouts */
} TimeOuts;

/* timeout classes for return and warning timeouts */
# define TOC_NORMAL	0	/* normal delivery */
# define TOC_URGENT	1	/* urgent delivery */
# define TOC_NONURGENT	2	/* non-urgent delivery */


/*
**  Trace information
*/

/* trace vector and macros for debugging flags */
EXTERN u_char	tTdvect[100];
# define tTd(flag, level)	(tTdvect[flag] >= level)
# define tTdlevel(flag)		(tTdvect[flag])
/*
**  Miscellaneous information.
*/



/*
**  Some in-line functions
*/

/* set exit status */
#define setstat(s)	{ \
				if (ExitStat == EX_OK || ExitStat == EX_TEMPFAIL) \
					ExitStat = s; \
			}

/* make a copy of a string */
#define newstr(s)	strcpy(xalloc(strlen(s) + 1), s)

#define STRUCTCOPY(s, d)	d = s


/*
**  Declarations of useful functions
*/

extern ADDRESS		*parseaddr __P((char *, ADDRESS *, int, int, char **, ENVELOPE *));
extern char		*xalloc __P((int));
extern bool		sameaddr __P((ADDRESS *, ADDRESS *));
extern FILE		*dfopen __P((char *, int, int));
extern EVENT		*setevent __P((time_t, void(*)(), int));
extern char		*sfgets __P((char *, int, FILE *, time_t, char *));
extern char		*queuename __P((ENVELOPE *, int));
extern time_t		curtime __P(());
extern bool		transienterror __P((int));
extern const char	*errstring __P((int));
extern void		expand __P((char *, char *, char *, ENVELOPE *));
extern void		define __P((int, char *, ENVELOPE *));
extern char		*macvalue __P((int, ENVELOPE *));
extern char		*macname __P((int));
extern int		macid __P((char *, char **));
extern char		**prescan __P((char *, int, char[], int, char **));
extern int		rewrite __P((char **, int, int, ENVELOPE *));
extern char		*fgetfolded __P((char *, int, FILE *));
extern ADDRESS		*recipient __P((ADDRESS *, ADDRESS **, int, ENVELOPE *));
extern ENVELOPE		*newenvelope __P((ENVELOPE *, ENVELOPE *));
extern void		dropenvelope __P((ENVELOPE *));
extern void		clearenvelope __P((ENVELOPE *, int));
extern char		*username __P(());
extern MCI		*mci_get __P((char *, MAILER *));
extern char		*pintvl __P((time_t, int));
extern char		*map_rewrite __P((MAP *, char *, int, char **));
extern ADDRESS		*getctladdr __P((ADDRESS *));
extern char		*anynet_ntoa __P((SOCKADDR *));
extern char		*remotename __P((char *, MAILER *, int, int *, ENVELOPE *));
extern bool		shouldqueue __P((long, time_t));
extern bool		lockfile __P((int, char *, char *, int));
extern char		*hostsignature __P((MAILER *, char *, ENVELOPE *));
extern void		openxscript __P((ENVELOPE *));
extern void		closexscript __P((ENVELOPE *));
extern sigfunc_t	setsignal __P((int, sigfunc_t));
extern char		*shortenstring __P((char *, int));
extern bool		usershellok __P((char *));
extern void		commaize __P((HDR *, char *, int, MCI *, ENVELOPE *));
extern char		*hvalue __P((char *, HDR *));
extern char		*defcharset __P((ENVELOPE *));
extern bool		emptyaddr __P((ADDRESS *));
extern int		sendtolist __P((char *, ADDRESS *, ADDRESS **, int, ENVELOPE *));
extern bool		wordinclass __P((char *, char));
extern char		*denlstring __P((char *));
extern void		printaddr __P((ADDRESS *, bool));
extern void		makelower __P((char *));
extern void		rebuildaliases __P((MAP *, bool));
extern void		readaliases __P((MAP *, FILE *, bool, bool));
extern void		finis __P(());
extern void		clrevent __P((EVENT *));
extern void		setsender __P((char *, ENVELOPE *, char **, bool));

/* ellipsis is a different case though */
#ifdef __STDC__
extern void		auth_warning(ENVELOPE *, const char *, ...);
extern void		syserr(const char *, ...);
extern void		usrerr(const char *, ...);
extern void		message(const char *, ...);
extern void		nmessage(const char *, ...);
#else
extern void		auth_warning();
extern void		syserr();
extern void		usrerr();
extern void		message();
extern void		nmessage();
#endif

/*
**  HACK to fix bug in C compiler on CCI
*/

#undef isascii
#define isascii(x)	(((x) & ~0177) == 0)
