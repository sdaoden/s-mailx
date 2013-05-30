/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Lexical processing of commands.
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

#include "rcv.h"

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "extern.h"

static int		*_msgvec;
static int		_reset_on_stop;	/* do a reset() if stopped */
static char const	*_prompt;
static sighandler_type	_oldpipe;

/* Update mailname (if *name* != NULL) and displayname */
static void	_update_mailname(char const *name);
#ifdef HAVE_MBLEN /* TODO unite __narrow_{pre,suf}fix() into one function! */
SINLINE size_t	__narrow_prefix(char const *cp, size_t maxl);
SINLINE size_t	__narrow_suffix(char const *cp, size_t cpl, size_t maxl);
#endif

static const struct cmd *lex(char *Word);
static void stop(int s);
static void hangup(int s);

#ifdef HAVE_MBLEN
SINLINE size_t
__narrow_prefix(char const *cp, size_t maxl)
{
	int err;
	size_t i, ok;

	for (err = ok = i = 0; i < maxl;) {
		int ml = mblen(cp, maxl - i);
		if (ml < 0) { /* XXX _narrow_prefix(): mblen() error; action? */
			(void)mblen(NULL, 0);
			err = 1;
			ml = 1;
		} else {
			if (! err)
				ok = i;
			err = 0;
			if (ml == 0)
				break;
		}
		cp += ml;
		i += ml;
	}
	return ok;
}

SINLINE size_t
__narrow_suffix(char const *cp, size_t cpl, size_t maxl)
{
	int err;
	size_t i, ok;

	for (err = ok = i = 0; cpl > maxl || err;) {
		int ml = mblen(cp, cpl);
		if (ml < 0) { /* XXX _narrow_suffix(): mblen() error; action? */
			(void)mblen(NULL, 0);
			err = 1;
			ml = 1;
		} else {
			if (! err)
				ok = i;
			err = 0;
			if (ml == 0)
				break;
		}
		cp += ml;
		i += ml;
		cpl -= ml;
	}
	return ok;
}
#endif /* HAVE_MBLEN */

static void
_update_mailname(char const *name)
{
	char tbuf[MAXPATHLEN], *mailp, *dispp;
	size_t i, j;

	/* Don't realpath(3) if it's only an update request */
	if (name != NULL) {
#ifdef HAVE_REALPATH
		enum protocol p = which_protocol(name);
		if (p == PROTO_FILE || p == PROTO_MAILDIR) {
			if (realpath(name, mailname) == NULL) {
				fprintf(stderr, tr(151,
					"Can't canonicalize `%s'\n"), name);
				goto jleave;
			}
		} else
#endif
			(void)n_strlcpy(mailname, name, MAXPATHLEN);
	}

	mailp = mailname;
	dispp = displayname;

	/* Don't display an absolute path but "+FOLDER" if under *folder* */
	if (getfold(tbuf, sizeof(tbuf)) >= 0) {
		i = strlen(tbuf);
		if (i < sizeof(tbuf) - 1)
			tbuf[i++] = '/';
		if (strncmp(tbuf, mailp, i) == 0) {
			mailp += i;
			*dispp++ = '+';
		}
	}

	/* We want to see the name of the folder .. on the screen */
	i = strlen(mailp);
	if (i < sizeof(displayname) - 1)
		memcpy(dispp, mailp, i + 1);
	else {
		/* Avoid disrupting multibyte sequences (if possible) */
#ifndef HAVE_MBLEN
		j = sizeof(displayname) / 3 - 1;
		i -= sizeof(displayname) - (1/* + */ + 3) - j;
#else
		j = __narrow_prefix(mailp, sizeof(displayname) / 3);
		i = j + __narrow_suffix(mailp + j, i - j,
			sizeof(displayname) - (1/* + */ + 3 + 1) - j);
#endif
		(void)snprintf(dispp, sizeof(displayname), "%.*s...%s",
			(int)j, mailp, mailp + i);
	}
#ifdef HAVE_REALPATH
jleave:	;
#endif
}

/*
 * Set up editing on the given file name.
 * If the first character of name is %, we are considered to be
 * editing the file, otherwise we are reading our mail which has
 * signficance for mbox and so forth.
 *
 * newmail: Check for new mail in the current folder only.
 */
int
setfile(char const *name, int newmail)
{
	FILE *ibuf;
	int i, compressed = 0;
	struct stat stb;
	bool_t isedit;
	char const *who = name[1] ? name + 1 : myname;
	static int shudclob;
	size_t offset;
	int omsgCount = 0;
	struct shortcut *sh;
	struct flock	flp;

	isedit = (*name != '%' && ((sh = get_shortcut(name)) == NULL ||
			*sh->sh_long != '%'));
	if ((name = expand(name)) == NULL)
		return (-1);

	switch (which_protocol(name)) {
	case PROTO_FILE:
		break;
	case PROTO_MAILDIR:
		return (maildir_setfile(name, newmail, isedit));
#ifdef USE_POP3
	case PROTO_POP3:
		shudclob = 1;
		return (pop3_setfile(name, newmail, isedit));
#endif
#ifdef USE_IMAP
	case PROTO_IMAP:
		shudclob = 1;
		if (newmail) {
			if (mb.mb_type == MB_CACHE)
				return 1;
		}
		return imap_setfile(name, newmail, isedit);
#endif
	default:
		fprintf(stderr, tr(217, "Cannot handle protocol: %s\n"), name);
		return (-1);
	}

	if ((ibuf = Zopen(name, "r", &compressed)) == NULL) {
		if ((!isedit && errno == ENOENT) || newmail) {
			if (newmail)
				goto nonewmail;
			goto nomail;
		}
		perror(name);
		return(-1);
	}

	if (fstat(fileno(ibuf), &stb) < 0) {
		Fclose(ibuf);
		if (newmail)
			goto nonewmail;
		perror("fstat");
		return (-1);
	}

	if (S_ISDIR(stb.st_mode)) {
		Fclose(ibuf);
		if (newmail)
			goto nonewmail;
		errno = EISDIR;
		perror(name);
		return (-1);
	} else if (S_ISREG(stb.st_mode)) {
		/*EMPTY*/
	} else {
		Fclose(ibuf);
		if (newmail)
			goto nonewmail;
		errno = EINVAL;
		perror(name);
		return (-1);
	}

	/*
	 * Looks like all will be well.  We must now relinquish our
	 * hold on the current set of stuff.  Must hold signals
	 * while we are reading the new file, else we will ruin
	 * the message[] data structure.
	 */

	holdsigs();
	if (shudclob && !newmail)
		quit();

#ifdef	HAVE_SOCKETS
	if (!newmail && mb.mb_sock.s_fd >= 0)
		sclose(&mb.mb_sock);
#endif	/* HAVE_SOCKETS */

	/*
	 * Copy the messages into /tmp
	 * and set pointers.
	 */

	flp.l_type = F_RDLCK;
	flp.l_start = 0;
	flp.l_whence = SEEK_SET;
	if (!newmail) {
		mb.mb_type = MB_FILE;
		mb.mb_perm = (options & OPT_R_FLAG) ? 0 : MB_DELE|MB_EDIT;
		mb.mb_compressed = compressed;
		if (compressed) {
			if (compressed & 0200)
				mb.mb_perm = 0;
		} else {
			if ((i = open(name, O_WRONLY)) < 0)
				mb.mb_perm = 0;
			else
				close(i);
		}
		if (shudclob) {
			if (mb.mb_itf) {
				fclose(mb.mb_itf);
				mb.mb_itf = NULL;
			}
			if (mb.mb_otf) {
				fclose(mb.mb_otf);
				mb.mb_otf = NULL;
			}
		}
		shudclob = 1;
		edit = isedit;
		initbox(name);
		offset = 0;
		flp.l_len = 0;
		if (!edit && fcntl(fileno(ibuf), F_SETLKW, &flp) < 0) {
			perror("Unable to lock mailbox");
			Fclose(ibuf);
			return -1;
		}
	} else /* newmail */{
		fseek(mb.mb_otf, 0L, SEEK_END);
		fseek(ibuf, mailsize, SEEK_SET);
		offset = mailsize;
		omsgCount = msgCount;
		flp.l_len = offset;
		if (!edit && fcntl(fileno(ibuf), F_SETLKW, &flp) < 0)
			goto nonewmail;
	}
	mailsize = fsize(ibuf);
	if (newmail && (size_t)mailsize <= offset) {
		relsesigs();
		goto nonewmail;
	}
	setptr(ibuf, offset);
	setmsize(msgCount);
	if (newmail && mb.mb_sorted) {
		mb.mb_threaded = 0;
		sort((void *)-1);
	}
	Fclose(ibuf);
	relsesigs();
	if (!newmail)
		sawcom = FAL0;
	if ((!edit || newmail) && msgCount == 0) {
nonewmail:
		if (!newmail) {
			if (value("emptystart") == NULL)
nomail:				fprintf(stderr, catgets(catd, CATSET, 88,
						"No mail for %s\n"), who);
		}
		return 1;
	}
	if (newmail) {
		newmailinfo(omsgCount);
	}
	return(0);
}

int
newmailinfo(int omsgCount)
{
	int	mdot;
	int	i;

	for (i = 0; i < omsgCount; i++)
		message[i].m_flag &= ~MNEWEST;
	if (msgCount > omsgCount) {
		for (i = omsgCount; i < msgCount; i++)
			message[i].m_flag |= MNEWEST;
		printf(tr(158, "New mail has arrived.\n"));
		if (msgCount - omsgCount == 1)
			printf(tr(214, "Loaded 1 new message.\n"));
		else
			printf(tr(215, "Loaded %d new messages.\n"),
				msgCount - omsgCount);
	} else
		printf(tr(224, "Loaded %d messages.\n"), msgCount);
	callhook(mailname, 1);
	mdot = getmdot(1);
	if (value("header")) {
#ifdef USE_IMAP
		if (mb.mb_type == MB_IMAP)
			imap_getheaders(omsgCount+1, msgCount);
#endif
		time_current_update(&time_current, FAL0);
		while (++omsgCount <= msgCount)
			if (visible(&message[omsgCount-1]))
				printhead(omsgCount, stdout, 0);
	}
	return mdot;
}

/*
 * Interpret user commands one by one.  If standard input is not a tty,
 * print no prompt.
 */
void 
commands(void)
{
	int eofloop = 0, n;
	char *linebuf = NULL, *av, *nv;
	size_t linesize = 0;
#ifdef USE_IMAP
	int x;
#endif

	if (!sourcing) {
		if (safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
			safe_signal(SIGINT, onintr);
		if (safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
			safe_signal(SIGHUP, hangup);
		safe_signal(SIGTSTP, stop);
		safe_signal(SIGTTOU, stop);
		safe_signal(SIGTTIN, stop);
	}
	_oldpipe = safe_signal(SIGPIPE, SIG_IGN);
	safe_signal(SIGPIPE, _oldpipe);
	setexit();

	for (;;) {
		interrupts = 0;
		handlerstacktop = NULL;
		/*
		 * Print the prompt, if needed.  Clear out
		 * string space, and flush the output.
		 */
		if (! sourcing && (options & OPT_INTERACTIVE)) {
			av = (av = value("autoinc")) ? savestr(av) : NULL;
			nv = (nv = value("newmail")) ? savestr(nv) : NULL;
			if (is_a_tty[0] && (av != NULL || nv != NULL ||
					mb.mb_type == MB_IMAP)) {
				struct stat st;

				n = (av && strcmp(av, "noimap") &&
						strcmp(av, "nopoll")) |
					(nv && strcmp(nv, "noimap") &&
					 	strcmp(nv, "nopoll"));
#ifdef USE_IMAP
				x = !(av || nv);
#endif
				if ((mb.mb_type == MB_FILE &&
						stat(mailname, &st) == 0 &&
						st.st_size > mailsize) ||
#ifdef USE_IMAP
						(mb.mb_type == MB_IMAP &&
						imap_newmail(n) > x) ||
#endif
						(mb.mb_type == MB_MAILDIR &&
						n != 0)) {
					int odot = dot - &message[0];
					bool_t odid = did_print_dot;

					setfile(mailname, 1);
					if (mb.mb_type != MB_IMAP) {
						dot = &message[odot];
						did_print_dot = odid;
					}
				}
			}
			_reset_on_stop = 1;
			exit_status = 0;
			if ((_prompt = value("prompt")) == NULL)
				_prompt = value("bsdcompat") ? "& " : "? ";
			printf("%s", _prompt);
		}
		fflush(stdout);

		if (! sourcing) {
			sreset();
			if ((nv = termios_state.ts_linebuf) != NULL) {
				termios_state.ts_linebuf = NULL;
				termios_state.ts_linesize = 0;
				free(nv); /* TODO pool give-back */
			}
		}

		/*
		 * Read a line of commands from the current input
		 * and handle end of file specially.
		 */
		n = 0;
		for (;;) {
			n = readline_restart(input, &linebuf, &linesize, n);
			if (n < 0)
				break;
			if (n == 0 || linebuf[n - 1] != '\\')
				break;
			linebuf[n - 1] = ' ';
		}
		_reset_on_stop = 0;
		if (n < 0) {
				/* eof */
			if (loading)
				break;
			if (sourcing) {
				unstack();
				continue;
			}
			if ((options & OPT_INTERACTIVE) &&
					value("ignoreeof") && ++eofloop < 25) {
				printf(tr(89, "Use `quit' to quit.\n"));
				continue;
			}
			break;
		}
		eofloop = 0;
		inhook = 0;
		if (execute(linebuf, 0, n))
			break;
	}

	if (linebuf)
		free(linebuf);
	if (sourcing)
		sreset();
}

/*
 * Execute a single command.
 * Command functions return 0 for success, 1 for error, and -1
 * for abort.  A 1 or -1 aborts a load or source.  A -1 aborts
 * the interactive command loop.
 * Contxt is non-zero if called while composing mail.
 */
int
execute(char *linebuf, int contxt, size_t linesize)
{
	char *arglist[MAXARGC], *word, *cp;
	struct cmd const *com = (struct cmd*)NULL;
	int muvec[2], c, e = 1;

	/* '~X' -> 'call X' */
	word = ac_alloc(MAX(sizeof("call"), linesize) + 1);

	/*
	 * Strip the white space away from the beginning
	 * of the command, then scan out a word, which
	 * consists of anything except digits and white space.
	 *
	 * Handle ! escapes differently to get the correct
	 * lexical conventions.
	 */
	for (cp = linebuf; whitechar(*cp); cp++)
		;
	if (*cp == '!') {
		if (sourcing) {
			fprintf(stderr, tr(90, "Can't `!' while sourcing\n"));
			goto jleave;
		}
		shell(cp + 1);
		goto jfree_ret0;
	}

	if (*cp == '#')
		goto jfree_ret0;

	if (*cp == '~') {
		++cp;
		memcpy(word, "call", 5);
	} else {
		char *cp2 = word;

		if (*cp != '|') {
			while (*cp && strchr(" \t0123456789$^.:/-+*'\",;(`",
					*cp) == NULL)
				*cp2++ = *cp++;
		} else
			*cp2++ = *cp++;
		*cp2 = '\0';
	}

	/*
	 * Look up the command; if not found, bitch.
	 * Normally, a blank command would map to the
	 * first command in the table; while sourcing,
	 * however, we ignore blank lines to eliminate
	 * confusion.
	 */
	if (sourcing && *word == '\0')
		goto jfree_ret0;

	com = lex(word);
	if (com == NULL || com->c_func == &ccmdnotsupp) {
		fprintf(stderr, tr(91, "Unknown command: `%s'\n"), word);
		if (com != NULL) {
			ccmdnotsupp(NULL);
			com = NULL;
		}
		goto jleave;
	}

	/*
	 * See if we should execute the command -- if a conditional
	 * we always execute it, otherwise, check the state of cond.
	 */
	if ((com->c_argtype & F) == 0) {
		if ((cond == CRCV && (options & OPT_SENDMODE)) ||
				(cond == CSEND && ! (options & OPT_SENDMODE)) ||
				(cond == CTERM && ! is_a_tty[0]) ||
				(cond == CNONTERM && is_a_tty[0]))
			goto jfree_ret0;
	}

	/*
	 * Process the arguments to the command, depending
	 * on the type he expects.  Default to an error.
	 * If we are sourcing an interactive command, it's
	 * an error.
	 */
	if ((options & OPT_SENDMODE) && (com->c_argtype & M) == 0) {
		fprintf(stderr, tr(92,
			"May not execute `%s' while sending\n"),
			com->c_name);
		goto jleave;
	}
	if (sourcing && com->c_argtype & I) {
		fprintf(stderr, tr(93,
			"May not execute `%s' while sourcing\n"),
			com->c_name);
		goto jleave;
	}
	if ((mb.mb_perm & MB_DELE) == 0 && com->c_argtype & W) {
		fprintf(stderr, tr(94, "May not execute `%s' -- "
			"message file is read only\n"),
			com->c_name);
		goto jleave;
	}
	if (contxt && com->c_argtype & R) {
		fprintf(stderr, tr(95,
			"Cannot recursively invoke `%s'\n"), com->c_name);
		goto jleave;
	}
	if (mb.mb_type == MB_VOID && com->c_argtype & A) {
		fprintf(stderr, tr(257,
			"Cannot execute `%s' without active mailbox\n"),
			com->c_name);
		goto jleave;
	}

	switch (com->c_argtype & ~(F|P|I|M|T|W|R|A)) {
	case MSGLIST:
		/* Message list defaulting to nearest forward legal message */
		if (_msgvec == 0)
			goto je96;
		if ((c = getmsglist(cp, _msgvec, com->c_msgflag)) < 0)
			break;
		if (c == 0) {
			*_msgvec = first(com->c_msgflag, com->c_msgmask);
			if (*_msgvec != 0)
				_msgvec[1] = 0;
		}
		if (*_msgvec == 0) {
			if (! inhook)
				printf(tr(97, "No applicable messages\n"));
			break;
		}
		e = (*com->c_func)(_msgvec);
		break;

	case NDMLIST:
		/* Message list with no defaults, but no error if none exist */
		if (_msgvec == 0) {
je96:
			fprintf(stderr, tr(96,
				"Illegal use of `message list'\n"));
			break;
		}
		if ((c = getmsglist(cp, _msgvec, com->c_msgflag)) < 0)
			break;
		e = (*com->c_func)(_msgvec);
		break;

	case STRLIST:
		/* Just the straight string, with leading blanks removed */
		while (whitechar(*cp))
			cp++;
		e = (*com->c_func)(cp);
		break;

	case RAWLIST:
	case ECHOLIST:
		/* A vector of strings, in shell style */
		if ((c = getrawlist(cp, linesize, arglist,
				sizeof arglist / sizeof *arglist,
				(com->c_argtype&~(F|P|I|M|T|W|R|A)) == ECHOLIST)
				) < 0)
			break;
		if (c < com->c_minargs) {
			fprintf(stderr, tr(99,
				"`%s' requires at least %d arg(s)\n"),
				com->c_name, com->c_minargs);
			break;
		}
		if (c > com->c_maxargs) {
			fprintf(stderr, tr(100,
				"`%s' takes no more than %d arg(s)\n"),
				com->c_name, com->c_maxargs);
			break;
		}
		e = (*com->c_func)(arglist);
		break;

	case NOLIST:
		/* Just the constant zero, for exiting, eg. */
		e = (*com->c_func)(0);
		break;

	default:
		panic(tr(101, "Unknown argument type"));
	}

jleave:
	ac_free(word);

	/* Exit the current source file on error */
	if (e) {
		if (e < 0)
			return 1;
		if (loading)
			return 1;
		if (sourcing)
			unstack();
		return 0;
	}
	if (com == (struct cmd*)NULL )
		return 0;
	if (boption("autoprint") && com->c_argtype & P)
		if (visible(dot)) {
			muvec[0] = dot - &message[0] + 1;
			muvec[1] = 0;
			type(muvec);
		}
	if (! sourcing && ! inhook && (com->c_argtype & T) == 0)
		sawcom = TRU1;
	return 0;

jfree_ret0:
	ac_free(word);
	return 0;
}

/*
 * Set the size of the message vector used to construct argument
 * lists to message list functions.
 */
void 
setmsize(int sz)
{

	if (_msgvec != 0)
		free(_msgvec);
	_msgvec = (int*)scalloc(sz + 1, sizeof *_msgvec);
}

/*
 * Find the correct command in the command table corresponding
 * to the passed command "word"
 */

static const struct cmd *
lex(char *Word)
{
	extern const struct cmd cmdtab[];
	const struct cmd *cp;

	for (cp = &cmdtab[0]; cp->c_name != NULL; cp++)
		if (is_prefix(Word, cp->c_name))
			return(cp);
	return(NULL);
}

/*
 * The following gets called on receipt of an interrupt.  This is
 * to abort printout of a command, mainly.
 * Dispatching here when command() is inactive crashes rcv.
 * Close all open files except 0, 1, 2, and the temporary.
 * Also, unstack all source files.
 */

static int	inithdr;		/* am printing startup headers */

/*ARGSUSED*/
void 
onintr(int s)
{
	if (handlerstacktop != NULL) {
		handlerstacktop(s);
		return;
	}
	safe_signal(SIGINT, onintr);
	noreset = 0;
	if (!inithdr)
		sawcom = TRU1;
	inithdr = 0;
	while (sourcing)
		unstack();

	close_all_files();

	if (image >= 0) {
		close(image);
		image = -1;
	}
	if (interrupts != 1)
		fprintf(stderr, catgets(catd, CATSET, 102, "Interrupt\n"));
	safe_signal(SIGPIPE, _oldpipe);
	reset(0);
}

/*
 * When we wake up after ^Z, reprint the prompt.
 */
static void 
stop(int s)
{
	sighandler_type old_action = safe_signal(s, SIG_DFL);
	sigset_t nset;

	sigemptyset(&nset);
	sigaddset(&nset, s);
	sigprocmask(SIG_UNBLOCK, &nset, (sigset_t *)NULL);
	kill(0, s);
	sigprocmask(SIG_BLOCK, &nset, (sigset_t *)NULL);
	safe_signal(s, old_action);
	if (_reset_on_stop) {
		_reset_on_stop = 0;
		reset(0);
	}
}

/*
 * Branch here on hangup signal and simulate "exit".
 */
/*ARGSUSED*/
static void 
hangup(int s)
{
	(void)s;
	/* nothing to do? */
	exit(1);
}

/*
 * Announce the presence of the current Mail version,
 * give the message count, and print a header listing.
 */
void 
announce(int printheaders)
{
	int vec[2], mdot;

	mdot = newfileinfo();
	vec[0] = mdot;
	vec[1] = 0;
	dot = &message[mdot - 1];
	if (printheaders && msgCount > 0 && value("header") != NULL) {
		inithdr++;
		headers(vec);
		inithdr = 0;
	}
}

/*
 * Announce information about the file we are editing.
 * Return a likely place to set dot.
 */
int 
newfileinfo(void)
{
	struct message *mp;
	int u, n, mdot, d, s, hidden, killed, moved;

	if (mb.mb_type == MB_VOID)
		return 1;
	mdot = getmdot(0);
	s = d = hidden = killed = moved =0;
	for (mp = &message[0], n = 0, u = 0; mp < &message[msgCount]; mp++) {
		if (mp->m_flag & MNEW)
			n++;
		if ((mp->m_flag & MREAD) == 0)
			u++;
		if ((mp->m_flag & (MDELETED|MSAVED)) == (MDELETED|MSAVED))
			moved++;
		if ((mp->m_flag & (MDELETED|MSAVED)) == MDELETED)
			d++;
		if ((mp->m_flag & (MDELETED|MSAVED)) == MSAVED)
			s++;
		if (mp->m_flag & MHIDDEN)
			hidden++;
		if (mp->m_flag & MKILL)
			killed++;
	}
	_update_mailname(NULL);
	printf(tr(103, "\"%s\": "), displayname);
	if (msgCount == 1)
		printf(tr(104, "1 message"));
	else
		printf(tr(105, "%d messages"), msgCount);
	if (n > 0)
		printf(tr(106, " %d new"), n);
	if (u-n > 0)
		printf(tr(107, " %d unread"), u);
	if (d > 0)
		printf(tr(108, " %d deleted"), d);
	if (s > 0)
		printf(tr(109, " %d saved"), s);
	if (moved > 0)
		printf(tr(136, " %d moved"), moved);
	if (hidden > 0)
		printf(tr(139, " %d hidden"), hidden);
	if (killed > 0)
		printf(tr(144, " %d killed"), killed);
	if (mb.mb_type == MB_CACHE)
		printf(" [Disconnected]");
	else if (mb.mb_perm == 0)
		printf(tr(110, " [Read only]"));
	printf("\n");
	return(mdot);
}

int 
getmdot(int newmail)
{
	struct message	*mp;
	char	*cp;
	int	mdot;
	enum mflag	avoid = MHIDDEN|MKILL|MDELETED;

	if (!newmail) {
		if (value("autothread"))
			thread(NULL);
		else if ((cp = value("autosort")) != NULL) {
			free(mb.mb_sorted);
			mb.mb_sorted = sstrdup(cp);
			sort(NULL);
		}
	}
	if (mb.mb_type == MB_VOID)
		return 1;
	if (newmail)
		for (mp = &message[0]; mp < &message[msgCount]; mp++)
			if ((mp->m_flag & (MNEWEST|avoid)) == MNEWEST)
				break;
	if (!newmail || mp >= &message[msgCount]) {
		for (mp = mb.mb_threaded ? threadroot : &message[0];
				mb.mb_threaded ?
					mp != NULL : mp < &message[msgCount];
				mb.mb_threaded ?
					mp = next_in_thread(mp) : mp++)
			if ((mp->m_flag & (MNEW|avoid)) == MNEW)
				break;
	}
	if (mb.mb_threaded ? mp == NULL : mp >= &message[msgCount])
		for (mp = mb.mb_threaded ? threadroot : &message[0];
				mb.mb_threaded ? mp != NULL:
					mp < &message[msgCount];
				mb.mb_threaded ? mp = next_in_thread(mp) : mp++)
			if (mp->m_flag & MFLAGGED)
				break;
	if (mb.mb_threaded ? mp == NULL : mp >= &message[msgCount])
		for (mp = mb.mb_threaded ? threadroot : &message[0];
				mb.mb_threaded ? mp != NULL:
					mp < &message[msgCount];
				mb.mb_threaded ? mp = next_in_thread(mp) : mp++)
			if ((mp->m_flag & (MREAD|avoid)) == 0)
				break;
	if (mb.mb_threaded ? mp != NULL : mp < &message[msgCount])
		mdot = mp - &message[0] + 1;
	else if (value("showlast")) {
		if (mb.mb_threaded) {
			for (mp = this_in_thread(threadroot, -1); mp;
					mp = prev_in_thread(mp))
				if ((mp->m_flag & avoid) == 0)
					break;
			mdot = mp ? mp - &message[0] + 1 : msgCount;
		} else {
			for (mp = &message[msgCount-1]; mp >= &message[0]; mp--)
				if ((mp->m_flag & avoid) == 0)
					break;
			mdot = mp >= &message[0] ? mp-&message[0]+1 : msgCount;
		}
	} else if (mb.mb_threaded) {
		for (mp = threadroot; mp; mp = next_in_thread(mp))
			if ((mp->m_flag & avoid) == 0)
				break;
		mdot = mp ? mp - &message[0] + 1 : 1;
	} else {
		for (mp = &message[0]; mp < &message[msgCount]; mp++)
			if ((mp->m_flag & avoid) == 0)
				break;
		mdot = mp < &message[msgCount] ? mp-&message[0]+1 : 1;
	}
	return mdot;
}

/*
 * Print the current version number.
 */

/*ARGSUSED*/
int 
pversion(void *v)
{
	(void)v;
	printf(catgets(catd, CATSET, 111, "Version %s\n"), version);
	return(0);
}

/*
 * Load a file of user definitions.
 */
void 
load(char const *name)
{
	FILE *in, *oldin;

	if (name == NULL || (in = Fopen(name, "r")) == NULL)
		return;
	oldin = input;
	input = in;
	loading = TRU1;
	sourcing = TRU1;
	commands();
	loading = FAL0;
	sourcing = FAL0;
	input = oldin;
	Fclose(in);
}

void 
initbox(const char *name)
{
	char *tempMesg;
	int dummy;

	if (mb.mb_type != MB_VOID)
		(void)n_strlcpy(prevfile, mailname, MAXPATHLEN);
	_update_mailname(name != mailname ? name : NULL);
	if ((mb.mb_otf = Ftemp(&tempMesg, "tmpbox", "w", 0600, 0)) == NULL) {
		perror(tr(87, "temporary mail message file"));
		exit(1);
	}
	(void)fcntl(fileno(mb.mb_otf), F_SETFD, FD_CLOEXEC);
	if ((mb.mb_itf = safe_fopen(tempMesg, "r", &dummy)) == NULL) {
		perror(tr(87, "temporary mail message file"));
		exit(1);
	}
	(void)fcntl(fileno(mb.mb_itf), F_SETFD, FD_CLOEXEC);
	rm(tempMesg);
	Ftfree(&tempMesg);
	msgCount = 0;
	if (message) {
		free(message);
		message = NULL;
		msgspace = 0;
	}
	mb.mb_threaded = 0;
	if (mb.mb_sorted != NULL) {
		free(mb.mb_sorted);
		mb.mb_sorted = NULL;
	}
#ifdef USE_IMAP
	mb.mb_flags = MB_NOFLAGS;
#endif
	prevdot = NULL;
	dot = NULL;
	did_print_dot = FAL0;
}
