/*	$Id: def.h,v 1.10 2000/08/02 21:16:22 gunnar Exp $	*/
/*	$OpenBSD: def.h,v 1.8 1996/06/08 19:48:18 christos Exp $	*/
/*	$NetBSD: def.h,v 1.8 1996/06/08 19:48:18 christos Exp $	*/
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
 *
 *	@(#)def.h	8.2 (Berkeley) 3/21/94
 *	NetBSD: def.h,v 1.8 1996/06/08 19:48:18 christos Exp 
 *	$Id: def.h,v 1.10 2000/08/02 21:16:22 gunnar Exp $
 */

/*
 * Mail -- a mail program
 *
 * Author: Kurt Shoens (UCB) March 25, 1978
 */

#include "config.h"

#define	APPEND				/* New mail goes to end of mailbox */

#define	ESCAPE		'~'		/* Default escape for sending */
#define	NMLSIZE		1024		/* max names in a message list */
#ifndef	MAXPATHLEN
#ifdef	_POSIX_MAX_PATH
#define	MAXPATHLEN	_POSIX_MAX_PATH
#else
#define	MAXPATHLEN	1024
#endif
#endif
#ifndef	PATHSIZE
#define	PATHSIZE	MAXPATHLEN	/* Size of pathnames throughout */
#endif
#define	HSHSIZE		59		/* Hash size for aliases and vars */
#define	LINESIZE	BUFSIZ		/* max readable line width */
#define	STRINGSIZE	((unsigned) 128)/* Dynamic allocation units */
#define	MAXARGC		1024		/* Maximum list of raw strings */
#define	NOSTR		((char *) 0)	/* Null string pointer */
#define	MAXEXP		25		/* Maximum expansion of aliases */

#define	equal(a, b)	(strcmp(a,b)==0)/* A nice function to string compare */

#ifndef	__P
#ifdef	__STDC__
#define	__P(a)	a
#else
#define	__P(a)	()
#endif
#endif

typedef RETSIGTYPE (*signal_handler_t) __P((int));

enum {
	MIME_NONE,			/* message is not in MIME format */
	MIME_BIN,			/* message is in binary encoding */
	MIME_8B,			/* message is in 8bit encoding */
	MIME_7B,			/* message is in 7bit encoding */
	MIME_QP,			/* message is quoted-printable */
	MIME_B64			/* message is in base64 encoding */
};

enum {
	CONV_NONE,			/* no conversion */
	CONV_7BIT,			/* no conversion, is 7bit */
	CONV_TODISP,			/* convert in displayable form */
	CONV_TOFILE,			/* convert for saving to a file */
	CONV_QUOTE,			/* first part body only */
	CONV_FROMQP,			/* convert from quoted-printable */
	CONV_TOQP,			/* convert to quoted-printable */
	CONV_FROMB64,			/* convert from base64 */
	CONV_FROMB64_T,			/* convert from base64/text */
	CONV_TOB64,			/* convert to base64 */
	CONV_FROMHDR,			/* convert from RFC1522 format */
	CONV_TOHDR,			/* convert to RFC1522 format */
	CONV_TOHDR_A			/* convert addresses for header */
};

enum {
	MIME_UNKNOWN,			/* unknown content */
	MIME_SUBHDR,			/* inside a multipart subheader */
	MIME_822,			/* message/rfc822 content */
	MIME_MESSAGE,			/* message/ content */
	MIME_TEXT,			/* text/ content */
	MIME_MULTI,			/* multipart/ content */
	MIME_DISCARD			/* content is discarded */
};

enum {
	MIME_7BITTEXT,			/* us ascii text */
	MIME_INTERTEXT,			/* international text */
	MIME_BINARY			/* binary content */
};

#define	TD_NONE		0		/* no display conversion */
#define	TD_ISPR		01		/* use isprint() checks */
#define	TD_ICONV	02		/* use iconv() */

struct str {
	char *s;			/* the string's content */
	size_t l;			/* the stings's length */
};

struct message {
	int	m_flag;			/* flags, see below */
	int	m_block;		/* block number of this message */
	size_t	m_offset;		/* offset in block of message */
	size_t	m_size;			/* Bytes in the message */
	int	m_lines;		/* Lines in the message */
};

/*
 * flag bits.
 */

#define	MUSED		(1<<0)		/* entry is used, but this bit isn't */
#define	MDELETED	(1<<1)		/* entry has been deleted */
#define	MSAVED		(1<<2)		/* entry has been saved */
#define	MTOUCH		(1<<3)		/* entry has been noticed */
#define	MPRESERVE	(1<<4)		/* keep entry in sys mailbox */
#define	MMARK		(1<<5)		/* message is marked! */
#define	MODIFY		(1<<6)		/* message has been modified */
#define	MNEW		(1<<7)		/* message has never been seen */
#define	MREAD		(1<<8)		/* message has been read sometime. */
#define	MSTATUS		(1<<9)		/* message status has changed */
#define	MBOX		(1<<10)		/* Send this to mbox, regardless */

/*
 * Given a file address, determine the block number it represents.
 */
#define blockof(off)			((int) ((off) / 4096))
#define offsetof(off)			((int) ((off) % 4096))
#define positionof(block, offset)	((off_t)(block) * 4096 + (offset))

/*
 * Format of the command description table.
 * The actual table is declared and initialized
 * in lex.c
 */
struct cmd {
	char	*c_name;		/* Name of command */
	int	(*c_func) __P((void *));/* Implementor of the command */
	short	c_argtype;		/* Type of arglist (see below) */
	short	c_msgflag;		/* Required flags of messages */
	short	c_msgmask;		/* Relevant flags of messages */
};

/* Yechh, can't initialize unions */

#define	c_minargs c_msgflag		/* Minimum argcount for RAWLIST */
#define	c_maxargs c_msgmask		/* Max argcount for RAWLIST */

/*
 * Argument types.
 */

#define	MSGLIST	 0		/* Message list type */
#define	STRLIST	 1		/* A pure string */
#define	RAWLIST	 2		/* Shell string list */
#define	NOLIST	 3		/* Just plain 0 */
#define	NDMLIST	 4		/* Message list, no defaults */

#define	P	040		/* Autoprint dot after command */
#define	I	0100		/* Interactive command bit */
#define	M	0200		/* Legal from send mode bit */
#define	W	0400		/* Illegal when read only bit */
#define	F	01000		/* Is a conditional command */
#define	T	02000		/* Is a transparent command */
#define	R	04000		/* Cannot be called from collect */

/*
 * Oft-used mask values
 */

#define	MMNORM		(MDELETED|MSAVED)/* Look at both save and delete bits */
#define	MMNDEL		MDELETED	/* Look only at deleted bit */

/*
 * Structure used to return a break down of a head
 * line (hats off to Bill Joy!)
 */

struct headline {
	char	*l_from;	/* The name of the sender */
	char	*l_tty;		/* His tty string (if any) */
	char	*l_date;	/* The entire date string */
};

#define	GTO	1		/* Grab To: line */
#define	GSUBJECT 2		/* Likewise, Subject: line */
#define	GCC	4		/* And the Cc: line */
#define	GBCC	8		/* And also the Bcc: line */
#define	GMASK	(GTO|GSUBJECT|GCC|GBCC)
				/* Mask of places from whence */

#define	GNL	16		/* Print blank line after */
#define	GDEL	32		/* Entity removed from list */
#define	GCOMMA	64		/* detract puts in commas */
#define	GUA	128		/* User-Agent field */
#define	GMIME	256		/* MIME 1.0 fields */
#define	GMSGID	512		/* a Message-ID */
#define	GATTACH	1024		/* attachment included */
#define	GIDENT	2048		/* From:, Reply-To: and Organization header */
#define	GREF	4096		/* References: header */
#define	GDATE	8192		/* Date: header */

/*
 * Structure used to pass about the current
 * state of the user-typed message header.
 */

struct header {
	struct name *h_to;		/* Dynamic "To:" string */
	char *h_subject;		/* Subject string */
	struct name *h_cc;		/* Carbon copies string */
	struct name *h_bcc;		/* Blind carbon copies */
	struct name *h_attach;		/* MIME attachments */
	struct name *h_ref;		/* References */
	struct name *h_smopts;		/* Sendmail options */
};

/*
 * Structure of namelist nodes used in processing
 * the recipients of mail and aliases and all that
 * kind of stuff.
 */

struct name {
	struct	name *n_flink;		/* Forward link in list. */
	struct	name *n_blink;		/* Backward list link */
	short	n_type;			/* From which list it came */
	char	*n_name;		/* This fella's name */
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

#define	NIL	((struct name *) 0)	/* The nil pointer for namelists */
#define	NONE	((struct cmd *) 0)	/* The nil pointer to command tab */
#define	NOVAR	((struct var *) 0)	/* The nil pointer to variables */
#define	NOGRP	((struct grouphead *) 0)/* The nil grouphead pointer */
#define	NOGE	((struct group *) 0)	/* The nil group pointer */

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

#define	TEOL	0			/* End of the command line */
#define	TNUMBER	1			/* A message number */
#define	TDASH	2			/* A simple dash */
#define	TSTRING	3			/* A string (possibly containing -) */
#define	TDOT	4			/* A "." */
#define	TUP	5			/* An "^" */
#define	TDOLLAR	6			/* A "$" */
#define	TSTAR	7			/* A "*" */
#define	TOPEN	8			/* An '(' */
#define	TCLOSE	9			/* A ')' */
#define TPLUS	10			/* A '+' */
#define TERROR	11			/* A lexical error */

#define	REGDEP	2			/* Maximum regret depth. */
#define	STRINGLEN	1024		/* Maximum length of string token */

/*
 * Constants for conditional commands.  These describe whether
 * we should be executing stuff or not.
 */

#define	CANY		0		/* Execute in send or receive mode */
#define	CRCV		1		/* Execute in receive mode only */
#define	CSEND		2		/* Execute in send mode only */

/*
 * Kludges to handle the change from setexit / reset to setjmp / longjmp
 */

#define	setexit()	sigsetjmp(srbuf, 1)
#define	reset(x)	siglongjmp(srbuf, x)

/*
 * Truncate a file to the last character written. This is
 * useful just before closing an old file that was opened
 * for read/write.
 */
#define trunc(stream) {							\
	(void)fflush(stream); 						\
	(void)ftruncate(fileno(stream), (off_t)ftell(stream));		\
}

/*
 * Linux stdio has the odd quirk that if you are in a stdio() function and
 * you longjmp() out of it, next time you enter that function you will
 * return with whatever data had been successfully read the last time.
 * Accordingly, you must fpurge() any outstanding data before longjmp()ing.
 * We test _IO_stdin to try to verify that it's the right implementation.
 *
 * For glibc it is better to use the IOSAFE implementation.
 */
#undef fpurge
#if defined(__linux__) && defined(_IO_stdin)
#  define fpurge(file) ((file)->_IO_read_ptr = (file)->_IO_read_end)
#else /* !__linux__ */
#  define fpurge(file)
#endif
#if defined(__GLIBC__) &&  (__GLIBC__ >= 2) && !defined(IOSAFE)
#  define IOSAFE
#endif

