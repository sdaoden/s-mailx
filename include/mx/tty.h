/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MLE (Mailx Line Editor) and some more TTY stuff.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
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
#ifndef mx_TTY_H
#define mx_TTY_H

#include <mx/nail.h>

#include <mx/go.h>

#define mx_HEADER
#include <su/code-in.h>

EXPORT_DATA FILE *mx_tty_fp; /* Our terminal output TODO input channel */

/* Return whether user says yes, on STDIN if interactive.
 * Uses noninteract_default, the return value for non-interactive use cases,
 * as a hint for n_boolify() and chooses the yes/no string to append to prompt
 * accordingly.  If prompt is NIL "Continue" is used instead.
 * Handles+reraises SIGINT */
EXPORT boole mx_tty_yesorno(char const *prompt, boole noninteract_default);

#ifdef mx_HAVE_NET
/* Get a password the expected way, return autorec string on success or NIL */
EXPORT char *mx_tty_getuser(char const *query);

/* Get a password the expected way, return autorec string on success or NIL.
 * SIGINT is temporarily blocked, *not* reraised */
EXPORT char *mx_tty_getpass(char const *query);
#endif

/* Via go_input_cp(); handles shell unquoting and returns the status of
 * n_shexp_unquote_one() (so TRU2 for empty input) */
EXPORT boole mx_tty_getfilename(struct n_string *store,
      BITENUM_IS(u32,mx_go_input_flags) gif, char const *prompt_or_nil,
      char const *init_content_or_nil);

/* Create the prompt and return its visual width in columns, which may be 0
 * if evaluation is disabled etc.  The data is placed in store.
 * xprompt is inspected only if prompt is enabled and no *prompt* evaluation
 * takes place */
EXPORT u32 mx_tty_create_prompt(struct n_string *store, char const *xprompt,
      BITENUM_IS(u32,mx_go_input_flags) gif);

/* MLE */

/* Overall interactive terminal life cycle for the MLE */
#ifdef mx_HAVE_MLE
EXPORT void mx_tty_init(void);
EXPORT void mx_tty_destroy(boole xit_fastpath);
#else
# define mx_tty_init() do{;}while(0)
# define mx_tty_destroy(B) do{;}while(0)
#endif

/* Read a line after printing prompt (if set and non-empty).
 * If n>0 assumes that *linebuf has n bytes of default content.
 * histok_or_nil like for go_input().
 * Only the _CTX_ bits in lif are used */
EXPORT int mx_tty_readline(BITENUM_IS(u32,mx_go_input_flags) gif,
      char const *prompt, char **linebuf, uz *linesize, uz n,
      boole *histok_or_nil  su_DVL_LOC_ARGS_DECL);
#ifdef su_HAVE_DVL_LOC_ARGS
# define mx_tty_readline(A,B,C,D,E,F) \
   (mx_tty_readline)(A, B, C, D, E, F  su_DVL_LOC_ARGS_INJ)
#endif

/* Add a line (most likely as returned by tty_readline()) to the history.
 * Whether and how an entry is added for real depends on gif, e.g.,
 * GO_INPUT_HIST_GABBY / *history-gabby* relation.
 * Empty strings are never stored */
EXPORT void mx_tty_addhist(char const *s,
      BITENUM_IS(u32,mx_go_input_flags) gif);

#ifdef mx_HAVE_HISTORY
EXPORT int c_history(void *vp);
#endif

/* `bind' and `unbind' */
#ifdef mx_HAVE_KEY_BINDINGS
EXPORT int c_bind(void *vp);
EXPORT int c_unbind(void *vp);
#endif

#include <su/code-ou.h>
#endif /* mx_TTY_H */
/* s-it-mode */
