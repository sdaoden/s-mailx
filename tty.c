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
static char sccsid[] = "@(#)tty.c	1.10 (gritter) 5/22/02";
#endif
#endif /* not lint */

/*
 * Mail -- a mail program
 *
 * Generally useful tty stuff.
 */

#include "rcv.h"
#include "extern.h"
#include <errno.h>
#ifdef	HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef	HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

static	cc_t		c_erase;	/* Current erase char */
static	cc_t		c_kill;		/* Current kill char */
static	sigjmp_buf	rewrite;	/* Place to go when continued */
static	sigjmp_buf	intjmp;		/* Place to go when interrupted */
#ifndef TIOCSTI
static	int		ttyset;		/* We must now do erase/kill */
#endif
static	long		vdis;		/* _POSIX_VDISABLE char */

#ifdef IOSAFE 
static int got_interrupt;
static int safegetc(FILE *ibuf);
#endif

/*
 * Receipt continuation.
 */
static RETSIGTYPE
ttystop(s)
	int s;
{
	signal_handler_t old_action = safe_signal(s, SIG_DFL);
	sigset_t nset;

	sigemptyset(&nset);
	sigaddset(&nset, s);
	sigprocmask(SIG_BLOCK, &nset, NULL);
	kill(0, s);
	sigprocmask(SIG_UNBLOCK, &nset, NULL);
	safe_signal(s, old_action);
#ifdef IOSAFE
	got_interrupt = s;
#else
	fpurge(stdin);
#endif
	siglongjmp(rewrite, 1);
}

/*ARGSUSED*/
static RETSIGTYPE
ttyint(s)
	int s;
{
#ifdef IOSAFE
	got_interrupt = s;
#else
	fpurge(stdin);
	siglongjmp(intjmp, 1);
#endif
}

#ifdef IOSAFE
/* it is very awful, but only way I see to be able to do a
   interruptable stdio call */ 
static int
safegetc(ibuf)
FILE *ibuf;
{
	fd_set rds;
	int oldfl;
	int res;
	while (1) {
		errno = 0;
		oldfl = fcntl(fileno(ibuf),F_GETFL);
		fcntl(fileno(ibuf),F_SETFL,oldfl | O_NONBLOCK);
		res = getc(ibuf);
		fcntl(fileno(ibuf),F_SETFL,oldfl);
		if (res != EOF)
			return res;
		else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			clearerr(ibuf);
			FD_ZERO(&rds);
			FD_SET(fileno(ibuf),&rds);
			select((SELECT_TYPE_ARG1)(fileno(ibuf) + 1),
					SELECT_TYPE_ARG234(&rds),
					SELECT_TYPE_ARG234(NULL),
					SELECT_TYPE_ARG234(NULL),
					SELECT_TYPE_ARG5(NULL));
			/* if an interrupt occur drops the current
			   line and returns */
			if (got_interrupt)
				return EOF;
		} else {
			/* probably EOF one the file descriptors */
			return EOF;
		}
	}
}
#endif

/*
 * Read up a header from standard input.
 * The source string has the preliminary contents to
 * be read.
 *
 */

static char *
rtty_internal(pr, src)
	char pr[], src[];
{
	char ch, canonb[BUFSIZ];
	int c;
	char *cp, *cp2;

#if __GNUC__
	/* Avoid longjmp clobbering */
	(void) &c;
	(void) &cp2;
#endif

	fputs(pr, stdout);
	fflush(stdout);
	if (src != NULL && strlen(src) > BUFSIZ - 2) {
		printf("too long to edit\n");
		return(src);
	}
#ifndef TIOCSTI
	if (src != NULL)
		cp = copy(src, canonb);
	else
		cp = copy("", canonb);
	fputs(canonb, stdout);
	fflush(stdout);
#else
	cp = src == NULL ? "" : src;
	while ((c = *cp++) != '\0') {
		if ((c_erase != vdis && c == c_erase) ||
		    (c_kill != vdis && c == c_kill)) {
			ch = '\\';
			ioctl(0, TIOCSTI, &ch);
		}
		ch = c;
		ioctl(0, TIOCSTI, &ch);
	}
	cp = canonb;
	*cp = 0;
#endif
	cp2 = cp;
	while (cp2 < canonb + BUFSIZ)
		*cp2++ = 0;
	cp2 = cp;
	if (sigsetjmp(rewrite, 1))
		goto redo;
#ifdef IOSAFE
	got_interrupt = 0;
#endif
	safe_signal(SIGTSTP, ttystop);
	safe_signal(SIGTTOU, ttystop);
	safe_signal(SIGTTIN, ttystop);
	clearerr(stdin);
	while (cp2 < canonb + BUFSIZ) {
#ifdef IOSAFE
		c = safegetc(stdin);
		/* this is full of ACE but hopefully, interrupts will only
		   occur in the above read */
		if (got_interrupt == SIGINT)
			siglongjmp(intjmp,1);
		else if (got_interrupt)
			siglongjmp(rewrite,1);
#else
		c = getc(stdin);
#endif
		if (c == EOF || c == '\n')
			break;
		*cp2++ = c;
	}
	*cp2 = 0;
	safe_signal(SIGTSTP, SIG_DFL);
	safe_signal(SIGTTOU, SIG_DFL);
	safe_signal(SIGTTIN, SIG_DFL);
	if (c == EOF && ferror(stdin)) {
redo:
		cp = strlen(canonb) > 0 ? canonb : NULL;
		clearerr(stdin);
		return(rtty_internal(pr, cp));
	}
#ifndef TIOCSTI
	if (cp == NULL || *cp == '\0')
		return(src);
	cp2 = cp;
	if (!ttyset)
		return(strlen(canonb) > 0 ? savestr(canonb) : NULL);
	while (*cp != '\0') {
		c = *cp++;
		if (c_erase != vdis && c == c_erase) {
			if (cp2 == canonb)
				continue;
			if (cp2[-1] == '\\') {
				cp2[-1] = c;
				continue;
			}
			cp2--;
			continue;
		}
		if (c_kill != vdis && c == c_kill) {
			if (cp2 == canonb)
				continue;
			if (cp2[-1] == '\\') {
				cp2[-1] = c;
				continue;
			}
			cp2 = canonb;
			continue;
		}
		*cp2++ = c;
	}
	*cp2 = '\0';
#endif
	if (equal("", canonb))
		return(NULL);
	return(savestr(canonb));
}

/*
 * Read all relevant header fields.
 */

int
grabh(hp, gflags)
	struct header *hp;
	int gflags;
{
	struct termios ttybuf;
	signal_handler_t saveint;
#ifndef TIOCSTI
	signal_handler_t savequit;
#endif
	signal_handler_t savetstp;
	signal_handler_t savettou;
	signal_handler_t savettin;
	int errs;
#ifdef __GNUC__
	/* Avoid longjmp clobbering */
	(void) &saveint;
#endif

	savetstp = safe_signal(SIGTSTP, SIG_DFL);
	savettou = safe_signal(SIGTTOU, SIG_DFL);
	savettin = safe_signal(SIGTTIN, SIG_DFL);
	errs = 0;
#ifndef TIOCSTI
	ttyset = 0;
#endif
	if (tcgetattr(fileno(stdin), &ttybuf) < 0) {
		perror("tcgetattr");
		return(-1);
	}
	c_erase = ttybuf.c_cc[VERASE];
	c_kill = ttybuf.c_cc[VKILL];
#if defined (_PC_VDISABLE)
	if ((vdis = fpathconf(0, _PC_VDISABLE)) < 0)
		vdis = '\377';
#elif defined (_POSIX_VDISABLE)
	vdis = _POSIX_VDISABLE;
#else
	vdis = '\377';
#endif
#ifndef TIOCSTI
	ttybuf.c_cc[VERASE] = 0;
	ttybuf.c_cc[VKILL] = 0;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) == SIG_DFL)
		safe_signal(SIGINT, SIG_DFL);
	if ((savequit = safe_signal(SIGQUIT, SIG_IGN)) == SIG_DFL)
		safe_signal(SIGQUIT, SIG_DFL);
#else	/* TIOCSTI */
#ifdef IOSAFE
	got_interrupt = 0;
#endif
	if (sigsetjmp(intjmp, 1)) {
		/* avoid garbled output with C-c */
		printf("\n");
		fflush(stdout);
		goto out;
	}
	saveint = safe_signal(SIGINT, ttyint);
#endif	/* TIOCSTI */
	if (gflags & GTO) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_to != NIL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_to = checkaddrs(extract(rtty_internal("To: ",
						detract(hp->h_to, 0)), GTO));
	}
	if (gflags & GSUBJECT) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_subject != NULL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_subject = rtty_internal("Subject: ", hp->h_subject);
	}
	if (gflags & GCC) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_cc != NIL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_cc = checkaddrs(extract(rtty_internal("Cc: ",
						detract(hp->h_cc, 0)), GCC));
	}
	if (gflags & GBCC) {
#ifndef TIOCSTI
		if (!ttyset && hp->h_bcc != NIL)
			ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
		hp->h_bcc = checkaddrs(extract(rtty_internal("Bcc: ",
						detract(hp->h_bcc, 0)), GBCC));
	}
out:
	safe_signal(SIGTSTP, savetstp);
	safe_signal(SIGTTOU, savettou);
	safe_signal(SIGTTIN, savettin);
#ifndef TIOCSTI
	ttybuf.c_cc[VERASE] = c_erase;
	ttybuf.c_cc[VKILL] = c_kill;
	if (ttyset)
		tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
	safe_signal(SIGQUIT, savequit);
#endif
	safe_signal(SIGINT, saveint);
	return(errs);
}

/*
 * Read a line from tty; to be called from elsewhere
 */

char *
readtty(prefix, string)
char *prefix, *string;
{
	char *ret = NULL;
	struct termios ttybuf;
	signal_handler_t saveint;
#ifndef TIOCSTI
	signal_handler_t savequit;
#endif
	signal_handler_t savetstp;
	signal_handler_t savettou;
	signal_handler_t savettin;
#ifdef __GNUC__
	/* Avoid longjmp clobbering */
	(void) &saveint;
	(void) &ret;
#endif

	savetstp = safe_signal(SIGTSTP, SIG_DFL);
	savettou = safe_signal(SIGTTOU, SIG_DFL);
	savettin = safe_signal(SIGTTIN, SIG_DFL);
#ifndef TIOCSTI
	ttyset = 0;
#endif
	if (tcgetattr(fileno(stdin), &ttybuf) < 0) {
		perror("tcgetattr");
		return NULL;
	}
	c_erase = ttybuf.c_cc[VERASE];
	c_kill = ttybuf.c_cc[VKILL];
#ifndef TIOCSTI
	ttybuf.c_cc[VERASE] = 0;
	ttybuf.c_cc[VKILL] = 0;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) == SIG_DFL)
		safe_signal(SIGINT, SIG_DFL);
	if ((savequit = safe_signal(SIGQUIT, SIG_IGN)) == SIG_DFL)
		safe_signal(SIGQUIT, SIG_DFL);
#else
#ifdef IOSAFE
	got_interrupt = 0;
#endif
	if (sigsetjmp(intjmp, 1)) {
		/* avoid garbled output with C-c */
		printf("\n");
		fflush(stdout);
		goto out2;
	}
	saveint = safe_signal(SIGINT, ttyint);
#endif
#ifndef TIOCSTI
	if (!ttyset && string != NIL)
		ttyset++, tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
#endif
	ret = rtty_internal(prefix, string);
	if (ret != NULL && *ret == '\0')
		ret = NULL;
out2:
	safe_signal(SIGTSTP, savetstp);
	safe_signal(SIGTTOU, savettou);
	safe_signal(SIGTTIN, savettin);
#ifndef TIOCSTI
	ttybuf.c_cc[VERASE] = c_erase;
	ttybuf.c_cc[VKILL] = c_kill;
	if (ttyset)
		tcsetattr(fileno(stdin), TCSADRAIN, &ttybuf);
	safe_signal(SIGQUIT, savequit);
#endif
	safe_signal(SIGINT, saveint);
	return ret;
}
