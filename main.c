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

#ifndef lint
static char copyright[]
#ifdef	__GNUC__
__attribute__ ((unused))
#endif
= "@(#) Copyright (c) 1980, 1993 The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)main.c	1.13 (gritter) 1/13/02";
#endif
#endif /* not lint */

/*
 * Most strcpy/sprintf functions have been changed to strncpy/snprintf to
 * correct several buffer overruns (at least one ot them was exploitable).
 * Sat Jun 20 04:58:09 CEST 1998 Alvaro Martinez Echevarria <alvaro@lander.es>
 * ---
 * Note: We set egid to realgid ... and only if we need the egid we will
 *       switch back temporary.  Nevertheless, I do not like seg faults.
 *       Werner Fink, <werner@suse.de>
 */


#define _MAIL_GLOBS_
#include "rcv.h"
#ifdef	HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Startup -- interface with user.
 */

sigjmp_buf	hdrjmp;
char *progname;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int i, existonly = 0;
	struct name *to, *attach, *cc, *bcc, *smopts;
	char *subject, *cp, *ef, *qf = NULL, *fromaddr = NULL;
	char nosrc = 0;
	signal_handler_t prevint;

	/*
	 * Absolutely the first thing we do is save our egid
	 * and set it to the rgid, so that we can safely run
	 * setgid.  We use the sgid (saved set-gid) to allow ourselves
	 * to revert to the egid if we want (temporarily) to become
	 * priveliged.
	 */
	effectivegid = getegid();
	realgid = getgid();
	if (setgid (realgid) < 0) {
		perror("setgid");
		exit(1);
	}

	progname = strrchr(argv[0], '/');
	if (progname != NULL)
		progname++;
	else
		progname = argv[0];
	/*
	 * Set up a reasonable environment.
	 * Figure out whether we are being run interactively,
	 * start the SIGCHLD catcher, and so forth.
	 */
	(void) safe_signal(SIGCHLD, sigchild);
	if (isatty(0))
		assign("interactive", "");
#ifdef	HAVE_SETLOCALE
	setlocale(LC_CTYPE, "");
#if defined (HAVE_NL_LANGINFO) && defined (CODESET)
	if (value("ttycharset") == NULL && (cp = nl_langinfo(CODESET)) != NULL)
		assign("ttycharset", cp);
#endif
#endif
#ifdef	HAVE_ICONV
	iconvd = (iconv_t) -1;
#endif
	image = -1;
	/*
	 * Now, determine how we are being used.
	 * We successively pick off - flags.
	 * If there is anything left, it is the base of the list
	 * of users to mail to.  Argp will be set to point to the
	 * first of these users.
	 */
	ef = NULL;
	to = NIL;
	cc = NIL;
	bcc = NIL;
	attach = NIL;
	smopts = NIL;
	subject = NULL;
#ifndef	__GLIBC__
	while ((i = getopt(argc, argv, "INVT:a:b:c:definqr:s:u:v")) != EOF) {
#else
	while ((i = getopt(argc, argv, "+INVT:a:b:c:definqr:s:u:v")) != EOF) {
#endif
		switch (i) {
		case 'V':
			puts(version);
			exit(0);
			/*NOTREACHED*/
		case 'T':
			/*
			 * Next argument is temp file to write which
			 * articles have been read/deleted for netnews.
			 */
			Tflag = optarg;
			if ((i = creat(Tflag, 0600)) < 0) {
				perror(Tflag);
				exit(1);
			}
			close(i);
			break;
		case 'u':
			/*
			 * Next argument is person to pretend to be.
			 */
			myname = optarg;
			break;
		case 'i':
			/*
			 * User wants to ignore interrupts.
			 * Set the variable "ignore"
			 */
			assign("ignore", "");
			break;
		case 'd':
			debug++;
			break;
		case 'e':
			existonly++;
			break;
		case 's':
			/*
			 * Give a subject field for sending from
			 * non terminal
			 */
			subject = optarg;
			break;
		case 'f':
			/*
			 * User is specifying file to "edit" with Mail,
			 * as opposed to reading system mailbox.
			 * If no argument is given after -f, we read his
			 * mbox file.
			 *
			 * getopt() can't handle optional arguments, so here
			 * is an ugly hack to get around it.
			 */
			if ((argv[optind]) && (argv[optind][0] != '-'))
				ef = argv[optind++];
			else
				ef = "&";
			break;
		case 'q':
			/*
			 * User is specifying file to quote in front of
			 * the mail to be collected.
			 */
			if ((argv[optind]) && (argv[optind][0] != '-'))
				qf = argv[optind++];
			else
				qf = NULL;
			break;
		case 'n':
			/*
			 * User doesn't want to source /usr/lib/Mail.rc
			 */
			nosrc++;
			break;
		case 'N':
			/*
			 * Avoid initial header printing.
			 */
			assign("noheader", "");
			break;
		case 'v':
			/*
			 * Send mailer verbose flag
			 */
			assign("verbose", "");
			break;
		case 'I':
			/*
			 * We're interactive
			 */
			assign("interactive", "");
			break;
		case 'r':
			/*
			 * Set From address.
			 */
			fromaddr = optarg;
			break;
		case 'a':
			/*
			 * Get attachment filenames
			 */
			attach = cat(attach, nalloc(optarg, GATTACH));
			break;
		case 'c':
			/*
			 * Get Carbon Copy Recipient list
			 */
			cc = checkaddrs(cat(cc, extract(optarg, GCC)));
			break;
		case 'b':
			/*
			 * Get Blind Carbon Copy Recipient list
			 */
			bcc = checkaddrs(cat(bcc, extract(optarg, GBCC)));
			break;
		case '?':
			fprintf(stderr,
			"Usage: %s [-iInv] [-s subject] [-a attachment] [-c cc-addr] [-b bcc-addr] [-r from-addr] to-addr ... [- sendmail-options ...]\n"
			"       %s [-eiInNv] -f [name]\n"
			"       %s [-eiInNv] [-u user]\n",
				progname, progname, progname);
			exit(1);
		}
	}
	for (i = optind; (argv[i]) && (*argv[i] != '-'); i++)
		to = checkaddrs(cat(to, nalloc(argv[i], GTO)));
	for (; argv[i]; i++)
		smopts = cat(smopts, nalloc(argv[i], 0));
	/*
	 * Check for inconsistent arguments.
	 */
	if (to == NIL && (subject != NULL || cc != NIL || bcc != NIL)) {
		fputs("You must specify direct recipients with -s, -c, or -b.\n", stderr);
		exit(1);
	}
	if (ef != NULL && to != NIL) {
		fprintf(stderr, "Cannot give -f and people to send to.\n");
		exit(1);
	}
	if (qf != NULL && to == NIL) {
		fprintf(stderr, "Cannot give -q without people to send to.\n");
		exit(1);
	}
	tinit();
	setscreensize();
	input = stdin;
	rcvmode = !to;
	spreserve();
	if (!nosrc)
		load(PATH_MASTER_RC);
	/*
	 * Expand returns a savestr, but load only uses the file name
	 * for fopen, so it's safe to do this.
	 */
	if ((cp = getenv("MAILRC")) != NULL)
		load(expand(cp));
	else
		load(expand("~/.mailrc"));
	/*
	 * From address from command line overrides rc files.
	 */
	if (fromaddr)
		assign("from", fromaddr);
	if (!rcvmode) {
		mail(to, cc, bcc, smopts, subject, attach, qf);
		/*
		 * why wait?
		 */
		exit(senderr);
	}
	/*
	 * Ok, we are reading mail.
	 * Decide whether we are editing a mailbox or reading
	 * the system mailbox, and open up the right stuff.
	 */
	if (ef == NULL)
		ef = "%";
	i = setfile(ef);
	if (i < 0)
		exit(1);		/* error already reported */
	if (existonly)
		exit(i);
	if (i > 0 && value("emptystart") == NULL)
		exit(1);
	if (sigsetjmp(hdrjmp, 1) == 0) {
		if ((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, hdrstop);
		if (value("quiet") == NULL)
			printf("Mail version %s.  Type ? for help.\n",
				version);
		announce();
		fflush(stdout);
		safe_signal(SIGINT, prevint);
	}
	commands();
	safe_signal(SIGHUP, SIG_IGN);
	safe_signal(SIGINT, SIG_IGN);
	safe_signal(SIGQUIT, SIG_IGN);
	quit();
	exit(0);
	/*NOTREACHED*/
	return 0;
}

/*
 * Interrupt printing of the headers.
 */
/*ARGSUSED*/
void
hdrstop(signo)
	int signo;
{

	fflush(stdout);
	fprintf(stderr, "\nInterrupt\n");
	siglongjmp(hdrjmp, 1);
}

/*
 * Compute what the screen size for printing headers should be.
 * We use the following algorithm for the height:
 *	If baud rate < 1200, use  9
 *	If baud rate = 1200, use 14
 *	If baud rate > 1200, use 24 or ws_row
 * Width is either 80 or ws_col;
 */
void
setscreensize()
{
	struct termios tbuf;
#ifdef	TIOCGWINSZ
	struct winsize ws;
#endif
	speed_t ospeed;

#ifdef	TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, (char *) &ws) < 0)
		ws.ws_col = ws.ws_row = 0;
#endif
	if (tcgetattr(1, &tbuf) < 0)
		ospeed = B9600;
	else
		ospeed = cfgetospeed(&tbuf);
	if (ospeed < B1200)
		scrnheight = 9;
	else if (ospeed == B1200)
		scrnheight = 14;
#ifdef	TIOCGWINSZ
	else if (ws.ws_row != 0)
		scrnheight = ws.ws_row;
#endif
	else
		scrnheight = 24;
#ifdef	TIOCGWINSZ
	if ((realscreenheight = ws.ws_row) == 0)
		realscreenheight = 24;
#endif
#ifdef	TIOCGWINSZ
	if ((scrnwidth = ws.ws_col) == 0)
#endif
		scrnwidth = 80;
}
