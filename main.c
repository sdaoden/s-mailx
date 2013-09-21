/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Startup -- interface with user.
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
 * Most strcpy/sprintf functions have been changed to strncpy/snprintf to
 * correct several buffer overruns (at least one ot them was exploitable).
 * Sat Jun 20 04:58:09 CEST 1998 Alvaro Martinez Echevarria <alvaro@lander.es>
 * ---
 * Note: We set egid to realgid ... and only if we need the egid we will
 *       switch back temporary.  Nevertheless, I do not like seg faults.
 *       Werner Fink, <werner@suse.de>
 */

#include "config.h"

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
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
#include "version.h"

struct a_arg {
	struct a_arg	*aa_next;
	char		*aa_file;
};

/* Perform basic startup initialization */
static void	_startup(void);

/* Grow a char** */
static int	_grow_cpp(char const ***cpp, int size, int cnt);

/* Initialize *tempdir*, *myname*, *homedir* */
static void	_setup_vars(void);

/* We're in an interactive session - compute what the screen size for printing
 * headers etc. should be; notify tty upon resize if *is_sighdl* is not 0.
 * We use the following algorithm for the height:
 *	If baud rate < 1200, use  9
 *	If baud rate = 1200, use 14
 *	If baud rate > 1200, use 24 or ws_row
 * Width is either 80 or ws_col */
static void	_setscreensize(int is_sighdl);

/* Ok, we are reading mail.  Decide whether we are editing a mailbox or reading
 * the system mailbox, and open up the right stuff */
static int	_rcv_mode(char const *folder);

/* Interrupt printing of the headers */
static void	_hdrstop(int signo);

static void
_startup(void)
{
	char *cp;

	/* Absolutely the first thing we do is save our egid
	 * and set it to the rgid, so that we can safely run
	 * setgid.  We use the sgid (saved set-gid) to allow ourselves
	 * to revert to the egid if we want (temporarily) to become
	 * privileged */
	effectivegid = getegid();
	realgid = getgid();
	if (setgid(realgid) < 0) {
		perror("setgid");
		exit(1);
	}

	image = -1;

	if ((cp = strrchr(progname, '/')) != NULL)
		progname = ++cp;

	/* Set up a reasonable environment.
	 * Figure out whether we are being run interactively,
	 * start the SIGCHLD catcher, and so forth */
	safe_signal(SIGCHLD, sigchild);

	if (isatty(0))
		options |= OPT_TTYIN | OPT_INTERACTIVE;
	if (isatty(1))
		options |= OPT_TTYOUT;
	if (IS_TTY_SESSION())
		safe_signal(SIGPIPE, dflpipe = SIG_IGN);
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
		/* Reset state - it may have been messed up; luckily this also
		 * gives us an indication wether the encoding has locking shift
		 * state sequences */
		/* TODO temporary - use option bits! */
		enc_has_state = mbtowc(&wc, NULL, mb_cur_max);
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
}

static int
_grow_cpp(char const ***cpp, int size, int cnt)
{
	/* Before spreserve(): use our string pool instead of LibC heap;
	 * Increment *size* by at least 5! */
	char const **newcpp = salloc(sizeof(char*) * (size += 8));

	if (cnt > 0)
		memcpy(newcpp, *cpp, (size_t)cnt * sizeof(char*));
	*cpp = newcpp;
	return size;
}

static void
_setup_vars(void)
{
	/* Before spreserve(): use our string pool instead of LibC heap */
	char const *cp;

	tempdir = ((cp = getenv("TMPDIR")) != NULL) ? savestr(cp) : "/tmp";

	myname = savestr(username());

	if ((cp = getenv("HOME")) == NULL)
		cp = ".";
	homedir = savestr(cp);
}

static void
_setscreensize(int is_sighdl)
{
	struct termios tbuf;
#ifdef TIOCGWINSZ
	struct winsize ws;
#elif defined TIOCGSIZE
	struct ttysize ts;
#endif

	scrnheight = realscreenheight = scrnwidth = 0;

	/* (Also) POSIX: LINES and COLUMNS always override.  Adjust this
	 * a little bit to be able to honour resizes during our lifetime and
	 * only honour it upon first run; abuse *is_sighdl* as an indicator */
	if (is_sighdl == 0) {
		char *cp;
		long i;

		if ((cp = getenv("LINES")) != NULL &&
				(i = strtol(cp, NULL, 10)) > 0)
			scrnheight = realscreenheight = (int)i;
		if ((cp = getenv("COLUMNS")) != NULL &&
				(i = strtol(cp, NULL, 10)) > 0)
			scrnwidth = (int)i;

		if (scrnwidth != 0 && scrnheight != 0)
			goto jleave;
	}

#ifdef TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, &ws) < 0)
		ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGSIZE
	if (ioctl(1, TIOCGSIZE, &ws) < 0)
		ts.ts_lines = ts.ts_cols = 0;
#endif

	if (scrnheight == 0) {
		speed_t ospeed = (tcgetattr(1, &tbuf) < 0) ? B9600 :
				cfgetospeed(&tbuf);

		if (ospeed < B1200)
			scrnheight = 9;
		else if (ospeed == B1200)
			scrnheight = 14;
#ifdef TIOCGWINSZ
		else if (ws.ws_row != 0)
			scrnheight = ws.ws_row;
#elif defined TIOCGSIZE
		else if (ts.ts_lines != 0)
			scrnheight = ts.ts_lines;
#endif
		else
			scrnheight = 24;

#if defined TIOCGWINSZ || defined TIOCGSIZE
		if (0 ==
# ifdef TIOCGWINSZ
				(realscreenheight = ws.ws_row)
# else
				(realscreenheight = ts.ts_lines)
# endif
		)
			realscreenheight = 24;
#endif
	}

	if (scrnwidth == 0 && 0 ==
#ifdef TIOCGWINSZ
			(scrnwidth = ws.ws_col)
#elif defined TIOCGSIZE
			(scrnwidth = ts.ts_cols)
#endif
	)
		scrnwidth = 80;

jleave:
#ifdef SIGWINCH
	if (is_sighdl && (options & OPT_INTERACTIVE))
		tty_signal(SIGWINCH);
#endif
}

static sigjmp_buf	__hdrjmp; /* XXX */

static int
_rcv_mode(char const *folder)
{
	char *cp;
	int i;
	sighandler_type prevint;

	if (folder == NULL)
		folder = "%";
	else if (*folder == '@') {
		/* This must be treated specially to make invocation like
		 * -A imap -f @mailbox work */
		if ((cp = value("folder")) != NULL &&
				which_protocol(cp) == PROTO_IMAP)
			(void)n_strlcpy(mailname, cp, MAXPATHLEN);
	}

	i = setfile(folder, 0);
	if (i < 0)
		exit(1);		/* error already reported */
	if (options & OPT_EXISTONLY)
		exit(i);

	if (options & OPT_HEADERSONLY) {
#ifdef HAVE_IMAP
		if (mb.mb_type == MB_IMAP)
			imap_getheaders(1, msgCount);
#endif
		time_current_update(&time_current, FAL0);
		for (i = 1; i <= msgCount; i++)
			printhead(i, stdout, 0);
		exit(exit_status);
	}

	callhook(mailname, 0);
	if (i > 0 && value("emptystart") == NULL)
		exit(1);

	if (sigsetjmp(__hdrjmp, 1) == 0) {
		if ((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, _hdrstop);
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

	/* Enter the command loop */
	if (options & OPT_INTERACTIVE)
		tty_init();
	commands();
	if (options & OPT_INTERACTIVE)
		tty_destroy();

	if (mb.mb_type == MB_FILE || mb.mb_type == MB_MAILDIR) {
		safe_signal(SIGHUP, SIG_IGN);
		safe_signal(SIGINT, SIG_IGN);
		safe_signal(SIGQUIT, SIG_IGN);
	}
	n_strlcpy(mboxname, expand("&"), sizeof(mboxname));
	quit();
	return exit_status;
}

static void
_hdrstop(int signo)
{
	(void)signo;

	fflush(stdout);
	fprintf(stderr, tr(141, "\nInterrupt\n"));
	siglongjmp(__hdrjmp, 1);
}

char const *const	weekday_names[7 + 1] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", NULL
};
char const *const	month_names[12 + 1] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL
};

char const *const	uagent = UAGENT;
char const *const	version = VERSION;

sighandler_type		dflpipe = SIG_DFL;

int 
main(int argc, char *argv[])
{
	static char const optstr[] = "A:a:Bb:c:DdEeFfHiNnO:q:Rr:S:s:tu:Vv~#",
		usagestr[] =
		"Synopsis:\n"
		"  %s [-BDdEFintv~] [-A acc] [-a attachment] "
			"[-b bcc-addr] [-c cc-addr]\n"
		"\t  [-O mtaopt [-O mtaopt-arg]] [-q file] [-r from-addr] "
			"[-S var[=value]]\n"
		"\t  [-s subject] to-addr...\n"
		"  %s [-BDdEeHiNnRv~#] [-A acct] "
			"[-S var[=value]] -f [file]\n"
		"  %s [-BDdEeiNnRv~#] [-A acc] [-S var[=value]] [-u user]\n";

	struct a_arg *a_head = NULL, *a_curr = /* silence CC */ NULL;
	struct name *to = NULL, *cc = NULL, *bcc = NULL;
	struct attachment *attach = NULL;
	char *cp = NULL, *subject = NULL, *qf = NULL, *Aflag = NULL;
	char const *okey, **oargs = NULL, *folder = NULL;
	int oargs_size = 0, oargs_count = 0, smopts_size = 0, i;

	/*
	 * Start our lengthy setup
	 */

	starting =
	unset_allow_undefined = TRU1;

	progname = argv[0];
	_startup();

	/* Command line parsing */
	while ((i = getopt(argc, argv, optstr)) >= 0) {
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
			options |= OPT_SENDMODE;
			break;
		case 'B':
			/* Make 0/1 line buffered */
			setvbuf(stdin, NULL, _IOLBF, 0);
			setvbuf(stdout, NULL, _IOLBF, 0);
			break;
		case 'b':
			/* Get Blind Carbon Copy Recipient list */
			bcc = cat(bcc, checkaddrs(lextract(optarg,GBCC|GFULL)));
			options |= OPT_SENDMODE;
			break;
		case 'c':
			/* Get Carbon Copy Recipient list */
			cc = cat(cc, checkaddrs(lextract(optarg, GCC|GFULL)));
			options |= OPT_SENDMODE;
			break;
		case 'D':
#ifdef HAVE_IMAP
			okey = "disconnected";
			goto joarg;
#else
			break;
#endif
		case 'd':
			okey = "debug";
#ifdef WANT_ASSERTS
			assign(okey, "");
#endif
			goto joarg;
		case 'E':
			okey = "skipemptybody";
			goto joarg;
		case 'e':
			options |= OPT_EXISTONLY;
			break;
		case 'F':
			options |= OPT_F_FLAG | OPT_SENDMODE;
			break;
		case 'f':
			/* User is specifying file to "edit" with Mail,
			 * as opposed to reading system mailbox.
			 * If no argument is given, we read his mbox file.
			 * Check for remaining arguments later */
			folder = "&";
			break;
		case 'H':
			options |= OPT_HEADERSONLY;
			break;
		case 'i':
			/* Ignore interrupts */
			okey = "ignore";
			goto joarg;
		case 'N':
			/* Avoid initial header printing */
			okey = "noheader";
			goto joarg;
		case 'n':
			/* Don't source "unspecified system start-up file" */
			options |= OPT_NOSRC;
			break;
		case 'O':
			/* Additional options to pass-through to MTA */
			if (smopts_count == (size_t)smopts_size)
				smopts_size = _grow_cpp(&smopts, smopts_size,
						(int)smopts_count);
			smopts[smopts_count++] = skin(optarg);
			break;
		case 'q':
			/* Quote file TODO drop? -Q with real quote?? what ? */
			if (*optarg != '-')
				qf = optarg;
			options |= OPT_SENDMODE;
			break;
		case 'R':
			/* Open folders read-only */
			options |= OPT_R_FLAG;
			break;
		case 'r':
			/* Set From address. */
			options |= OPT_r_FLAG;
			if ((option_r_arg = optarg)[0] != '\0') {
				struct name *fa = nalloc(optarg, GFULL);

				if (is_addr_invalid(fa, 1) ||
						is_fileorpipe_addr(fa)) {
					fprintf(stderr, tr(271,
						"Invalid address argument "
						"with -r\n"));
					goto usage;
				}
				option_r_arg = fa->n_name;
				/* TODO -r options is set in smopts, but may
				 * TODO be overwritten by setting from= in
				 * TODO an interactive session!
				 * TODO Maybe disable setting of from?
				 * TODO Warn user?  Update manual!! */
				okey = savecat("from=", fa->n_fullname);
				goto joarg;
				/* ..and fa goes even though it is ready :/ */
			}
			break;
		case 'S':
			/* Set variable.  We need to do this twice, since the
			 * user surely wants the setting to take effect
			 * immediately, but also doesn't want it to be
			 * overwritten from within resource files */
			{	char *a[2];
				okey = a[0] = optarg;
				a[1] = NULL;
				set(a);
			}
joarg:
			if (oargs_count == oargs_size)
				oargs_size = _grow_cpp(&oargs, oargs_size,
						oargs_count);
			oargs[oargs_count++] = okey;
			break;
		case 's':
			/* Subject: */
			subject = optarg;
			options |= OPT_SENDMODE;
			break;
		case 't':
			/* Read defined set of headers from mail to be send */
			options |= OPT_SENDMODE | OPT_t_FLAG;
			break;
		case 'u':
			/* Set user name to pretend to be  */
			myname = option_u_arg = optarg;
			break;
		case 'V':
			puts(version);
			exit(0);
			/* NOTREACHED */
		case 'v':
			/* Be verbose */
			okey = "verbose";
#ifdef WANT_ASSERTS
			assign(okey, "");
#endif
			goto joarg;
		case '~':
			/* Enable tilde escapes even in non-interactive mode */
			options |= OPT_TILDE_FLAG;
			break;
		case '#':
			/* Work in batch mode, even if non-interactive */
			if (oargs_count + 3 >= oargs_size)
				oargs_size = _grow_cpp(&oargs, oargs_size,
						oargs_count);
			oargs[oargs_count++] = "dot";
			oargs[oargs_count++] = "emptystart";
			oargs[oargs_count++] = "noheader";
			oargs[oargs_count++] = "sendwait";
			options |= OPT_TILDE_FLAG | OPT_BATCH_FLAG;
			break;
		case '?':
usage:			fprintf(stderr, tr(135, usagestr),
				progname, progname, progname);
			exit(2);
		}
	}

	if (folder != NULL) {
		if (optind < argc) {
			if (optind + 1 < argc) {
				fprintf(stderr, tr(205,
					"More than one file given with -f\n"));
				goto usage;
			}
			folder = argv[optind];
		}
	} else {
		for (i = optind; argv[i]; i++)
			to = cat(to, checkaddrs(lextract(argv[i], GTO|GFULL)));
		if (to != NULL)
			options |= OPT_SENDMODE;
	}

	/* Check for inconsistent arguments */
	if (folder != NULL && to != NULL) {
		fprintf(stderr, tr(137,
			"Cannot give -f and people to send to.\n"));
		goto usage;
	}
	if ((options & (OPT_SENDMODE|OPT_t_FLAG)) == OPT_SENDMODE &&
			to == NULL) {
		fprintf(stderr, tr(138,
			"Send options without primary recipient specified.\n"));
		goto usage;
	}
	if ((options & OPT_R_FLAG) && to != NULL) {
		fprintf(stderr, "The -R option is meaningless in send mode.\n");
		goto usage;
	}

	/*
	 * Likely to go, perform more setup
	 */

	_setup_vars();

	if (options & OPT_INTERACTIVE) {
		_setscreensize(0);
#ifdef SIGWINCH
# ifndef TTY_WANTS_SIGWINCH
		if (safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
# endif
			safe_signal(SIGWINCH, _setscreensize);
#endif
	} else
		scrnheight = realscreenheight = 24, scrnwidth = 80;

	/* Snapshot our string pools.  Memory is auto-reclaimed from now on */
	spreserve();

	if ((options & OPT_NOSRC) == 0 && getenv("NAIL_NO_SYSTEM_RC") == NULL)
		load(SYSCONFRC);
	/* *expand() returns a savestr(), but load only uses the file name
	 * for fopen(), so it's safe to do this */
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

	/* Ensure the -S and other command line options take precedence over
	 * anything that may have been placed in resource files */
	for (i = 0; i < oargs_count; ++i) {
		char const *a[2];
		a[0] = oargs[i];
		a[1] = NULL;
		set(a);
	}

	/*
	 * We're finally completely setup and ready to go
	 */

	starting =
	unset_allow_undefined = FAL0;

	if (options & OPT_DEBUG)
		fprintf(stderr, tr(199, "user = %s, homedir = %s\n"),
			myname, homedir);

	if (! (options & OPT_SENDMODE))
		return _rcv_mode(folder);

	/* Now that full mailx(1)-style file expansion is possible handle the
	 * attachments which we had delayed due to this.
	 * This may use savestr(), but since we won't enter the command loop we
	 * don't need to care about that */
	while (a_head != NULL) {
		attach = add_attachment(attach, a_head->aa_file, NULL);
		if (attach != NULL) {
			a_curr = a_head;
			a_head = a_head->aa_next;
			ac_free(a_curr);
		} else {
			perror(a_head->aa_file);
			exit(1);
		}
	}

	/* xxx exit_status = EXIT_OK; */
	if (options & OPT_INTERACTIVE)
		tty_init();
	mail(to, cc, bcc, subject, attach, qf, ((options & OPT_F_FLAG) != 0));
	if (options & OPT_INTERACTIVE)
		tty_destroy();
	return exit_status;
}
