/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
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

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)lex.c	2.38 (gritter) 8/7/04";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/*
 * Mail -- a mail program
 *
 * Lexical processing of commands.
 */

static char	*prompt;

static const struct cmd	*lex __P((char []));
static int	is_prefix __P((char *, char *));
static void	intr __P((int));
static void	stop __P((int));
static void	hangup __P((int));

/*
 * Set up editing on the given file name.
 * If the first character of name is %, we are considered to be
 * editing the file, otherwise we are reading our mail which has
 * signficance for mbox and so forth.
 *
 * newmail: Check for new mail in the current folder only.
 */
int
setfile(name, newmail)
	char *name;
	int newmail;
{
	FILE *ibuf;
	int i, compressed = 0;
	struct stat stb;
	char isedit;
	char *who = name[1] ? name + 1 : myname;
	static int shudclob;
	extern int errno;
	size_t offset;
	int omsgcount = 0;
	struct shortcut *sh;

	isedit = *name != '%' && ((sh = get_shortcut(name)) == NULL ||
			*sh->sh_long != '%');
	if ((name = expand(name)) == NULL)
		return -1;

	switch (which_protocol(name)) {
	case PROTO_FILE:
		break;
	case PROTO_POP3:
		shudclob = 1;
		return pop3_setfile(name, newmail, isedit);
	case PROTO_IMAP:
		shudclob = 1;
		if (newmail)
			omsgcount = msgcount;
		i = imap_setfile(name, newmail, isedit);
		if (newmail)
			goto newmail;
		else
			return i;
	case PROTO_UNKNOWN:
		fprintf(stderr, catgets(catd, CATSET, 217,
				"Cannot handle protocol: %s\n"), name);
		return -1;
	}
	if ((ibuf = Zopen(name, "r", &compressed)) == NULL) {
		if ((!isedit && errno == ENOENT) || newmail)
			goto nomail;
		perror(name);
		return(-1);
	}

	if (fstat(fileno(ibuf), &stb) < 0) {
		Fclose(ibuf);
		if (newmail)
			goto nomail;
		perror("fstat");
		return (-1);
	}

	if (S_ISDIR(stb.st_mode)) {
		Fclose(ibuf);
		if (newmail)
			goto nomail;
		errno = EISDIR;
		perror(name);
		return (-1);
	} else if (S_ISREG(stb.st_mode)) {
		/*EMPTY*/
	} else {
		Fclose(ibuf);
		if (newmail)
			goto nomail;
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

	/*
	 * Copy the messages into /tmp
	 * and set pointers.
	 */

	if (!newmail) {
		mb.mb_type = MB_FILE;
		mb.mb_perm = MB_DELE|MB_EDIT;
		mb.mb_compressed = compressed;
		if ((i = open(name, O_WRONLY)) < 0 && !mb.mb_compressed)
			mb.mb_perm = 0;
		else
			close(i);
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
	} else /* newmail */{
		fseek(mb.mb_otf, 0L, SEEK_END);
		fseek(ibuf, mailsize, SEEK_SET);
		offset = mailsize;
		omsgcount = msgcount;
	}
	mailsize = fsize(ibuf);
	if (newmail && mailsize <= offset) {
		relsesigs();
		goto nomail;
	}
	setptr(ibuf, offset);
	setmsize(msgcount);
	Fclose(ibuf);
	relsesigs();
	sawcom = 0;
	if ((!edit || newmail) && msgcount == 0) {
nomail:
		if (!newmail) {
			if (value("emptystart") == NULL)
				fprintf(stderr, catgets(catd, CATSET, 88,
						"No mail for %s\n"), who);
		}
		return 1;
	}
	if (newmail) {
newmail:	newmailinfo(omsgcount);
	}
	return(0);
}


int
newmailinfo(omsgcount)
	int	omsgcount;
{
	int	mdot = getmdot();

	if (msgcount > omsgcount) {
		printf(catgets(catd, CATSET, 158, "New mail has arrived.\n"));
		if (msgcount - omsgcount == 1)
			printf(catgets(catd, CATSET, 214,
				"Loaded 1 new message\n"));
		else
			printf(catgets(catd, CATSET, 215,
				"Loaded %d new messages\n"),
				msgcount - omsgcount);
	} else
		printf("Loaded %d messages\n", msgcount);
	if (value("header")) {
		if (mb.mb_type == MB_IMAP)
			imap_getheaders(omsgcount+1, msgcount);
		while (++omsgcount <= msgcount)
			if (!(message[omsgcount-1].m_flag & MDELETED))
				printhead(omsgcount, stdout);
	}
	return mdot;
}

static int	*msgvec;
static int	reset_on_stop;			/* do a reset() if stopped */

/*
 * Interpret user commands one by one.  If standard input is not a tty,
 * print no prompt.
 */
void
commands()
{
	int eofloop = 0;
	int n;
	char *linebuf = NULL, *av, *nv;
	size_t linesize = 0;

	(void)&eofloop;
	if (!sourcing) {
		if (safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
			safe_signal(SIGINT, intr);
		if (safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
			safe_signal(SIGHUP, hangup);
		safe_signal(SIGTSTP, stop);
		safe_signal(SIGTTOU, stop);
		safe_signal(SIGTTIN, stop);
	}
	setexit();
	for (;;) {
		/*
		 * Print the prompt, if needed.  Clear out
		 * string space, and flush the output.
		 */
		if (!sourcing && value("interactive") != NULL) {
			av = (av = value("autoinc")) ? savestr(av) : NULL;
			nv = (nv = value("newmail")) ? savestr(nv) : NULL;
			if (is_a_tty[0] && (av != NULL || nv != NULL ||
					mb.mb_type == MB_IMAP)) {
				struct stat st;

				n = (av && strcmp(av, "noimap")) |
					(nv && strcmp(nv, "noimap"));
				if ((mb.mb_type == MB_FILE &&
						stat(mailname, &st) == 0 &&
						st.st_size > mailsize) ||
						(mb.mb_type == MB_IMAP &&
						imap_newmail(n))) {
					int odot = dot - &message[0];
					int odid = did_print_dot;

					setfile(mailname, 1);
					dot = &message[odot];
					did_print_dot = odid;
				}
			}
			reset_on_stop = 1;
			if ((prompt = value("prompt")) == NULL)
				prompt = value("bsdcompat") ? "& " : "? ";
			printf(prompt);
		}
		fflush(stdout);
		sreset();
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
		reset_on_stop = 0;
		if (n < 0) {
				/* eof */
			if (loading)
				break;
			if (sourcing) {
				unstack();
				continue;
			}
			if (value("interactive") != NULL &&
			    value("ignoreeof") != NULL &&
			    ++eofloop < 25) {
				printf(catgets(catd, CATSET, 89,
						"Use \"quit\" to quit.\n"));
				continue;
			}
			break;
		}
		eofloop = 0;
		if (execute(linebuf, 0, n))
			break;
	}
	if (linebuf)
		free(linebuf);
}

/*
 * Execute a single command.
 * Command functions return 0 for success, 1 for error, and -1
 * for abort.  A 1 or -1 aborts a load or source.  A -1 aborts
 * the interactive command loop.
 * Contxt is non-zero if called while composing mail.
 */
int
execute(linebuf, contxt, linesize)
	char linebuf[];
	int contxt;
	size_t linesize;
{
	char *word;
	char *arglist[MAXARGC];
	const struct cmd *com = (struct cmd *)NULL;
	char *cp, *cp2;
	int c;
	int muvec[2];
	int e = 1;

	/*
	 * Strip the white space away from the beginning
	 * of the command, then scan out a word, which
	 * consists of anything except digits and white space.
	 *
	 * Handle ! escapes differently to get the correct
	 * lexical conventions.
	 */
	word = ac_alloc(linesize + 1);
	for (cp = linebuf; whitechar(*cp & 0377); cp++);
	if (*cp == '!') {
		if (sourcing) {
			printf(catgets(catd, CATSET, 90,
					"Can't \"!\" while sourcing\n"));
			goto out;
		}
		shell(cp+1);
		ac_free(word);
		return(0);
	}
	if (*cp == '#') {
		ac_free(word);
		return 0;
	}
	cp2 = word;
	if (*cp != '|') {
		while (*cp && strchr(" \t0123456789$^.:/-+*'\",;", *cp) == NULL)
			*cp2++ = *cp++;
	} else
		*cp2++ = *cp++;
	*cp2 = '\0';

	/*
	 * Look up the command; if not found, bitch.
	 * Normally, a blank command would map to the
	 * first command in the table; while sourcing,
	 * however, we ignore blank lines to eliminate
	 * confusion.
	 */

	if (sourcing && *word == '\0') {
		ac_free(word);
		return(0);
	}
	com = lex(word);
	if (com == NULL) {
		printf(catgets(catd, CATSET, 91,
				"Unknown command: \"%s\"\n"), word);
		goto out;
	}

	/*
	 * See if we should execute the command -- if a conditional
	 * we always execute it, otherwise, check the state of cond.
	 */

	if ((com->c_argtype & F) == 0) {
		if ((cond == CRCV && !rcvmode) ||
				(cond == CSEND && rcvmode) ||
				(cond == CTERM && !is_a_tty[0]) ||
				(cond == CNONTERM && is_a_tty[0])) {
			ac_free(word);
			return(0);
		}
	}

	/*
	 * Process the arguments to the command, depending
	 * on the type he expects.  Default to an error.
	 * If we are sourcing an interactive command, it's
	 * an error.
	 */

	if (!rcvmode && (com->c_argtype & M) == 0) {
		printf(catgets(catd, CATSET, 92,
			"May not execute \"%s\" while sending\n"), com->c_name);
		goto out;
	}
	if (sourcing && com->c_argtype & I) {
		printf(catgets(catd, CATSET, 93,
			"May not execute \"%s\" while sourcing\n"),
				com->c_name);
		goto out;
	}
	if ((mb.mb_perm & MB_DELE) == 0 && com->c_argtype & W) {
		printf(catgets(catd, CATSET, 94,
		"May not execute \"%s\" -- message file is read only\n"),
		   com->c_name);
		goto out;
	}
	if (contxt && com->c_argtype & R) {
		printf(catgets(catd, CATSET, 95,
			"Cannot recursively invoke \"%s\"\n"), com->c_name);
		goto out;
	}
	if (mb.mb_type == MB_VOID && com->c_argtype & A) {
		printf(catgets(catd, CATSET, 257,
			"Cannot execute \"%s\" without active mailbox\n"),
				com->c_name);
		goto out;
	}
	switch (com->c_argtype & ~(F|P|I|M|T|W|R|A)) {
	case MSGLIST:
		/*
		 * A message list defaulting to nearest forward
		 * legal message.
		 */
		if (msgvec == 0) {
			printf(catgets(catd, CATSET, 96,
				"Illegal use of \"message list\"\n"));
			break;
		}
		if ((c = getmsglist(cp, msgvec, com->c_msgflag)) < 0)
			break;
		if (c == 0) {
			if ((*msgvec = first(com->c_msgflag, com->c_msgmask))
					!= 0)
				msgvec[1] = 0;
		}
		if (*msgvec == 0) {
			printf(catgets(catd, CATSET, 97,
					"No applicable messages\n"));
			break;
		}
		e = (*com->c_func)(msgvec);
		break;

	case NDMLIST:
		/*
		 * A message list with no defaults, but no error
		 * if none exist.
		 */
		if (msgvec == 0) {
			printf(catgets(catd, CATSET, 98,
				"Illegal use of \"message list\"\n"));
			break;
		}
		if (getmsglist(cp, msgvec, com->c_msgflag) < 0)
			break;
		e = (*com->c_func)(msgvec);
		break;

	case STRLIST:
		/*
		 * Just the straight string, with
		 * leading blanks removed.
		 */
		while (whitechar(*cp & 0377))
			cp++;
		e = (*com->c_func)(cp);
		break;

	case RAWLIST:
	case ECHOLIST:
		/*
		 * A vector of strings, in shell style.
		 */
		if ((c = getrawlist(cp, linesize, arglist,
				sizeof arglist / sizeof *arglist,
				(com->c_argtype&~(F|P|I|M|T|W|R|A))==ECHOLIST))
					< 0)
			break;
		if (c < com->c_minargs) {
			printf(catgets(catd, CATSET, 99,
				"%s requires at least %d arg(s)\n"),
				com->c_name, com->c_minargs);
			break;
		}
		if (c > com->c_maxargs) {
			printf(catgets(catd, CATSET, 100,
				"%s takes no more than %d arg(s)\n"),
				com->c_name, com->c_maxargs);
			break;
		}
		e = (*com->c_func)(arglist);
		break;

	case NOLIST:
		/*
		 * Just the constant zero, for exiting,
		 * eg.
		 */
		e = (*com->c_func)(0);
		break;

	default:
		panic(catgets(catd, CATSET, 101, "Unknown argtype"));
	}

out:
	ac_free(word);
	/*
	 * Exit the current source file on
	 * error.
	 */
	if (e) {
		if (e < 0)
			return 1;
		if (loading)
			return 1;
		if (sourcing)
			unstack();
		return 0;
	}
	if (com == (struct cmd *)NULL)
		return(0);
	if (value("autoprint") != NULL && com->c_argtype & P)
		if ((dot->m_flag & (MDELETED|MHIDDEN)) == 0) {
			muvec[0] = dot - &message[0] + 1;
			muvec[1] = 0;
			type(muvec);
		}
	if (!sourcing && (com->c_argtype & T) == 0)
		sawcom = 1;
	return(0);
}

/*
 * Set the size of the message vector used to construct argument
 * lists to message list functions.
 */
void
setmsize(sz)
	int sz;
{

	if (msgvec != 0)
		free(msgvec);
	msgvec = (int *)scalloc((sz + 1), sizeof *msgvec);
}

/*
 * Find the correct command in the command table corresponding
 * to the passed command "word"
 */

static const struct cmd *
lex(Word)
	char Word[];
{
	extern const struct cmd cmdtab[];
	const struct cmd *cp;

	for (cp = &cmdtab[0]; cp->c_name != NULL; cp++)
		if (is_prefix(Word, cp->c_name))
			return(cp);
	return(NULL);
}

/*
 * Determine if as1 is a valid prefix of as2.
 * Return true if yep.
 */
static int
is_prefix(as1, as2)
	char *as1, *as2;
{
	char *s1, *s2;

	s1 = as1;
	s2 = as2;
	while (*s1++ == *s2)
		if (*s2++ == '\0')
			return(1);
	return(*--s1 == '\0');
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
static void
intr(s)
	int s;
{

	noreset = 0;
	if (!inithdr)
		sawcom++;
	inithdr = 0;
	while (sourcing)
		unstack();

	close_all_files();

	if (image >= 0) {
		close(image);
		image = -1;
	}
	fprintf(stderr, catgets(catd, CATSET, 102, "Interrupt\n"));
	reset(0);
}

/*
 * When we wake up after ^Z, reprint the prompt.
 */
static void
stop(s)
	int s;
{
	sighandler_type old_action = safe_signal(s, SIG_DFL);
	sigset_t nset;

	sigemptyset(&nset);
	sigaddset(&nset, s);
	sigprocmask(SIG_UNBLOCK, &nset, (sigset_t *)NULL);
	kill(0, s);
	sigprocmask(SIG_BLOCK, &nset, (sigset_t *)NULL);
	safe_signal(s, old_action);
	if (reset_on_stop) {
		reset_on_stop = 0;
		reset(0);
	}
}

/*
 * Branch here on hangup signal and simulate "exit".
 */
/*ARGSUSED*/
static void
hangup(s)
	int s;
{

	/* nothing to do? */
	exit(1);
}

/*
 * Announce the presence of the current Mail version,
 * give the message count, and print a header listing.
 */
void
announce(printheaders)
	int printheaders;
{
	int vec[2], mdot;

	mdot = newfileinfo();
	vec[0] = mdot;
	vec[1] = 0;
	dot = &message[mdot - 1];
	if (printheaders && msgcount > 0 && value("header") != NULL) {
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
newfileinfo()
{
	struct message *mp;
	int u, n, mdot, d, s, hidden;
	char fname[PATHSIZE], zname[PATHSIZE], *ename;

	if (mb.mb_type == MB_VOID)
		return 1;
	mdot = getmdot();
	s = d = hidden = 0;
	for (mp = &message[0], n = 0, u = 0; mp < &message[msgcount]; mp++) {
		if (mp->m_flag & MNEW)
			n++;
		if ((mp->m_flag & MREAD) == 0)
			u++;
		if (mp->m_flag & MDELETED)
			d++;
		if (mp->m_flag & MSAVED)
			s++;
		if (mp->m_flag & MHIDDEN)
			hidden++;
	}
	ename = mailname;
	if (getfold(fname, sizeof fname - 1) >= 0) {
		strcat(fname, "/");
		if (which_protocol(fname) != PROTO_IMAP &&
				strncmp(fname, mailname, strlen(fname)) == 0) {
			snprintf(zname, sizeof zname, "+%s",
					mailname + strlen(fname));
			ename = zname;
		}
	}
	printf(catgets(catd, CATSET, 103, "\"%s\": "), ename);
	if (msgcount == 1)
		printf(catgets(catd, CATSET, 104, "1 message"));
	else
		printf(catgets(catd, CATSET, 105, "%d messages"), msgcount);
	if (n > 0)
		printf(catgets(catd, CATSET, 106, " %d new"), n);
	if (u-n > 0)
		printf(catgets(catd, CATSET, 107, " %d unread"), u);
	if (d > 0)
		printf(catgets(catd, CATSET, 108, " %d deleted"), d);
	if (s > 0)
		printf(catgets(catd, CATSET, 109, " %d saved"), s);
	if (hidden > 0)
		printf(catgets(catd, CATSET, 109, " %d hidden"), hidden);
	if (mb.mb_type == MB_CACHE)
		printf(" [Disconnected]");
	else if (mb.mb_perm == 0)
		printf(catgets(catd, CATSET, 110, " [Read only]"));
	printf("\n");
	return(mdot);
}

int
getmdot()
{
	struct message	*mp;
	int	mdot;

	if (mb.mb_type == MB_VOID)
		return 1;
	for (mp = &message[0]; mp < &message[msgcount]; mp++)
		if ((mp->m_flag & (MNEW|MHIDDEN)) == MNEW)
			break;
	if (mp >= &message[msgcount])
		for (mp = &message[0]; mp < &message[msgcount]; mp++)
			if ((mp->m_flag & (MREAD|MHIDDEN)) == 0)
				break;
	if (mp >= &message[msgcount])
		for (mp = &message[0]; mp < &message[msgcount]; mp++)
			if ((mp->m_flag & MHIDDEN) == 0)
				break;
	if (mp < &message[msgcount])
		mdot = mp - &message[0] + 1;
	else if (value("showlast"))
		mdot = mp - &message[0];
	else
		mdot = 1;
	return mdot;
}

/*
 * Print the current version number.
 */

/*ARGSUSED*/
int
pversion(v)
	void *v;
{
	printf(catgets(catd, CATSET, 111, "Version %s\n"), version);
	return(0);
}

/*
 * Load a file of user definitions.
 */
void
load(name)
	char *name;
{
	FILE *in, *oldin;

	if ((in = Fopen(name, "r")) == NULL)
		return;
	oldin = input;
	input = in;
	loading = 1;
	sourcing = 1;
	commands();
	loading = 0;
	sourcing = 0;
	input = oldin;
	Fclose(in);
}

void
initbox(name)
	const char *name;
{
	char *tempMesg;
	int dummy;

	if (mb.mb_type != MB_VOID) {
		strncpy(prevfile, mailname, PATHSIZE);
		prevfile[PATHSIZE-1]='\0';
	}
	if (name != mailname) {
		strncpy(mailname, name, PATHSIZE);
		mailname[PATHSIZE-1]='\0';
	}
	if ((mb.mb_otf = Ftemp(&tempMesg, "Rx", "w", 0600, 0)) == NULL) {
		perror(catgets(catd, CATSET, 87,
					"temporary mail message file"));
		exit(1);
	}
	fcntl(fileno(mb.mb_otf), F_SETFD, FD_CLOEXEC);
	if ((mb.mb_itf = safe_fopen(tempMesg, "r", &dummy)) == NULL) {
		perror(tempMesg);
		exit(1);
	}
	fcntl(fileno(mb.mb_itf), F_SETFD, FD_CLOEXEC);
	rm(tempMesg);
	Ftfree(&tempMesg);
	msgcount = 0;
	if (message) {
		free(message);
		message = NULL;
		msgspace = 0;
	}
	prevdot = NULL;
	dot = NULL;
}
