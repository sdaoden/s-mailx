/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Constants, types etc.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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

/*
 * Mail -- a mail program
 *
 * Author: Kurt Shoens (UCB) March 25, 1978
 */

#if ! defined NI_MAXHOST || NI_MAXHOST < 1025
# undef NI_MAXHOST
# define NI_MAXHOST	1025
#endif

#define APPEND				/* New mail goes to end of mailbox */

#ifndef MAXPATHLEN
# ifdef PATH_MAX
#  define MAXPATHLEN	PATH_MAX
# else
#  define MAXPATHLEN	1024
# endif
#endif
#if BUFSIZ > 2560			/* TODO simply use BUFSIZ? */
# define LINESIZE	BUFSIZ		/* max readable line width */
#else
# define LINESIZE	2560
#endif

#ifndef STDIN_FILENO
# define STDIN_FILENO	0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO	1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO	2
#endif

#define MAXARGC		1024		/* Maximum list of raw strings */
#define MAXEXP		25		/* Maximum expansion of aliases */
#define HSHSIZE		23		/* Hash size aliases, vars, macros */

#define FROM_DATEBUF	64		/* Size of RFC 4155 From_ line date */
#define DATE_DAYSYEAR	365L
#define DATE_SECSMIN	60L
#define DATE_MINSHOUR	60L
#define DATE_HOURSDAY	24L
#define DATE_SECSDAY	(DATE_SECSMIN * DATE_MINSHOUR * DATE_HOURSDAY)

#define ESCAPE		'~'		/* Default escape for sending */

#define CBAD		(-15555)

#define SHELL		"/bin/sh"
#define LISTER		"ls"
#define PAGER		"more"

#define HIST_SIZE	242

/*
 * Funs, CC support etc.
 */

/* Members in constant array */
#define ARRAY_COUNT(A)	(sizeof(A) / sizeof(A[0]))

/* sizeof() for member fields */
#define SIZEOF_FIELD(T,F) sizeof(((T *)NULL)->F)

/* Casts-away (*NOT* cast-away) */
#define UNCONST(P)	((void*)(unsigned long)(void const*)(P))
#define UNVOLATILE(P)	((void*)(unsigned long)(void volatile*)(P))

/* __STDC_VERSION__ is ISO C99, so also use __STDC__, which should work */
#if defined __STDC__ || defined __STDC_VERSION__ /*|| defined __cplusplus*/
# define STRING(X)	#X
# define XSTRING(X)	STRING(X)
# define CONCAT(S1,S2)	_CONCAT(S1, S2)
# define _CONCAT(S1,S2)	S1 ## S2
#else
# define STRING(X)	"X"
# define XSTRING	STRING
# define CONCAT(S1,S2)	S1/**/S2
#endif

#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
  /* Variable size arrays and structure fields */
# define VFIELD_SIZE(X)
# define VFIELD_SIZEOF(T,F)	(0)
  /* Inline functions */
# define HAVE_INLINE
# define INLINE			inline
# define SINLINE		static inline
#else
# define VFIELD_SIZE(X)		(X)
# define VFIELD_SIZEOF(T,F)	SIZEOF_FIELD(T, F)
# define INLINE
# define SINLINE		static
#endif

#undef ISPOW2
#define ISPOW2(X)	((((X) - 1) & (X)) == 0)
#undef MIN
#define MIN(A, B)	((A) < (B) ? (A) : (B))
#undef MAX
#define MAX(A, B)	((A) < (B) ? (B) : (A))
#undef ABS
#define ABS(A)		((A) < 0 ? -(A) : (A))

#define smin(a, b)	((a) < (b) ? (a) : (b)) /* TODO OBSOLETE */
#define smax(a, b)	((a) < (b) ? (b) : (a)) /* TODO OBSOLETE */

/* Compile-Time-Assert */
#define CTA(TEST)	_CTA_1(TEST, __LINE__)
#define _CTA_1(TEST,L)  _CTA_2(TEST, L)
#define _CTA_2(TEST,L)	\
	typedef char COMPILE_TIME_ASSERT_failed_at_line_ ## L[(TEST) ? 1 : -1]

/*
 * MIME (mime.c)
 */

/* Is *C* a quoting character (for *quote-fold* compression) */
#define ISQUOTE(C)	((C) == '>' || (C) == '|' || (C) == '}')

/* Locations of mime.types(5) */
#define MIME_TYPES_USR	"~/.mime.types"
#define MIME_TYPES_SYS	"/etc/mime.types"

/* Default *encoding* as enum conversion below */
#define MIME_DEFAULT_ENCODING	CONV_TOQP

/* Maximum allowed line length in a mail before QP folding is necessary), and
 * the real limit we go for */
#define MIME_LINELEN_MAX	1000
#define MIME_LINELEN_LIMIT	(MIME_LINELEN_MAX - 50)

/* Fallback charsets, if *charset-7bit* and *charset-8bit* or not set, resp. */
#define CHARSET_7BIT		"US-ASCII"
#ifdef HAVE_ICONV
# define CHARSET_8BIT		"UTF-8"
# define CHARSET_8BIT_VAR	"charset-8bit"
#else
# define CHARSET_8BIT		"ISO-8859-1"
# define CHARSET_8BIT_VAR	"ttycharset"
#endif

/*
 * Auto-reclaimed string storage (strings.c)
 */

/* Dynamic buffer size, and size of the single builtin one that's used first */
#define SBUFFER_SIZE	0x18000u
#define SBUFFER_BUILTIN	0x2000u

/* Huge allocation if GT; those are never cached but will be auto freed */
#define SHUGE_CUTLIMIT	LINESIZE

/*
 * Translation (init in main.c) TODO convert all catgets() that remain to tr()
 */
#undef tr
#ifdef HAVE_CATGETS
# define CATSET		1
# define tr(c,d)	catgets(catd, CATSET, c, d)
#else
# define catgets(a,b,c,d) (d)
# define tr(c,d)	(d)
#endif

/*
 * Types
 */

typedef unsigned long	ul_it;
typedef unsigned int	ui_it;
typedef unsigned short	us_it;
typedef unsigned char	uc_it;

typedef signed long	sl_it;
typedef signed int	si_it;
typedef signed short	ss_it;
typedef signed char	sc_it;

typedef enum {FAL0, TRU1} bool_t;

typedef void (		*sighandler_type)(int);

enum user_options {
	OPT_NONE	= 0,
	OPT_DEBUG	= 1<< 0,	/* -d / *debug* */
	OPT_VERBOSE	= 1<< 1,	/* -v / *verbose* */
	OPT_EXISTONLY	= 1<< 2,	/* -e */
	OPT_HEADERSONLY	= 1<< 3,	/* -H */
	OPT_NOSRC	= 1<< 4,	/* -n */
	OPT_E_FLAG	= 1<< 5,	/* -E / *skipemptybody* */
	OPT_F_FLAG	= 1<< 6,	/* -F */
	OPT_N_FLAG	= 1<< 7,	/* -N / *header* */
	OPT_R_FLAG	= 1<< 8,	/* -R */
	OPT_r_FLAG	= 1<< 9,	/* -r (plus option_r_arg) */
	OPT_t_FLAG	= 1<<10,	/* -t */
	OPT_TILDE_FLAG	= 1<<11,	/* -~ */
	OPT_BATCH_FLAG	= 1<<12,	/* -# */

	OPT_SENDMODE	= 1<<13,	/* Usage case forces send mode */
	OPT_INTERACTIVE	= 1<<14,	/* isatty(0) */
	OPT_TTYIN	= OPT_INTERACTIVE,
	OPT_TTYOUT	= 1<<15
};
#define IS_TTY_SESSION() \
	((options & (OPT_TTYIN | OPT_TTYOUT)) == (OPT_TTYIN | OPT_TTYOUT))

enum exit_status {
	EXIT_OK		= EXIT_SUCCESS,
	EXIT_ERR	= EXIT_FAILURE,
	EXIT_COLL_ABORT	= 1<<1,		/* Message collection was aborted */
	EXIT_SEND_ERROR	= 1<<2		/* Unspecified send error occurred */
};

enum fexp_mode {
	FEXP_FULL,			/* Full expansion */
	FEXP_LOCAL 	= 1<<0,		/* Result must be local file/maildir */
	FEXP_SHELL 	= 1<<1,		/* No folder %,#,&,+ stuff, yet sh(1) */
	FEXP_NSHORTCUT	= 1<<2,		/* Don't expand shortcuts */
	FEXP_SILENT	= 1<<3,		/* Don't print but only return errors */
	FEXP_MULTIOK	= 1<<4		/* Expansion to many entries is ok */
};

enum lned_mode {
	LNED_NONE	= 0,
	LNED_LF_ESC	= 1<<0,		/* LF can be backslash escaped */
	LNED_HIST_ADD	= 1<<1		/* Add completed line to history */
};

enum okay {
	STOP = 0,
	OKAY = 1
};

enum mimeenc {
	MIME_NONE,			/* message is not in MIME format */
	MIME_BIN,			/* message is in binary encoding */
	MIME_8B,			/* message is in 8bit encoding */
	MIME_7B,			/* message is in 7bit encoding */
	MIME_QP,			/* message is quoted-printable */
	MIME_B64			/* message is in base64 encoding */
};

enum conversion {
	CONV_NONE,			/* no conversion */
	CONV_7BIT,			/* no conversion, is 7bit */
	CONV_FROMQP,			/* convert from quoted-printable */
	CONV_TOQP,			/* convert to quoted-printable */
	CONV_8BIT,			/* convert to 8bit (iconv) */
	CONV_FROMB64,			/* convert from base64 */
	CONV_FROMB64_T,			/* convert from base64/text */
	CONV_TOB64,			/* convert to base64 */
	CONV_FROMHDR,			/* convert from RFC1522 format */
	CONV_TOHDR,			/* convert to RFC1522 format */
	CONV_TOHDR_A			/* convert addresses for header */
};

enum sendaction {
	SEND_MBOX,			/* no conversion to perform */
	SEND_RFC822,			/* no conversion, no From_ line */
	SEND_TODISP,			/* convert to displayable form */
	SEND_TODISP_ALL,		/* same, include all MIME parts */
	SEND_SHOW,			/* convert to 'show' command form */
	SEND_TOSRCH,			/* convert for IMAP SEARCH */
	SEND_TOFLTR,			/* convert for junk mail filtering */
	SEND_TOFILE,			/* convert for saving body to a file */
	SEND_TOPIPE,			/* convert for pipe-content/subc. */
	SEND_QUOTE,			/* convert for quoting */
	SEND_QUOTE_ALL,			/* same, include all MIME parts */
	SEND_DECRYPT			/* decrypt */
};

enum mimecontent {
	MIME_UNKNOWN,			/* unknown content */
	MIME_SUBHDR,			/* inside a multipart subheader */
	MIME_822,			/* message/rfc822 content */
	MIME_MESSAGE,			/* other message/ content */
	MIME_TEXT_PLAIN,		/* text/plain content */
	MIME_TEXT_HTML,			/* text/html content */
	MIME_TEXT,			/* other text/ content */
	MIME_ALTERNATIVE,		/* multipart/alternative content */
	MIME_DIGEST,			/* multipart/digest content */
	MIME_MULTI,			/* other multipart/ content */
	MIME_PKCS7,			/* PKCS7 content */
	MIME_DISCARD			/* content is discarded */
};

enum tdflags {
	TD_NONE		= 0,	/* no display conversion */
	TD_ISPR		= 1<<0,	/* use isprint() checks */
	TD_ICONV	= 1<<1,	/* use iconv() */
	TD_DELCTRL	= 1<<2,	/* delete control characters */

	/*
	 * NOTE: _TD_EOF and _TD_BUFCOPY may be ORd with enum conversion and
	 * enum sendaction, and may thus NOT clash with their bit range!
	 */
	_TD_EOF		= 1<<14,/* EOF seen, last round! */
	_TD_BUFCOPY	= 1<<15	/* Buffer may be constant, copy it */
};

enum protocol {
	PROTO_FILE,			/* refers to a local file */
	PROTO_POP3,			/* is a pop3 server string */
	PROTO_IMAP,			/* is an imap server string */
	PROTO_MAILDIR,			/* refers to a maildir folder */
	PROTO_UNKNOWN			/* unknown protocol */
};

#ifdef USE_SSL
enum ssl_vrfy_level {
	VRFY_IGNORE,
	VRFY_WARN,
	VRFY_ASK,
	VRFY_STRICT
};
#endif

struct str {
	char	*s;			/* the string's content */
	size_t	l;			/* the stings's length */
};

struct time_current {
	time_t		tc_time;
	struct tm	tc_gm;
	struct tm	tc_local;
	char		tc_ctime[32];
};

struct termios_state {
	struct termios	ts_tios;
	char		*ts_linebuf;
	size_t		ts_linesize;
	bool_t		ts_needs_reset;
};

#define termios_state_reset() \
do {\
	if (termios_state.ts_needs_reset) {\
		tcsetattr(0, TCSADRAIN, &termios_state.ts_tios);\
		termios_state.ts_needs_reset = FAL0;\
	}\
} while (0)

struct sock {				/* data associated with a socket */
	int	s_fd;			/* file descriptor */
#ifdef USE_SSL
	int	s_use_ssl;		/* SSL is used */
# ifdef USE_OPENSSL
	void	*s_ssl;			/* SSL object */
	void	*s_ctx;			/* SSL context object */
# endif
#endif
	char	*s_wbuf;		/* for buffered writes */
	int	s_wbufsize;		/* allocated size of s_buf */
	int	s_wbufpos;		/* position of first empty data byte */
	char	*s_rbufptr;		/* read pointer to s_rbuf */
	int	s_rsz;			/* size of last read in s_rbuf */
	char const *s_desc;		/* description of error messages */
	void	(*s_onclose)(void);	/* execute on close */
	char	s_rbuf[LINESIZE + 1];	/* for buffered reads */
};

struct mailbox {
	enum {
		MB_NONE		= 000,	/* no reply expected */
		MB_COMD		= 001,	/* command reply expected */
		MB_MULT		= 002,	/* multiline reply expected */
		MB_PREAUTH	= 004,	/* not in authenticated state */
		MB_BYE		= 010	/* may accept a BYE state */
	} mb_active;
	FILE *mb_itf;			/* temp file with messages, read open */
	FILE *mb_otf;			/* same, write open */
	char *mb_sorted;		/* sort method */
	enum {
		MB_VOID,		/* no type (e. g. connection failed) */
		MB_FILE,		/* local file */
		MB_POP3,		/* POP3 mailbox */
		MB_IMAP,		/* IMAP mailbox */
		MB_MAILDIR,		/* maildir folder */
		MB_CACHE		/* cached mailbox */
	} mb_type;			/* type of mailbox */
	enum {
		MB_DELE = 01,		/* may delete messages in mailbox */
		MB_EDIT = 02		/* may edit messages in mailbox */
	} mb_perm;
	int mb_compressed;		/* is a compressed mbox file */
	int mb_threaded;		/* mailbox has been threaded */
#ifdef USE_IMAP
	enum mbflags {
		MB_NOFLAGS	= 000,
		MB_UIDPLUS	= 001	/* supports IMAP UIDPLUS */
	} mb_flags;
	unsigned long	mb_uidvalidity;	/* IMAP unique identifier validity */
	char	*mb_imap_account;	/* name of current IMAP account */
	char	*mb_imap_mailbox;	/* name of current IMAP mailbox */
	char	*mb_cache_directory;	/* name of cache directory */
#endif
	struct sock	mb_sock;	/* socket structure */
};

enum needspec {
	NEED_UNSPEC,			/* unspecified need, don't fetch */
	NEED_HEADER,			/* need the header of a message */
	NEED_BODY			/* need header and body of a message */
};

enum havespec {
	HAVE_NOTHING = 0,		/* nothing downloaded yet */
	HAVE_HEADER = 01,		/* header is downloaded */
	HAVE_BODY = 02			/* entire message is downloaded */
};

/*
 * flag bits. Attention: Flags that are used in cache.c may not change.
 */
enum mflag {
	MUSED		= (1<<0),	/* entry is used, but this bit isn't */
	MDELETED	= (1<<1),	/* entry has been deleted */
	MSAVED		= (1<<2),	/* entry has been saved */
	MTOUCH		= (1<<3),	/* entry has been noticed */
	MPRESERVE	= (1<<4),	/* keep entry in sys mailbox */
	MMARK		= (1<<5),	/* message is marked! */
	MODIFY		= (1<<6),	/* message has been modified */
	MNEW		= (1<<7),	/* message has never been seen */
	MREAD		= (1<<8),	/* message has been read sometime. */
	MSTATUS		= (1<<9),	/* message status has changed */
	MBOX		= (1<<10),	/* Send this to mbox, regardless */
	MNOFROM		= (1<<11),	/* no From line */
	MHIDDEN		= (1<<12),	/* message is hidden to user */
	MFULLYCACHED	= (1<<13),	/* message is completely cached */
	MBOXED		= (1<<14),	/* message has been sent to mbox */
	MUNLINKED	= (1<<15),	/* message was unlinked from cache */
	MNEWEST		= (1<<16),	/* message is very new (newmail) */
	MFLAG		= (1<<17),	/* message has been flagged recently */
	MUNFLAG		= (1<<18),	/* message has been unflagged */
	MFLAGGED	= (1<<19),	/* message is `flagged' */
	MANSWER		= (1<<20),	/* message has been answered recently */
	MUNANSWER	= (1<<21),	/* message has been unanswered */
	MANSWERED	= (1<<22),	/* message is `answered' */
	MDRAFT		= (1<<23),	/* message has been drafted recently */
	MUNDRAFT	= (1<<24),	/* message has been undrafted */
	MDRAFTED	= (1<<25),	/* message is marked as `draft' */
	MKILL		= (1<<26),	/* message has been killed */
	MOLDMARK	= (1<<27),	/* messages was marked previously */
	MJUNK		= (1<<28)	/* message is classified as junk */
};

struct mimepart {
	enum mflag	m_flag;		/* flags */
	enum havespec	m_have;		/* downloaded parts of the part */
	int	m_block;		/* block number of this part */
	size_t	m_offset;		/* offset in block of part */
	size_t	m_size;			/* Bytes in the part */
	size_t	m_xsize;		/* Bytes in the full part */
	long	m_lines;		/* Lines in the message */
	long	m_xlines;		/* Lines in the full message */
	time_t	m_time;			/* time the message was sent */
	char const *m_from;		/* message sender */
	struct mimepart	*m_nextpart;	/* next part at same level */
	struct mimepart	*m_multipart;	/* parts of multipart */
	struct mimepart	*m_parent;	/* enclosing multipart part */
	char	*m_ct_type;		/* content-type */
	char	*m_ct_type_plain;	/* content-type without specs */
	enum mimecontent	m_mimecontent;	/* same in enum */
	char const *m_charset;		/* charset */
	char	*m_ct_transfer_enc;	/* content-transfer-encoding */
	enum mimeenc	m_mimeenc;	/* same in enum */
	char	*m_partstring;		/* part level string */
	char	*m_filename;		/* attachment filename */
};

struct message {
	enum mflag	m_flag;		/* flags */
	enum havespec	m_have;		/* downloaded parts of the message */
	int	m_block;		/* block number of this message */
	size_t	m_offset;		/* offset in block of message */
	size_t	m_size;			/* Bytes in the message */
	size_t	m_xsize;		/* Bytes in the full message */
	long	m_lines;		/* Lines in the message */
	long	m_xlines;		/* Lines in the full message */
	time_t	m_time;			/* time the message was sent */
	time_t	m_date;			/* time in the 'Date' field */
	unsigned	m_idhash;	/* hash on Message-ID for threads */
	struct message	*m_child;	/* first child of this message */
	struct message	*m_younger;	/* younger brother of this message */
	struct message	*m_elder;	/* elder brother of this message */
	struct message	*m_parent;	/* parent of this message */
	unsigned	m_level;	/* thread level of message */
	long		m_threadpos;	/* position in threaded display */
#ifdef USE_SCORE
	float		m_score;	/* score of message */
#endif
#ifdef USE_IMAP
	unsigned long	m_uid;		/* IMAP unique identifier */
#endif
	char	*m_maildir_file;	/* original maildir file of msg */
	unsigned	m_maildir_hash;	/* hash of file name in maildir sub */
	int	m_collapsed;		/* collapsed thread information */
};

/*
 * Given a file address, determine the block number it represents.
 */
#define mailx_blockof(off)		((int) ((off) / 4096))
#define mailx_offsetof(off)		((int) ((off) % 4096))
#define mailx_positionof(block, offset)	((off_t)(block) * 4096 + (offset))

/*
 * Argument types.
 */
enum argtype {
	MSGLIST	= 0,		/* Message list type */
	STRLIST	= 1,		/* A pure string */
	RAWLIST	= 2,		/* Shell string list */
	NOLIST	= 3,		/* Just plain 0 */
	NDMLIST	= 4,		/* Message list, no defaults */
	ECHOLIST= 5,		/* Like raw list, but keep quote chars */
	P	= 040,		/* Autoprint dot after command */
	I	= 0100,		/* Interactive command bit */
	M	= 0200,		/* Legal from send mode bit */
	W	= 0400,		/* Illegal when read only bit */
	F	= 01000,	/* Is a conditional command */
	T	= 02000,	/* Is a transparent command */
	R	= 04000,	/* Cannot be called from collect */
	A	= 010000	/* Needs an active mailbox */
};

/*
 * Oft-used mask values
 */
#define MMNORM	(MDELETED|MSAVED|MHIDDEN) /* Look at save *and* delete bits */
#define MMNDEL		(MDELETED|MHIDDEN)	/* Look only at deleted bit */

/*
 * Format of the command description table.
 * The actual table is declared and initialized
 * in lex.c
 */
struct cmd {
	char const	*c_name;		/* Name of command */
	int		(*c_func)(void *);	/* Implementor of command */
	enum argtype	c_argtype;		/* Arglist type (see below) */
	short		c_msgflag;		/* Required flags of msgs*/
	short		c_msgmask;		/* Relevant flags of msgs */
#ifdef USE_DOCSTRINGS
	int		c_docid;		/* Translation id of .c_doc */
	char const	*c_doc;			/* One line doc for command */
#endif
};

/* Yechh, can't initialize unions */

#define c_minargs	c_msgflag	/* Minimum argcount for RAWLIST */
#define c_maxargs	c_msgmask	/* Max argcount for RAWLIST */

enum gfield {
	GTO	= 1,		/* Grab To: line */
	GSUBJECT= 2,		/* Likewise, Subject: line */
	GCC	= 4,		/* And the Cc: line */
	GBCC	= 8,		/* And also the Bcc: line */

	GNL	= 16,		/* Print blank line after */
	GDEL	= 32,		/* Entity removed from list */
	GCOMMA	= 64,		/* detract puts in commas */
	GUA	= 128,		/* User-Agent field */
	GMIME	= 256,		/* MIME 1.0 fields */
	GMSGID	= 512,		/* a Message-ID */
	/*	  1024 */	/* unused */
	GIDENT	= 2048,		/* From:, Reply-To: and Organization: field */
	GREF	= 4096,		/* References: field */
	GDATE	= 8192,		/* Date: field */
	GFULL	= 16384,	/* include full names */
	GSKIN	= 32768,	/* skin names */
	GEXTRA	= 65536, 	/* extra fields */
	GFILES	= 131072	/* include filename addresses */
};

#define GMASK		(GTO|GSUBJECT|GCC|GBCC)	/* Mask of places from whence */

#define visible(mp)	(((mp)->m_flag & (MDELETED|MHIDDEN|MKILL)) == 0 || \
				(dot == (mp) && (mp)->m_flag & MKILL))

/*
 * Structure used to pass about the current state of a message (header).
 */
struct header {
	struct name	*h_to;		/* Dynamic "To:" string */
	char		*h_subject;	/* Subject string */
	struct name	*h_cc;		/* Carbon copies string */
	struct name	*h_bcc;		/* Blind carbon copies */
	struct name	*h_ref;		/* References */
	struct attachment *h_attach;	/* MIME attachments */
	char		*h_charset;	/* preferred charset */
	struct name	*h_from;	/* overridden "From:" field */
	struct name	*h_replyto;	/* overridden "Reply-To:" field */
	struct name	*h_sender;	/* overridden "Sender:" field */
	char		*h_organization; /* overridden "Organization:" field */
};

/*
 * Handling of namelist nodes used in processing the recipients of mail and
 * aliases, inspection of mail-addresses and all that kind of stuff.
 */

enum nameflags {
	NAME_NAME_SALLOC	= 1<< 0,	/* .n_name is doped */
	NAME_FULLNAME_SALLOC	= 1<< 1,	/* .n_fullname is doped */
	NAME_SKINNED		= 1<< 2,	/* Is actually skin()ned */
	NAME_IDNA		= 1<< 3,	/* IDNA was applied */
	NAME_ADDRSPEC_CHECKED	= 1<< 4,	/* Address has been .. and */
	NAME_ADDRSPEC_ISFILE	= 1<< 5,	/* ..is a file path */
	NAME_ADDRSPEC_ISPIPE	= 1<< 6,	/* ..is a command for piping */
	NAME_ADDRSPEC_ISFILEORPIPE = NAME_ADDRSPEC_ISFILE |
					NAME_ADDRSPEC_ISPIPE,
	NAME_ADDRSPEC_ERR_EMPTY	= 1<< 7,	/* An empty string (or NULL) */
	NAME_ADDRSPEC_ERR_ATSEQ	= 1<< 8,	/* Weird @ sequence */
	NAME_ADDRSPEC_ERR_CHAR	= 1<< 9,	/* Invalid character */
	NAME_ADDRSPEC_ERR_IDNA	= 1<<10,	/* IDNA convertion failed */
	NAME_ADDRSPEC_INVALID	= NAME_ADDRSPEC_ERR_EMPTY |
					NAME_ADDRSPEC_ERR_ATSEQ |
					NAME_ADDRSPEC_ERR_CHAR |
					NAME_ADDRSPEC_ERR_IDNA,

	_NAME_SHIFTWC		= 11,
	_NAME_MAXWC		= 0xFFFFF,
	_NAME_MASKWC		= _NAME_MAXWC << _NAME_SHIFTWC
};

/* In the !_ERR_EMPTY case, the failing character can be queried */
#define NAME_ADDRSPEC_ERR_GETWC(F)	\
	((((unsigned int)(F) & _NAME_MASKWC) >> _NAME_SHIFTWC) & _NAME_MAXWC)
#define NAME_ADDRSPEC_ERR_SET(F, E, WC)	\
do	(F) = ((F) & ~(NAME_ADDRSPEC_INVALID | _NAME_MASKWC)) | \
		(E) | (((unsigned int)(WC) & _NAME_MAXWC) << _NAME_SHIFTWC); \
while (0)

struct name {
	struct	name *n_flink;		/* Forward link in list. */
	struct	name *n_blink;		/* Backward list link */
	enum gfield	n_type;		/* From which list it came */
	enum nameflags	n_flags;	/* enum nameflags */
	char	*n_name;		/* This fella's name */
	char	*n_fullname;		/* Sometimes, name including comment */
};

struct addrguts {
	char const 	*ag_input;	/* Input string as given */
	size_t		ag_ilen;	/* strlen() of input */
	size_t		ag_iaddr_start;	/* Start of *addr-spec* in .ag_input */
	size_t		ag_iaddr_aend;	/* ..and one past its end */
	char		*ag_skinned;	/* Output (alloced if !=.ag_input) */
	size_t		ag_slen;	/* strlen() of .ag_skinned */
	size_t		ag_sdom_start;	/* Start of domain in .ag_skinned, */
	enum nameflags	ag_n_flags;	/* enum nameflags of .ag_skinned */
};

/*
 * MIME attachments.
 */

enum attach_conv {
	AC_DEFAULT,			/* _get_lc() -> _iter_*() */
	AC_FIX_OUTCS,			/* _get_lc() -> "charset=" .a_charset */
	AC_FIX_INCS,			/* "charset=".a_input_charset (nocnv) */
	AC_TMPFILE			/* attachment.a_tmpf is converted */
};

struct attachment {
	struct attachment *a_flink;	/* Forward link in list. */
	struct attachment *a_blink;	/* Backward list link */
	char const *a_name;		/* file name */
	char const *a_content_type;	/* content type */
	char const *a_content_disposition; /* content disposition */
	char const *a_content_id;	/* content id */
	char const *a_content_description; /* content description */
	char const *a_input_charset;	/* Interpretation depends on .a_conv */
	char const *a_charset;		/* ... */
	FILE	*a_tmpf;		/* If AC_TMPFILE */
	enum attach_conv a_conv;	/* User chosen conversion */
	int	a_msgno;		/* message number */
};

/*
 * Structure of a variable node.  All variables are
 * kept on a singly-linked list of these, rooted by
 * "variables"
 */
struct var {
	struct	var *v_link;		/* Forward link to next variable */
	char	*v_name;		/* The variable's name */
	char	*v_value;		/* And it's current value */
};

struct group {
	struct	group *ge_link;		/* Next person in this group */
	char	*ge_name;		/* This person's user name */
};

struct grouphead {
	struct	grouphead *g_link;	/* Next grouphead in list */
	char	*g_name;		/* Name of this group */
	struct	group *g_list;		/* Users in group. */
};

/*
 * Structure of the hash table of ignored header fields
 */
struct ignoretab {
	int i_count;			/* Number of entries */
	struct ignore {
		struct ignore *i_link;	/* Next ignored field in bucket */
		char *i_field;		/* This ignored field */
	} *i_head[HSHSIZE];
};

/*
 * Token values returned by the scanner used for argument lists.
 * Also, sizes of scanner-related things.
 */
enum ltoken {
	TEOL	= 0,			/* End of the command line */
	TNUMBER	= 1,			/* A message number */
	TDASH	= 2,			/* A simple dash */
	TSTRING	= 3,			/* A string (possibly containing -) */
	TDOT	= 4,			/* A "." */
	TUP	= 5,			/* An "^" */
	TDOLLAR	= 6,			/* A "$" */
	TSTAR	= 7,			/* A "*" */
	TOPEN	= 8,			/* An '(' */
	TCLOSE	= 9,			/* A ')' */
	TPLUS	= 10,			/* A '+' */
	TERROR	= 11,			/* A lexical error */
	TCOMMA	= 12,			/* A ',' */
	TSEMI	= 13,			/* A ';' */
	TBACK	= 14			/* A '`' */
};

#define REGDEP	2			/* Maximum regret depth. */

/*
 * Constants for conditional commands.  These describe whether
 * we should be executing stuff or not.
 */
enum condition {
	CANY	= 0,	/* Execute in send or receive mode */
	CRCV	= 1,	/* Execute in receive mode only */
	CSEND	= 2,	/* Execute in send mode only */
	CTERM	= 3,	/* Execute only if stdin is a tty */
	CNONTERM= 4	/* Execute only if stdin not tty */
};

/*
 * For the 'shortcut' and 'unshortcut' functionality.
 */
struct shortcut {
	struct shortcut	*sh_next;	/* next shortcut in list */
	char	*sh_short;		/* shortcut string */
	char	*sh_long;		/* expanded form */
};

/*
 * Kludges to handle the change from setexit / reset to setjmp / longjmp
 */
#define setexit()	(void)sigsetjmp(srbuf, 1)
#define reset(x)	siglongjmp(srbuf, x)

/*
 * Content-Transfer-Encodings as defined in RFC 2045:
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8
 */

#define QP_LINESIZE	(4 * 19  +1)	/* Max. compliant QP linesize (+1) */

#define B64_LINESIZE	(4 * 19  +1)	/* Max. compl. Base64 linesize (+1) */
#define B64_ENCODE_INPUT_PER_LINE 57	/* Max. input for Base64 encode/line */

/* xxx QP came later, maybe rewrite all to use mimecte_flags directly? */
enum __mimecte_flags {
	MIMECTE_NONE,
	MIMECTE_SALLOC	= 1<<0,		/* Use salloc(), not srealloc().. */
	/* ..result .s,.l point to user buffer of *_LINESIZE+ bytes instead */
	MIMECTE_BUF	= 1<<1,
	MIMECTE_CRLF	= 1<<2,		/* (encode) Append "\r\n" to lines */
	MIMECTE_LF	= 1<<3,		/* (encode) Append "\n" to lines */
	/* (encode) If one of _CRLF/_LF is set, honour *_LINESIZE and
	 * inject the desired line-ending whenever a linewrap is desired */
	MIMECTE_MULTILINE = 1<<4,
	/* (encode) Quote with header rules, do not generate soft NL breaks? */
	MIMECTE_ISHEAD	= 1<<5
};

enum qpflags {
	QP_NONE		= MIMECTE_NONE,
	QP_SALLOC	= MIMECTE_SALLOC,
	QP_BUF		= MIMECTE_BUF,
	QP_ISHEAD	= MIMECTE_ISHEAD
};

enum b64flags {
	B64_NONE	= MIMECTE_NONE,
	B64_SALLOC	= MIMECTE_SALLOC,
	B64_BUF		= MIMECTE_BUF,
	B64_CRLF	= MIMECTE_CRLF,
	B64_LF		= MIMECTE_LF,
	B64_MULTILINE	= MIMECTE_MULTILINE
};

/*
 * Locale-independent character classes.
 */

enum {
	C_CNTRL	= 0000,
	C_BLANK	= 0001,
	C_WHITE = 0002,
	C_SPACE	= 0004,
	C_PUNCT	= 0010,
	C_OCTAL	= 0020,
	C_DIGIT	= 0040,
	C_UPPER	= 0100,
	C_LOWER	= 0200
};

extern uc_it const 	class_char[];

#define __ischarof(C, FLAGS)	\
	(asciichar(C) && (class_char[(uc_it)(C)] & (FLAGS)) != 0)

#define asciichar(c)	((uc_it)(c) <= 0177)
#define alnumchar(c)	__ischarof(c, C_DIGIT|C_OCTAL|C_UPPER|C_LOWER)
#define alphachar(c)	__ischarof(c, C_UPPER|C_LOWER)
#define blankchar(c)	__ischarof(c, C_BLANK)
#define blankspacechar(c) __ischarof(c, C_BLANK|C_SPACE)
#define cntrlchar(c)	__ischarof(c, C_CNTRL)
#define digitchar(c)	__ischarof(c, C_DIGIT|C_OCTAL)
#define lowerchar(c)	__ischarof(c, C_LOWER)
#define punctchar(c)	__ischarof(c, C_PUNCT)
#define spacechar(c)	__ischarof(c, C_BLANK|C_SPACE|C_WHITE)
#define upperchar(c)	__ischarof(c, C_UPPER)
#define whitechar(c)	__ischarof(c, C_BLANK|C_WHITE)
#define octalchar(c)	__ischarof(c, C_OCTAL)

#define upperconv(c)	(lowerchar(c) ? (char)((uc_it)(c) - 'a' + 'A') : (c))
#define lowerconv(c)	(upperchar(c) ? (char)((uc_it)(c) - 'A' + 'a') : (c))
/* RFC 822, 3.2. */
#define fieldnamechar(c) \
	(asciichar(c) && (c) > 040 && (c) != 0177 && (c) != ':')

/*
 * Try to use alloca() for some function-local buffers and data,
 * fall back to smalloc()/free() if not available.
 */
#ifdef HAVE_ALLOCA
# define ac_alloc(n)	HAVE_ALLOCA(n)
# define ac_free(n)	do {} while (0)
#else
# define ac_alloc(n)	smalloc(n)
# define ac_free(n)	free(n)
#endif

/*
 * Single-threaded, use unlocked I/O.
 */
#ifdef HAVE_PUTC_UNLOCKED
# undef getc
# define getc(c)	getc_unlocked(c)
# undef putc
# define putc(c, f)	putc_unlocked(c, f)
# undef putchar
# define putchar(c)	putc_unlocked((c), stdout)
#endif

/*
 * Truncate a file to the last character written. This is
 * useful just before closing an old file that was opened
 * for read/write.
 */
#define ftrunc(stream) do {\
	off_t off;\
	fflush(stream);\
	off = ftell(stream);\
	if (off >= 0)\
		ftruncate(fileno(stream), off);\
} while (0)

/*
 * fflush() and rewind()
 */
#define fflush_rewind(stream) do {	\
	fflush(stream);			\
	rewind(stream);			\
} while (0)

/*
 * For saving the current directory and later returning.
 */
struct cw {
#ifdef HAVE_FCHDIR
	int	cw_fd;
#else
	char	cw_wd[PATHSIZE];
#endif
};
