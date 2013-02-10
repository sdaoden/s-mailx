/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012, 2013 Steffen "Daode" Nurpmeso.
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
 * Most strcpy/sprintf functions have been changed to strncpy/snprintf to
 * correct several buffer overruns (at least one ot them was exploitable).
 * Sat Jun 20 04:58:09 CEST 1998 Alvaro Martinez Echevarria <alvaro@lander.es>
 * ---
 * Note: We set egid to realgid ... and only if we need the egid we will
 *       switch back temporary.  Nevertheless, I do not like seg faults.
 *       Werner Fink, <werner@suse.de>
 */

#include "config.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#ifdef HAVE_NL_LANGINFO
# include <langinfo.h>
#endif
#ifdef HAVE_SETLOCALE
# include <locale.h>
#endif

#define _MAIL_GLOBS_
#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Startup -- interface with user.
 */

struct a_arg {
	struct a_arg	*aa_next;
	char		*aa_file;
};

static sigjmp_buf	hdrjmp;

/* Add an option for sendmail(1) */
static void	add_smopt(int argc_left, char *arg);

/* Initialize *tempdir*, *myname*, *homedir* */
static void	setup_vars(void);

/* Compute what the screen size for printing headers should be.
 * We use the following algorithm for the height:
 *	If baud rate < 1200, use  9
 *	If baud rate = 1200, use 14
 *	If baud rate > 1200, use 24 or ws_row
 * Width is either 80 or ws_col */
static void	setscreensize(int dummy);

/* Interrupt printing of the headers */
static void	hdrstop(int signo);

static void
add_smopt(int argc_left, char *arg)
{
	/* Before spreserve(): use our string pool instead of LibC heap */
	if (smopts == NULL)
		smopts = salloc((argc_left + 1) * sizeof(char*));
	smopts[smopts_count++] = arg;
}

static void
setup_vars(void)
{
	/* Before spreserve(): use our string pool instead of LibC heap */
	char *cp;

	tempdir = ((cp = getenv("TMPDIR")) != NULL) ? savestr(cp) : "/tmp";

	myname = savestr(username());

	if ((cp = getenv("HOME")) == NULL)
		cp = ".";
	homedir = savestr(cp);
}

static void
setscreensize(int dummy)
{
	struct termios tbuf;
#ifdef TIOCGWINSZ
	struct winsize ws;
#endif
	speed_t ospeed;
	(void)dummy;

#ifdef TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, &ws) < 0)
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
#ifdef TIOCGWINSZ
	else if (ws.ws_row != 0)
		scrnheight = ws.ws_row;
#endif
	else
		scrnheight = 24;
#ifdef TIOCGWINSZ
	if ((realscreenheight = ws.ws_row) == 0)
		realscreenheight = 24;
#endif
#ifdef TIOCGWINSZ
	if ((scrnwidth = ws.ws_col) == 0)
#endif
		scrnwidth = 80;
}

static void
hdrstop(int signo)
{
	(void)signo;

	fflush(stdout);
	fprintf(stderr, tr(141, "\nInterrupt\n"));
	siglongjmp(hdrjmp, 1);
}

char const *const	weekday_names[7 + 1] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", NULL
};
char const *const	month_names[12 + 1] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL
};

sighandler_type		dflpipe = SIG_DFL;

int 
main(int argc, char *argv[])
{
	static char const optstr[] = "A:a:Bb:c:DdEeFfHIiNnO:q:Rr:S:s:T:tu:Vv~",
		usagestr[] =
		"Usage:\t%s [-BDdEFintv~] [-A acc] [-a attachment]\n"
		"\t\t[-b bcc-addr] [-c cc-addr] [-O mtaopt [-O mtaopt-arg]]\n"
		"\t\t[-q file] [-r from-addr] [-S var[=value]]\n"
		"\t\t[-s subject] to-addr...\n"
		"\t%s [-BDdEeHIiNnRv~] [-A acct]\n"
		"\t\t[-S var[=value]] [-T name] -f [file]\n"
		"\t%s [-BDdEeiNnRv~] [-A acc] [-S var[=value]] [-u user]\n";

	struct a_arg *a_head = NULL, *a_curr = /* silence CC */ NULL;
	int scnt = 0, i;
	struct name *to = NULL, *cc = NULL, *bcc = NULL;
	struct attachment *attach = NULL;
	char *cp = NULL, *subject = NULL, *ef = NULL, *qf = NULL,
		*fromaddr = NULL, *Aflag = NULL;
	sighandler_type prevint;

	/*
	 * Absolutely the first thing we do is save our egid
	 * and set it to the rgid, so that we can safely run
	 * setgid.  We use the sgid (saved set-gid) to allow ourselves
	 * to revert to the egid if we want (temporarily) to become
	 * priveliged.
	 */
	effectivegid = getegid();
	realgid = getgid();
	if (setgid(realgid) < 0) {
		perror("setgid");
		exit(1);
	}

	starting = 1;
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
	safe_signal(SIGCHLD, sigchild);
	is_a_tty[0] = isatty(0);
	is_a_tty[1] = isatty(1);
	if (is_a_tty[0]) {
		assign("interactive", "");
		if (is_a_tty[1])
			safe_signal(SIGPIPE, dflpipe = SIG_IGN);
	}
	assign("header", "");
	assign("save", "");
#ifdef HAVE_SETLOCALE
	setlocale(LC_ALL, "");
	mb_cur_max = MB_CUR_MAX;
# if defined HAVE_NL_LANGINFO && defined CODESET
	if (value("ttycharset") == NULL && (cp = nl_langinfo(CODESET)) != NULL)
		assign("ttycharset", cp);
# endif
# if defined HAVE_MBTOWC && defined HAVE_WCTYPE_H
	if (mb_cur_max > 1) {
		wchar_t	wc;
		if (mbtowc(&wc, "\303\266", 2) == 2 && wc == 0xF6 &&
				mbtowc(&wc, "\342\202\254", 3) == 3 &&
				wc == 0x20AC)
			utf8 = 1;
	}
# endif
#else
	mb_cur_max = 1;
#endif
#ifdef HAVE_CATGETS
# ifdef NL_CAT_LOCALE
	catd = catopen(CATNAME, NL_CAT_LOCALE);
# else
	catd = catopen(CATNAME, 0);
# endif
#endif
#ifdef HAVE_ICONV
	iconvd = (iconv_t)-1;
#endif
	image = -1;

	/*
	 * Command line options
	 */

	while ((i = getopt(argc, argv, optstr)) != EOF) {
		switch (i) {
		case 'A':
			/* Execute an account command later on */
			Aflag = optarg;
			break;
		case 'a':
			{	struct a_arg *nap = ac_alloc(
						sizeof(struct a_arg));
				if (a_head == NULL)
					a_head = nap;
				else
					a_curr->aa_next = nap;
				nap->aa_next = NULL;
				nap->aa_file = optarg;
				a_curr = nap;
			}
			options |= OPT_SENDFLAG;
			break;
		case 'B':
			/* Make 0/1 line buffered */
			setvbuf(stdin, NULL, _IOLBF, 0);
			setvbuf(stdout, NULL, _IOLBF, 0);
			break;
		case 'b':
			/* Get Blind Carbon Copy Recipient list */
			bcc = cat(bcc, checkaddrs(lextract(optarg,GBCC|GFULL)));
			options |= OPT_SENDFLAG;
			break;
		case 'c':
			/* Get Carbon Copy Recipient list */
			cc = cat(cc, checkaddrs(lextract(optarg, GCC|GFULL)));
			options |= OPT_SENDFLAG;
			break;
		case 'D':
#ifdef USE_IMAP
			assign("disconnected", "");
#endif
			break;
		case 'd':
			assign("debug", "");
			options |= OPT_DEBUG;
			break;
		case 'E':
			options |= OPT_E_FLAG;
			break;
		case 'e':
			options |= OPT_EXISTONLY;
			break;
		case 'F':
			options |= OPT_F_FLAG | OPT_SENDFLAG;
			break;
		case 'f':
			/*
			 * User is specifying file to "edit" with Mail,
			 * as opposed to reading system mailbox.
			 * If no argument is given, we read his mbox file.
			 * Check for remaining arguments later.
			 */
			ef = "&";
			break;
		case 'H':
			options |= OPT_HEADERSONLY;
			break;
jIflag:		case 'I':
			/* Show Newsgroups: field in header summary */
			options |= OPT_I_FLAG;
			break;
		case 'i':
			/* Ignore interrupts */
			assign("ignore", "");
			break;
		case 'N':
			/* Avoid initial header printing */
			unset_internal("header");
			options |= OPT_N_FLAG;
			break;
		case 'n':
			/* Don't source "unspecified system start-up file" */
			options |= OPT_NOSRC;
			break;
		case 'O':
			/* Additional options to pass-through to MTA */
			add_smopt(argc - optind, skin(optarg));
			break;
		case 'q':
			/* Quote file TODO drop? -Q with real quote?? what ? */
			if (*optarg != '-')
				qf = optarg;
			options |= OPT_SENDFLAG;
			break;
		case 'R':
			/* Open folders read-only */
			options |= OPT_R_FLAG;
			break;
		case 'r':
			/* Set From address. */
			{	struct name *fa = nalloc(optarg, GFULL);
				if (is_addr_invalid(fa, 1) ||
						is_fileorpipe_addr(fa)) {
					fprintf(stderr, tr(271,
						"Invalid address argument "
						"with -r\n"));
					goto usage;
				}
				fromaddr = fa->n_fullname;
				add_smopt(argc - optind, "-r");
				add_smopt(-1, fa->n_name);
				/* ..and fa goes even though it is ready :/ */
			}
			break;
		case 'S':
			/*
			 * Set variable.  We need to do this twice, since the
			 * user surely wants the setting to take effect
			 * immediately, but also doesn't want it to be
			 * overwritten from within resource files
			 */
			argv[scnt++] = optarg;
			{	char *a[2];
				a[0] = optarg;
				a[1] = NULL;
				unset_allow_undefined = 1;
				set(a);
				unset_allow_undefined = 0;
			}
			break;
		case 's':
			/* Subject: */
			subject = optarg;
			options |= OPT_SENDFLAG;
			break;
		case 'T':
			/*
			 * Next argument is temp file to write which
			 * articles have been read/deleted for netnews.
			 */
			option_T_arg = optarg;
			if ((i = creat(option_T_arg, 0600)) < 0) {
				perror(option_T_arg);
				exit(1);
			}
			close(i);
			goto jIflag;
		case 't':
			/* Read defined set of headers from mail to be send */
			options |= OPT_SENDFLAG | OPT_t_FLAG;
			break;
		case 'u':
			/* Set user name to pretend to be  */
			option_u_arg = myname = optarg;
			break;
		case 'V':
			puts(version);
			exit(0);
			/* NOTREACHED */
		case 'v':
			/* Be verbose */
			assign("verbose", "");
			options |= OPT_VERBOSE;
			break;
		case '~':
			/* Enable tilde escapes even in non-interactive mode */
			options |= OPT_TILDE_FLAG;
			break;
		case '?':
usage:			fprintf(stderr, tr(135, usagestr),
				progname, progname, progname);
			exit(2);
		}
	}

	if (ef != NULL) {
		if (optind < argc) {
			if (optind + 1 < argc) {
				fprintf(stderr, tr(205,
					"More than one file given with -f\n"));
				goto usage;
			}
			ef = argv[optind];
		}
	} else {
		for (i = optind; argv[i]; i++)
			to = cat(to, checkaddrs(lextract(argv[i], GTO|GFULL)));
	}

	/* Check for inconsistent arguments */
	if (ef != NULL && to != NULL) {
		fprintf(stderr, tr(137,
			"Cannot give -f and people to send to.\n"));
		goto usage;
	}
	if ((options & (OPT_SENDFLAG|OPT_t_FLAG)) == OPT_SENDFLAG &&
			to == NULL) {
		fprintf(stderr, tr(138,
			"Send options without primary recipient specified.\n"));
		goto usage;
	}
	if ((options & OPT_R_FLAG) && to != NULL) {
		fprintf(stderr, "The -R option is meaningless in send mode.\n");
		goto usage;
	}
	if ((options & OPT_I_FLAG) && ef == NULL) {
		fprintf(stderr, tr(204, "Need -f with -I.\n"));
		goto usage;
	}

	setup_vars();
	setscreensize(0);
#ifdef SIGWINCH
	if (value("interactive"))
		if (safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
			safe_signal(SIGWINCH, setscreensize);
#endif
	input = stdin;
	if (to == NULL && (options & OPT_t_FLAG) == 0)
		options |= OPT_RCVMODE;

	/* Snapshot our string pools.  Memory is auto-reclaimed from now on */
	spreserve();

	if ((options & OPT_NOSRC) == 0)
		load(SYSCONFRC);
	/*
	 * Expand returns a savestr, but load only uses the file name
	 * for fopen, so it's safe to do this.
	 */
	if ((cp = getenv("MAILRC")) != NULL)
		load(file_expand(cp));
	else if ((cp = getenv("NAILRC")) != NULL)
		load(file_expand(cp));
	else
		load(file_expand("~/.mailrc"));
	if (getenv("NAIL_EXTRA_RC") == NULL &&
			(cp = value("NAIL_EXTRA_RC")) != NULL)
		load(file_expand(cp));

	/* Now we can set the account */
	if (Aflag != NULL) {
		char *a[2];
		a[0] = Aflag;
		a[1] = NULL;
		account(a);
	}
	/* Override 'skipemptybody' if '-E' flag was given */
	if (options & OPT_E_FLAG)
		assign("skipemptybody", "");
	/* -S arguments override rc files */
	for (i = 0; i < scnt; ++i) {
		char *a[2];
		a[0] = argv[i];
		a[1] = NULL;
		set(a);
	}
	/* From address from command line overrides rc files */
	if (fromaddr != NULL)
		assign("from", fromaddr);

	if (value("debug"))
		options |= OPT_DEBUG;
	if (options & OPT_DEBUG) {
		options |= OPT_VERBOSE;
		fprintf(stderr, tr(199, "user = %s, homedir = %s\n"),
			myname, homedir);
	}
	if (value("verbose"))
		options |= OPT_VERBOSE;

	/* We have delayed attachments until full file expansion is possible */
	while (a_head != NULL) {
		attach = add_attachment(attach, a_head->aa_file, 1, NULL);
		if (attach != NULL) {
			a_curr = a_head;
			a_head = a_head->aa_next;
			ac_free(a_curr);
		} else {
			perror(a_head->aa_file);
			exit(1);
		}
	}

	/*
	 * We are actually ready to go, finally
	 */

	starting = 0;

	if ((options & OPT_RCVMODE) == 0) {
		mail(to, cc, bcc, subject, attach, qf,
			((options & OPT_F_FLAG) != 0),
			((options & OPT_t_FLAG) != 0),
			((options & OPT_E_FLAG) != 0));
		exit(senderr ? 1 : 0);
	}

	/*
	 * Ok, we are reading mail.
	 * Decide whether we are editing a mailbox or reading
	 * the system mailbox, and open up the right stuff.
	 */
	if (ef == NULL)
		ef = "%";
	else if (*ef == '@') {
		/*
		 * This must be treated specially to make invocation like
		 * -A imap -f @mailbox work.
		 */
		if ((cp = value("folder")) != NULL &&
				which_protocol(cp) == PROTO_IMAP)
			strncpy(mailname, cp, PATHSIZE)[PATHSIZE-1] = '\0';
	}
	i = setfile(ef, 0);
	if (i < 0)
		exit(1);		/* error already reported */
	if (options & OPT_EXISTONLY)
		exit(i);
	if (options & OPT_HEADERSONLY) {
#ifdef USE_IMAP
		if (mb.mb_type == MB_IMAP)
			imap_getheaders(1, msgCount);
#endif
		for (i = 1; i <= msgCount; i++)
			printhead(i, stdout, 0);
		exit(exit_status);
	}

	callhook(mailname, 0);
	if (i > 0 && value("emptystart") == NULL)
		exit(1);
	if (sigsetjmp(hdrjmp, 1) == 0) {
		if ((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, hdrstop);
		if ((options & OPT_N_FLAG) == 0) {
			if (! value("quiet"))
				printf(tr(140,
					"%s version %s.  Type ? for help.\n"),
					value("bsdcompat") ? "Mail" : uagent,
					version);
			announce(1);
			fflush(stdout);
		}
		safe_signal(SIGINT, prevint);
	}
	commands();
	if (mb.mb_type == MB_FILE || mb.mb_type == MB_MAILDIR) {
		safe_signal(SIGHUP, SIG_IGN);
		safe_signal(SIGINT, SIG_IGN);
		safe_signal(SIGQUIT, SIG_IGN);
	}
	strncpy(mboxname, expand("&"), sizeof mboxname)[sizeof mboxname-1]='\0';
	quit();
	return (exit_status);
}
