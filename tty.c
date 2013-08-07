/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ TTY interaction.
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
/* The NCL version is
 *
 * Copyright (c) 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "rcv.h"

#include <errno.h>
#include <unistd.h>

#ifdef HAVE_READLINE
# include <readline/history.h>
# include <readline/readline.h>
#elif defined HAVE_EDITLINE
# include <histedit.h>
#elif defined HAVE_LINE_EDITOR
# define __NCL
# include <sys/stat.h>
# include <limits.h>
# include <wchar.h>
# include <wctype.h>
#endif

#include "extern.h"

/* Only used for OPT_INTERACTIVE, so simply use STDIN_FILENO */
#ifndef STDIN_FILENO
# define STDIN_FILENO	0
#endif

/* */
#define _CL_HISTFILE(S)	\
do {\
	S = voption("NAIL_HISTFILE");\
	if ((S) != NULL)\
		S = fexpand(S, FEXP_LOCAL);\
} while (0)

/* */
#define _CL_HISTSIZE(V) \
do {\
	char const *__sv = voption("NAIL_HISTSIZE");\
	long __rv;\
	if (__sv == NULL || *__sv == '\0' ||\
			(__rv = strtol(__sv, NULL, 10)) == 0)\
		(V) = HIST_SIZE;\
	else if (__rv < 0)\
		(V) = 0;\
	else\
		(V) = __rv;\
} while (0)

/* */
#define _CL_CHECK_ADDHIST(S,NOACT) \
do {\
	switch (*(S)) {\
	case '\0':\
	case ' ':\
		NOACT;\
		/* FALLTHRU */\
	default:\
		break;\
	}\
} while (0)

/* fexpand() flags for expand-on-tab */
#define _CL_TAB_FEXP_FL	(FEXP_FULL | FEXP_SILENT | FEXP_MULTIOK)

/*
 * Because we have multiple identical implementations, change file layout a bit
 * and place the implementations one after the other below the other externals
 */

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

	if (readline_input(LNED_NONE, query, &termios_state.ts_linebuf,
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

/*
 * readline(3)
 */

#ifdef HAVE_READLINE
static char *	_rl_buf;	/* for pre_input() hook: initial line */
static int	_rl_buflen;	/* content, and its length */

static int	_rl_pre_input(void);

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

void
tty_init(void)
{
	long hs;
	char *v;

	_CL_HISTSIZE(hs);

	rl_readline_name = UNCONST(uagent);
	using_history();
	stifle_history((int)hs);
	rl_read_init_file(NULL);

	/* Because rl_read_init_file() may have introduced yet a different
	 * history size limit, simply load and incorporate the history, leave
	 * it up to readline(3) to do the rest */
	_CL_HISTFILE(v);
	if (v != NULL)
		read_history(v);
}

void
tty_destroy(void)
{
	char *v;

	_CL_HISTFILE(v);
	if (v != NULL)
		write_history(v);
}

void
tty_signal(int sig)
{
	switch (sig) {
# ifdef SIGWINCH
	case SIGWINCH:
		break;
# endif
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
	line = readline(prompt);

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
	(free)(line);
	(*linebuf)[n] = '\0';
	nn = (int)n;
jleave:
	return nn;
}

void
tty_addhist(char const *s)
{
	_CL_CHECK_ADDHIST(s, goto jleave);
	add_history(s);
jleave:	;
}
#endif /* HAVE_READLINE */

/*
 * BSD editline(3)
 */

#ifdef HAVE_EDITLINE
static EditLine *	_el_el;		/* editline(3) handle */
static History *	_el_hcom;	/* History handle for commline */
static char const *	_el_prompt;	/* Current prompt */

static char const *	_el_getprompt(void);

static char const *
_el_getprompt(void)
{
	return _el_prompt;
}

void
tty_init(void)
{
	HistEvent he;
	long hs;
	char *v;

	_CL_HISTSIZE(hs);

	_el_hcom = history_init();
	history(_el_hcom, &he, H_SETSIZE, (int)hs);
	history(_el_hcom, &he, H_SETUNIQUE, 1);

	_el_el = el_init(uagent, stdin, stdout, stderr);
	el_set(_el_el, EL_SIGNAL, 1);
	el_set(_el_el, EL_TERMINAL, NULL);
	/* Need to set HIST before EDITOR, otherwise it won't work automatic */
	el_set(_el_el, EL_HIST, &history, _el_hcom);
	el_set(_el_el, EL_EDITOR, "emacs");
	el_set(_el_el, EL_PROMPT, &_el_getprompt);
# if 0
	el_set(_el_el, EL_ADDFN, "tab_complete",
		"editline(3) internal completion function", &_el_file_cpl);
	el_set(_el_el, EL_BIND, "^I", "tab_complete", NULL);
# endif
	el_set(_el_el, EL_BIND, "^R", "ed-search-prev-history", NULL);
	el_source(_el_el, NULL); /* Source ~/.editrc */

	/* Because el_source() may have introduced yet a different history size
	 * limit, simply load and incorporate the history, leave it up to
	 * editline(3) to do the rest */
	_CL_HISTFILE(v);
	if (v != NULL)
		history(_el_hcom, &he, H_LOAD, v);
}

void
tty_destroy(void)
{
	HistEvent he;
	char *v;

	el_end(_el_el);

	_CL_HISTFILE(v);
	if (v != NULL)
		history(_el_hcom, &he, H_SAVE, v);
	history_end(_el_hcom);
}

void
tty_signal(int sig)
{
	switch (sig) {
# ifdef SIGWINCH
	case SIGWINCH:
		el_resize(_el_el);
		break;
# endif
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
	line = el_gets(_el_el, &nn);

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
	 * xxx unfortunately this is expensive to do with editline(3)
	 * xxx maybe it would be better to hook the ptfs instead? */
	HistEvent he;
	int i;

	_CL_CHECK_ADDHIST(s, goto jleave);

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
jleave:	;
}
#endif /* HAVE_EDITLINE */

/*
 * NCL: our homebrew version (inspired from NetBSD sh(1) / dash(1)s hetio.c).
 *
 * We do not handle character widths because the terminal must deal with that
 * anyway on the one hand, and also wcwidth(3) doesn't support zero-width
 * characters by definition on the other.  We're addicted.
 *
 * To avoid memory leaks etc. with the current codebase that simply longjmp(3)s
 * we're forced to use the very same buffer--the one that is passed through to
 * us from the outside--to store anything we need, i.e., a `struct cell[]', and
 * convert that on-the-fly back to the plain char* result once we're done.
 * To simplify our live, use savestr() buffers for all other needed memory */

#ifdef __NCL
union xsighdl {
	sighandler_type	shdl;		/* Try avoid races by setting */
	sl_it		sint;		/* .sint=-1 when inactive */
};
CTA(sizeof(sl_it) >= sizeof(sighandler_type));

struct xtios {
	struct termios	told;
	struct termios	tnew;
};

struct cell {
	wchar_t	wc;
	ui_it	count;
	char	cbuf[MB_LEN_MAX * 2];	/* .. plus reset shift sequence */
};

struct hist {
	struct hist *	older;
	struct hist *	younger;
	size_t		len;
	char		dat[VFIELD_SIZE(sizeof(size_t))];
};

struct line {
	size_t		cursor;		/* Current cursor position */
	size_t		topins;		/* Outermost cursor col set */
	union {
		char *		cbuf;	/* *x_buf */
		struct cell *	cells;
	}		line;
	struct str	defc;		/* Current default content */
	struct str	savec;		/* Saved default content */
	struct hist *	hist;		/* History cursor */
	char const *	prompt;
	char **		x_buf;		/* Caller pointers */
	size_t *	x_bufsize;
};

static union xsighdl	_ncl_oint;
static union xsighdl	_ncl_oquit;
static union xsighdl	_ncl_oterm;
static union xsighdl	_ncl_ohup;
static union xsighdl	_ncl_otstp;
static union xsighdl	_ncl_ottin;
static union xsighdl	_ncl_ottou;
static struct xtios	_ncl_tios;
static struct hist *	_ncl_hist;
static bool_t		_ncl_hist_load;

static void	_ncl_sigs_up(void);
static void	_ncl_sigs_down(void);

static void	_ncl_check_grow(struct line *l SMALLOC_DEBUG_ARGS);
static void	_ncl_bs_eof_dvup(struct cell *cap, size_t i);
static ssize_t 	_ncl_wboundary(struct line *l, ssize_t dir);
static ssize_t	_ncl_cell2dat(struct line *l);
static void	_ncl_cell2save(struct line *l);

static void	_ncl_khome(struct line *l);
static void	_ncl_kend(struct line *l);
static void	_ncl_kbs(struct line *l);
static void	_ncl_kkill(struct line *l);
static ssize_t	_ncl_keof(struct line *l);
static void	_ncl_kleft(struct line *l);
static void	_ncl_kright(struct line *l);
static void	_ncl_krefresh(struct line *l);
static size_t	_ncl_kht(struct line *l);
static size_t	__ncl_khist_shared(struct line *l, struct hist *hp);
static size_t	_ncl_khist(struct line *l, bool_t backwd);
static size_t	_ncl_krhist(struct line *l);
static void	_ncl_kbwddelw(struct line *l);
static void	_ncl_kgow(struct line *l, ssize_t dir);
static void	_ncl_kother(struct line *l, wchar_t wc);
static ssize_t	_ncl_readline(char const *prompt, char **buf, size_t *bufsize,
			size_t len SMALLOC_DEBUG_ARGS);

static void
_ncl_sigs_up(void)
{
	if (_ncl_oint.sint == -1)
		_ncl_oint.shdl = safe_signal(SIGINT, &tty_signal);
	if (_ncl_oquit.sint == -1)
		_ncl_oquit.shdl = safe_signal(SIGQUIT, &tty_signal);
	if (_ncl_oterm.sint == -1)
		_ncl_oterm.shdl = safe_signal(SIGTERM, &tty_signal);
	if (_ncl_ohup.sint == -1)
		_ncl_ohup.shdl = safe_signal(SIGHUP, &tty_signal);
	if (_ncl_otstp.sint == -1)
		_ncl_otstp.shdl = safe_signal(SIGTSTP, &tty_signal);
	if (_ncl_ottin.sint == -1)
		_ncl_ottin.shdl = safe_signal(SIGTTIN, &tty_signal);
	if (_ncl_ottou.sint == -1)
		_ncl_ottou.shdl  = safe_signal(SIGTTOU, &tty_signal);
}

static void
_ncl_sigs_down(void)
{
	/* aaah.. atomic cas would be nice (but isn't it all redundant?) */
	sighandler_type st;

	if (_ncl_ottou.sint != -1) {
		st = _ncl_ottou.shdl, _ncl_ottou.sint = -1;
		safe_signal(SIGTTOU, st);
	}
	if (_ncl_ottin.sint != -1) {
		st = _ncl_ottin.shdl, _ncl_ottin.sint = -1;
		safe_signal(SIGTTIN, st);
	}
	if (_ncl_otstp.sint != -1) {
		st = _ncl_otstp.shdl, _ncl_otstp.sint = -1;
		safe_signal(SIGTSTP, st);
	}
	if (_ncl_ohup.sint != -1) {
		st = _ncl_ohup.shdl, _ncl_ohup.sint = -1;
		safe_signal(SIGHUP, st);
	}
	if (_ncl_oterm.sint != -1) {
		st = _ncl_oterm.shdl, _ncl_oterm.sint = -1;
		safe_signal(SIGTERM, st);
	}
	if (_ncl_oquit.sint != -1) {
		st = _ncl_oquit.shdl, _ncl_oquit.sint = -1;
		safe_signal(SIGQUIT, st);
	}
	if (_ncl_oint.sint != -1) {
		st = _ncl_oint.shdl, _ncl_oint.sint = -1;
		safe_signal(SIGINT, st);
	}
}

static void
_ncl_check_grow(struct line *l SMALLOC_DEBUG_ARGS)
{
	size_t i = l->topins * sizeof(struct cell) + 2 * sizeof(struct cell);

	if (i >= *l->x_bufsize) {
		i <<= 1;
		*l->x_bufsize = i;
		l->line.cbuf =
		*l->x_buf = (srealloc)(*l->x_buf, i SMALLOC_DEBUG_ARGSCALL);
	}
}

static void
_ncl_bs_eof_dvup(struct cell *cap, size_t i)
{
	size_t j;

	if (i > 0)
		memmove(cap, cap + 1, i * sizeof(*cap));

	/* And.. the (rest of the) visual update */
	for (j = 0; j < i; ++j)
		fwrite(cap[j].cbuf, sizeof *cap->cbuf, cap[j].count, stdout);
	fputs(" \b", stdout);
	for (j = 0; j < i; ++j)
		putchar('\b');
}

static ssize_t
_ncl_wboundary(struct line *l, ssize_t dir)
{
	size_t c = l->cursor, t = l->topins;
	ssize_t i;
	struct cell *cap;
	bool_t anynon;

	i = -1;
	if (dir < 0) {
		if (c == 0)
			goto jleave;
	} else if (c == t)
		goto jleave;
	else
		--t, --c; /* Unsigned wrapping may occur (twice), then */

	for (i = 0, cap = l->line.cells, anynon = FAL0;;) {
		wchar_t wc = cap[c + dir].wc;
		if (iswblank(wc) || iswpunct(wc)) {
			if (anynon)
				break;
		} else
			anynon = TRU1;
		++i;
		c += dir;
		if (dir < 0) {
			if (c == 0)
				break;
		} else if (c == t)
			break;
	}
jleave:
	return i;
}

static ssize_t
_ncl_cell2dat(struct line *l)
{
	size_t len = 0, i;

	if (l->topins > 0)
		for (i = 0; i < l->topins; ++i) {
			struct cell *cap = l->line.cells + i;
			memcpy(l->line.cbuf + len, cap->cbuf, cap->count);
			len += cap->count;
		}
	l->line.cbuf[len] = '\0';
	return (ssize_t)len;
}

static void
_ncl_cell2save(struct line *l)
{
	size_t len, i;
	struct cell *cap;

	l->savec.s = NULL, l->savec.l = 0;
	if (l->topins == 0)
		goto jleave;

	for (cap = l->line.cells, len = i = 0; i < l->topins; ++cap, ++i)
		len += cap->count;

	l->savec.l = len;
	l->savec.s = salloc(len + 1);

	for (cap = l->line.cells, len = i = 0; i < l->topins; ++cap, ++i) {
		memcpy(l->savec.s + len, cap->cbuf, cap->count);
		len += cap->count;
	}
	l->savec.s[len] = '\0';
jleave:	;
}

static void
_ncl_khome(struct line *l)
{
	size_t c = l->cursor;

	if (c > 0) {
		l->cursor = 0;
		while (c-- != 0)
			putchar('\b');
	} else
		putchar('\a');
}

static void
_ncl_kend(struct line *l) /* XXX optionally repl. esc-seq with full repaint?! */
{
	ssize_t i = (ssize_t)(l->topins - l->cursor);

	if (i > 0) {
		l->cursor = l->topins;
		while (i-- != 0)
			fputs("\033[C", stdout);
	} else
		putchar('\a');
}

static void
_ncl_kbs(struct line *l)
{
	ssize_t c = l->cursor, t = l->topins;

	if (c > 0) {
		putchar('\b');
		l->cursor = --c;
		l->topins = --t;
		t -= c;
		_ncl_bs_eof_dvup(l->line.cells + c, t);
	} else
		putchar('\a');
}

static void
_ncl_kkill(struct line *l)
{
	size_t j, c = l->cursor, i = (size_t)(l->topins - c);

	if (i > 0) {
		l->topins = c;
		for (j = i; j != 0; --j)
			putchar(' ');
		for (j = i; j != 0; --j)
			putchar('\b');
	} else
		putchar('\a');
}

static ssize_t
_ncl_keof(struct line *l)
{
	size_t c = l->cursor, t = l->topins;
	ssize_t i = (ssize_t)(t - c);

	if (i > 0) {
		l->topins = --t;
		_ncl_bs_eof_dvup(l->line.cells + c, --i);
	} else if (t == 0 && ! boption("ignoreeof")) {
		fputs("^D", stdout);
		fflush(stdout);
		i = -1;
	} else {
		putchar('\a');
		i = 0;
	}
	return i;
}

static void
_ncl_kleft(struct line *l)
{
	if (l->cursor > 0) {
		--l->cursor;
		putchar('\b');
	} else
		putchar('\a');
}

static void
_ncl_kright(struct line *l)
{
	if (l->cursor < l->topins) {
		++l->cursor;
		fputs("\033[C", stdout);
	} else
		putchar('\a');
}

static void
_ncl_krefresh(struct line *l)
{
	struct cell *cap;
	size_t i;

	putchar('\r');
	if (*l->prompt)
		fputs(l->prompt, stdout);
	for (cap = l->line.cells, i = l->topins; i > 0; ++cap, --i)
		fwrite(cap->cbuf, sizeof *cap->cbuf, cap->count, stdout);
	for (i = l->topins - l->cursor; i > 0; --i)
		putchar('\b');
}

static size_t
_ncl_kht(struct line *l)
{
	struct str orig, bot, top, sub, exp;
	bool_t set_savec = FAL0;
	size_t rv = 0;

	/* We cannot expand an empty line */
	if (l->topins == 0)
		goto jleave;

	/* Get plain line data; if this is the first expansion/xy, update the
	 * very original content so that ^G gets the origin back */
	orig = l->savec;
	_ncl_cell2save(l);
	bot = l->savec;
	if (orig.s != NULL)
		l->savec = orig;
	else
		set_savec = TRU1;

	/* Narrow down the sections of the line as necessary */
	if (l->cursor != l->topins) {
		struct cell *cap = l->line.cells;

		for (top = bot, rv = l->cursor; rv != 0; ++cap, --rv) {
			top.s += cap->count;
			top.l -= cap->count;
		}
		bot.l -= top.l;
	} else
		top.s = NULL, top.l = 0;

	/* We're not interested in the entire section, only in the last "word";
	 * this means we're in trouble, since we'd need to convert to wide to
	 * be able to perform proper classification!  However, be massively
	 * simple-minded instead and look out for ASCII SP U+0020, which should
	 * always work, too (otherwise the entire codebase won't work anyway) */
	for (sub = bot, rv = sub.l; rv != 0; --rv) {
		if (rv == 1)
			--rv;
		else if (sub.s[rv - 1] != ' ')
			continue;
		sub.s += rv;
		sub.l -= rv;
		bot.l -= sub.l;

		/* TODO there is a TODO note upon fexpand() with multi-return;
		 * TODO if that will change, the if() below can be simplified */
		sub.s = savestrbuf(sub.s, sub.l);
		/* Super-Heavy-Metal: block all sigs, avoid leaks on jump */
		hold_all_sigs();
		exp.s = fexpand(sub.s, _CL_TAB_FEXP_FL);
		rele_all_sigs();
		if (exp.s != NULL && (exp.l = strlen(exp.s)) > 0 &&
				(exp.l != sub.l || strcmp(exp.s, sub.s))) {
			orig.l = bot.l + exp.l + top.l;
			orig.s = salloc(orig.l + 1);
			memcpy(orig.s, bot.s, (rv = bot.l));
			memcpy(orig.s + rv, exp.s, exp.l);
			rv += exp.l;
			memcpy(orig.s + rv, top.s, top.l);
			rv += top.l;
			orig.s[rv] = '\0';

			l->defc = orig;
			_ncl_khome(l);
			_ncl_kkill(l);
			goto jleave;
		}
		break;
	}

	/* If we've provided a default content, but failed to expand, there is
	 * nothing we can "revert to": drop that default again */
	if (set_savec)
		l->savec.s = NULL, l->savec.l = 0;
	rv = 0;
jleave:
	return rv;
}

static size_t
__ncl_khist_shared(struct line *l, struct hist *hp)
{
	size_t rv;

	if ((l->hist = hp) != NULL) {
		l->defc.s = savestrbuf(hp->dat, hp->len);
		rv =
		l->defc.l = hp->len;
		if (l->topins > 0) {
			_ncl_khome(l);
			_ncl_kkill(l);
		}
	} else {
		putchar('\a');
		rv = 0;
	}
	return rv;
}

static size_t
_ncl_khist(struct line *l, bool_t backwd)
{
	struct hist *hp;

	/* If we're not in history mode yet, save line content;
	 * also, disallow forward search, then, and, of course, bail unless we
	 * do have any history at all */
	if ((hp = l->hist) == NULL) {
		if (! backwd)
			goto jleave;
		if ((hp = _ncl_hist) == NULL)
			goto jleave;
		_ncl_cell2save(l);
		goto jleave;
	}

	hp = backwd ? hp->older : hp->younger;
jleave:
	return __ncl_khist_shared(l, hp);
}

static size_t
_ncl_krhist(struct line *l)
{
	struct hist *hp = NULL;

	/* We cannot complete an empty line */
	if (l->topins == 0)
		goto jleave;
	if ((hp = l->hist) == NULL) {
		if ((hp = _ncl_hist) == NULL)
			goto jleave;
		_ncl_cell2save(l);
	} else
		goto jumpin;

	while (hp != NULL) {
		if (is_prefix(l->savec.s, hp->dat))
			break;
jumpin:
		hp = hp->older;
	}
jleave:
	return __ncl_khist_shared(l, hp);
}

static void
_ncl_kbwddelw(struct line *l)
{
	ssize_t i;
	size_t c = l->cursor, t, j;
	struct cell *cap;

	i = _ncl_wboundary(l, -1);
	if (i <= 0) {
		if (i < 0)
			putchar('\a');
		goto jleave;
	}

	c = l->cursor - i;
	t = l->topins;
	l->topins = t - i;
	l->cursor = c;
	cap = l->line.cells + c;

	if (t != l->cursor) {
		j = t - c + i;
		memmove(cap, cap + i, j * sizeof(*cap));
	}

	for (j = i; j > 0; --j)
		putchar('\b');
	for (j = l->topins - c; j > 0; ++cap, --j)
		fwrite(cap[0].cbuf, sizeof *cap->cbuf, cap[0].count, stdout);
	for (j = i; j > 0; --j)
		putchar(' ');
	for (j = t - c; j > 0; --j)
		putchar('\b');
jleave:	;
}

static void
_ncl_kgow(struct line *l, ssize_t dir)
{
	ssize_t i = _ncl_wboundary(l, dir);
	if (i <= 0) {
		if (i < 0)
			putchar('\a');
		goto jleave;
	}

	if (dir < 0) {
		l->cursor -= i;
		while (i-- > 0)
			putchar('\b');
	} else {
		l->cursor += i;
		while (i-- > 0)
			fputs("\033[C", stdout);
	}
jleave:	;
}

static void
_ncl_kother(struct line *l, wchar_t wc)
{
	/* Append if at EOL, insert otherwise;
	 * since we may move around character-wise, always use a fresh ps */
	mbstate_t ps;
	struct cell cell, *cap;
	size_t i, c;

	/* First init a cell and see wether we'll really handle this wc */
	cell.wc = wc;
	memset(&ps, 0, sizeof ps);
	i = wcrtomb(cell.cbuf, wc, &ps);
	if (i > MB_LEN_MAX)
		goto jleave;
	cell.count = (ui_it)i;
	if (enc_has_state) {
		i = wcrtomb(cell.cbuf + i, L'\0', &ps);
		if (i == 1)
			;
		else if (--i < MB_LEN_MAX)
			cell.count += (ui_it)i;
		else
			goto jleave;
	}

	/* Yes, we will!  Place it in the array */
	c = l->cursor++;
	i = l->topins++ - c;
	cap = l->line.cells + c;
	if (i > 0)
		memmove(cap + 1, cap, i * sizeof(cell));
	memcpy(cap, &cell, sizeof cell);

	/* And update visual */
	c = i;
	do
		fwrite(cap->cbuf, sizeof *cap->cbuf, cap->count, stdout);
	while ((++cap, i-- != 0));
	while (c-- != 0)
		putchar('\b');
jleave:	;
}

static ssize_t
_ncl_readline(char const *prompt, char **buf, size_t *bufsize, size_t len
	SMALLOC_DEBUG_ARGS)
{
	/* We want to save code, yet we may have to incorporate a lines'
	 * default content and / or default input to switch back to after some
	 * history movement; let "len > 0" mean "have to display some data
	 * buffer", and only otherwise read(2) it */
	mbstate_t ps[2];
	struct line l;
	char cbuf_base[MB_LEN_MAX], *cbuf, *cbufp;
	wchar_t wc;
	ssize_t rv;

	memset(&l, 0, sizeof l);
	l.line.cbuf = *buf;
	if (len != 0) {
		l.defc.s = savestrbuf(*buf, len);
		l.defc.l = len;
	}
	l.prompt = prompt;
	l.x_buf = buf;
	l.x_bufsize = bufsize;

	if (*prompt) {
		fputs(prompt, stdout);
		fflush(stdout);
	}
jrestart:
	memset(ps, 0, sizeof ps);
	for (;;) {
		_ncl_check_grow(&l SMALLOC_DEBUG_ARGSCALL);

		/* Normal read(2)?  Else buffer-takeover */
		if (len == 0)
			cbuf = cbuf_base;
		else {
			assert(l.defc.l > 0 && l.defc.s != NULL);
			cbuf = l.defc.s + (l.defc.l - len);
		}
		cbufp = cbuf;
		for (;;) {
			if (len != 0) {
				if (--len == 0)
					l.defc.s = NULL, l.defc.l = 0;
			} else if (cbuf != cbuf_base)
				goto jbell;
			else if ((rv = read(STDIN_FILENO, cbufp, 1)) < 1) {
				if (errno == EINTR) /* xxx #if !SA_RESTART ? */
					continue;
				goto jleave;
			}
			++cbufp;

			/* Ach! the ISO C multibyte handling!  Take care not to
			 * mess up our mbstate_t, it's undefined on error!
			 * Encodings with locking shift states cannot really be
			 * helped, since it is impossible to only query the
			 * shift state, as opposed to the entire shift state
			 * + character pair (via ISO C functions), so, after
			 * cursor movement, we cannot restore only the shift
			 * state but have to keep on going with the last active
			 * one.  XXX Maybe it would be better to simply force
			 * XXX a complete mbstate_t reset after *any* error?
			 * XXX Does anything else really make sense? */
			rv = mbrtowc(&wc, cbuf, (size_t)(cbufp - cbuf), ps + 0);
			if (rv <= 0) {
				/* If it's a hard error, or if too many
				 * redundant shift sequences overflow our
				 * buffer, simply perform a reset.
				 * It is anyway suboptimal, but in the worst
				 * case the user could ^U */
				if (rv == -1 || MB_LEN_MAX ==
						(size_t)(cbufp - cbuf))
					cbufp = cbuf;
				ps[0] = ps[1];
				continue;
			}
			ps[1] = ps[0];
			break;
		}

		/* Don't interpret control bytes during buffer take-over */
		if (cbuf != cbuf_base)
			goto jprint;
		switch (wc) {
		case 'A' ^ 0x40: /* cursor home */
			_ncl_khome(&l);
			break;
		case 'B' ^ 0x40: /* ("history backward") */
			if ((len = _ncl_khist(&l, TRU1)) > 0)
				goto jrestart;
			wc = 'G' ^ 0x40;
			goto jreset;
		/* 'C': interrupt (CTRL-C) */
		case 'D' ^ 0x40: /* delete char forward if any, else EOF */
			if ((rv = _ncl_keof(&l)) < 0)
				goto jleave;
			break;
		case 'E' ^ 0x40: /* end of line */
			_ncl_kend(&l);
			break;
		case 'F' ^ 0x40: /* history forward */
			if (l.hist == NULL)
				goto jbell;
			if ((len = _ncl_khist(&l, FAL0)) > 0)
				goto jrestart;
			wc = 'G' ^ 0x40;
			goto jreset;
		/* 'G' below */
		case 'H' ^ 0x40: /* backspace */
		case '\177':
			_ncl_kbs(&l);
			break;
		case 'I' ^ 0x40: /* horizontal tab */
			if ((len = _ncl_kht(&l)) > 0)
				goto jrestart;
			goto jbell;
		case 'J' ^ 0x40: /* NL (\n) */
			goto jdone;
		case 'G' ^ 0x40: /* full reset */
jreset:
			/* FALLTHRU */
		case 'U' ^ 0x40: /* ^U: ^A + ^K */
			_ncl_khome(&l);
			/* FALLTHRU */
		case 'K' ^ 0x40: /* kill from cursor to end of line */
			_ncl_kkill(&l);
			/* (Handle full reset?) */
			if (wc == ('G' ^ 0x40)) {
				l.hist = NULL;
				if ((len = l.savec.l) != 0) {
					l.defc = l.savec;
					l.savec.s = NULL, l.savec.l = 0;
				} else
					len = l.defc.l;
			}
			fflush(stdout);
			goto jrestart;
		case 'L' ^ 0x40: /* repaint line */
			_ncl_krefresh(&l);
			break;
		/* 'M': CR (\r) */
		/* 'N' */
		case 'O' ^ 0x40: /* cursor left */
			_ncl_kleft(&l);
			break;
		case 'P' ^ 0x40: /* cursor right */
			_ncl_kright(&l);
			break;
		/* 'Q': no code */
		case 'R' ^ 0x40: /* reverse history search */
			if ((len = _ncl_krhist(&l)) > 0)
				goto jrestart;
			wc = 'G' ^ 0x40;
			goto jreset;
		/* 'S': no code */
		/* 'U' above */
		/*case 'V' ^ 0x40: TODO*/ /* forward delete "word" */
		case 'W' ^ 0x40: /* backward delete "word" */
			_ncl_kbwddelw(&l);
			break;
		case 'X' ^ 0x40: /* move cursor forward "word" */
			_ncl_kgow(&l, +1);
			break;
		case 'Y' ^ 0x40: /* move cursor backward "word" */
			_ncl_kgow(&l, -1);
			break;
		/* 'Z': suspend (CTRL-Z) */
		default:
jprint:
			if (iswprint(wc)) {
				_ncl_kother(&l, wc);
				/* Don't clear the history during takeover..
				 * ..and also avoid fflush()ing unless we've
				 * worked the entire buffer */
				if (len > 0)
					continue;
				if (cbuf == cbuf_base)
					l.hist = NULL;
			} else {
jbell:
				putchar('\a');
			}
			break;
		}
		fflush(stdout);
	}

	/* We have a completed input line, convert the struct cell data to its
	 * plain character equivalent */
jdone:
	putchar('\n');
	fflush(stdout);
	len = _ncl_cell2dat(&l);
	rv = (ssize_t)len;
jleave:
	return rv;
}

void
tty_init(void)
{
	long hs;
	char *v, *lbuf;
	FILE *f;
	size_t lsize, count, llen;

	_ncl_oint.sint = _ncl_oquit.sint = _ncl_oterm.sint =
	_ncl_ohup.sint = _ncl_otstp.sint = _ncl_ottin.sint =
	_ncl_ottou.sint = -1;

	tcgetattr(STDIN_FILENO, &_ncl_tios.told);
	memcpy(&_ncl_tios.tnew, &_ncl_tios.told, sizeof _ncl_tios.tnew);
	_ncl_tios.tnew.c_cc[VMIN] = 1;
	_ncl_tios.tnew.c_cc[VTIME] = 0;
	_ncl_tios.tnew.c_lflag &= ~(ECHO | ICANON | IEXTEN);

	_CL_HISTSIZE(hs);
	if (hs == 0)
		goto jleave;

	_CL_HISTFILE(v);
	if (v == NULL)
		goto jleave;

	/* Much too Super-Heavy-Metal: block all sigs */
	hold_all_sigs();
	f = fopen(v, "r"); /* TODO HISTFILE LOAD: use linebuf pool */
	if (f == NULL)
		goto jdone;

	lbuf = NULL;
	lsize = 0;
	count = fsize(f);
	while (fgetline(&lbuf, &lsize, &count, &llen, f, FAL0) != NULL) {
		if (llen > 0 && lbuf[llen - 1] == '\n')
			lbuf[--llen] = '\0';
		if (llen == 0 || lbuf[0] == '#') /* xxx comments? noone! */
			continue;
		_ncl_hist_load = TRU1;
		tty_addhist(lbuf);
		_ncl_hist_load = FAL0;
	}
	if (lbuf != NULL)
		free(lbuf);

	fclose(f);
jdone:
	rele_all_sigs();
jleave:	;
}

void
tty_destroy(void)
{
	long hs;
	char *v;
	struct hist *hp;
	FILE *f;

	_CL_HISTSIZE(hs);
	if (hs == 0)
		goto jleave;

	_CL_HISTFILE(v);
	if (v == NULL)
		goto jleave;

	if ((hp = _ncl_hist) != NULL)
		while (hp->older != NULL && hs-- != 0)
			hp = hp->older;

	/* Much too Super-Heavy-Metal: block all sigs */
	hold_all_sigs();
	f = fopen(v, "w"); /* TODO temporary + rename?! */
	if (f == NULL)
		goto jdone;
	if (fchmod(fileno(f), S_IRUSR | S_IWUSR) != 0)
		goto jclose;

	for (; hp != NULL; hp = hp->younger) {
		fwrite(hp->dat, sizeof *hp->dat, hp->len, f);
		putc('\n', f);
	}
jclose:
	fclose(f);
jdone:
	rele_all_sigs();
jleave:	;
}

void
tty_signal(int sig)
{
	sigset_t nset, oset;

	switch (sig) {
	case SIGWINCH:
		/* We don't deal with SIGWINCH, yet get called from main.c */
		break;
	default:
		tcsetattr(STDIN_FILENO, TCSANOW, &_ncl_tios.told);
		_ncl_sigs_down();
		sigemptyset(&nset);
		sigaddset(&nset, sig);
		sigprocmask(SIG_UNBLOCK, &nset, &oset);
		kill(0, sig);
		/* When we come here we'll continue editing, so reestablish */
		sigprocmask(SIG_BLOCK, &oset, (sigset_t*)NULL);
		_ncl_sigs_up();
		tcsetattr(STDIN_FILENO, TCSANOW, &_ncl_tios.tnew);
		break;
	}
}

int
(tty_readline)(char const *prompt, char **linebuf, size_t *linesize, size_t n
	SMALLOC_DEBUG_ARGS)
{
	ssize_t nn;

	/* Of course we have races here, but they cannot be avoided on POSIX */
	_ncl_sigs_up();
	tcsetattr(STDIN_FILENO, TCSANOW, &_ncl_tios.tnew);
	nn = _ncl_readline(prompt, linebuf, linesize, n SMALLOC_DEBUG_ARGSCALL);
	tcsetattr(STDIN_FILENO, TCSANOW, &_ncl_tios.told);
	_ncl_sigs_down();

	return (int)nn;
}

void
tty_addhist(char const *s)
{
	/* Super-Heavy-Metal: block all sigs, avoid leaks+ on jump */
	size_t l = strlen(s);
	struct hist *h, *o, *y;

	_CL_CHECK_ADDHIST(s, goto j_leave);

	/* Eliminating duplicates is expensive, but simply inacceptable so
	 * during the load of a potentially large history file! */
	if (! _ncl_hist_load)
		for (h = _ncl_hist; h != NULL; h = h->older)
			if (h->len == l && strcmp(h->dat, s) == 0) {
				hold_all_sigs();
				o = h->older;
				y = h->younger;
				if (o != NULL) {
					if ((o->younger = y) == NULL)
						_ncl_hist = o;
				}
				if (y != NULL)
					y->older = o;
				else
					_ncl_hist = o;
				goto jleave;
			}
	hold_all_sigs();

	h = smalloc((sizeof(struct hist) - VFIELD_SIZEOF(struct hist, dat)) +
		l + 1);
	h->len = l;
	memcpy(h->dat, s, l + 1);
jleave:
	if ((h->older = _ncl_hist) != NULL)
		_ncl_hist->younger = h;
	h->younger = NULL;
	_ncl_hist = h;

	rele_all_sigs();
j_leave:;
}
#endif /* __NCL */

/*
 * The really-nothing-at-all implementation
 */

#ifndef HAVE_LINE_EDITOR
void
tty_init(void)
{}

void
tty_destroy(void)
{}

void
tty_signal(int sig)
{
	(void)sig;
}

int
(tty_readline)(char const *prompt, char **linebuf, size_t *linesize, size_t n
	SMALLOC_DEBUG_ARGS)
{
	/*
	 * TODO The nothing-at-all tty layer even forces re-entering all the
	 * TODO original data when re-editing a field
	 */
	bool_t doffl = FAL0;

	if (*prompt != '\0') {
		fputs(prompt, stdout);
		doffl = TRU1;
	}
	if (n > 0) {
		fprintf(stdout, tr(511, "{former content: %.*s} "),
			(int)n, *linebuf);
		n = 0;
		doffl = TRU1;
	}
	if (doffl)
		fflush(stdout);
	return (readline_restart)(stdin, linebuf, linesize, n
		SMALLOC_DEBUG_ARGSCALL);
}

void
tty_addhist(char const *s)
{
	(void)s;
}
#endif /* ! HAVE_LINE_EDITOR */
