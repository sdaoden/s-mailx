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

static struct name *
grabaddrs(const char *field, struct name *np, int comma, enum gfield gflags)
{
	struct name *nq;

jloop:
	np = lextract(readstr_input(field, detract(np, comma)), gflags);
	for (nq = np; nq != NULL; nq = nq->n_flink)
		if (is_addr_invalid(nq, 1))
			goto jloop;
	return np;
}

int
grabh(struct header *hp, enum gfield gflags, int subjfirst)
{
	int errs;
	int volatile comma;

	errs = 0;
	comma = value("bsdcompat") || value("bsdmsgs") ? 0 : GCOMMA;

	if (gflags & GTO)
		hp->h_to = grabaddrs("To: ", hp->h_to, comma, GTO|GFULL);
	if (subjfirst && (gflags & GSUBJECT))
		hp->h_subject = readstr_input("Subject: ", hp->h_subject);
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
			hp->h_sender = extract(value("sender"), GEXTRA|GFULL);
		hp->h_sender = grabaddrs("Sender: ", hp->h_sender, comma,
				GEXTRA|GFULL);
		if (hp->h_organization == NULL)
			hp->h_organization = value("ORGANIZATION");
		hp->h_organization = readstr_input("Organization: ",
				hp->h_organization);
	}
	if (! subjfirst && (gflags & GSUBJECT))
		hp->h_subject = readstr_input("Subject: ", hp->h_subject);

	return errs;
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
	do if ((cp = readstr_input(msg, NULL)) == NULL)
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
