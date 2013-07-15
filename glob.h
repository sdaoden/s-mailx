/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ A bunch of global variable declarations lie herein.
 *@ def.h must be included first.
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

/* These two come from version.c */
extern char const *const uagent;	/* User agent */
extern char const *const version;	/* The version string */

/* The rest will end up in main.o */
#ifdef _MAIL_GLOBS_
# undef _E
# define _E
#else
# define _E	extern
#endif

_E gid_t	effectivegid;		/* Saved from when we started up */
_E gid_t	realgid;		/* Saved from when we started up */

_E int		mb_cur_max;		/* value of MB_CUR_MAX */
_E int		realscreenheight;	/* the real screen height */
_E int		scrnwidth;		/* Screen width, or best guess */
_E int		scrnheight;		/* Screen height, or best guess,
					 *  for "header" command */
_E int		utf8;			/* UTF-8 encoding in use for locale */

_E char		**altnames;		/* List of alternate names for user */
_E char const	*homedir;		/* Path name of home directory */
_E char const	*myname;		/* My login name */
_E char const	*progname;		/* Our name */
_E char const	*tempdir;		/* The temporary directory */

_E int		exit_status;		/* Exit status */
_E int		options;		/* Bits of enum user_options */
_E char		*option_u_arg;		/* name given with -u option */
_E char 	*option_r_arg;		/* argument to -r option */
_E char const	**smopts;		/* sendmail(1) options, command line */
_E size_t	smopts_count;		/* Entries in smopts */

_E int		inhook;			/* currently executing a hook */
_E bool_t	edit;			/* Indicates editing a file */
_E bool_t	did_print_dot;		/* current message has been printed */
_E bool_t	msglist_is_single;	/* Last NDMLIST/MSGLIST *chose* 1 msg */
_E bool_t	loading;		/* Loading user definitions */
_E bool_t	sourcing;		/* Currently reading variant file */
_E bool_t	sawcom;			/* Set after first command */
_E bool_t	starting;		/* still in startup code */
_E bool_t	unset_allow_undefined;	/* allow to unset undefined variables */
_E int		noreset;		/* String resets suspended */

/* XXX stylish sorting */
_E int		msgCount;		/* Count of messages read in */
_E enum condition cond;			/* Current state of conditional exc. */
_E struct mailbox mb;			/* Current mailbox */
_E int		image;			/* File descriptor for image of msg */
_E FILE		*input;			/* Current command input file */
_E char		mailname[MAXPATHLEN];	/* Name of current file */
_E char		displayname[80 - 40];	/* Prettyfied for display */
_E char		prevfile[MAXPATHLEN];	/* Name of previous file */
_E char		mboxname[MAXPATHLEN];	/* Name of mbox */
_E off_t	mailsize;		/* Size of system mailbox */
_E struct message *dot;			/* Pointer to current message */
_E struct message *prevdot;		/* Previous current message */
_E struct message *message;		/* The actual message structure */
_E struct message *threadroot;		/* first threaded message */
_E int		msgspace;		/* Number of allocated struct m */
_E struct var	*variables[HSHSIZE];	/* Pointer to active var list */
_E struct grouphead *groups[HSHSIZE];	/* Pointer to active groups */
_E struct ignoretab ignore[2];		/* ignored and retained fields
					 * 0 is ignore, 1 is retain */
_E struct ignoretab saveignore[2];	/* ignored and retained fields
					 * on save to folder */
_E struct ignoretab allignore[2];	/* special, ignore all headers */
_E struct ignoretab fwdignore[2];	/* fields to ignore for forwarding */
_E struct shortcut *shortcuts;		/* list of shortcuts */
_E int		imap_created_mailbox;	/* hack to get feedback from imap */

_E struct time_current time_current;	/* time(3); send: mail1() XXX->carrier*/
_E struct termios_state termios_state;	/* getpassword(); see commands().. */

/* These are initialized strings */
_E char const	*const month_names[12 + 1];
_E char const	*const weekday_names[7 + 1];

#ifdef USE_SSL
_E enum ssl_vrfy_level ssl_vrfy_level;	/* SSL verification level */
#endif

#ifdef HAVE_ICONV
_E iconv_t	iconvd;
#endif

#ifdef HAVE_CATGETS
_E nl_catd	catd;
#endif

#include <setjmp.h>

_E sigjmp_buf	srbuf;
_E int		interrupts;
_E sighandler_type handlerstacktop;
#define	handlerpush(f)	(savedtop = handlerstacktop, handlerstacktop = (f))
#define	handlerpop()	(handlerstacktop = savedtop)
_E sighandler_type dflpipe;

#undef _E
