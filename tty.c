/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Generally useful tty stuff.
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

#include <errno.h>
#include <unistd.h>

#ifdef HAVE_READLINE
# include <readline/history.h>
# include <readline/readline.h>
#elif defined HAVE_EDITLINE
# include <histedit.h>
#endif/*#else FIXME*/
# include <sys/ioctl.h>
# include <termios.h>
/*#endif*/

#include "extern.h"

static	sigjmp_buf	rewrite;	/* Place to go when continued */
static	sigjmp_buf	intjmp;		/* Place to go when interrupted */

#ifdef HAVE_CLEDIT /* FIXME */
static sighandler_type	_sigcont_save;
static bool_t		_handle_cont;
#endif
#ifdef HAVE_READLINE
static char *		_rl_buf;
static int		_rl_buflen;
#endif
#ifdef HAVE_EDITLINE
static EditLine *	_el_el;		/* editline(3) handle */
static History *	_el_hcom;	/* History handle for commline */
static char const *	_el_prompt;	/* Current prompt */
#endif

/*FIXME#ifndef HAVE_EDITLINE*/
# ifndef TIOCSTI
static	int		ttyset;		/* We must now do erase/kill */
# endif
static	struct termios	ttybuf;
static	long		vdis;		/* _POSIX_VDISABLE char */
static	cc_t		c_erase;	/* Current erase char */
static	cc_t		c_kill;		/* Current kill char */
/*FIXME#endif*/

/* A couple of things for command line editing / history */
#ifdef HAVE_CLEDIT
static char *		_cl_histfile(void);
#endif
#ifdef HAVE_READLINE
static int		_rl_pre_input(void);
#endif
#ifdef HAVE_EDITLINE
static char const *	_el_getprompt(void);
#endif

static void ttystop(int s);
static void ttyint(int s);
static int safe_getc(FILE *ibuf);
static char *rtty_internal(char const *pr, char const *src);

#ifdef HAVE_CLEDIT
static char *
_cl_histfile(void)
{
	char *rv;

	rv = voption("NAIL_HISTFILE");
	if (rv != NULL)
		rv = fexpand(rv, FEXP_LOCAL);
	return rv;
}
#endif

#ifdef HAVE_READLINE
static int
_rl_pre_input(void)
{
	/* Handle leftover data from \ escaped former line */
	rl_extend_line_buffer(_rl_buflen + 10);
	strcpy(rl_line_buffer, _rl_buf);
	rl_point = rl_end = _rl_buflen;
	rl_pre_input_hook = (rl_hook_func_t*)NULL;
	rl_redisplay();
	return 0;
}
#endif

#ifdef HAVE_EDITLINE
static char const *
_el_getprompt(void)
{
	return _el_prompt;
}
#endif

/*
 * Receipt continuation.
 */
static void
ttystop(int s)
{
	sighandler_type old_action = safe_signal(s, SIG_DFL);
	sigset_t nset;

	sigemptyset(&nset);
	sigaddset(&nset, s);
	sigprocmask(SIG_BLOCK, &nset, NULL);
	kill(0, s);
	sigprocmask(SIG_UNBLOCK, &nset, NULL);
	safe_signal(s, old_action);
	siglongjmp(rewrite, 1);
}

/*ARGSUSED*/
static void
ttyint(int s)
{
	(void)s;
	siglongjmp(intjmp, 1);
}

/*
 * Interrupts will cause trouble if we are inside a stdio call. As
 * this is only relevant if input comes from a terminal, we can simply
 * bypass it by read() then.
 */
static int
safe_getc(FILE *ibuf)
{
	if (fileno(ibuf) == 0 && is_a_tty[0]) {
		char c;
		int sz;

again:
		if ((sz = read(0, &c, 1)) != 1) {
			if (sz < 0 && errno == EINTR)
				goto again;
			return EOF;
		}
		return c & 0377;
	} else
		return getc(ibuf);
}

/*
 * Read up a header from standard input.
 * The source string has the preliminary contents to
 * be read.
 */
static char *
rtty_internal(char const *pr, char const *src)
{
	char ch, canonb[LINESIZE];
	int c;
	char *cp, *cp2;

	fputs(pr, stdout);
	fflush(stdout);
	if (src != NULL && strlen(src) > sizeof canonb - 2) {
		printf(catgets(catd, CATSET, 200, "too long to edit\n"));
		return(savestr(src));
	}

/*FIXME#ifndef HAVE_EDITLINE*/
# ifndef TIOCSTI
	if (src != NULL)
		cp = sstpcpy(canonb, src);
	else
		cp = sstpcpy(canonb, "");
	fputs(canonb, stdout);
	fflush(stdout);
# else
	cp = UNCONST(src == NULL ? "" : src);
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
# endif
/*FIXME#endif*/

	cp2 = cp;
	while (cp2 < canonb + sizeof canonb)
		*cp2++ = 0;
	cp2 = cp;
	if (sigsetjmp(rewrite, 1))
		goto redo;
	safe_signal(SIGTSTP, ttystop);
	safe_signal(SIGTTOU, ttystop);
	safe_signal(SIGTTIN, ttystop);
	clearerr(stdin);

/*FIXME#ifndef HAVE_EDITLINE*/
	while (cp2 < canonb + sizeof canonb - 1) {
		c = safe_getc(stdin);
		if (c == EOF || c == '\n')
			break;
		*cp2++ = c;
	}
	*cp2 = 0;
/*FIXME#endif*/
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
		return(savestr(src));
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

	return (*canonb == '\0') ? NULL : savestr(canonb);
}

/*
 * Read all relevant header fields.
 */

#ifndef	TIOCSTI
#define	TTYSET_CHECK(h)	if (!ttyset && (h) != NULL) \
					ttyset++, tcsetattr(0, TCSADRAIN, \
					&ttybuf);
#else
#define	TTYSET_CHECK(h)
#endif

#define	GRAB_SUBJECT	if (gflags & GSUBJECT) { \
				TTYSET_CHECK(hp->h_subject) \
				hp->h_subject = rtty_internal("Subject: ", \
						hp->h_subject); \
			}

static struct name *
grabaddrs(const char *field, struct name *np, int comma, enum gfield gflags)
{
	struct name	*nq;

	TTYSET_CHECK(np);
	loop:
		np = lextract(rtty_internal(field, detract(np, comma)), gflags);
		for (nq = np; nq != NULL; nq = nq->n_flink)
			if (is_addr_invalid(nq, 1))
				goto loop;
	return np;
}

int
grabh(struct header *hp, enum gfield gflags, int subjfirst)
{
	sighandler_type saveint, savetstp, savettou, savettin;
#ifndef TIOCSTI
	sighandler_type savequit;
#endif
	int errs;
	int volatile comma;

	savetstp = safe_signal(SIGTSTP, SIG_DFL);
	savettou = safe_signal(SIGTTOU, SIG_DFL);
	savettin = safe_signal(SIGTTIN, SIG_DFL);
	errs = 0;
	comma = value("bsdcompat") || value("bsdmsgs") ? 0 : GCOMMA;
#ifndef TIOCSTI
	ttyset = 0;
#endif
	if (tcgetattr(fileno(stdin), &ttybuf) < 0) {
		perror("tcgetattr");
		return(-1);
	}
	c_erase = ttybuf.c_cc[VERASE];
	c_kill = ttybuf.c_cc[VKILL];
#if defined (_PC_VDISABLE) && defined (HAVE_FPATHCONF)
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
	saveint = safe_signal(SIGINT, SIG_IGN);
	if (sigsetjmp(intjmp, 1)) {
		/* avoid garbled output with C-c */
		printf("\n");
		fflush(stdout);
		goto out;
	}
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, ttyint);
#endif	/* TIOCSTI */

	if (gflags & GTO)
		hp->h_to = grabaddrs("To: ", hp->h_to, comma, GTO|GFULL);
	if (subjfirst)
		GRAB_SUBJECT
	if (gflags & GCC)
		hp->h_cc = grabaddrs("Cc: ", hp->h_cc, comma, GCC|GFULL);
	if (gflags & GBCC)
		hp->h_bcc = grabaddrs("Bcc: ", hp->h_bcc, comma, GBCC|GFULL);
	if (gflags & GEXTRA) {
		if (hp->h_from == NULL)
			hp->h_from = lextract(myaddrs(hp), GEXTRA|GFULL);
		hp->h_from = grabaddrs("From: ", hp->h_from, comma,
				GEXTRA|GFULL);
		if (hp->h_replyto == NULL)
			hp->h_replyto = lextract(value("replyto"),
					GEXTRA|GFULL);
		hp->h_replyto = grabaddrs("Reply-To: ", hp->h_replyto, comma,
				GEXTRA|GFULL);
		if (hp->h_sender == NULL)
			hp->h_sender = extract(value("sender"),
					GEXTRA|GFULL);
		hp->h_sender = grabaddrs("Sender: ", hp->h_sender, comma,
				GEXTRA|GFULL);
		if (hp->h_organization == NULL)
			hp->h_organization = value("ORGANIZATION");
		TTYSET_CHECK(hp->h_organization);
		hp->h_organization = rtty_internal("Organization: ",
				hp->h_organization);
	}
	if (!subjfirst)
		GRAB_SUBJECT

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

	return errs;
}

/*
 * Read a line from tty; to be called from elsewhere
 */

char *
readtty(char const *prefix, char const *string)
{
	char *ret = NULL;
	struct termios ttybuf;
	sighandler_type saveint = SIG_DFL;
#ifndef TIOCSTI
	sighandler_type savequit;
#endif
	sighandler_type savetstp;
	sighandler_type savettou;
	sighandler_type savettin;

	/* If STDIN is not a terminal, simply read from it */
	if (! is_a_tty[0]) {
		char *line = NULL;
		size_t linesize = 0;
		if (readline_restart(stdin, &line, &linesize, 0) > 0)
			ret = savestr(line);
		if (line != NULL)
			free(line);
		goto jleave;
	}

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
	if (sigsetjmp(intjmp, 1)) {
		/* avoid garbled output with C-c */
		printf("\n");
		fflush(stdout);
		goto out2;
	}
	saveint = safe_signal(SIGINT, ttyint);
#endif
	TTYSET_CHECK(string)
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
jleave:
	return ret;
}

#ifdef HAVE_READLINE
void
tty_init(void)
{
	char *v;

	rl_readline_name = UNCONST(uagent);
	using_history();
	stifle_history(HIST_SIZE);
	rl_read_init_file(NULL);

	if ((v = _cl_histfile()) != NULL)
		read_history(v);

	_sigcont_save = safe_signal(SIGCONT, &tty_signal);
}

void
tty_destroy(void)
{
	char *v;

# ifdef WANT_ASSERTS
	safe_signal(SIGCONT, _sigcont_save);
# endif
	if ((v = _cl_histfile()) != NULL)
		write_history(v);
}

void
tty_signal(int sig)
{
	switch (sig) {
	case SIGCONT:
		if (_handle_cont)
			rl_forced_update_display();
		break;
#ifdef SIGWINCH
	case SIGWINCH:
		break;
#endif
	default:
		break;
	}
}

int
(tty_readline)(char const *prompt, char **linebuf, size_t *linesize, size_t n
	SMALLOC_DEBUG_ARGS)
{
	int nn;
	char *line;

	if (n > 0) {
		_rl_buf = *linebuf;
		_rl_buflen = (int)n;
		rl_pre_input_hook = &_rl_pre_input;
	}
	_handle_cont = TRU1;
	line = readline(prompt);
	_handle_cont = FAL0;
	if (line == NULL) {
		nn = -1;
		goto jleave;
	}
	n = strlen(line);

	if (n >= *linesize)
		*linebuf = (srealloc)(*linebuf,
				(*linesize = LINESIZE + n + 1)
				SMALLOC_DEBUG_ARGSCALL);
	memcpy(*linebuf, line, n);
	(free)(line); /* Grrrr.. but readline(3) can't be helped */
	(*linebuf)[n] = '\0';
	nn = (int)n;
jleave:
	return nn;
}

void
tty_addhist(char const *s)
{
	add_history(s);
}

#elif defined HAVE_EDITLINE /* HAVE_READLINE */
void
tty_init(void)
{
	HistEvent he;
	char *v;

	_el_hcom = history_init();
	history(_el_hcom, &he, H_SETSIZE, HIST_SIZE);
	history(_el_hcom, &he, H_SETUNIQUE, 1);

	_el_el = el_init(uagent, stdin, stdout, stderr);
	el_set(_el_el, EL_SIGNAL, 1);
	el_set(_el_el, EL_TERMINAL, NULL);
	/* Need to set HIST before EDITOR, otherwise it won't work automatic */
	el_set(_el_el, EL_HIST, &history, _el_hcom);
	el_set(_el_el, EL_EDITOR, "emacs");
	el_set(_el_el, EL_PROMPT, &_el_getprompt);
#if 0
	el_set(_el_el, EL_ADDFN, "tab_complete",
		"editline(3) internal completion function", &_el_file_cpl);
	el_set(_el_el, EL_BIND, "^I", "tab_complete", NULL);
#endif
	el_set(_el_el, EL_BIND, "^R", "ed-search-prev-history", NULL);
	el_source(_el_el, NULL); /* Source ~/.editrc */

	if ((v = _cl_histfile()) != NULL)
		history(_el_hcom, &he, H_LOAD, v);

	_sigcont_save = safe_signal(SIGCONT, &tty_signal);
}

void
tty_destroy(void)
{
	HistEvent he;
	char *v;

# ifdef WANT_ASSERTS
	safe_signal(SIGCONT, _sigcont_save);
# endif
	el_end(_el_el);

	if ((v = _cl_histfile()) != NULL)
		history(_el_hcom, &he, H_SAVE, v);
	history_end(_el_hcom);
}

void
tty_signal(int sig)
{
	switch (sig) {
	case SIGCONT:
		if (_handle_cont)
			el_set(_el_el, EL_REFRESH);
		break;
#ifdef SIGWINCH
	case SIGWINCH:
		el_resize(_el_el);
		break;
#endif
	default:
		break;
	}
}

int
(tty_readline)(char const *prompt, char **linebuf, size_t *linesize, size_t n
	SMALLOC_DEBUG_ARGS)
{
	int nn;
	char const *line;

	_el_prompt = prompt;
	if (n > 0)
		el_push(_el_el, *linebuf);
	_handle_cont = TRU1;
	line = el_gets(_el_el, &nn);
	_handle_cont = FAL0;
	if (line == NULL) {
		nn = -1;
		goto jleave;
	}
	assert(nn >= 0);
	n = (size_t)nn;
	if (n > 0 && line[n - 1] == '\n')
		nn = (int)--n;

	if (n >= *linesize)
		*linebuf = (srealloc)(*linebuf,
				(*linesize = LINESIZE + n + 1)
				SMALLOC_DEBUG_ARGSCALL);
	memcpy(*linebuf, line, n);
	(*linebuf)[n] = '\0';
jleave:
	return nn;
}

void
tty_addhist(char const *s)
{
	/* Enlarge meaning of unique .. to something that rocks;
	 * xxx unfortunately this is expensive to do with editline(3) */
	HistEvent he;
	int i;

	if (history(_el_hcom, &he, H_GETUNIQUE) < 0 || he.num == 0)
		goto jadd;

	for (i = history(_el_hcom, &he, H_FIRST); i >= 0;
			i = history(_el_hcom, &he, H_NEXT))
		if (strcmp(he.str, s) == 0) {
			history(_el_hcom, &he, H_DEL, he.num);
			break;
		}
jadd:
	history(_el_hcom, &he, H_ENTER, s);
}

#else /* HAVE_EDITLINE */
/* TODO steal hetio stuff from NetBSD ash / dash */
#endif /* ! HAVE_READLINE && ! HAVE_EDITLINE */

bool_t
yorn(char const *msg)
{
	char *cp;

	if (! (options & OPT_INTERACTIVE))
		return TRU1;
	do if ((cp = readtty(msg, NULL)) == NULL)
		return FAL0;
	while (*cp != 'y' && *cp != 'Y' && *cp != 'n' && *cp != 'N');
	return (*cp == 'y' || *cp == 'Y');
}

char *
getuser(char const *query)
{
	char *user = NULL;

	if (query == NULL)
		query = tr(509, "User: ");

	if (readline_input(query, &termios_state.ts_linebuf,
			&termios_state.ts_linesize) >= 0)
		user = termios_state.ts_linebuf;
	termios_state_reset();
	return user;
}

char *
getpassword(char const *query) /* FIXME encaps ttystate signal safe */
{
	struct termios tios;
	char *pass = NULL;

	if (query == NULL)
		query = tr(510, "Password: ");
	fputs(query, stdout);
	fflush(stdout);

	if (options & OPT_TTYIN) {
		tcgetattr(0, &termios_state.ts_tios);
		memcpy(&tios, &termios_state.ts_tios, sizeof tios);
		termios_state.ts_needs_reset = TRU1;
		tios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
		tcsetattr(0, TCSAFLUSH, &tios);
	}

	if (readline_restart(stdin, &termios_state.ts_linebuf,
			&termios_state.ts_linesize, 0) >= 0)
		pass = termios_state.ts_linebuf;
	termios_state_reset();

	if (options & OPT_TTYIN)
		fputc('\n', stdout);
	return pass;
}

bool_t
getcredentials(char **user, char **pass)
{
	bool_t rv = TRU1;
	char *u = *user, *p = *pass;

	if (u == NULL) {
		if ((u = getuser(NULL)) == NULL)
			rv = FAL0;
		else if (p == NULL)
			u = savestr(u);
		*user = u;
	}

	if (p == NULL) {
		if ((p = getpassword(NULL)) == NULL)
			rv = FAL0;
		*pass = p;
	}
	return rv;
}
