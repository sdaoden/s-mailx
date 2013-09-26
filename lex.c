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

#include "nail.h"

#include <fcntl.h>

/* Yechh, can't initialize unions */
#define c_minargs	c_msgflag	/* Minimum argcount for RAWLIST */
#define c_maxargs	c_msgmask	/* Max argcount for RAWLIST */

struct cmd {
	char const	*c_name;		/* Name of command */
	int		(*c_func)(void *);	/* Implementor of command */
	enum argtype	c_argtype;		/* Arglist type (see below) */
	short		c_msgflag;		/* Required flags of msgs*/
	short		c_msgmask;		/* Relevant flags of msgs */
#ifdef HAVE_DOCSTRINGS
	int		c_docid;		/* Translation id of .c_doc */
	char const	*c_doc;			/* One line doc for command */
#endif
};

static int		*_msgvec;
static int		_reset_on_stop;	/* do a reset() if stopped */
static sighandler_type	_oldpipe;
/* _cmd_tab[] after fun protos */

/* Update mailname (if *name* != NULL) and displayname */
static void	_update_mailname(char const *name);
#ifdef HAVE_MBLEN /* TODO unite __narrow_{pre,suf}fix() into one function! */
SINLINE size_t	__narrow_prefix(char const *cp, size_t maxl);
SINLINE size_t	__narrow_suffix(char const *cp, size_t cpl, size_t maxl);
#endif

static const struct cmd *lex(char *Word);
/* Print a list of all commands */
static int	_pcmdlist(void *v);
static int	__pcmd_cmp(void const *s1, void const *s2);

static void stop(int s);
static void hangup(int s);

/* List of all commands */
static struct cmd const	_cmd_tab[] = {
#include "cmd_tab.h"	
};

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
	if (getfold(tbuf, sizeof tbuf)) {
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

static int
__pcmd_cmp(void const *s1, void const *s2)
{
	struct cmd const * const *c1 = s1,
		* const *c2 = s2;
	return (strcmp((*c1)->c_name, (*c2)->c_name));
}

static int
_pcmdlist(void *v)
{
	struct cmd const **cpa, *cp, **cursor;
	size_t i;
	(void)v;

	for (i = 0; _cmd_tab[i].c_name != NULL; ++i)
		;
	++i;
	cpa = ac_alloc(sizeof(cp) * i);

	for (i = 0; (cp = _cmd_tab + i)->c_name != NULL; ++i)
		cpa[i] = cp;
	cpa[i] = NULL;

	qsort(cpa, i, sizeof(cp), &__pcmd_cmp);

	printf(tr(14, "Commands are:\n"));
	for (i = 0, cursor = cpa; (cp = *cursor++) != NULL;) {
		size_t j;
		if (cp->c_func == &ccmdnotsupp)
			continue;
		j = strlen(cp->c_name) + 2;
		if ((i += j) > 72) {
			i = j;
			printf("\n");
		}
		printf((*cursor != NULL ? "%s, " : "%s\n"), cp->c_name);
	}

	ac_free(cpa);
	return 0;
}

/*
 * Set up editing on the given file name.
 * If the first character of name is %, we are considered to be
 * editing the file, otherwise we are reading our mail which has
 * signficance for mbox and so forth.
 *
 * nmail: Check for new mail in the current folder only.
 */
int
setfile(char const *name, int nmail)
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
		return (maildir_setfile(name, nmail, isedit));
#ifdef HAVE_POP3
	case PROTO_POP3:
		shudclob = 1;
		return (pop3_setfile(name, nmail, isedit));
#endif
#ifdef HAVE_IMAP
	case PROTO_IMAP:
		shudclob = 1;
		if (nmail) {
			if (mb.mb_type == MB_CACHE)
				return 1;
		}
		return imap_setfile(name, nmail, isedit);
#endif
	default:
		fprintf(stderr, tr(217, "Cannot handle protocol: %s\n"), name);
		return (-1);
	}

	if ((ibuf = Zopen(name, "r", &compressed)) == NULL) {
		if ((!isedit && errno == ENOENT) || nmail) {
			if (nmail)
				goto jnonmail;
			goto nomail;
		}
		perror(name);
		return(-1);
	}

	if (fstat(fileno(ibuf), &stb) < 0) {
		Fclose(ibuf);
		if (nmail)
			goto jnonmail;
		perror("fstat");
		return (-1);
	}

	if (S_ISDIR(stb.st_mode)) {
		Fclose(ibuf);
		if (nmail)
			goto jnonmail;
		errno = EISDIR;
		perror(name);
		return (-1);
	} else if (S_ISREG(stb.st_mode)) {
		/*EMPTY*/
	} else {
		Fclose(ibuf);
		if (nmail)
			goto jnonmail;
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

	holdsigs(); /* TODO note on this one in quit.c:quit() */
	if (shudclob && !nmail)
		quit();
#ifdef HAVE_SOCKETS
	if (!nmail && mb.mb_sock.s_fd >= 0)
		sclose(&mb.mb_sock);
#endif

	/*
	 * Copy the messages into /tmp
	 * and set pointers.
	 */

	flp.l_type = F_RDLCK;
	flp.l_start = 0;
	flp.l_whence = SEEK_SET;
	if (!nmail) {
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
	} else /* nmail */{
		fseek(mb.mb_otf, 0L, SEEK_END);
		fseek(ibuf, mailsize, SEEK_SET);
		offset = mailsize;
		omsgCount = msgCount;
		flp.l_len = offset;
		if (!edit && fcntl(fileno(ibuf), F_SETLKW, &flp) < 0)
			goto jnonmail;
	}
	mailsize = fsize(ibuf);
	if (nmail && (size_t)mailsize <= offset) {
		relsesigs();
		goto jnonmail;
	}
	setptr(ibuf, offset);
	setmsize(msgCount);
	if (nmail && mb.mb_sorted) {
		mb.mb_threaded = 0;
		sort((void *)-1);
	}
	Fclose(ibuf);
	relsesigs();
	if (!nmail)
		sawcom = FAL0;
	if ((!edit || nmail) && msgCount == 0) {
jnonmail:
		if (!nmail) {
			if (value("emptystart") == NULL)
nomail:				fprintf(stderr, tr(88, "No mail for %s\n"),
					who);
		}
		return 1;
	}
	if (nmail) {
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
#ifdef HAVE_IMAP
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
#ifdef HAVE_IMAP
	int x;
#endif

	if (!sourcing) {
		if (safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
			safe_signal(SIGINT, onintr);
		if (safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
			safe_signal(SIGHUP, hangup);
		/* TODO We do a lot of redundant signal handling, especially
		 * TODO with the line editor(s); try to merge this */
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
			if ((options & OPT_TTYIN) &&
					(av != NULL || nv != NULL ||
					mb.mb_type == MB_IMAP)) {
				struct stat st;

				n = (av && strcmp(av, "noimap") &&
						strcmp(av, "nopoll")) |
					(nv && strcmp(nv, "noimap") &&
					 	strcmp(nv, "nopoll"));
#ifdef HAVE_IMAP
				x = !(av || nv);
#endif
				if ((mb.mb_type == MB_FILE &&
						stat(mailname, &st) == 0 &&
						st.st_size > mailsize) ||
#ifdef HAVE_IMAP
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
		}

		if (! sourcing) {
			sreset();
			/* TODO Note: this buffer may contain a password
			 * TODO We should redefine the code flow which has
			 * TODO to do that */
			if ((nv = termios_state.ts_linebuf) != NULL) {
				termios_state.ts_linebuf = NULL;
				termios_state.ts_linesize = 0;
				free(nv); /* TODO pool give-back */
			}
			/* TODO Due to expand-on-tab of our line editor the
			 * TODO buffer may grow */
			if (linesize > LINESIZE * 3) {
				free(linebuf); /* TODO pool! but what? */
				linebuf = NULL;
				linesize = 0;
			}
		}

		/*
		 * Read a line of commands from the current input
		 * and handle end of file specially.
		 */
		n = readline_input(LNED_LF_ESC | LNED_HIST_ADD, NULL,
			&linebuf, &linesize);
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
		if (exit_status != EXIT_OK && (options & OPT_BATCH_FLAG) &&
				boption("batch-exit-on-error"))
			break;
	}

	if (linebuf != NULL)
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
	char _wordbuf[2], *arglist[MAXARGC], *cp, *word;
	struct cmd const *com = (struct cmd*)NULL;
	int muvec[2], c, e = 1;

	/* Strip the white space away from the beginning of the command */
	for (cp = linebuf; whitechar(*cp); ++cp)
		;

	/* Ignore comments */
	if (*cp == '#')
		goto jleave0;

	/* Handle ! differently to get the correct lexical conventions */
	if (*cp == '!') {
		if (sourcing) {
			fprintf(stderr, tr(90, "Can't `!' while sourcing\n"));
			goto jleave;
		}
		shell(++cp);
		goto jleave0;
	}

	/* Isolate the actual command; since it may not necessarily be
	 * separated from the arguments (as in `p1') we need to duplicate it to
	 * be able to create a NUL terminated version.
	 * We must be aware of special one letter commands here */
	arglist[0] = cp;
	switch (*cp) {
	case '|':
	case '~':
	case '?':
		++cp;
		/* FALLTHRU */
	case '\0':
		break;
	default:
		while (*cp && strchr(" \t0123456789$^.:/-+*'\",;(`",
				*cp) == NULL)
			++cp;
		break;
	}
	c = (int)(cp - arglist[0]);
	linesize -= c;
	word = (c <= 1) ? _wordbuf : salloc(c + 1);
	memcpy(word, arglist[0], c);
	word[c] = '\0';

	/* Look up the command; if not found, bitch.
	 * Normally, a blank command would map to the first command in the
	 * table; while sourcing, however, we ignore blank lines to eliminate
	 * confusion */
	if (sourcing && *word == '\0')
		goto jleave0;

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
				(cond == CTERM && ! (options & OPT_TTYIN)) ||
				(cond == CNONTERM && (options & OPT_TTYIN)))
			goto jleave0;
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
	/* Exit the current source file on error */
	if ((exec_last_comm_error = (e != 0))) {
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
jleave0:
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
	const struct cmd *cp;

	for (cp = _cmd_tab; cp->c_name != NULL; ++cp)
		if (is_prefix(Word, cp->c_name))
			goto jleave;
	cp = NULL;
jleave:
	return cp;
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

	termios_state_reset();
	close_all_files();

	if (image >= 0) {
		close(image);
		image = -1;
	}
	if (interrupts != 1)
		fprintf(stderr, tr(102, "Interrupt\n"));
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
	int u, n, mdot, d, s, hidden, moved;

	if (mb.mb_type == MB_VOID)
		return 1;
	mdot = getmdot(0);
	s = d = hidden = moved =0;
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
	if (mb.mb_type == MB_CACHE)
		printf(" [Disconnected]");
	else if (mb.mb_perm == 0)
		printf(tr(110, " [Read only]"));
	printf("\n");
	return(mdot);
}

int 
getmdot(int nmail)
{
	struct message	*mp;
	char	*cp;
	int	mdot;
	enum mflag	avoid = MHIDDEN|MDELETED;

	if (!nmail) {
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
	if (nmail)
		for (mp = &message[0]; mp < &message[msgCount]; mp++)
			if ((mp->m_flag & (MNEWEST|avoid)) == MNEWEST)
				break;
	if (!nmail || mp >= &message[msgCount]) {
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
	printf(tr(111, "Version %s\n"), version);
	return(0);
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
#ifdef HAVE_IMAP
	mb.mb_flags = MB_NOFLAGS;
#endif
	prevdot = NULL;
	dot = NULL;
	did_print_dot = FAL0;
}

#ifdef HAVE_DOCSTRINGS
bool_t
print_comm_docstr(char const *comm)
{
	bool_t rv = FAL0;
	struct cmd const *cp;

	for (cp = _cmd_tab; cp->c_name != NULL; ++cp) {
		if (cp->c_func == &ccmdnotsupp)
			continue;
		if (strcmp(comm, cp->c_name) == 0)
			printf("%s: %s\n", comm, tr(cp->c_docid, cp->c_doc));
		else if (is_prefix(comm, cp->c_name))
			printf("%s (%s): %s\n", comm, cp->c_name,
				tr(cp->c_docid, cp->c_doc));
		else
			continue;
		rv = TRU1;
		break;
	}
	return rv;
}
#endif
