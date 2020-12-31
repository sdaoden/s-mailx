/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Program input of all sorts, input lexing, event loops, command evaluation.
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
#ifndef mx_GO_H
#define mx_GO_H

#include <mx/nail.h>

#include <su/mem-bag.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_colour_env;

enum mx_go_input_flags{
   mx_GO_INPUT_NONE,
   mx_GO_INPUT_CTX_BASE = 0, /* Generic shared base: don't use! */
   mx_GO_INPUT_CTX_DEFAULT = 1, /* Default input */
   mx_GO_INPUT_CTX_COMPOSE = 2, /* Compose mode input */
   mx__GO_INPUT_CTX_MASK = 3,
   /* _MASK is not a valid index here, but the lower bits are not misused,
    * therefore -- to save space! -- indexing is performed via "& _MASK".
    * This is CTA()d!  For actual spacing of arrays we use _MAX1 instead */
   mx__GO_INPUT_CTX_MAX1 = mx_GO_INPUT_CTX_COMPOSE + 1,

   mx_GO_INPUT_HOLDALLSIGS = 1u<<8, /* sigs_all_hold() active TODO */
   /* `xcall' should work like `call' (except that rest of function is not
    * evaluated, and only at the level where this is set): to be set when
    * teardown of top level has undesired effects, e.g., for `account's and
    * folder hooks etc., if we do not want to loose `localopts' unroll list */
   mx_GO_INPUT_NO_XCALL = 1u<<9,

   mx_GO_INPUT_FORCE_STDIN = 1u<<10, /* Even in macro, use stdin (`read')! */
   mx_GO_INPUT_DELAY_INJECTIONS = 1u<<11, /* Skip go_input_inject()ions */
   mx_GO_INPUT_NL_ESC = 1u<<12, /* Support "\\$" line continuation */
   mx_GO_INPUT_NL_FOLLOW = 1u<<13, /* ..on such a follow line */
   mx_GO_INPUT_PROMPT_NONE = 1u<<14, /* Do not print prompt */
   mx_GO_INPUT_PROMPT_EVAL = 1u<<15, /* Instead, evaluate *prompt* */

   /* XXX The remains are mostly hacks */

   mx_GO_INPUT_HIST_ADD = 1u<<16, /* Add the result to history list */
   mx_GO_INPUT_HIST_GABBY = 1u<<17, /* Consider history entry as gabby */
   /* Command was erroneous; only in combination with _HIST_GABBY! */
   mx_GO_INPUT_HIST_ERROR = 1u<<18,

   mx_GO_INPUT_IGNERR = 1u<<19, /* Imply `ignerr' command modifier */

   mx__GO_FREEBIT = 24u
};

enum mx_go_input_inject_flags{
   mx_GO_INPUT_INJECT_NONE = 0,
   mx_GO_INPUT_INJECT_COMMIT = 1u<<0, /* Auto-commit input */
   mx_GO_INPUT_INJECT_HISTORY = 1u<<1 /* Allow history addition */
};

struct mx_go_data_ctx{
   struct su_mem_bag *gdc_membag; /* Could be su__mem_bag_mx - or FIRST! */
   void *gdc_ifcond; /* Saved state of conditional stack */
#ifdef mx_HAVE_COLOUR
   struct mx_colour_env *gdc_colour;
   boole gdc_colour_active;
   u8 gdc__colour_pad[7];
# define mx_COLOUR_IS_ACTIVE() \
   (/*mx_go_data->gc_data.gdc_colour != NIL &&*/\
    /*mx_go_data->gc_data.gdc_colour->ce_enabled*/\
    mx_go_data->gdc_colour_active)
#endif
   struct su_mem_bag gdc__membag_buf[1];
};

EXPORT_DATA struct mx_go_data_ctx *mx_go_data;

/* Setup the run environment; this i *only* for main() */
EXPORT void mx_go_init(void);

/* Interpret user commands.  If stdin is not a tty, print no prompt; return
 * whether last processed command returned error; this is *only* for main()! */
EXPORT boole mx_go_main_loop(boole main_call);

/**/
EXPORT void mx_go_input_clearerr(void);

/* Force mx_go_input() to read EOF next */
EXPORT void mx_go_input_force_eof(void);

/* Returns true if force_eof() has been set -- it is set automatically if
 * an input context enters EOF state (rather than error, as in ferror(3)) */
EXPORT boole mx_go_input_is_eof(void);

/* Are there any go_input_inject()ions pending? */
EXPORT boole mx_go_input_have_injections(void);

/* Force mx_go_input() to read that buffer next.
 * If mx_GO_INPUT_INJECT_COMMIT is not set the line editor is reentered with
 * buf as the default/initial line content */
EXPORT void mx_go_input_inject(
      BITENUM_IS(u32,mx_go_input_inject_flags giif), char const *buf, uz len);

/* Read a complete line of input, with editing if interactive and possible.
 * string_or_nil is the optional initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility.
 * If histok_or_nil is set it will be updated to FAL0 if input shall not be
 * placed in history.
 * Return number of octets or a value <0 on error.
 * Note: may use the currently `source'd file stream instead of stdin!
 * Manages the n_PS_READLINE_NL hack
 * TODO We need an OnReadLineCompletedEvent and drop this function */
EXPORT int mx_go_input(BITENUM_IS(u32,mx_go_input_flags gif),
      char const *prompt_or_nil, char **linebuf, uz *linesize,
      char const *string_or_nil, boole *histok_or_nil  su_DBG_LOC_ARGS_DECL);
#ifdef su_HAVE_DBG_LOC_ARGS
# define mx_go_input(A,B,C,D,E,F) mx_go_input(A,B,C,D,E,F  su_DBG_LOC_ARGS_INJ)
#endif

/* Like go_input(), but return savestr()d result or NIL in case of errors or if
 * an empty line would be returned.
 * This may only be called from toplevel (not during n_PS_ROBOT) */
EXPORT char *mx_go_input_cp(BITENUM_IS(u32,mx_go_input_flags) gif,
      char const *prompt_or_nil, char const *string_or_nil);

/* Load a file of user system startup resources.
 * *Only* for main(), returns whether program shall continue */
EXPORT boole mx_go_load_rc(char const *name);

/* "Load" or go_inject() command line option "cmd" arguments in order.
 * *Only* for main(), returns whether program shall continue unless injectit
 * is set, in which case this function does not fail.
 * If lines is NIL the builtin RC file is used, and errors are ignored */
EXPORT boole mx_go_load_lines(boole injectit, char const **lines, uz cnt);

/* Evaluate a complete macro / a single command.  For the former lines will
 * be free()d, for the latter cmd will always be duplicated internally */
EXPORT boole mx_go_macro(BITENUM_IS(u32,mx_go_input_flags) gif,
      char const *name, char **lines,
      void (*on_finalize)(void*), void *finalize_arg);
EXPORT boole mx_go_command(BITENUM_IS(u32,mx_go_input_flags) gif,
      char const *cmd);

/* XXX See a_GO_SPLICE in source */
EXPORT void mx_go_splice_hack(char const *cmd,
      FILE *new_stdin, FILE *new_stdout, u32 new_psonce,
      void (*on_finalize)(void*), void *finalize_arg);
EXPORT void mx_go_splice_hack_remove_after_jump(void);

/* XXX Hack: may we release our (interactive) (terminal) control to a different
 * XXX program, e.g., a $PAGER? */
EXPORT boole mx_go_may_yield_control(void);

/* Whether the current go context is a macro */
EXPORT boole mx_go_ctx_is_macro(void);

/* The name or identifier of the current go context, as well as the callee of
 * the current go context, or NIL if unknown or target context neither of
 * macro or file */
EXPORT char const *mx_go_ctx_name(void);
EXPORT char const *mx_go_ctx_parent_name(void);

/* `source' and consorts */
EXPORT int c_source(void *vp);
EXPORT int c_source_if(void *vp);

/* `xcall' */
EXPORT int c_xcall(void *vp);

/* `exit' and `quit' commands */
EXPORT int c_exit(void *vp);
EXPORT int c_quit(void *vp);

/* `readctl' */
EXPORT int c_readctl(void *vp);

#include <su/code-ou.h>
#endif /* mx_GO_H */
/* s-it-mode */
