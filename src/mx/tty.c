/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ TTY (command line) editing interaction.
 *@ Because we have (had) multiple line-editor implementations, including our
 *@ own M(ailx) L(ine) E(ditor), change the file layout a bit and place those
 *@ one after the other below the other externals.
 *
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE tty
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#if defined mx_HAVE_MLE
# include <su/cs.h>
# include <su/utf.h>

# ifdef mx_HAVE_KEY_BINDINGS
#  include <su/icodec.h>
# endif
#endif

#include "mx/ui-str.h"

#if defined mx_HAVE_MLE || defined mx_HAVE_TERMCAP
# define a_TTY_SIGNALS
#endif

#ifdef a_TTY_SIGNALS
static sighandler_type a_tty_oint, a_tty_oquit, a_tty_oterm,
   a_tty_ohup,
   a_tty_otstp, a_tty_ottin, a_tty_ottou;
#endif

#ifdef a_TTY_SIGNALS
/**/
static void a_tty_sigs_up(void), a_tty_sigs_down(void);

/* Is editor specific code */
static void a_tty_signal(int sig);
#endif

#ifdef a_TTY_SIGNALS
static void
a_tty_sigs_up(void){
   sigset_t nset, oset;
   n_NYD2_IN;

   sigfillset(&nset);

   sigprocmask(SIG_BLOCK, &nset, &oset);
   a_tty_oint = safe_signal(SIGINT, &a_tty_signal);
   a_tty_oquit = safe_signal(SIGQUIT, &a_tty_signal);
   a_tty_oterm = safe_signal(SIGTERM, &a_tty_signal);
   a_tty_ohup = safe_signal(SIGHUP, &a_tty_signal);
   a_tty_otstp = safe_signal(SIGTSTP, &a_tty_signal);
   a_tty_ottin = safe_signal(SIGTTIN, &a_tty_signal);
   a_tty_ottou = safe_signal(SIGTTOU, &a_tty_signal);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   n_NYD2_OU;
}

static void
a_tty_sigs_down(void){
   sigset_t nset, oset;
   n_NYD2_IN;

   sigfillset(&nset);

   sigprocmask(SIG_BLOCK, &nset, &oset);
   safe_signal(SIGINT, a_tty_oint);
   safe_signal(SIGQUIT, a_tty_oquit);
   safe_signal(SIGTERM, a_tty_oterm);
   safe_signal(SIGHUP, a_tty_ohup);
   safe_signal(SIGTSTP, a_tty_otstp);
   safe_signal(SIGTTIN, a_tty_ottin);
   safe_signal(SIGTTOU, a_tty_ottou);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   n_NYD2_OU;
}
#endif /* a_TTY_SIGNALS */

static sigjmp_buf a_tty__actjmp; /* TODO someday, we won't need it no more */
static void
a_tty__acthdl(int s) /* TODO someday, we won't need it no more */
{
   n_NYD_X; /* Signal handler */
   siglongjmp(a_tty__actjmp, s);
}

FL bool_t
getapproval(char const * volatile prompt, bool_t noninteract_default)
{
   sighandler_type volatile oint, ohup;
   bool_t volatile rv;
   int volatile sig;
   n_NYD_IN;

   if(!(n_psonce & n_PSO_INTERACTIVE) || (n_pstate & n_PS_ROBOT)){
      sig = 0;
      rv = noninteract_default;
      goto jleave;
   }
   rv = FAL0;

   /* C99 */{
      char const *quest = noninteract_default
            ? _("[yes]/no? ") : _("[no]/yes? ");

      if (prompt == NULL)
         prompt = _("Continue");
      prompt = savecatsep(prompt, ' ', quest);
   }

   oint = safe_signal(SIGINT, SIG_IGN);
   ohup = safe_signal(SIGHUP, SIG_IGN);
   if ((sig = sigsetjmp(a_tty__actjmp, 1)) != 0)
      goto jrestore;
   safe_signal(SIGINT, &a_tty__acthdl);
   safe_signal(SIGHUP, &a_tty__acthdl);

   while(n_go_input(n_GO_INPUT_CTX_DEFAULT | n_GO_INPUT_NL_ESC, prompt,
            &termios_state.ts_linebuf, &termios_state.ts_linesize, NULL,NULL
         ) >= 0){
      bool_t x;

      x = n_boolify(termios_state.ts_linebuf, UIZ_MAX, noninteract_default);
      if(x >= FAL0){
         rv = x;
         break;
      }
   }
jrestore:
   safe_signal(SIGHUP, ohup);
   safe_signal(SIGINT, oint);
jleave:
   n_NYD_OU;
   if (sig != 0)
      n_raise(sig);
   return rv;
}

#ifdef mx_HAVE_SOCKETS
FL char *
getuser(char const * volatile query) /* TODO v15-compat obsolete */
{
   sighandler_type volatile oint, ohup;
   char * volatile user = NULL;
   int volatile sig;
   n_NYD_IN;

   if (query == NULL)
      query = _("User: ");

   oint = safe_signal(SIGINT, SIG_IGN);
   ohup = safe_signal(SIGHUP, SIG_IGN);
   if ((sig = sigsetjmp(a_tty__actjmp, 1)) != 0)
      goto jrestore;
   safe_signal(SIGINT, &a_tty__acthdl);
   safe_signal(SIGHUP, &a_tty__acthdl);

   if (n_go_input(n_GO_INPUT_CTX_DEFAULT | n_GO_INPUT_NL_ESC, query,
         &termios_state.ts_linebuf, &termios_state.ts_linesize, NULL, NULL
         ) >= 0)
      user = termios_state.ts_linebuf;

jrestore:
   safe_signal(SIGHUP, ohup);
   safe_signal(SIGINT, oint);

   n_NYD_OU;
   if (sig != 0)
      n_raise(sig);
   return user;
}

FL char *
getpassword(char const *query)/* TODO v15: use _only_ n_tty_fp! */
{
   sighandler_type volatile oint, ohup;
   struct termios tios;
   char * volatile pass;
   int volatile sig;
   n_NYD_IN;

   pass = NULL;
   if(!(n_psonce & n_PSO_TTYIN))
      goto j_leave;

   if (query == NULL)
      query = _("Password: ");
   fputs(query, n_tty_fp);
   fflush(n_tty_fp);

   /* FIXME everywhere: tcsetattr() generates SIGTTOU when we're not in
    * FIXME foreground pgrp, and can fail with EINTR!! also affects
    * FIXME termios_state_reset() */
   tcgetattr(STDIN_FILENO, &termios_state.ts_tios);
   memcpy(&tios, &termios_state.ts_tios, sizeof tios);
   termios_state.ts_needs_reset = TRU1;
   tios.c_iflag &= ~(ISTRIP);
   tios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);

   oint = safe_signal(SIGINT, SIG_IGN);
   ohup = safe_signal(SIGHUP, SIG_IGN);
   if ((sig = sigsetjmp(a_tty__actjmp, 1)) != 0)
      goto jrestore;
   safe_signal(SIGINT, &a_tty__acthdl);
   safe_signal(SIGHUP, &a_tty__acthdl);

   tcsetattr(STDIN_FILENO, TCSAFLUSH, &tios);
   if (readline_restart(n_stdin, &termios_state.ts_linebuf,
         &termios_state.ts_linesize, 0) >= 0)
      pass = termios_state.ts_linebuf;
jrestore:
   termios_state_reset();
   putc('\n', n_tty_fp);

   safe_signal(SIGHUP, ohup);
   safe_signal(SIGINT, oint);
   n_NYD_OU;
   if (sig != 0)
      n_raise(sig);
j_leave:
   return pass;
}
#endif /* mx_HAVE_SOCKETS */

FL ui32_t
n_tty_create_prompt(struct n_string *store, char const *xprompt,
      enum n_go_input_flags gif){
   struct n_visual_info_ctx vic;
   struct str in, out;
   ui32_t pwidth;
   char const *cp;
   n_NYD2_IN;

   /* Prompt creation indicates that prompt printing is directly ahead, so take
    * this opportunity of UI-in-a-known-state and advertise the error ring */
#ifdef mx_HAVE_ERRORS
   if((n_psonce & (n_PSO_INTERACTIVE | n_PSO_ERRORS_NOTED)
         ) == n_PSO_INTERACTIVE && (n_pstate & n_PS_ERRORS_PROMPT)){
      n_psonce |= n_PSO_ERRORS_NOTED;
      fprintf(n_stdout, _("There are new messages in the error message ring "
         "(denoted by %s)\n"
         "  The `errors' command manages this message ring\n"),
         V_(n_error));
   }
#endif

jredo:
   n_string_trunc(store, 0);

   if(gif & n_GO_INPUT_PROMPT_NONE){
      pwidth = 0;
      goto jleave;
   }
#ifdef mx_HAVE_ERRORS
   if(n_pstate & n_PS_ERRORS_PROMPT){
      n_pstate &= ~n_PS_ERRORS_PROMPT;
      store = n_string_push_cp(store, V_(n_error));
      store = n_string_push_c(store, '#');
      store = n_string_push_c(store, ' ');
   }
#endif

   cp = (gif & n_GO_INPUT_PROMPT_EVAL)
         ? (gif & n_GO_INPUT_NL_FOLLOW ? ok_vlook(prompt2) : ok_vlook(prompt))
         : xprompt;
   if(cp != NULL && *cp != '\0'){
      enum n_shexp_state shs;

      store = n_string_push_cp(store, cp);
      in.s = n_string_cp(store);
      in.l = store->s_len;
      out = in;
      store = n_string_drop_ownership(store);

      shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
            n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
            n_SHEXP_PARSE_QUOTE_AUTO_DSQ), store, &in, NULL);
      if((shs & n_SHEXP_STATE_ERR_MASK) || !(shs & n_SHEXP_STATE_STOP)){
         store = n_string_clear(store);
         store = n_string_take_ownership(store, out.s, out.l +1, out.l);
jeeval:
         n_err(_("*prompt2?* evaluation failed, actively unsetting it\n"));
         if(gif & n_GO_INPUT_NL_FOLLOW)
            ok_vclear(prompt2);
         else
            ok_vclear(prompt);
         goto jredo;
      }

      if(!store->s_auto)
         n_free(out.s);
   }

   /* Make all printable TODO not know, we want to pass through ESC/CSI! */
#if 0
   in.s = n_string_cp(store);
   in.l = store->s_len;
   makeprint(&in, &out);
   store = n_string_assign_buf(store, out.s, out.l);
   n_free(out.s);
#endif

   /* We need the visual width.. */
   memset(&vic, 0, sizeof vic);
   vic.vic_indat = n_string_cp(store);
   vic.vic_inlen = store->s_len;
   for(pwidth = 0; vic.vic_inlen > 0;){
      /* but \[ .. \] is not taken into account */
      if(vic.vic_indat[0] == '\\' && vic.vic_inlen > 1 &&
            vic.vic_indat[1] == '['){
         size_t i;

         i = PTR2SIZE(vic.vic_indat - store->s_dat);
         store = n_string_cut(store, i, 2);
         cp = &n_string_cp(store)[i];
         i = store->s_len - i;
         for(;; ++cp, --i){
            if(i < 2){
               n_err(_("Open \\[ sequence not closed in *prompt2?*\n"));
               goto jeeval;
            }
            if(cp[0] == '\\' && cp[1] == ']')
               break;
         }
         i = PTR2SIZE(cp - store->s_dat);
         store = n_string_cut(store, i, 2);
         vic.vic_indat = &n_string_cp(store)[i];
         vic.vic_inlen = store->s_len - i;
      }else if(!n_visual_info(&vic, n_VISUAL_INFO_WIDTH_QUERY |
            n_VISUAL_INFO_ONE_CHAR)){
         n_err(_("Character set error in evaluation of *prompt2?*\n"));
         goto jeeval;
      }else{
         pwidth += (ui32_t)vic.vic_vi_width;
         vic.vic_indat = vic.vic_oudat;
         vic.vic_inlen = vic.vic_oulen;
      }
   }

   /* And there may be colour support, too */
#ifdef mx_HAVE_COLOUR
   if(n_COLOUR_IS_ACTIVE()){
      struct str const *psp, *rsp;
      struct n_colour_pen *ccp;

      if((ccp = n_colour_pen_create(n_COLOUR_ID_MLE_PROMPT, NULL)) != NULL &&
            (psp = n_colour_pen_to_str(ccp)) != NULL &&
            (rsp = n_colour_reset_to_str()) != NULL){
         store = n_string_unshift_buf(store, psp->s, psp->l);
         /*store =*/ n_string_push_buf(store, rsp->s, rsp->l);
      }
   }
#endif /* mx_HAVE_COLOUR */

jleave:
   n_NYD2_OU;
   return pwidth;
}

/*
 * MLE: the Mailx-Line-Editor, our homebrew editor
 * (inspired from NetBSDs sh(1) and dash(1)s hetio.c).
 *
 * Only used in interactive mode.
 * TODO . This code should be splitted in funs/raw input/bind modules.
 * TODO . We work with wide characters, but not for buffer takeovers and
 * TODO   cell2save()ings.  This should be changed.  For the former the buffer
 * TODO   thus needs to be converted to wide first, and then simply be fed in.
 * TODO . We repaint too much.  To overcome this use the same approach that my
 * TODO   terminal library uses, add a true "virtual screen line" that stores
 * TODO   the actually visible content, keep a notion of "first modified slot"
 * TODO   and "last modified slot" (including "unknown" and "any" specials),
 * TODO   update that virtual instead, then synchronize what has truly changed.
 * TODO   I.e., add an indirection layer.
 * TODO . No BIDI support.
 * TODO . `bind': we currently use only one lookup tree.
 * TODO   For graceful behaviour (in conjunction with mx_HAVE_TERMCAP) we
 * TODO   need a lower level tree, which possibly combines bytes into "symbolic
 * TODO   wchar_t values", into "keys" that is, as applicable, and an upper
 * TODO   layer which only works on "keys" in order to possibly combine them
 * TODO   into key sequences.  We can reuse existent tree code for that.
 * TODO   We need an additional hashmap which maps termcap/terminfo names to
 * TODO   (their byte representations and) a dynamically assigned unique
 * TODO   "symbolic wchar_t value".  This implies we may have incompatibilities
 * TODO   when __STDC_ISO_10646__ is not defined.  Also we do need takeover-
 * TODO   bytes storage, but it can be a string_creat_auto in the line struct.
 * TODO   Until then we can run into ambiguities; in rare occasions.
 */
#ifdef mx_HAVE_MLE
/* To avoid memory leaks etc. with the current codebase that simply longjmp(3)s
 * we're forced to use the very same buffer--the one that is passed through to
 * us from the outside--to store anything we need, i.e., a "struct cell[]", and
 * convert that on-the-fly back to the plain char* result once we're done.
 * To simplify our live, use savestr() buffers for all other needed memory */

# ifdef mx_HAVE_KEY_BINDINGS
   /* Default *bind-timeout* key-sequence continuation timeout, in tenths of
    * a second.  Must fit in 8-bit!  Update the manual upon change! */
#  define a_TTY_BIND_TIMEOUT 2
#  define a_TTY_BIND_TIMEOUT_MAX SI8_MAX

n_CTAV(a_TTY_BIND_TIMEOUT_MAX <= UI8_MAX);

   /* We have a chicken-and-egg problem with `bind' and our termcap layer,
    * because we may not initialize the latter automatically to allow users to
    * specify *termcap-disable* and let it mean exactly that.
    * On the other hand users can be expected to use `bind' in resources.
    * Therefore bindings which involve termcap/terminfo sequences, and which
    * are defined before n_PSO_STARTED signals usability of termcap/terminfo,
    * will be (partially) delayed until tty_init() is called.
    * And we preallocate space for the expansion of the resolved capability */
#  define a_TTY_BIND_CAPNAME_MAX 15
#  define a_TTY_BIND_CAPEXP_ROUNDUP 16

n_CTAV(n_ISPOW2(a_TTY_BIND_CAPEXP_ROUNDUP));
n_CTA(a_TTY_BIND_CAPEXP_ROUNDUP <= SI8_MAX / 2, "Variable must fit in 6-bit");
n_CTA(a_TTY_BIND_CAPEXP_ROUNDUP >= 8, "Variable too small");
# endif /* mx_HAVE_KEY_BINDINGS */

# ifdef mx_HAVE_HISTORY
   /* The first line of the history file is used as a marker after >v14.9.6 */
#  define a_TTY_HIST_MARKER "@s-mailx history v2"
# endif

/* The maximum size (of a_tty_cell's) in a line */
# define a_TTY_LINE_MAX SI32_MAX

/* (Some more CTAs around) */
n_CTA(a_TTY_LINE_MAX <= SI32_MAX,
   "a_TTY_LINE_MAX larger than SI32_MAX, but the MLE uses 32-bit arithmetic");

/* When shall the visual screen be scrolled, in % of usable screen width */
# define a_TTY_SCROLL_MARGIN_LEFT 15
# define a_TTY_SCROLL_MARGIN_RIGHT 10

/* fexpand() flags for expand-on-tab */
# define a_TTY_TAB_FEXP_FL \
   (FEXP_NOPROTO | FEXP_FULL | FEXP_SILENT | FEXP_MULTIOK)

/* Columns to ripoff: position indicator.
 * Should be >= 4 to dig the position indicator that we place (if there is
 * sufficient space) */
# define a_TTY_WIDTH_RIPOFF 4

/* The implementation of the MLE functions always exists, and is based upon
 * the a_TTY_BIND_FUN_* constants, so most of this enum is always necessary */
enum a_tty_bind_flags{
# ifdef mx_HAVE_KEY_BINDINGS
   a_TTY_BIND_RESOLVE = 1u<<8,   /* Term cap. yet needs to be resolved */
   a_TTY_BIND_DEFUNCT = 1u<<9,   /* Unicode/term cap. used but not avail. */
   a_TTY__BIND_MASK = a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT,
   /* MLE fun assigned to a one-byte-sequence: this may be used for special
    * key-sequence bypass processing */
   a_TTY_BIND_MLE1CNTRL = 1u<<10,
   a_TTY_BIND_NOCOMMIT = 1u<<11, /* Expansion shall be editable */
# endif

   /* MLE internal commands XXX Can these be values not bits? */
   a_TTY_BIND_FUN_INTERNAL = 1u<<15,
   a_TTY__BIND_FUN_SHIFT = 16u,
   a_TTY__BIND_FUN_SHIFTMAX = 24u,
   a_TTY__BIND_FUN_MASK = ((1u << a_TTY__BIND_FUN_SHIFTMAX) - 1) &
         ~((1u << a_TTY__BIND_FUN_SHIFT) - 1),
# define a_TTY_BIND_FUN_REDUCE(X) \
   (((ui32_t)(X) & a_TTY__BIND_FUN_MASK) >> a_TTY__BIND_FUN_SHIFT)
# define a_TTY_BIND_FUN_EXPAND(X) \
   (((ui32_t)(X) & (a_TTY__BIND_FUN_MASK >> a_TTY__BIND_FUN_SHIFT)) << \
      a_TTY__BIND_FUN_SHIFT)
# undef a_X
# define a_X(N,I)\
   a_TTY_BIND_FUN_ ## N = a_TTY_BIND_FUN_EXPAND(I),

   a_X(BELL, 0)
   a_X(GO_BWD, 1) a_X(GO_FWD, 2)
   a_X(GO_WORD_BWD, 3) a_X(GO_WORD_FWD, 4)
   a_X(GO_SCREEN_BWD, 5) a_X(GO_SCREEN_FWD, 6)
   a_X(GO_HOME, 7) a_X(GO_END, 8)
   a_X(DEL_BWD, 9) a_X(DEL_FWD, 10)
   a_X(SNARF_WORD_BWD, 11) a_X(SNARF_WORD_FWD, 12)
   a_X(SNARF_END, 13) a_X(SNARF_LINE, 14)
   a_X(HIST_BWD, 15) a_X(HIST_FWD, 16)
   a_X(HIST_SRCH_BWD, 17) a_X(HIST_SRCH_FWD, 18)
   a_X(REPAINT, 19)
   a_X(QUOTE_RNDTRIP, 20)
   a_X(PROMPT_CHAR, 21)
   a_X(COMPLETE, 22)
   a_X(PASTE, 23)
   a_X(CLEAR_SCREEN, 24)

   a_X(CANCEL, 25)
   a_X(RESET, 26)
   a_X(FULLRESET, 27)
   a_X(COMMIT, 28) /* Must be last one! */
# undef a_X

   a_TTY__BIND_LAST = 1<<28
};
# ifdef mx_HAVE_KEY_BINDINGS
n_CTA((ui32_t)a_TTY_BIND_RESOLVE >= (ui32_t)n__GO_INPUT_CTX_MAX1,
   "Bit carrier lower boundary must be raised to avoid value sharing");
# endif
n_CTA(a_TTY_BIND_FUN_EXPAND(a_TTY_BIND_FUN_COMMIT) <
      (1 << a_TTY__BIND_FUN_SHIFTMAX),
   "Bit carrier range must be expanded to represent necessary bits");
n_CTA(a_TTY__BIND_LAST >= (1u << a_TTY__BIND_FUN_SHIFTMAX),
   "Bit carrier upper boundary must be raised to avoid value sharing");
n_CTA(UICMP(64, a_TTY__BIND_LAST, <=, SI32_MAX),
   "Flag bits excess storage datatype" /* And we need one bit free */);

enum a_tty_fun_status{
   a_TTY_FUN_STATUS_OK,       /* Worked, next character */
   a_TTY_FUN_STATUS_COMMIT,   /* Line done */
   a_TTY_FUN_STATUS_RESTART,  /* Complete restart, reset multibyte etc. */
   a_TTY_FUN_STATUS_END       /* End, return EOF */
};

# ifdef mx_HAVE_HISTORY
enum a_tty_hist_flags{
   a_TTY_HIST_CTX_DEFAULT = n_GO_INPUT_CTX_DEFAULT,
   a_TTY_HIST_CTX_COMPOSE = n_GO_INPUT_CTX_COMPOSE,
   a_TTY_HIST_CTX_MASK = n__GO_INPUT_CTX_MASK,
   /* Cannot use enum n_go_input_flags for the rest, need to stay in 8-bit */
   a_TTY_HIST_GABBY = 1u<<7,
   a_TTY_HIST__MAX = a_TTY_HIST_GABBY
};
n_CTA(a_TTY_HIST_CTX_MASK < a_TTY_HIST_GABBY, "Enumeration value overlap");
# endif

enum a_tty_visual_flags{
   a_TTY_VF_NONE,
   a_TTY_VF_MOD_CURSOR = 1u<<0,  /* Cursor moved */
   a_TTY_VF_MOD_CONTENT = 1u<<1, /* Content modified */
   a_TTY_VF_MOD_DIRTY = 1u<<2,   /* Needs complete repaint */
   a_TTY_VF_MOD_SINGLE = 1u<<3,  /* TODO Drop if indirection as above comes */
   a_TTY_VF_REFRESH = a_TTY_VF_MOD_DIRTY | a_TTY_VF_MOD_CURSOR |
         a_TTY_VF_MOD_CONTENT | a_TTY_VF_MOD_SINGLE,
   a_TTY_VF_BELL = 1u<<8,        /* Ring the bell */
   a_TTY_VF_SYNC = 1u<<9,        /* Flush/Sync I/O channel */

   a_TTY_VF_ALL_MASK = a_TTY_VF_REFRESH | a_TTY_VF_BELL | a_TTY_VF_SYNC,
   a_TTY__VF_LAST = a_TTY_VF_SYNC
};

# ifdef mx_HAVE_KEY_BINDINGS
struct a_tty_bind_ctx{
   struct a_tty_bind_ctx *tbc_next;
   char *tbc_seq;       /* quence as given (poss. re-quoted), in .tb__buf */
   char *tbc_exp;       /* ansion, in .tb__buf */
   /* The .tbc_seq'uence with any terminal capabilities resolved; in fact an
    * array of structures, the first entry of which is {si32_t buf_len_iscap;}
    * where the signed bit indicates whether the buffer is a resolved terminal
    * capability instead of a (possibly multibyte) character.  In .tbc__buf */
   char *tbc_cnv;
   ui32_t tbc_seq_len;
   ui32_t tbc_exp_len;
   ui32_t tbc_cnv_len;
   ui32_t tbc_flags;
   char tbc__buf[n_VFIELD_SIZE(0)];
};
# endif /* mx_HAVE_KEY_BINDINGS */

struct a_tty_bind_builtin_tuple{
   bool_t tbbt_iskey;   /* Whether this is a control key; else termcap query */
   char tbbt_ckey;      /* Control code */
   ui16_t tbbt_query;   /* enum n_termcap_query (instead) */
   char tbbt_exp[12];   /* String or [0]=NUL/[1]=BIND_FUN_REDUCE() */
};
n_CTA(n__TERMCAP_QUERY_MAX1 <= UI16_MAX,
   "Enumeration cannot be stored in datatype");

# ifdef mx_HAVE_KEY_BINDINGS
struct a_tty_bind_parse_ctx{
   char const *tbpc_cmd;      /* Command which parses */
   char const *tbpc_in_seq;   /* In: key sequence */
   struct str tbpc_exp;       /* In/Out: expansion (or NULL) */
   struct a_tty_bind_ctx *tbpc_tbcp;  /* Out: if yet existent */
   struct a_tty_bind_ctx *tbpc_ltbcp; /* Out: the one before .tbpc_tbcp */
   char *tbpc_seq;            /* Out: normalized sequence */
   char *tbpc_cnv;            /* Out: sequence when read(2)ing it */
   ui32_t tbpc_seq_len;
   ui32_t tbpc_cnv_len;
   ui32_t tbpc_cnv_align_mask; /* For creating a_tty_bind_ctx.tbc_cnv */
   ui32_t tbpc_flags;         /* n_go_input_flags | a_tty_bind_flags */
};

/* Input character tree */
struct a_tty_bind_tree{
   struct a_tty_bind_tree *tbt_sibling; /* s at same level */
   struct a_tty_bind_tree *tbt_childs; /* Sequence continues.. here */
   struct a_tty_bind_tree *tbt_parent;
   struct a_tty_bind_ctx *tbt_bind;    /* NULL for intermediates */
   wchar_t tbt_char;                   /* acter this level represents */
   bool_t tbt_isseq;                   /* Belongs to multibyte sequence */
   bool_t tbt_isseq_trail;             /* ..is trailing byte of it */
   ui8_t tbt__dummy[2];
};
# endif /* mx_HAVE_KEY_BINDINGS */

struct a_tty_cell{
   wchar_t tc_wc;
   ui16_t tc_count;  /* ..of bytes */
   ui8_t tc_width;   /* Visual width; TAB==UI8_MAX! */
   bool_t tc_novis;  /* Don't display visually as such (control character) */
   char tc_cbuf[MB_LEN_MAX * 2]; /* .. plus reset shift sequence */
};

struct a_tty_global{
   struct a_tty_line *tg_line;   /* To be able to access it from signal hdl */
# ifdef mx_HAVE_HISTORY
   struct a_tty_hist *tg_hist;
   struct a_tty_hist *tg_hist_tail;
   size_t tg_hist_size;
   size_t tg_hist_size_max;
# endif
# ifdef mx_HAVE_KEY_BINDINGS
   ui32_t tg_bind_cnt;           /* Overall number of bindings */
   bool_t tg_bind_isdirty;
   bool_t tg_bind_isbuild;
#  define a_TTY_SHCUT_MAX (3 +1) /* Note: update manual on change! */
   ui8_t tg_bind__dummy[2];
   char tg_bind_shcut_cancel[n__GO_INPUT_CTX_MAX1][a_TTY_SHCUT_MAX];
   char tg_bind_shcut_prompt_char[n__GO_INPUT_CTX_MAX1][a_TTY_SHCUT_MAX];
   struct a_tty_bind_ctx *tg_bind[n__GO_INPUT_CTX_MAX1];
   struct a_tty_bind_tree *tg_bind_tree[n__GO_INPUT_CTX_MAX1][HSHSIZE];
# endif
   struct termios tg_tios_old;
   struct termios tg_tios_new;
};
# ifdef mx_HAVE_KEY_BINDINGS
n_CTA(n__GO_INPUT_CTX_MAX1 == 3 && a_TTY_SHCUT_MAX == 4 &&
   n_SIZEOF_FIELD(struct a_tty_global, tg_bind__dummy) == 2,
   "Value results in array sizes that results in bad structure layout");
n_CTA(a_TTY_SHCUT_MAX > 1,
   "Users need at least one shortcut, plus NUL terminator");
# endif

# ifdef mx_HAVE_HISTORY
struct a_tty_hist{
   struct a_tty_hist *th_older;
   struct a_tty_hist *th_younger;
   ui32_t th_len;
   ui8_t th_flags;                  /* enum a_tty_hist_flags */
   char th_dat[n_VFIELD_SIZE(3)];
};
n_CTA(UI8_MAX >= a_TTY_HIST__MAX, "Value exceeds datatype storage");
# endif

#if defined mx_HAVE_KEY_BINDINGS || defined mx_HAVE_HISTORY
struct a_tty_input_ctx_map{
   enum n_go_input_flags ticm_ctx;
   char const ticm_name[12];  /* Name of `bind' context */
};
#endif

struct a_tty_line{
   /* Caller pointers */
   char **tl_x_buf;
   size_t *tl_x_bufsize;
   /* Input processing */
# ifdef mx_HAVE_KEY_BINDINGS
   wchar_t tl_bind_takeover;     /* Leftover byte to consume next */
   ui8_t tl_bind_timeout;        /* In-seq. inter-byte-timer, in 1/10th secs */
   ui8_t tl__bind_dummy[3];
   char (*tl_bind_shcut_cancel)[a_TTY_SHCUT_MAX]; /* Special _CANCEL control */
   char (*tl_bind_shcut_prompt_char)[a_TTY_SHCUT_MAX]; /* ..for _PROMPT_CHAR */
   struct a_tty_bind_tree *(*tl_bind_tree_hmap)[HSHSIZE];/* Bind lookup tree */
   struct a_tty_bind_tree *tl_bind_tree;
# endif
   /* Line data / content handling */
   ui32_t tl_count;              /* ..of a_tty_cell's (<= a_TTY_LINE_MAX) */
   ui32_t tl_cursor;             /* Current a_tty_cell insertion point */
   union{
      char *cbuf;                /* *.tl_x_buf */
      struct a_tty_cell *cells;
   } tl_line;
   struct str tl_defc;           /* Current default content */
   size_t tl_defc_cursor_byte;   /* Desired cursor position after takeover */
   struct str tl_savec;          /* Saved default content */
   struct str tl_pastebuf;       /* Last snarfed data */
# ifdef mx_HAVE_HISTORY
   struct a_tty_hist *tl_hist;   /* History cursor */
# endif
   ui32_t tl_count_max;          /* ..before buffer needs to grow */
   /* Visual data representation handling */
   ui32_t tl_vi_flags;           /* enum a_tty_visual_flags */
   ui32_t tl_lst_count;          /* .tl_count after last sync */
   ui32_t tl_lst_cursor;         /* .tl_cursor after last sync */
   /* TODO Add another indirection layer by adding a tl_phy_line of
    * TODO a_tty_cell objects, incorporate changes in visual layer,
    * TODO then check what _really_ has changed, sync those changes only */
   struct a_tty_cell const *tl_phy_start; /* First visible cell, left border */
   ui32_t tl_phy_cursor;         /* Physical cursor position */
   bool_t tl_quote_rndtrip;      /* For _kht() expansion */
   ui8_t tl__dummy2[3 + 4];
   ui32_t tl_goinflags;          /* enum n_go_input_flags */
   ui32_t tl_prompt_length;      /* Preclassified (TODO needed as tty_cell) */
   ui32_t tl_prompt_width;
   char const *tl_prompt;        /* Preformatted prompt (including colours) */
   /* .tl_pos_buf is a hack */
# ifdef mx_HAVE_COLOUR
   char *tl_pos_buf;             /* mle-position colour-on, [4], reset seq. */
   char *tl_pos;                 /* Address of the [4] */
# endif
};

# if defined mx_HAVE_KEY_BINDINGS || defined mx_HAVE_HISTORY
/* C99: use [INDEX]={} */
n_CTAV(n_GO_INPUT_CTX_BASE == 0);
n_CTAV(n_GO_INPUT_CTX_DEFAULT == 1);
n_CTAV(n_GO_INPUT_CTX_COMPOSE == 2);
static struct a_tty_input_ctx_map const
      a_tty_input_ctx_maps[n__GO_INPUT_CTX_MAX1] = {
   n_FIELD_INITI(n_GO_INPUT_CTX_BASE){n_GO_INPUT_CTX_BASE, "base"},
   n_FIELD_INITI(n_GO_INPUT_CTX_DEFAULT){n_GO_INPUT_CTX_DEFAULT, "default"},
   n_FIELD_INITI(n_GO_INPUT_CTX_COMPOSE){n_GO_INPUT_CTX_COMPOSE, "compose"}
};
#endif

# ifdef mx_HAVE_KEY_BINDINGS
/* Special functions which our MLE provides internally.
 * Update the manual upon change! */
static char const a_tty_bind_fun_names[][24] = {
#  undef a_X
#  define a_X(I,N) \
   n_FIELD_INITI(a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## I)) "mle-" N "\0",

   a_X(BELL, "bell")
   a_X(GO_BWD, "go-bwd") a_X(GO_FWD, "go-fwd")
   a_X(GO_WORD_BWD, "go-word-bwd") a_X(GO_WORD_FWD, "go-word-fwd")
   a_X(GO_SCREEN_BWD, "go-screen-bwd") a_X(GO_SCREEN_FWD, "go-screen-fwd")
   a_X(GO_HOME, "go-home") a_X(GO_END, "go-end")
   a_X(DEL_BWD, "del-bwd") a_X(DEL_FWD, "del-fwd")
   a_X(SNARF_WORD_BWD, "snarf-word-bwd") a_X(SNARF_WORD_FWD, "snarf-word-fwd")
   a_X(SNARF_END, "snarf-end") a_X(SNARF_LINE, "snarf-line")
   a_X(HIST_BWD, "hist-bwd") a_X(HIST_FWD, "hist-fwd")
   a_X(HIST_SRCH_BWD, "hist-srch-bwd") a_X(HIST_SRCH_FWD, "hist-srch-fwd")
   a_X(REPAINT, "repaint")
   a_X(QUOTE_RNDTRIP, "quote-rndtrip")
   a_X(PROMPT_CHAR, "prompt-char")
   a_X(COMPLETE, "complete")
   a_X(PASTE, "paste")
   a_X(CLEAR_SCREEN, "clear-screen")

   a_X(CANCEL, "cancel")
   a_X(RESET, "reset")
   a_X(FULLRESET, "fullreset")
   a_X(COMMIT, "commit")

#  undef a_X
};
# endif /* mx_HAVE_KEY_BINDINGS */

/* The default key bindings (unless disallowed).  Update manual upon change!
 * A logical subset of this table is also used if !mx_HAVE_KEY_BINDINGS (more
 * expensive than a switch() on control codes directly, but less redundant).
 * The table for the "base" context */
static struct a_tty_bind_builtin_tuple const a_tty_bind_base_tuples[] = {
# undef a_X
# define a_X(K,S) \
   {TRU1, K, 0, {'\0', (char)a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## S),}},

   a_X('A', GO_HOME)
   a_X('B', GO_BWD)
   /* C: SIGINT */
   a_X('D', DEL_FWD)
   a_X('E', GO_END)
   a_X('F', GO_FWD)
   a_X('G', RESET)
   a_X('H', DEL_BWD)
   a_X('I', COMPLETE)
   a_X('J', COMMIT)
   a_X('K', SNARF_END)
   a_X('L', REPAINT)
   /* M: same as J */
   a_X('N', HIST_FWD)
   /* O: below */
   a_X('P', HIST_BWD)
   a_X('Q', QUOTE_RNDTRIP)
   a_X('R', HIST_SRCH_BWD)
   a_X('S', HIST_SRCH_FWD)
   a_X('T', PASTE)
   a_X('U', SNARF_LINE)
   a_X('V', PROMPT_CHAR)
   a_X('W', SNARF_WORD_BWD)
   a_X('X', GO_WORD_FWD)
   a_X('Y', GO_WORD_BWD)
   /* Z: SIGTSTP */

   a_X('[', CANCEL)
   /* \: below */
   /* ]: below */
   /* ^: below */
   a_X('_', SNARF_WORD_FWD)

   a_X('?', DEL_BWD)

# undef a_X
# define a_X(K,S) {TRU1, K, 0, {S}},

   /* The remains only if we have `bind' functionality available */
# ifdef mx_HAVE_KEY_BINDINGS
#  undef a_X
#  define a_X(Q,S) \
   {FAL0, '\0', n_TERMCAP_QUERY_ ## Q,\
      {'\0', (char)a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## S),}},

   a_X(key_backspace, DEL_BWD) a_X(key_dc, DEL_FWD)
   a_X(key_eol, SNARF_END)
   a_X(key_home, GO_HOME) a_X(key_end, GO_END)
   a_X(key_left, GO_BWD) a_X(key_right, GO_FWD)
   a_X(xkey_aleft, GO_WORD_BWD) a_X(xkey_aright, GO_WORD_FWD)
   a_X(xkey_cleft, GO_SCREEN_BWD) a_X(xkey_cright, GO_SCREEN_FWD)
   a_X(key_sleft, GO_HOME) a_X(key_sright, GO_END)
   a_X(key_up, HIST_BWD) a_X(key_down, HIST_FWD)
# endif /* mx_HAVE_KEY_BINDINGS */
};

/* The table for the "default" context */
static struct a_tty_bind_builtin_tuple const a_tty_bind_default_tuples[] = {
# undef a_X
# define a_X(K,S) \
   {TRU1, K, 0, {'\0', (char)a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## S),}},

# undef a_X
# define a_X(K,S) {TRU1, K, 0, {S}},

   a_X('O', "dt")

   a_X('\\', "z+")
   a_X(']', "z$")
   a_X('^', "z0")

   /* The remains only if we have `bind' functionality available */
# ifdef mx_HAVE_KEY_BINDINGS
#  undef a_X
#  define a_X(Q,S) {FAL0, '\0', n_TERMCAP_QUERY_ ## Q, {S}},

   a_X(key_shome, "z0") a_X(key_send, "z$")
   a_X(xkey_sup, "z0") a_X(xkey_sdown, "z$")
   a_X(key_ppage, "z-") a_X(key_npage, "z+")
   a_X(xkey_cup, "dotmove-") a_X(xkey_cdown, "dotmove+")
# endif /* mx_HAVE_KEY_BINDINGS */
};
# undef a_X

static struct a_tty_global a_tty;

/* Change from canonical to raw, non-canonical mode, and way back */
static void a_tty_term_mode(bool_t raw);

# ifdef mx_HAVE_HISTORY
/* Load and save the history file, respectively */
static bool_t a_tty_hist_load(void);
static bool_t a_tty_hist_save(void);

/* Initialize .tg_hist_size_max and return desired history file, or NULL */
static char const *a_tty_hist__query_config(void);

/* (Definetely) Add an entry TODO yet assumes hold_all_sigs() is held! */
static void a_tty_hist_add(char const *s, enum n_go_input_flags gif);
# endif

/* Adjust an active raw mode to use / not use a timeout */
# ifdef mx_HAVE_KEY_BINDINGS
static void a_tty_term_rawmode_timeout(struct a_tty_line *tlp, bool_t enable);
# endif

/* 0-X (2), UI8_MAX == \t / HT */
static ui8_t a_tty_wcwidth(wchar_t wc);

/* Memory / cell / word generics */
static void a_tty_check_grow(struct a_tty_line *tlp, ui32_t no
               su_DBG_LOC_ARGS_DECL);
static ssize_t a_tty_cell2dat(struct a_tty_line *tlp);
static void a_tty_cell2save(struct a_tty_line *tlp);

/* Save away data bytes of given range (max = non-inclusive) */
static void a_tty_copy2paste(struct a_tty_line *tlp, struct a_tty_cell *tcpmin,
               struct a_tty_cell *tcpmax);

/* Ask user for hexadecimal number, interpret as UTF-32 */
static wchar_t a_tty_vinuni(struct a_tty_line *tlp);

/* Visual screen synchronization */
static bool_t a_tty_vi_refresh(struct a_tty_line *tlp);

static bool_t a_tty_vi__paint(struct a_tty_line *tlp);

/* Search for word boundary, starting at tl_cursor, in "dir"ection (<> 0).
 * Return <0 when moving is impossible (backward direction but in position 0,
 * forward direction but in outermost column), and relative distance to
 * tl_cursor otherwise */
static si32_t a_tty_wboundary(struct a_tty_line *tlp, si32_t dir);

/* Most function implementations */
static void a_tty_khome(struct a_tty_line *tlp, bool_t dobell);
static void a_tty_kend(struct a_tty_line *tlp);
static void a_tty_kbs(struct a_tty_line *tlp);
static void a_tty_ksnarf(struct a_tty_line *tlp, bool_t cplline,bool_t dobell);
static si32_t a_tty_kdel(struct a_tty_line *tlp);
static void a_tty_kleft(struct a_tty_line *tlp);
static void a_tty_kright(struct a_tty_line *tlp);
static void a_tty_ksnarfw(struct a_tty_line *tlp, bool_t fwd);
static void a_tty_kgow(struct a_tty_line *tlp, si32_t dir);
static void a_tty_kgoscr(struct a_tty_line *tlp, si32_t dir);
static bool_t a_tty_kother(struct a_tty_line *tlp, wchar_t wc);
static ui32_t a_tty_kht(struct a_tty_line *tlp);

# ifdef mx_HAVE_HISTORY
/* Return UI32_MAX on "exhaustion" */
static ui32_t a_tty_khist(struct a_tty_line *tlp, bool_t fwd);
static ui32_t a_tty_khist_search(struct a_tty_line *tlp, bool_t fwd);

static ui32_t a_tty__khist_shared(struct a_tty_line *tlp,
                  struct a_tty_hist *thp);
# endif

/* Handle a function */
static enum a_tty_fun_status a_tty_fun(struct a_tty_line *tlp,
                              enum a_tty_bind_flags tbf, size_t *len);

/* Readline core */
static ssize_t a_tty_readline(struct a_tty_line *tlp, size_t len,
                  bool_t *histok_or_null  su_DBG_LOC_ARGS_DECL);

# ifdef mx_HAVE_KEY_BINDINGS
/* Find context or -1 */
static enum n_go_input_flags a_tty_bind_ctx_find(char const *name);

/* Create (or replace, if allowed) a binding */
static bool_t a_tty_bind_create(struct a_tty_bind_parse_ctx *tbpcp,
               bool_t replace);

/* Shared implementation to parse `bind' and `unbind' "key-sequence" and
 * "expansion" command line arguments into something that we can work with */
static bool_t a_tty_bind_parse(bool_t isbindcmd,
               struct a_tty_bind_parse_ctx *tbpcp);

/* Lazy resolve a termcap(5)/terminfo(5) (or *termcap*!) capability */
static void a_tty_bind_resolve(struct a_tty_bind_ctx *tbcp);

/* Delete an existing binding */
static void a_tty_bind_del(struct a_tty_bind_parse_ctx *tbpcp);

/* Life cycle of all input node trees */
static void a_tty_bind_tree_build(void);
static void a_tty_bind_tree_teardown(void);

static void a_tty__bind_tree_add(ui32_t hmap_idx,
               struct a_tty_bind_tree *store[HSHSIZE],
               struct a_tty_bind_ctx *tbcp);
static struct a_tty_bind_tree *a_tty__bind_tree_add_wc(
               struct a_tty_bind_tree **treep, struct a_tty_bind_tree *parentp,
               wchar_t wc, bool_t isseq);
static void a_tty__bind_tree_free(struct a_tty_bind_tree *tbtp);
# endif /* mx_HAVE_KEY_BINDINGS */

static void
a_tty_signal(int sig){
   /* Prototype at top */
   sigset_t nset, oset;
   n_NYD_X; /* Signal handler */

   n_COLOUR( n_colour_env_gut(); ) /* TODO NO SIMPLE SUSPENSION POSSIBLE.. */
   a_tty_term_mode(FAL0);
   n_TERMCAP_SUSPEND(TRU1);
   a_tty_sigs_down();

   sigemptyset(&nset);
   sigaddset(&nset, sig);
   sigprocmask(SIG_UNBLOCK, &nset, &oset);
   n_raise(sig);
   /* When we come here we'll continue editing, so reestablish */
   sigprocmask(SIG_BLOCK, &oset, (sigset_t*)NULL);

   /* TODO THEREFORE NEED TO _GUT() .. _CREATE() ENTIRE ENVS!! */
   n_COLOUR( n_colour_env_create(n_COLOUR_CTX_MLE, n_tty_fp, FAL0); )
   a_tty_sigs_up();
   n_TERMCAP_RESUME(TRU1);
   a_tty_term_mode(TRU1);
   a_tty.tg_line->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
}

static void
a_tty_term_mode(bool_t raw){
   struct termios *tiosp;
   n_NYD2_IN;

   tiosp = &a_tty.tg_tios_old;
   if(!raw)
      goto jleave;

   /* Always requery the attributes, in case we've been moved from background
    * to foreground or however else in between sessions */
   /* XXX Always enforce ECHO and ICANON in the OLD attributes - do so as long
    * XXX as we don't properly deal with TTIN and TTOU etc. */
   tcgetattr(STDIN_FILENO, tiosp); /* TODO v15: use _only_ n_tty_fp! */
   tiosp->c_lflag |= ECHO | ICANON;

   memcpy(&a_tty.tg_tios_new, tiosp, sizeof *tiosp);
   tiosp = &a_tty.tg_tios_new;
   tiosp->c_cc[VMIN] = 1;
   tiosp->c_cc[VTIME] = 0;
   /* Enable ^\, ^Q and ^S to be used for key bindings */
   tiosp->c_cc[VQUIT] = tiosp->c_cc[VSTART] = tiosp->c_cc[VSTOP] = '\0';
   tiosp->c_iflag &= ~(ISTRIP | IGNCR);
   tiosp->c_lflag &= ~(ECHO /*| ECHOE | ECHONL */| ICANON | IEXTEN);
jleave:
   tcsetattr(STDIN_FILENO, TCSADRAIN, tiosp);
   n_NYD2_OU;
}

# ifdef mx_HAVE_HISTORY
static bool_t
a_tty_hist_load(void){
   ui8_t version;
   size_t lsize, cnt, llen;
   char *lbuf, *cp;
   FILE *f;
   char const *v;
   bool_t rv;
   n_NYD_IN;

   rv = TRU1;

   if((v = a_tty_hist__query_config()) == NULL ||
         a_tty.tg_hist_size_max == 0)
      goto jleave;

   hold_all_sigs(); /* TODO too heavy, yet we may jump even here!? */
   f = fopen(v, "r"); /* TODO HISTFILE LOAD: use linebuf pool */
   if(f == NULL){
      int e;

      e = errno;
      n_err(_("Cannot read *history-file*=%s: %s\n"),
         n_shexp_quote_cp(v, FAL0), su_err_doc(e));
      rv = FAL0;
      goto jrele;
   }
   (void)n_file_lock(fileno(f), FLT_READ, 0,0, UIZ_MAX);

   /* Clear old history */
   /* C99 */{
      struct a_tty_hist *thp;

      while((thp = a_tty.tg_hist) != NULL){
         a_tty.tg_hist = thp->th_older;
         n_free(thp);
      }
      a_tty.tg_hist_tail = NULL;
      a_tty.tg_hist_size = 0;
   }

   lbuf = NULL;
   lsize = 0;
   cnt = (size_t)fsize(f);
   version = 0;

   while(fgetline(&lbuf, &lsize, &cnt, &llen, f, FAL0) != NULL){
      cp = lbuf;
      /* Hand-edited history files may have this, probably */
      while(llen > 0 && su_cs_is_space(cp[0])){
         ++cp;
         --llen;
      }
      if(llen > 0 && cp[llen - 1] == '\n')
         cp[--llen] = '\0';

      /* Skip empty and comment lines */
      if(llen == 0 || cp[0] == '#')
         continue;

      if(n_UNLIKELY(version == 0) &&
            (version = su_cs_cmp(cp, a_TTY_HIST_MARKER) ? 1 : 2) != 1)
         continue;

      /* C99 */{
         enum n_go_input_flags gif;

         if(version == 2){
            if(llen <= 2){
               /* XXX n_err(_("Skipped invalid *history-file* entry: %s\n"),
                * XXX  n_shexp_quote_cp(cp));*/
               continue;
            }
            switch(*cp++){
            default:
            case 'd':
               gif = n_GO_INPUT_CTX_DEFAULT; /* == a_TTY_HIST_CTX_DEFAULT */
               break;
            case 'c':
               gif = n_GO_INPUT_CTX_COMPOSE; /* == a_TTY_HIST_CTX_COMPOSE */
               break;
            }

            if(*cp++ == '*')
               gif |= n_GO_INPUT_HIST_GABBY;

            while(*cp == ' ')
               ++cp;
         }else{
            gif = n_GO_INPUT_CTX_DEFAULT;
            if(cp[0] == '*'){
               ++cp;
               gif |= n_GO_INPUT_HIST_GABBY;
            }
         }

         a_tty_hist_add(cp, gif);
      }
   }

   if(lbuf != NULL)
      n_free(lbuf);

   fclose(f);
jrele:
   rele_all_sigs(); /* XXX remove jumps */
jleave:
   n_NYD_OU;
   return rv;
}

static bool_t
a_tty_hist_save(void){
   size_t i;
   struct a_tty_hist *thp;
   FILE *f;
   char const *v;
   bool_t rv, dogabby;
   n_NYD_IN;

   rv = TRU1;

   if((v = a_tty_hist__query_config()) == NULL ||
         a_tty.tg_hist_size_max == 0)
      goto jleave;

   dogabby = ok_blook(history_gabby_persist);

   if((thp = a_tty.tg_hist) != NULL)
      for(i = a_tty.tg_hist_size_max; thp->th_older != NULL;
            thp = thp->th_older)
         if((dogabby || !(thp->th_flags & a_TTY_HIST_GABBY)) && --i == 0)
            break;

   hold_all_sigs(); /* TODO too heavy, yet we may jump even here!? */
   if((f = fopen(v, "w")) == NULL){ /* TODO temporary + rename?! */
      int e;

      e = errno;
      n_err(_("Cannot write *history-file*=%s: %s\n"),
         n_shexp_quote_cp(v, FAL0), su_err_doc(e));
      rv = FAL0;
      goto jrele;
   }
   (void)n_file_lock(fileno(f), FLT_WRITE, 0,0, UIZ_MAX);

   if(fwrite(a_TTY_HIST_MARKER "\n", sizeof *a_TTY_HIST_MARKER,
         sizeof(a_TTY_HIST_MARKER "\n") -1, f) !=
         sizeof(a_TTY_HIST_MARKER "\n") -1)
      goto jioerr;
   else for(; thp != NULL; thp = thp->th_younger){
      if(dogabby || !(thp->th_flags & a_TTY_HIST_GABBY)){
         char c;

         switch(thp->th_flags & a_TTY_HIST_CTX_MASK){
         default:
         case a_TTY_HIST_CTX_DEFAULT:
            c = 'd';
            break;
         case a_TTY_HIST_CTX_COMPOSE:
            c = 'c';
            break;
         }
         if(putc(c, f) == EOF)
            goto jioerr;

         if((thp->th_flags & a_TTY_HIST_GABBY) && putc('*', f) == EOF)
            goto jioerr;

         if(putc(' ', f) == EOF ||
               fwrite(thp->th_dat, sizeof *thp->th_dat, thp->th_len, f) !=
                  sizeof(*thp->th_dat) * thp->th_len ||
               putc('\n', f) == EOF){
jioerr:
            n_err(_("I/O error while writing *history-file* %s\n"),
               n_shexp_quote_cp(v, FAL0));
            rv = FAL0;
            break;
         }
      }
   }

   fclose(f);
jrele:
   rele_all_sigs(); /* XXX remove jumps */
jleave:
   n_NYD_OU;
   return rv;
}

static char const *
a_tty_hist__query_config(void){
   char const *rv, *cp;
   n_NYD2_IN;

   if((cp = ok_vlook(NAIL_HISTSIZE)) != NULL)
      n_OBSOLETE(_("please use *history-size* instead of *NAIL_HISTSIZE*"));
   if((rv = ok_vlook(history_size)) == NULL)
      rv = cp;
   if(rv == NULL)
      a_tty.tg_hist_size_max = UIZ_MAX;
   else
      (void)su_idec_uz_cp(&a_tty.tg_hist_size_max, rv, 10, NULL);

   if((cp = ok_vlook(NAIL_HISTFILE)) != NULL)
      n_OBSOLETE(_("please use *history-file* instead of *NAIL_HISTFILE*"));
   if((rv = ok_vlook(history_file)) == NULL)
      rv = cp;
   if(rv != NULL)
      rv = fexpand(rv, FEXP_LOCAL | FEXP_NSHELL);
   n_NYD2_OU;
   return rv;
}

static void
a_tty_hist_add(char const *s, enum n_go_input_flags gif){
   ui32_t l;
   struct a_tty_hist *thp, *othp, *ythp;
   n_NYD2_IN;

   l = (ui32_t)su_cs_len(s); /* xxx simply do not store if >= SI32_MAX */

   /* Eliminating duplicates is expensive, but simply inacceptable so
    * during the load of a potentially large history file! */
   if(n_psonce & n_PSO_LINE_EDITOR_INIT)
      for(thp = a_tty.tg_hist; thp != NULL; thp = thp->th_older)
         if(thp->th_len == l && !su_cs_cmp(thp->th_dat, s)){
            thp->th_flags = (gif & a_TTY_HIST_CTX_MASK) |
                  (gif & n_GO_INPUT_HIST_GABBY ? a_TTY_HIST_GABBY : 0);
            othp = thp->th_older;
            ythp = thp->th_younger;
            if(othp != NULL)
               othp->th_younger = ythp;
            else
               a_tty.tg_hist_tail = ythp;
            if(ythp != NULL)
               ythp->th_older = othp;
            else
               a_tty.tg_hist = othp;
            goto jleave;
         }

   if(n_LIKELY(a_tty.tg_hist_size <= a_tty.tg_hist_size_max))
      ++a_tty.tg_hist_size;
   else{
      --a_tty.tg_hist_size;
      if((thp = a_tty.tg_hist_tail) != NULL){
         if((a_tty.tg_hist_tail = thp->th_younger) == NULL)
            a_tty.tg_hist = NULL;
         else
            a_tty.tg_hist_tail->th_older = NULL;
         n_free(thp);
      }
   }

   thp = n_alloc(n_VSTRUCT_SIZEOF(struct a_tty_hist, th_dat) + l +1);
   thp->th_len = l;
   thp->th_flags = (gif & a_TTY_HIST_CTX_MASK) |
         (gif & n_GO_INPUT_HIST_GABBY ? a_TTY_HIST_GABBY : 0);
   memcpy(thp->th_dat, s, l +1);
jleave:
   if((thp->th_older = a_tty.tg_hist) != NULL)
      a_tty.tg_hist->th_younger = thp;
   else
      a_tty.tg_hist_tail = thp;
   thp->th_younger = NULL;
   a_tty.tg_hist = thp;
   n_NYD2_OU;
}
# endif /* mx_HAVE_HISTORY */

# ifdef mx_HAVE_KEY_BINDINGS
static void
a_tty_term_rawmode_timeout(struct a_tty_line *tlp, bool_t enable){
   n_NYD2_IN;
   if(enable){
      ui8_t bt;

      a_tty.tg_tios_new.c_cc[VMIN] = 0;
      if((bt = tlp->tl_bind_timeout) == 0)
         bt = a_TTY_BIND_TIMEOUT;
      a_tty.tg_tios_new.c_cc[VTIME] = bt;
   }else{
      a_tty.tg_tios_new.c_cc[VMIN] = 1;
      a_tty.tg_tios_new.c_cc[VTIME] = 0;
   }
   tcsetattr(STDIN_FILENO, TCSANOW, &a_tty.tg_tios_new);
   n_NYD2_OU;
}
# endif /* mx_HAVE_KEY_BINDINGS */

static ui8_t
a_tty_wcwidth(wchar_t wc){
   ui8_t rv;
   n_NYD2_IN;

   /* Special case the reverse solidus at first */
   if(wc == '\t')
      rv = UI8_MAX;
   else{
      int i;

# ifdef mx_HAVE_WCWIDTH
      rv = ((i = wcwidth(wc)) > 0) ? (ui8_t)i : 0;
# else
      rv = iswprint(wc) ? 1 + (wc >= 0x1100u) : 0; /* TODO use S-CText */
# endif
   }
   n_NYD2_OU;
   return rv;
}

static void
a_tty_check_grow(struct a_tty_line *tlp, ui32_t no  su_DBG_LOC_ARGS_DECL){
   ui32_t cmax;
   n_NYD2_IN;

   if(n_UNLIKELY((cmax = tlp->tl_count + no) > tlp->tl_count_max)){
      size_t i;

      i = cmax * sizeof(struct a_tty_cell) + 2 * sizeof(struct a_tty_cell);
      if(n_LIKELY(i >= *tlp->tl_x_bufsize)){
         hold_all_sigs(); /* XXX v15 drop */
         i <<= 1;
         tlp->tl_line.cbuf =
         *tlp->tl_x_buf = su_MEM_REALLOC_LOCOR(*tlp->tl_x_buf, i,
               su_DBG_LOC_ARGS_ORUSE);
         rele_all_sigs(); /* XXX v15 drop */
      }
      tlp->tl_count_max = cmax;
      *tlp->tl_x_bufsize = i;
   }
   n_NYD2_OU;
}

static ssize_t
a_tty_cell2dat(struct a_tty_line *tlp){
   size_t len, i;
   n_NYD2_IN;

   len = 0;

   if(n_LIKELY((i = tlp->tl_count) > 0)){
      struct a_tty_cell const *tcap;

      tcap = tlp->tl_line.cells;
      do{
         memcpy(tlp->tl_line.cbuf + len, tcap->tc_cbuf, tcap->tc_count);
         len += tcap->tc_count;
      }while(++tcap, --i > 0);
   }

   tlp->tl_line.cbuf[len] = '\0';
   n_NYD2_OU;
   return (ssize_t)len;
}

static void
a_tty_cell2save(struct a_tty_line *tlp){
   size_t len, i;
   struct a_tty_cell *tcap;
   n_NYD2_IN;

   tlp->tl_savec.s = NULL;
   tlp->tl_savec.l = 0;

   if(n_UNLIKELY(tlp->tl_count == 0))
      goto jleave;

   for(tcap = tlp->tl_line.cells, len = 0, i = tlp->tl_count; i > 0;
         ++tcap, --i)
      len += tcap->tc_count;

   tlp->tl_savec.s = n_autorec_alloc((tlp->tl_savec.l = len) +1);

   for(tcap = tlp->tl_line.cells, len = 0, i = tlp->tl_count; i > 0;
         ++tcap, --i){
      memcpy(tlp->tl_savec.s + len, tcap->tc_cbuf, tcap->tc_count);
      len += tcap->tc_count;
   }
   tlp->tl_savec.s[len] = '\0';
jleave:
   n_NYD2_OU;
}

static void
a_tty_copy2paste(struct a_tty_line *tlp, struct a_tty_cell *tcpmin,
      struct a_tty_cell *tcpmax){
   char *cp;
   struct a_tty_cell *tcp;
   size_t l;
   n_NYD2_IN;

   l = 0;
   for(tcp = tcpmin; tcp < tcpmax; ++tcp)
      l += tcp->tc_count;

   tlp->tl_pastebuf.s = cp = n_autorec_alloc((tlp->tl_pastebuf.l = l) +1);

   for(tcp = tcpmin; tcp < tcpmax; cp += l, ++tcp)
      memcpy(cp, tcp->tc_cbuf, l = tcp->tc_count);
   *cp = '\0';
   n_NYD2_OU;
}

static wchar_t
a_tty_vinuni(struct a_tty_line *tlp){
   char buf[16];
   uiz_t i;
   wchar_t wc;
   n_NYD2_IN;

   wc = '\0';

   if(!n_termcap_cmdx(n_TERMCAP_CMD_cr) ||
         !n_termcap_cmd(n_TERMCAP_CMD_ce, 0, -1))
      goto jleave;

   /* C99 */{
      struct str const *cpre, *csuf;

      cpre = csuf = NULL;
#ifdef mx_HAVE_COLOUR
      if(n_COLOUR_IS_ACTIVE()){
         struct n_colour_pen *cpen;

         cpen = n_colour_pen_create(n_COLOUR_ID_MLE_PROMPT, NULL);
         if((cpre = n_colour_pen_to_str(cpen)) != NULL)
            csuf = n_colour_reset_to_str();
      }
#endif
      fprintf(n_tty_fp, _("%sPlease enter Unicode code point:%s "),
         (cpre != NULL ? cpre->s : n_empty),
         (csuf != NULL ? csuf->s : n_empty));
   }
   fflush(n_tty_fp);

   buf[sizeof(buf) -1] = '\0';
   for(i = 0;;){
      if(read(STDIN_FILENO, &buf[i], 1) != 1){
         if(su_err_no() == su_ERR_INTR) /* xxx #if !SA_RESTART ? */
            continue;
         goto jleave;
      }
      if(buf[i] == '\n')
         break;
      if(!su_cs_is_xdigit(buf[i])){
         char const emsg[] = "[0-9a-fA-F]";

         n_LCTA(sizeof emsg <= sizeof(buf), "Preallocated buffer too small");
         memcpy(buf, emsg, sizeof emsg);
         goto jerr;
      }

      putc(buf[i], n_tty_fp);
      fflush(n_tty_fp);
      if(++i == sizeof buf)
         goto jerr;
   }
   buf[i] = '\0';

   if((su_idec_uz_cp(&i, buf, 16, NULL
            ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED || i > 0x10FFFF/* XXX magic; CText */){
jerr:
      n_err(_("\nInvalid input: %s\n"), buf);
      goto jleave;
   }

   wc = (wchar_t)i;
jleave:
   tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY | (wc == '\0' ? a_TTY_VF_BELL : 0);
   n_NYD2_OU;
   return wc;
}

static bool_t
a_tty_vi_refresh(struct a_tty_line *tlp){
   bool_t rv;
   n_NYD2_IN;

   if(tlp->tl_vi_flags & a_TTY_VF_BELL){
      tlp->tl_vi_flags |= a_TTY_VF_SYNC;
      if(putc('\a', n_tty_fp) == EOF)
         goto jerr;
   }

   if(tlp->tl_vi_flags & a_TTY_VF_REFRESH){
      /* kht may want to restore a cursor position after inserting some
       * data somewhere */
      if(tlp->tl_defc_cursor_byte > 0){
         size_t i, j;
         ssize_t k;

         a_tty_khome(tlp, FAL0);

         i = tlp->tl_defc_cursor_byte;
         tlp->tl_defc_cursor_byte = 0;
         for(j = 0; tlp->tl_cursor < tlp->tl_count; ++j){
            a_tty_kright(tlp);
            if((k = tlp->tl_line.cells[j].tc_count) > i)
               break;
            i -= k;
         }
      }

      if(!a_tty_vi__paint(tlp))
         goto jerr;
   }

   if(tlp->tl_vi_flags & a_TTY_VF_SYNC){
      tlp->tl_vi_flags &= ~a_TTY_VF_SYNC;
      if(fflush(n_tty_fp))
         goto jerr;
   }

   rv = TRU1;
jleave:
   tlp->tl_vi_flags &= ~a_TTY_VF_ALL_MASK;
   n_NYD2_OU;
   return rv;

jerr:
   clearerr(n_tty_fp); /* xxx I/O layer rewrite */
   n_err(_("Visual refresh failed!  Is $TERM set correctly?\n"
      "  Setting *line-editor-disable* to get us through!\n"));
   ok_bset(line_editor_disable);
   rv = FAL0;
   goto jleave;
}

static bool_t
a_tty_vi__paint(struct a_tty_line *tlp){
   enum{
      a_TRUE_RV = a_TTY__VF_LAST<<1,         /* Return value bit */
      a_HAVE_PROMPT = a_TTY__VF_LAST<<2,     /* Have a prompt */
      a_SHOW_PROMPT = a_TTY__VF_LAST<<3,     /* Shall print the prompt */
      a_MOVE_CURSOR = a_TTY__VF_LAST<<4,     /* Move visual cursor for user! */
      a_LEFT_MIN = a_TTY__VF_LAST<<5,        /* On left boundary */
      a_RIGHT_MAX = a_TTY__VF_LAST<<6,
      a_HAVE_POSITION = a_TTY__VF_LAST<<7,   /* Print the position indicator */

      /* We carry some flags over invocations (not worth a specific field) */
      a_VISIBLE_PROMPT = a_TTY__VF_LAST<<8,  /* The prompt is on the screen */
      a_PERSIST_MASK = a_VISIBLE_PROMPT,
      a__LAST = a_PERSIST_MASK
   };

   ui32_t f, w, phy_wid_base, phy_wid, phy_base, phy_cur, cnt,
      su_DBG(lstcur COMMA) cur,
      vi_left, /*vi_right,*/ phy_nxtcur;
   struct a_tty_cell const *tccp, *tcp_left, *tcp_right, *tcxp;
   n_NYD2_IN;
   n_LCTA(UICMP(64, a__LAST, <, UI32_MAX), "Flag bits excess storage type");

   f = tlp->tl_vi_flags;
   tlp->tl_vi_flags = (f & ~(a_TTY_VF_REFRESH | a_PERSIST_MASK)) |
         a_TTY_VF_SYNC;
   f |= a_TRUE_RV;
   if((w = tlp->tl_prompt_width) > 0)
      f |= a_HAVE_PROMPT;
   f |= a_HAVE_POSITION;

   /* XXX We don't have a OnTerminalResize event (see main.c) yet, so we need
    * XXX to reevaluate our circumstances over and over again */
   /* Don't display prompt or position indicator on very small screens */
   if((phy_wid_base = (ui32_t)n_scrnwidth) <= a_TTY_WIDTH_RIPOFF)
      f &= ~(a_HAVE_PROMPT | a_HAVE_POSITION);
   else{
      phy_wid_base -= a_TTY_WIDTH_RIPOFF;

      /* Disable the prompt if the screen is too small; due to lack of some
       * indicator simply add a second ripoff */
      if((f & a_HAVE_PROMPT) && w + a_TTY_WIDTH_RIPOFF >= phy_wid_base)
         f &= ~a_HAVE_PROMPT;
   }

   phy_wid = phy_wid_base;
   phy_base = 0;
   phy_cur = tlp->tl_phy_cursor;
   cnt = tlp->tl_count;
   su_DBG( lstcur = tlp->tl_lst_cursor; )

   /* XXX Assume dirty screen if shrunk */
   if(cnt < tlp->tl_lst_count)
      f |= a_TTY_VF_MOD_DIRTY;

   /* TODO Without mx_HAVE_TERMCAP, it would likely be much cheaper to simply
    * TODO always "cr + paint + ce + ch", since ce is simulated via spaces.. */

   /* Quickshot: if the line is empty, possibly print prompt and out */
   if(cnt == 0){
      /* In that special case dirty anything if it seems better */
      if((f & a_TTY_VF_MOD_CONTENT) || tlp->tl_lst_count > 0)
         f |= a_TTY_VF_MOD_DIRTY;

      if((f & a_TTY_VF_MOD_DIRTY) && phy_cur != 0){
         if(!n_termcap_cmdx(n_TERMCAP_CMD_cr))
            goto jerr;
         phy_cur = 0;
      }

      if((f & (a_TTY_VF_MOD_DIRTY | a_HAVE_PROMPT)) ==
            (a_TTY_VF_MOD_DIRTY | a_HAVE_PROMPT)){
         if(fputs(tlp->tl_prompt, n_tty_fp) == EOF)
            goto jerr;
         phy_cur = tlp->tl_prompt_width + 1;
      }

      /* May need to clear former line content */
      if((f & a_TTY_VF_MOD_DIRTY) &&
            !n_termcap_cmd(n_TERMCAP_CMD_ce, phy_cur, -1))
         goto jerr;

      tlp->tl_phy_start = tlp->tl_line.cells;
      goto jleave;
   }

   /* Try to get an idea of the visual window */

   /* Find the left visual boundary */
   phy_wid = (phy_wid >> 1) + (phy_wid >> 2);
   if((cur = tlp->tl_cursor) == cnt)
      --cur;

   w = (tcp_left = tccp = tlp->tl_line.cells + cur)->tc_width;
   if(w == UI8_MAX) /* TODO yet HT == SPACE */
      w = 1;
   while(tcp_left > tlp->tl_line.cells){
      ui16_t cw = tcp_left[-1].tc_width;

      if(cw == UI8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      if(w + cw >= phy_wid)
         break;
      w += cw;
      --tcp_left;
   }
   vi_left = w;

   /* If the left hand side of our visual viewpoint consumes less than half
    * of the screen width, show the prompt */
   if(tcp_left == tlp->tl_line.cells)
      f |= a_LEFT_MIN;

   if((f & (a_LEFT_MIN | a_HAVE_PROMPT)) == (a_LEFT_MIN | a_HAVE_PROMPT) &&
         w + tlp->tl_prompt_width < phy_wid){
      phy_base = tlp->tl_prompt_width;
      f |= a_SHOW_PROMPT;
   }

   /* Then search for right boundary.  Dependent upon n_PSO_FULLWIDTH (termcap
    * am/xn) We leave the rightmost column empty because some terminals
    * [cw]ould wrap the line if we write into that, or not.
    * TODO We do not deal with !n_TERMCAP_QUERY_sam */
   phy_wid = phy_wid_base - phy_base;
   tcp_right = tlp->tl_line.cells + cnt;

   while(&tccp[1] < tcp_right){
      ui16_t cw = tccp[1].tc_width;
      ui32_t i;

      if(cw == UI8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      i = w + cw;
      if(i > phy_wid)
         break;
      w = i;
      ++tccp;
   }
   /*vi_right = w - vi_left;*/

   /* If the complete line including prompt fits on the screen, show prompt */
   if(--tcp_right == tccp){
      f |= a_RIGHT_MAX;

      /* Since we did brute-force walk also for the left boundary we may end up
       * in a situation were anything effectively fits on the screen, including
       * the prompt that is, but where we don't recognize this since we
       * restricted the search to fit in some visual viewpoint.  Therefore try
       * again to extend the left boundary to overcome that */
      if(!(f & a_LEFT_MIN)){
         struct a_tty_cell const *tc1p = tlp->tl_line.cells;
         ui32_t vil1 = vi_left;

         assert(!(f & a_SHOW_PROMPT));
         w += tlp->tl_prompt_width;
         for(tcxp = tcp_left;;){
            ui32_t i = tcxp[-1].tc_width;

            if(i == UI8_MAX) /* TODO yet HT == SPACE */
               i = 1;
            vil1 += i;
            i += w;
            if(i > phy_wid)
               break;
            w = i;
            if(--tcxp == tc1p){
               tcp_left = tc1p;
               /*vi_left = vil1;*/
               f |= a_LEFT_MIN;
               break;
            }
         }
         /*w -= tlp->tl_prompt_width;*/
      }
   }
   tcp_right = tccp;
   tccp = tlp->tl_line.cells + cur;

   if((f & (a_LEFT_MIN | a_RIGHT_MAX | a_HAVE_PROMPT | a_SHOW_PROMPT)) ==
            (a_LEFT_MIN | a_RIGHT_MAX | a_HAVE_PROMPT) &&
         w + tlp->tl_prompt_width <= phy_wid){
      phy_wid -= (phy_base = tlp->tl_prompt_width);
      f |= a_SHOW_PROMPT;
   }

   /* Try to avoid repainting the complete line - this is possible if the
    * cursor "did not leave the screen" and the prompt status hasn't changed.
    * I.e., after clamping virtual viewpoint, compare relation to physical */
   if((f & (a_TTY_VF_MOD_SINGLE/*FIXME*/ |
            a_TTY_VF_MOD_CONTENT/* xxx */ | a_TTY_VF_MOD_DIRTY)) ||
         (tcxp = tlp->tl_phy_start) == NULL ||
         tcxp > tccp || tcxp <= tcp_right)
         f |= a_TTY_VF_MOD_DIRTY;
   else{
         f |= a_TTY_VF_MOD_DIRTY;
#if 0
      struct a_tty_cell const *tcyp;
      si32_t cur_displace;
      ui32_t phy_lmargin, phy_rmargin, fx, phy_displace;

      phy_lmargin = (fx = phy_wid) / 100;
      phy_rmargin = fx - (phy_lmargin * a_TTY_SCROLL_MARGIN_RIGHT);
      phy_lmargin *= a_TTY_SCROLL_MARGIN_LEFT;
      fx = (f & (a_SHOW_PROMPT | a_VISIBLE_PROMPT));

      if(fx == 0 || fx == (a_SHOW_PROMPT | a_VISIBLE_PROMPT)){
      }
#endif
   }
   goto jpaint;

   /* We know what we have to paint, start synchronizing */
jpaint:
   assert(phy_cur == tlp->tl_phy_cursor);
   assert(phy_wid == phy_wid_base - phy_base);
   assert(cnt == tlp->tl_count);
   assert(cnt > 0);
   assert(lstcur == tlp->tl_lst_cursor);
   assert(tccp == tlp->tl_line.cells + cur);

   phy_nxtcur = phy_base; /* FIXME only if repaint cpl. */

   /* Quickshot: is it only cursor movement within the visible screen? */
   if((f & a_TTY_VF_REFRESH) == a_TTY_VF_MOD_CURSOR){
      f |= a_MOVE_CURSOR;
      goto jcursor;
   }

   /* To be able to apply some quick jump offs, clear line if possible */
   if(f & a_TTY_VF_MOD_DIRTY){
      /* Force complete clearance and cursor reinitialization */
      if(!n_termcap_cmdx(n_TERMCAP_CMD_cr) ||
            !n_termcap_cmd(n_TERMCAP_CMD_ce, 0, -1))
         goto jerr;
      tlp->tl_phy_start = tcp_left;
      phy_cur = 0;
   }

   if((f & (a_TTY_VF_MOD_DIRTY | a_SHOW_PROMPT)) && phy_cur != 0){
      if(!n_termcap_cmdx(n_TERMCAP_CMD_cr))
         goto jerr;
      phy_cur = 0;
   }

   if(f & a_SHOW_PROMPT){
      assert(phy_base == tlp->tl_prompt_width);
      if(fputs(tlp->tl_prompt, n_tty_fp) == EOF)
         goto jerr;
      phy_cur = phy_nxtcur;
      f |= a_VISIBLE_PROMPT;
   }else
      f &= ~a_VISIBLE_PROMPT;

/* FIXME reposition cursor for paint */
   for(w = phy_nxtcur; tcp_left <= tcp_right; ++tcp_left){
      ui16_t cw;

      cw = tcp_left->tc_width;

      if(n_LIKELY(!tcp_left->tc_novis)){
         if(fwrite(tcp_left->tc_cbuf, sizeof *tcp_left->tc_cbuf,
               tcp_left->tc_count, n_tty_fp) != tcp_left->tc_count)
            goto jerr;
      }else{ /* XXX Shouldn't be here <-> CText, ui_str.c */
         char wbuf[8]; /* XXX magic */

         if(n_psonce & n_PSO_UNICODE){
            ui32_t wc;

            wc = (ui32_t)tcp_left->tc_wc;
            if((wc & ~0x1Fu) == 0)
               wc |= 0x2400;
            else if(wc == 0x7F)
               wc = 0x2421;
            else
               wc = 0x2426;
            su_utf_32_to_8(wc, wbuf);
         }else
            wbuf[0] = '?', wbuf[1] = '\0';

         if(fputs(wbuf, n_tty_fp) == EOF)
            goto jerr;
         cw = 1;
      }

      if(cw == UI8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      w += cw;
      if(tcp_left == tccp)
         phy_nxtcur = w;
      phy_cur += cw;
   }

   /* Write something position marker alike if it does not fit on screen */
   if((f & a_HAVE_POSITION) &&
         ((f & (a_LEFT_MIN | a_RIGHT_MAX)) != (a_LEFT_MIN | a_RIGHT_MAX) /*||
          ((f & a_HAVE_PROMPT) && !(f & a_SHOW_PROMPT))*/)){
# ifdef mx_HAVE_COLOUR
      char *posbuf = tlp->tl_pos_buf, *pos = tlp->tl_pos;
# else
      char posbuf[5], *pos = posbuf;

      pos[4] = '\0';
# endif

      if(phy_cur != (w = phy_wid_base) &&
            !n_termcap_cmd(n_TERMCAP_CMD_ch, phy_cur = w, 0))
         goto jerr;

      *pos++ = '|';
      if(f & a_LEFT_MIN)
         memcpy(pos, "^.+", 3);
      else if(f & a_RIGHT_MAX)
         memcpy(pos, ".+$", 3);
      else{
         /* Theoretical line length limit a_TTY_LINE_MAX, choose next power of
          * ten (10 ** 10) to represent 100 percent; we do not have a macro
          * that generates a constant, and i do not trust the standard "u type
          * suffix automatically scales": calculate the large number */
         static char const itoa[] = "0123456789";

         ui64_t const fact100 = (ui64_t)0x3B9ACA00u * 10u,
               fact = fact100 / 100;
         ui32_t i = (ui32_t)(((fact100 / cnt) * tlp->tl_cursor) / fact);
         n_LCTA(a_TTY_LINE_MAX <= SI32_MAX, "a_TTY_LINE_MAX too large");

         if(i < 10)
            pos[0] = ' ', pos[1] = itoa[i];
         else
            pos[1] = itoa[i % 10], pos[0] = itoa[i / 10];
         pos[2] = '%';
      }

      if(fputs(posbuf, n_tty_fp) == EOF)
         goto jerr;
      phy_cur += 4;
   }

   /* Users are used to see the cursor right of the point of interest, so we
    * need some further adjustments unless in special conditions.  Be aware
    * that we may have adjusted cur at the beginning, too */
   if((cur = tlp->tl_cursor) == 0)
      phy_nxtcur = phy_base;
   else if(cur != cnt){
      ui16_t cw = tccp->tc_width;

      if(cw == UI8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      phy_nxtcur -= cw;
   }

jcursor:
   if(((f & a_MOVE_CURSOR) || phy_nxtcur != phy_cur) &&
         !n_termcap_cmd(n_TERMCAP_CMD_ch, phy_cur = phy_nxtcur, 0))
      goto jerr;

jleave:
   tlp->tl_vi_flags |= (f & a_PERSIST_MASK);
   tlp->tl_lst_count = tlp->tl_count;
   tlp->tl_lst_cursor = tlp->tl_cursor;
   tlp->tl_phy_cursor = phy_cur;

   n_NYD2_OU;
   return ((f & a_TRUE_RV) != 0);
jerr:
   f &= ~a_TRUE_RV;
   goto jleave;
}

static si32_t
a_tty_wboundary(struct a_tty_line *tlp, si32_t dir){/* TODO shell token-wise */
   bool_t anynon;
   struct a_tty_cell *tcap;
   ui32_t cur, cnt;
   si32_t rv;
   n_NYD2_IN;

   assert(dir == 1 || dir == -1);

   rv = -1;
   cnt = tlp->tl_count;
   cur = tlp->tl_cursor;

   if(dir < 0){
      if(cur == 0)
         goto jleave;
   }else if(cur + 1 >= cnt)
      goto jleave;
   else
      --cnt, --cur; /* xxx Unsigned wrapping may occur (twice), then */

   for(rv = 0, tcap = tlp->tl_line.cells, anynon = FAL0;;){
      wchar_t wc;

      wc = tcap[cur += (ui32_t)dir].tc_wc;
      if(/*TODO not everywhere iswblank(wc)*/ wc == L' ' || wc == L'\t' ||
            iswpunct(wc)){
         if(anynon)
            break;
      }else
         anynon = TRU1;

      ++rv;

      if(dir < 0){
         if(cur == 0)
            break;
      }else if(cur + 1 >= cnt){
         ++rv;
         break;
      }
   }
jleave:
   n_NYD2_OU;
   return rv;
}

static void
a_tty_khome(struct a_tty_line *tlp, bool_t dobell){
   ui32_t f;
   n_NYD2_IN;

   if(n_LIKELY(tlp->tl_cursor > 0)){
      tlp->tl_cursor = 0;
      f = a_TTY_VF_MOD_CURSOR;
   }else if(dobell)
      f = a_TTY_VF_BELL;
   else
      f = a_TTY_VF_NONE;

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static void
a_tty_kend(struct a_tty_line *tlp){
   ui32_t f;
   n_NYD2_IN;

   if(n_LIKELY(tlp->tl_cursor < tlp->tl_count)){
      tlp->tl_cursor = tlp->tl_count;
      f = a_TTY_VF_MOD_CURSOR;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static void
a_tty_kbs(struct a_tty_line *tlp){
   ui32_t f, cur, cnt;
   n_NYD2_IN;

   cur = tlp->tl_cursor;
   cnt = tlp->tl_count;

   if(n_LIKELY(cur > 0)){
      tlp->tl_cursor = --cur;
      tlp->tl_count = --cnt;

      if((cnt -= cur) > 0){
         struct a_tty_cell *tcap;

         tcap = tlp->tl_line.cells + cur;
         memmove(tcap, &tcap[1], cnt *= sizeof(*tcap));
      }
      f = a_TTY_VF_MOD_CURSOR | a_TTY_VF_MOD_CONTENT;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static void
a_tty_ksnarf(struct a_tty_line *tlp, bool_t cplline, bool_t dobell){
   ui32_t i, f;
   n_NYD2_IN;

   f = a_TTY_VF_NONE;
   i = tlp->tl_cursor;

   if(cplline && i > 0){
      tlp->tl_cursor = i = 0;
      f = a_TTY_VF_MOD_CURSOR;
   }

   if(n_LIKELY(i < tlp->tl_count)){
      struct a_tty_cell *tcap;

      tcap = &tlp->tl_line.cells[0];
      a_tty_copy2paste(tlp, &tcap[i], &tcap[tlp->tl_count]);
      tlp->tl_count = i;
      f = a_TTY_VF_MOD_CONTENT;
   }else if(dobell)
      f |= a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static si32_t
a_tty_kdel(struct a_tty_line *tlp){
   ui32_t cur, cnt, f;
   si32_t i;
   n_NYD2_IN;

   cur = tlp->tl_cursor;
   cnt = tlp->tl_count;
   i = (si32_t)(cnt - cur);

   if(n_LIKELY(i > 0)){
      tlp->tl_count = --cnt;

      if(n_LIKELY(--i > 0)){
         struct a_tty_cell *tcap;

         tcap = &tlp->tl_line.cells[cur];
         memmove(tcap, &tcap[1], (ui32_t)i * sizeof(*tcap));
      }
      f = a_TTY_VF_MOD_CONTENT;
   }else if(cnt == 0 && !ok_blook(ignoreeof)){
      putc('^', n_tty_fp);
      putc('D', n_tty_fp);
      i = -1;
      f = a_TTY_VF_NONE;
   }else{
      i = 0;
      f = a_TTY_VF_BELL;
   }

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
   return i;
}

static void
a_tty_kleft(struct a_tty_line *tlp){
   ui32_t f;
   n_NYD2_IN;

   if(n_LIKELY(tlp->tl_cursor > 0)){
      --tlp->tl_cursor;
      f = a_TTY_VF_MOD_CURSOR;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static void
a_tty_kright(struct a_tty_line *tlp){
   ui32_t i;
   n_NYD2_IN;

   if(n_LIKELY((i = tlp->tl_cursor + 1) <= tlp->tl_count)){
      tlp->tl_cursor = i;
      i = a_TTY_VF_MOD_CURSOR;
   }else
      i = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= i;
   n_NYD2_OU;
}

static void
a_tty_ksnarfw(struct a_tty_line *tlp, bool_t fwd){
   struct a_tty_cell *tcap;
   ui32_t cnt, cur, f;
   si32_t i;
   n_NYD2_IN;

   if(n_UNLIKELY((i = a_tty_wboundary(tlp, (fwd ? +1 : -1))) <= 0)){
      f = (i < 0) ? a_TTY_VF_BELL : a_TTY_VF_NONE;
      goto jleave;
   }

   cnt = tlp->tl_count - (ui32_t)i;
   cur = tlp->tl_cursor;
   if(!fwd)
      cur -= (ui32_t)i;
   tcap = &tlp->tl_line.cells[cur];

   a_tty_copy2paste(tlp, &tcap[0], &tcap[i]);

   if((tlp->tl_count = cnt) != (tlp->tl_cursor = cur)){
      cnt -= cur;
      memmove(&tcap[0], &tcap[i], cnt * sizeof(*tcap)); /* FIXME*/
   }

   f = a_TTY_VF_MOD_CURSOR | a_TTY_VF_MOD_CONTENT;
jleave:
   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static void
a_tty_kgow(struct a_tty_line *tlp, si32_t dir){
   ui32_t f;
   si32_t i;
   n_NYD2_IN;

   if(n_UNLIKELY((i = a_tty_wboundary(tlp, dir)) <= 0))
      f = (i < 0) ? a_TTY_VF_BELL : a_TTY_VF_NONE;
   else{
      if(dir < 0)
         i = -i;
      tlp->tl_cursor += (ui32_t)i;
      f = a_TTY_VF_MOD_CURSOR;
   }

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static void
a_tty_kgoscr(struct a_tty_line *tlp, si32_t dir){
   ui32_t sw, i, cur, f, cnt;
   n_NYD2_IN;

   if((sw = (ui32_t)n_scrnwidth) > 2)
      sw -= 2;
   if(sw > (i = tlp->tl_prompt_width))
      sw -= i;
   cur = tlp->tl_cursor;
   f = a_TTY_VF_BELL;

   if(dir > 0){
      for(cnt = tlp->tl_count; cur < cnt && sw > 0; ++cur){
         i = tlp->tl_line.cells[cur].tc_width;
         i = n_MIN(sw, i);
         sw -= i;
      }
   }else{
       while(cur > 0 && sw > 0){
         i = tlp->tl_line.cells[--cur].tc_width;
         i = n_MIN(sw, i);
         sw -= i;
      }
   }
   if(cur != tlp->tl_cursor){
      tlp->tl_cursor = cur;
      f = a_TTY_VF_MOD_CURSOR;
   }

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
}

static bool_t
a_tty_kother(struct a_tty_line *tlp, wchar_t wc){
   /* Append if at EOL, insert otherwise;
    * since we may move around character-wise, always use a fresh ps */
   mbstate_t ps;
   struct a_tty_cell tc, *tcap;
   ui32_t f, cur, cnt;
   bool_t rv;
   n_NYD2_IN;

   rv = FAL0;
   f = a_TTY_VF_NONE;

   n_LCTA(a_TTY_LINE_MAX <= SI32_MAX, "a_TTY_LINE_MAX too large");
   if(tlp->tl_count + 1 >= a_TTY_LINE_MAX){
      n_err(_("Stop here, we can't extend line beyond size limit\n"));
      goto jleave;
   }

   /* First init a cell and see whether we'll really handle this wc */
   memset(&ps, 0, sizeof ps);
   /* C99 */{
      size_t l;

      l = wcrtomb(tc.tc_cbuf, tc.tc_wc = wc, &ps);
      if(n_UNLIKELY(l > MB_LEN_MAX)){
jemb:
         n_err(_("wcrtomb(3) error: too many multibyte character bytes\n"));
         goto jleave;
      }
      tc.tc_count = (ui16_t)l;

      if(n_UNLIKELY((n_psonce & n_PSO_ENC_MBSTATE) != 0)){
         l = wcrtomb(&tc.tc_cbuf[l], L'\0', &ps);
         if(n_LIKELY(l == 1))
            /* Only NUL terminator */;
         else if(n_LIKELY(--l < MB_LEN_MAX))
            tc.tc_count += (ui16_t)l;
         else
            goto jemb;
      }
   }

   /* Yes, we will!  Place it in the array */
   tc.tc_novis = (iswprint(wc) == 0);
   tc.tc_width = a_tty_wcwidth(wc);
   /* TODO if(tc.tc_novis && tc.tc_width > 0) */

   cur = tlp->tl_cursor++;
   cnt = tlp->tl_count++ - cur;
   tcap = &tlp->tl_line.cells[cur];
   if(cnt >= 1){
      memmove(&tcap[1], tcap, cnt * sizeof(*tcap));
      f = a_TTY_VF_MOD_CONTENT;
   }else
      f = a_TTY_VF_MOD_SINGLE;
   memcpy(tcap, &tc, sizeof *tcap);

   f |= a_TTY_VF_MOD_CURSOR;
   rv = TRU1;
jleave:
   if(!rv)
      f |= a_TTY_VF_BELL;
   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
   return rv;
}

static ui32_t
a_tty_kht(struct a_tty_line *tlp){
   struct su_mem_bag *membag, *membag_persist, membag__buf[1];
   struct stat sb;
   struct str orig, bot, topp, sub, exp, preexp;
   struct n_string shou, *shoup;
   struct a_tty_cell *ctop, *cx;
   bool_t wedid, set_savec;
   ui32_t rv, f;
   n_NYD2_IN;

   /* Get plain line data; if this is the first expansion/xy, update the
    * very original content so that ^G gets the origin back */
   orig = tlp->tl_savec;
   a_tty_cell2save(tlp);
   exp = tlp->tl_savec;
   if(orig.s != NULL){
      /*tlp->tl_savec = orig;*/
      set_savec = FAL0;
   }else
      set_savec = TRU1;
   orig = exp;

   membag = su_mem_bag_create(&membag__buf[0], 0);
   membag_persist = su_mem_bag_top(n_go_data->gdc_membag);
   su_mem_bag_push(n_go_data->gdc_membag, membag);

   shoup = n_string_creat_auto(&shou);
   f = a_TTY_VF_NONE;

   /* C99 */{
      size_t max;
      struct a_tty_cell *cword;

      /* Find the word to be expanded */
      cword = tlp->tl_line.cells;
      ctop = &cword[tlp->tl_cursor];
      cx = &cword[tlp->tl_count];

      /* topp: separate data right of cursor */
      if(cx > ctop){
         for(rv = 0; ctop < cx; ++ctop)
            rv += ctop->tc_count;
         topp.l = rv;
         topp.s = orig.s + orig.l - rv;
         ctop = cword + tlp->tl_cursor;
      }else
         topp.s = NULL, topp.l = 0;

      /* Find the shell token that corresponds to the cursor position */
      max = 0;
      if(ctop > cword){
         for(; cword < ctop; ++cword)
            max += cword->tc_count;
      }
      bot = sub = orig;
      bot.l = 0;
      sub.l = max;

      if(max > 0){
         for(;;){
            enum n_shexp_state shs;

            exp = sub;
            shs = n_shexp_parse_token((n_SHEXP_PARSE_DRYRUN |
                  n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_IGNORE_EMPTY |
                  n_SHEXP_PARSE_QUOTE_AUTO_CLOSE), NULL, &sub, NULL);
            if(sub.l != 0){
               size_t x;

               assert(max >= sub.l);
               x = max - sub.l;
               bot.l += x;
               max -= x;
               continue;
            }
            if(shs & n_SHEXP_STATE_ERR_MASK){
               n_err(_("Invalid completion pattern: %.*s\n"),
                  (int)exp.l, exp.s);
               f |= a_TTY_VF_BELL;
               goto jnope;
            }

            /* All WS?  Trailing WS that has been "jumped over"? */
            if(exp.l == 0 || (shs & n_SHEXP_STATE_WS_TRAIL))
               break;

            n_shexp_parse_token((n_SHEXP_PARSE_TRIM_SPACE |
                  n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_CLOSE),
                  shoup, &exp, NULL);
            break;
         }

         sub.s = n_string_cp(shoup);
         sub.l = shoup->s_len;
      }
   }

   /* Leave room for "implicit asterisk" expansion, as below */
   if(sub.l == 0){
      sub.s = n_UNCONST(n_star);
      sub.l = sizeof(n_star) -1;
   }

   preexp.s = n_UNCONST(n_empty);
   preexp.l = sizeof(n_empty) -1;
   wedid = FAL0;
jredo:
   /* TODO Super-Heavy-Metal: block all sigs, avoid leaks on jump */
   hold_all_sigs();
   exp.s = fexpand(sub.s, a_TTY_TAB_FEXP_FL);
   rele_all_sigs();

   if(exp.s == NULL || (exp.l = su_cs_len(exp.s)) == 0){
      if(wedid < FAL0)
         goto jnope;
      /* No.  But maybe the users' desire was to complete only a part of the
       * shell token of interest!  TODO This can be improved, we would need to
       * TODO have shexp_parse to create a DOM structure of parsed snippets, so
       * TODO that we can tell for each snippet which quote is active and
       * TODO whether we may cross its boundary and/or apply expansion for it!
       * TODO Only like that we would be able to properly requote user input
       * TODO like "'['a-z]<TAB>" to e.g. "\[a-z]" for glob purposes! */
      if(wedid == TRU1){
         size_t i, li;

         wedid = TRUM1;
         for(li = UIZ_MAX, i = sub.l;;){
            char c;

            if(--i == 0)
               goto jnope;
            if((c = sub.s[i]) == '/')
               li = i;
            else if((c == '+' /* *folder*! */ || c == '&' /* *MBOX* */ ||
                     c == '%' /* $MAIL or *inbox* */) &&
                  i == sub.l - 1){
               li = i;
               break;
            }
            /* Do stop once some "magic" characters are seen XXX magic set */
            else if(c == '<' || c == '>' || c == '=' || c == ':' ||
                  su_cs_is_space(c))
               break;
         }
         if(li == UIZ_MAX)
            goto jnope;
         preexp = sub;
         preexp.l = li;
         sub.l -= li;
         sub.s += li;
         goto jredo;
      }

      /* A different case is that the user input includes for example character
       * classes: here fexpand() will go over glob, and that will not find any
       * match, thus returning NULL; try to wildcard expand this pattern! */
jaster_check:
      if(sub.s[sub.l - 1] != '*'){
         wedid = TRU1;
         shoup = n_string_push_c(shoup, '*');
         sub.s = n_string_cp(shoup);
         sub.l = shoup->s_len;
         goto jredo;
      }
      goto jnope;
   }

   if(wedid == TRUM1 && preexp.l > 0)
      preexp.s = savestrbuf(preexp.s, preexp.l);

   /* May be multi-return! */
   if(n_pstate & n_PS_EXPAND_MULTIRESULT)
      goto jmulti;

   /* xxx That not really true since the limit counts characters not bytes */
   n_LCTA(a_TTY_LINE_MAX <= SI32_MAX, "a_TTY_LINE_MAX too large");
   if(exp.l >= a_TTY_LINE_MAX - 1 || a_TTY_LINE_MAX - 1 - exp.l < preexp.l){
      n_err(_("Tabulator expansion would extend beyond line size limit\n"));
      f |= a_TTY_VF_BELL;
      goto jnope;
   }

   /* If the expansion equals the original string, assume the user wants what
    * is usually known as tab completion, append `*' and restart */
   if(!wedid && exp.l == sub.l && !memcmp(exp.s, sub.s, exp.l))
      goto jaster_check;

   if(exp.s[exp.l - 1] != '/'){
      if(!stat(exp.s, &sb) && S_ISDIR(sb.st_mode)){
         shoup = n_string_assign_buf(shoup, exp.s, exp.l);
         shoup = n_string_push_c(shoup, '/');
         exp.s = n_string_cp(shoup);
         goto jset;
      }
   }
   exp.s[exp.l] = '\0';

jset:
   exp.l = su_cs_len(exp.s = n_shexp_quote_cp(exp.s, tlp->tl_quote_rndtrip));
   tlp->tl_defc_cursor_byte = bot.l + preexp.l + exp.l -1;

   orig.l = bot.l + preexp.l + exp.l + topp.l;
   su_mem_bag_push(n_go_data->gdc_membag, membag_persist);
   orig.s = su_MEM_BAG_SELF_AUTO_ALLOC(orig.l + 5 +1);
   su_mem_bag_pop(n_go_data->gdc_membag, membag_persist);
   if((rv = (ui32_t)bot.l) > 0)
      memcpy(orig.s, bot.s, rv);
   if(preexp.l > 0){
      memcpy(&orig.s[rv], preexp.s, preexp.l);
      rv += preexp.l;
   }
   memcpy(&orig.s[rv], exp.s, exp.l);
   rv += exp.l;
   if(topp.l > 0){
      memcpy(&orig.s[rv], topp.s, topp.l);
      rv += topp.l;
   }
   orig.s[rv] = '\0';

   tlp->tl_defc = orig;
   tlp->tl_count = tlp->tl_cursor = 0;
   f |= a_TTY_VF_MOD_DIRTY;
jleave:
   su_mem_bag_pop(n_go_data->gdc_membag, membag);
   su_mem_bag_gut(membag);
   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
   return rv;

jmulti:{
      struct n_visual_info_ctx vic;
      struct str input;
      wc_t c2, c1;
      bool_t isfirst;
      char const *lococp;
      size_t locolen, scrwid, lnlen, lncnt, prefixlen;
      FILE *fp;

      if((fp = Ftmp(NULL, "tabex", OF_RDWR | OF_UNLINK | OF_REGISTER)
            ) == NULL){
         n_perr(_("tmpfile"), 0);
         fp = n_tty_fp;
      }

      /* How long is the result string for real?  Search the NUL NUL
       * terminator.  While here, detect the longest entry to perform an
       * initial allocation of our accumulator string */
      locolen = preexp.l;
      do{
         size_t i;

         i = su_cs_len(&exp.s[++exp.l]);
         locolen = n_MAX(locolen, i);
         exp.l += i;
      }while(exp.s[exp.l + 1] != '\0');

      shoup = n_string_reserve(n_string_trunc(shoup, 0),
            locolen + (locolen >> 1));

      /* Iterate (once again) over all results */
      scrwid = n_SCRNWIDTH_FOR_LISTS;
      lnlen = lncnt = 0;
      n_UNINIT(prefixlen, 0);
      n_UNINIT(lococp, NULL);
      n_UNINIT(c1, '\0');
      for(isfirst = TRU1; exp.l > 0; isfirst = FAL0, c1 = c2){
         size_t i;
         char const *fullpath;

         /* Next result */
         sub = exp;
         sub.l = i = su_cs_len(sub.s);
         assert(exp.l >= i);
         if((exp.l -= i) > 0)
            --exp.l;
         exp.s += ++i;

         /* Separate dirname and basename */
         fullpath = sub.s;
         if(isfirst){
            char const *cp;

            if((cp = su_cs_rfind_c(fullpath, '/')) != NULL)
               prefixlen = PTR2SIZE(++cp - fullpath);
            else
               prefixlen = 0;
         }
         if(prefixlen > 0 && prefixlen < sub.l){
            sub.l -= prefixlen;
            sub.s += prefixlen;
         }

         /* We want case-insensitive sort-order */
         memset(&vic, 0, sizeof vic);
         vic.vic_indat = sub.s;
         vic.vic_inlen = sub.l;
         c2 = n_visual_info(&vic, n_VISUAL_INFO_ONE_CHAR) ? vic.vic_waccu
               : (ui8_t)*sub.s;
#ifdef mx_HAVE_C90AMEND1
         c2 = towlower(c2);
#else
         c2 = su_cs_to_lower(c2);
#endif

         /* Query longest common prefix along the way */
         if(isfirst){
            c1 = c2;
            lococp = sub.s;
            locolen = sub.l;
         }else if(locolen > 0){
            for(i = 0; i < locolen; ++i)
               if(lococp[i] != sub.s[i]){
                  i = field_detect_clip(i, lococp, i);
                  locolen = i;
                  break;
               }
         }

         /* Prepare display */
         input = sub;
         shoup = n_shexp_quote(n_string_trunc(shoup, 0), &input,
               tlp->tl_quote_rndtrip);
         memset(&vic, 0, sizeof vic);
         vic.vic_indat = shoup->s_dat;
         vic.vic_inlen = shoup->s_len;
         if(!n_visual_info(&vic,
               n_VISUAL_INFO_SKIP_ERRORS | n_VISUAL_INFO_WIDTH_QUERY))
            vic.vic_vi_width = shoup->s_len;

         /* Put on screen.  Indent follow lines of same sort slot.
          * Leave enough room for filename tagging */
         if((c1 = (c1 != c2))){
#ifdef mx_HAVE_C90AMEND1
            c1 = (iswalnum(c2) != 0);
#else
            c1 = (su_cs_is_alnum(c2) != 0);
#endif
         }
         if(isfirst || c1 ||
               scrwid < lnlen || scrwid - lnlen <= vic.vic_vi_width + 2){
            putc('\n', fp);
            if(scrwid < lnlen)
               ++lncnt;
            ++lncnt, lnlen = 0;
            if(!isfirst && !c1)
               goto jsep;
         }else if(lnlen > 0){
jsep:
            fputs("  ", fp);
            lnlen += 2;
         }
         fputs(n_string_cp(shoup), fp);
         lnlen += vic.vic_vi_width;

         /* Support the known filename tagging
          * XXX *line-editor-completion-filetype* or so */
         if(!lstat(fullpath, &sb)){
            char c = '\0';

            if(S_ISDIR(sb.st_mode))
               c = '/';
            else if(S_ISLNK(sb.st_mode))
               c = '@';
# ifdef S_ISFIFO
            else if(S_ISFIFO(sb.st_mode))
               c = '|';
# endif
# ifdef S_ISSOCK
            else if(S_ISSOCK(sb.st_mode))
               c = '=';
# endif
# ifdef S_ISCHR
            else if(S_ISCHR(sb.st_mode))
               c = '%';
# endif
# ifdef S_ISBLK
            else if(S_ISBLK(sb.st_mode))
               c = '#';
# endif

            if(c != '\0'){
               putc(c, fp);
               ++lnlen;
            }
         }
      }
      putc('\n', fp);
      ++lncnt;

      page_or_print(fp, lncnt);
      if(fp != n_tty_fp)
         Fclose(fp);

      n_string_gut(shoup);

      /* A common prefix of 0 means we cannot provide the user any auto
       * completed characters for the multiple possible results.
       * Otherwise we can, so extend the visual line content by the common
       * prefix (in a reversible way) */
      f |= a_TTY_VF_BELL; /* XXX -> *line-editor-completion-bell*? or so */
      if(locolen > 0){
         (exp.s = n_UNCONST(lococp))[locolen] = '\0';
         exp.s -= prefixlen;
         exp.l = (locolen += prefixlen);
         goto jset;
      }
   }

jnope:
   /* If we've provided a default content, but failed to expand, there is
    * nothing we can "revert to": drop that default again */
   if(set_savec){
      tlp->tl_savec.s = NULL;
      tlp->tl_savec.l = 0;
   }
   f &= a_TTY_VF_BELL; /* XXX -> *line-editor-completion-bell*? or so */
   rv = 0;
   goto jleave;
}

# ifdef mx_HAVE_HISTORY
static ui32_t
a_tty__khist_shared(struct a_tty_line *tlp, struct a_tty_hist *thp){
   ui32_t f, rv;
   n_NYD2_IN;

   if(n_LIKELY((tlp->tl_hist = thp) != NULL)){
      char *cp;
      size_t i;

      i = thp->th_len;
      if(tlp->tl_goinflags & n_GO_INPUT_CTX_COMPOSE){
         ++i;
         if(!(thp->th_flags & a_TTY_HIST_CTX_COMPOSE))
            ++i;
      }
      tlp->tl_defc.s = cp = n_autorec_alloc(i +1);
      if(tlp->tl_goinflags & n_GO_INPUT_CTX_COMPOSE){
         if((*cp = ok_vlook(escape)[0]) == '\0')
            *cp = n_ESCAPE[0];
         ++cp;
         if(!(thp->th_flags & a_TTY_HIST_CTX_COMPOSE))
            *cp++ = ':';
      }
      memcpy(cp, thp->th_dat, thp->th_len +1);
      rv = tlp->tl_defc.l = i;

      f = (tlp->tl_count > 0) ? a_TTY_VF_MOD_DIRTY : a_TTY_VF_NONE;
      tlp->tl_count = tlp->tl_cursor = 0;
   }else{
      f = a_TTY_VF_BELL;
      rv = UI32_MAX;
   }

   tlp->tl_vi_flags |= f;
   n_NYD2_OU;
   return rv;
}

static ui32_t
a_tty_khist(struct a_tty_line *tlp, bool_t fwd){
   struct a_tty_hist *thp;
   ui32_t rv;
   n_NYD2_IN;

   /* If we're not in history mode yet, save line content */
   if((thp = tlp->tl_hist) == NULL){
      a_tty_cell2save(tlp);
      if((thp = a_tty.tg_hist) == NULL)
         goto jleave;
      if(fwd)
         while(thp->th_older != NULL)
            thp = thp->th_older;
      goto jleave;
   }

   while((thp = fwd ? thp->th_younger : thp->th_older) != NULL){
      /* Applicable to input context?  Compose mode swallows anything */
      if((tlp->tl_goinflags & n__GO_INPUT_CTX_MASK) == n_GO_INPUT_CTX_COMPOSE)
         break;
      if((thp->th_flags & a_TTY_HIST_CTX_MASK) != a_TTY_HIST_CTX_COMPOSE)
         break;
   }
jleave:
   rv = a_tty__khist_shared(tlp, thp);
   n_NYD2_OU;
   return rv;
}

static ui32_t
a_tty_khist_search(struct a_tty_line *tlp, bool_t fwd){
   struct str orig_savec;
   ui32_t xoff, rv;
   struct a_tty_hist *thp;
   n_NYD2_IN;

   thp = NULL;

   /* We cannot complete an empty line */
   if(n_UNLIKELY(tlp->tl_count == 0)){
      /* XXX The upcoming hard reset would restore a set savec buffer,
       * XXX so forcefully reset that.  A cleaner solution would be to
       * XXX reset it whenever a restore is no longer desired */
      tlp->tl_savec.s = NULL;
      tlp->tl_savec.l = 0;
      goto jleave;
   }

   /* xxx It is a bit of a hack, but let's just hard-code the knowledge that
    * xxx in compose mode the first character is *escape* and must be skipped
    * xxx when searching the history.  Alternatively we would need to have
    * xxx context-specific history search hooks which would do the search,
    * xxx which is overkill for our sole special case compose mode */
   if((tlp->tl_goinflags & n__GO_INPUT_CTX_MASK) == n_GO_INPUT_CTX_COMPOSE)
      xoff = 1;
   else
      xoff = 0;

   if((thp = tlp->tl_hist) == NULL){
      a_tty_cell2save(tlp);
      if((thp = a_tty.tg_hist) == NULL) /* TODO Should end "doing nothing"! */
         goto jleave;
      if(fwd)
         while(thp->th_older != NULL)
            thp = thp->th_older;
      orig_savec.s = NULL;
      orig_savec.l = 0; /* silence CC */
   }else{
      while((thp = fwd ? thp->th_younger : thp->th_older) != NULL){
         /* Applicable to input context?  Compose mode swallows anything */
         if(xoff != 0)
            break;
         if((thp->th_flags & a_TTY_HIST_CTX_MASK) != a_TTY_HIST_CTX_COMPOSE)
            break;
      }
      if(thp == NULL)
         goto jleave;

      orig_savec = tlp->tl_savec;
   }

   if(xoff >= tlp->tl_savec.l){
      thp = NULL;
      goto jleave;
   }

   if(orig_savec.s == NULL)
      a_tty_cell2save(tlp);

   for(; thp != NULL; thp = fwd ? thp->th_younger : thp->th_older){
      /* Applicable to input context?  Compose mode swallows anything */
      if(xoff != 1 && (thp->th_flags & a_TTY_HIST_CTX_MASK) ==
            a_TTY_HIST_CTX_COMPOSE)
         continue;
      if(su_cs_starts_with(thp->th_dat, &tlp->tl_savec.s[xoff]))
         break;
   }

   if(orig_savec.s != NULL)
      tlp->tl_savec = orig_savec;
jleave:
   rv = a_tty__khist_shared(tlp, thp);
   n_NYD2_OU;
   return rv;
}
# endif /* mx_HAVE_HISTORY */

static enum a_tty_fun_status
a_tty_fun(struct a_tty_line *tlp, enum a_tty_bind_flags tbf, size_t *len){
   enum a_tty_fun_status rv;
   n_NYD2_IN;

   rv = a_TTY_FUN_STATUS_OK;
# undef a_X
# define a_X(N) a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## N)
   switch(a_TTY_BIND_FUN_REDUCE(tbf)){
   case a_X(BELL):
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
      break;
   case a_X(GO_BWD):
      a_tty_kleft(tlp);
      break;
   case a_X(GO_FWD):
      a_tty_kright(tlp);
      break;
   case a_X(GO_WORD_BWD):
      a_tty_kgow(tlp, -1);
      break;
   case a_X(GO_WORD_FWD):
      a_tty_kgow(tlp, +1);
      break;
   case a_X(GO_SCREEN_BWD):
      a_tty_kgoscr(tlp, -1);
      break;
   case a_X(GO_SCREEN_FWD):
      a_tty_kgoscr(tlp, +1);
      break;
   case a_X(GO_HOME):
      a_tty_khome(tlp, TRU1);
      break;
   case a_X(GO_END):
      a_tty_kend(tlp);
      break;
   case a_X(DEL_BWD):
      a_tty_kbs(tlp);
      break;
   case a_X(DEL_FWD):
      if(a_tty_kdel(tlp) < 0)
         rv = a_TTY_FUN_STATUS_END;
      break;
   case a_X(SNARF_WORD_BWD):
      a_tty_ksnarfw(tlp, FAL0);
      break;
   case a_X(SNARF_WORD_FWD):
      a_tty_ksnarfw(tlp, TRU1);
      break;
   case a_X(SNARF_END):
      a_tty_ksnarf(tlp, FAL0, TRU1);
      break;
   case a_X(SNARF_LINE):
      a_tty_ksnarf(tlp, TRU1, (tlp->tl_count == 0));
      break;

   case a_X(HIST_FWD):{
# ifdef mx_HAVE_HISTORY
         bool_t isfwd = TRU1;

         if(0){
# endif
      /* FALLTHRU */
   case a_X(HIST_BWD):
# ifdef mx_HAVE_HISTORY
            isfwd = FAL0;
         }
         if((*len = a_tty_khist(tlp, isfwd)) != UI32_MAX){
            rv = a_TTY_FUN_STATUS_RESTART;
            break;
         }
         goto jreset;
# endif
      }
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
      break;

   case a_X(HIST_SRCH_FWD):{
# ifdef mx_HAVE_HISTORY
      bool_t isfwd = TRU1;

      if(0){
# endif
      /* FALLTHRU */
   case a_X(HIST_SRCH_BWD):
# ifdef mx_HAVE_HISTORY
         isfwd = FAL0;
      }
      if((*len = a_tty_khist_search(tlp, isfwd)) != UI32_MAX){
         rv = a_TTY_FUN_STATUS_RESTART;
         break;
      }
      goto jreset;
# else
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
# endif
      }break;

   case a_X(REPAINT):
      tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
      break;
   case a_X(QUOTE_RNDTRIP):
      tlp->tl_quote_rndtrip = !tlp->tl_quote_rndtrip;
      break;
   case a_X(PROMPT_CHAR):{
      wchar_t wc;

      if((wc = a_tty_vinuni(tlp)) > 0)
         a_tty_kother(tlp, wc);
      }break;
   case a_X(COMPLETE):
      if((*len = a_tty_kht(tlp)) > 0)
         rv = a_TTY_FUN_STATUS_RESTART;
      break;

   case a_X(PASTE):
      if(tlp->tl_pastebuf.l > 0)
         *len = (tlp->tl_defc = tlp->tl_pastebuf).l;
      else
         tlp->tl_vi_flags |= a_TTY_VF_BELL;
      break;

   case a_X(CLEAR_SCREEN):
      tlp->tl_vi_flags |= (n_termcap_cmdx(n_TERMCAP_CMD_cl) == TRU1)
            ? a_TTY_VF_MOD_DIRTY : a_TTY_VF_BELL;
      break;

   case a_X(CANCEL):
      /* Normally this just causes a restart and thus resets the state
       * machine  */
      if(tlp->tl_savec.l == 0 && tlp->tl_defc.l == 0){
      }
# ifdef mx_HAVE_KEY_BINDINGS
      tlp->tl_bind_takeover = '\0';
# endif
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
      rv = a_TTY_FUN_STATUS_RESTART;
      break;

   case a_X(RESET):
      if(tlp->tl_count == 0 && tlp->tl_savec.l == 0 && tlp->tl_defc.l == 0){
# ifdef mx_HAVE_KEY_BINDINGS
         tlp->tl_bind_takeover = '\0';
# endif
         tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY | a_TTY_VF_BELL;
         break;
      }else if(0){
   case a_X(FULLRESET):
         tlp->tl_savec.s = tlp->tl_defc.s = NULL;
         tlp->tl_savec.l = tlp->tl_defc.l = 0;
         tlp->tl_defc_cursor_byte = 0;
         tlp->tl_vi_flags |= a_TTY_VF_BELL;
      }
jreset:
# ifdef mx_HAVE_KEY_BINDINGS
      tlp->tl_bind_takeover = '\0';
# endif
      tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
      tlp->tl_cursor = tlp->tl_count = 0;
# ifdef mx_HAVE_HISTORY
      tlp->tl_hist = NULL;
# endif
      if((*len = tlp->tl_savec.l) != 0){
         tlp->tl_defc = tlp->tl_savec;
         tlp->tl_savec.s = NULL;
         tlp->tl_savec.l = 0;
      }else
         *len = tlp->tl_defc.l;
      rv = a_TTY_FUN_STATUS_RESTART;
      break;

   default:
   case a_X(COMMIT):
      rv = a_TTY_FUN_STATUS_COMMIT;
      break;
   }
# undef a_X

   n_NYD2_OU;
   return rv;
}

static ssize_t
a_tty_readline(struct a_tty_line *tlp, size_t len, bool_t *histok_or_null
      su_DBG_LOC_ARGS_DECL){
   /* We want to save code, yet we may have to incorporate a lines'
    * default content and / or default input to switch back to after some
    * history movement; let "len > 0" mean "have to display some data
    * buffer" -> a_BUFMODE, and only otherwise read(2) it */
   mbstate_t ps[2];
   char cbuf_base[MB_LEN_MAX * 2], *cbuf, *cbufp;
   ssize_t rv;
   struct a_tty_bind_tree *tbtp;
   wchar_t wc;
   enum a_tty_bind_flags tbf;
   enum {a_NONE, a_WAS_HERE = 1<<0, a_BUFMODE = 1<<1, a_MAYBEFUN = 1<<2,
      a_TIMEOUT = 1<<3, a_TIMEOUT_EXPIRED = 1<<4,
         a_TIMEOUT_MASK = a_TIMEOUT | a_TIMEOUT_EXPIRED,
      a_READ_LOOP_MASK = ~(a_WAS_HERE | a_MAYBEFUN | a_TIMEOUT_MASK)
   } flags;
   n_NYD_IN;

   n_UNINIT(rv, 0);
# ifdef mx_HAVE_KEY_BINDINGS
   assert(tlp->tl_bind_takeover == '\0');
# endif
jrestart:
   memset(ps, 0, sizeof ps);
   flags = a_NONE;
   tlp->tl_vi_flags |= a_TTY_VF_REFRESH | a_TTY_VF_SYNC;

jinput_loop:
   for(;;){
      if(len != 0)
         flags |= a_BUFMODE;

      /* Ensure we have valid pointers, and room for grow */
      a_tty_check_grow(tlp, ((flags & a_BUFMODE) ? (ui32_t)len : 1)
         su_DBG_LOC_ARGS_USE);

      /* Handle visual state flags, except in buffer mode */
      if(!(flags & a_BUFMODE) && (tlp->tl_vi_flags & a_TTY_VF_ALL_MASK))
         if(!a_tty_vi_refresh(tlp)){
            rv = -1;
            goto jleave;
         }

      /* Ready for messing around.
       * Normal read(2)?  Else buffer mode: speed this one up */
      if(!(flags & a_BUFMODE)){
         cbufp =
         cbuf = cbuf_base;
      }else{
         assert(tlp->tl_defc.l > 0 && tlp->tl_defc.s != NULL);
         assert(tlp->tl_defc.l >= len);
         cbufp =
         cbuf = tlp->tl_defc.s + (tlp->tl_defc.l - len);
         cbufp += len;
      }

      /* Read in the next complete multibyte character */
      /* C99 */{
# ifdef mx_HAVE_KEY_BINDINGS
         struct a_tty_bind_tree *xtbtp;
         struct inseq{
            struct inseq *last;
            struct inseq *next;
            struct a_tty_bind_tree *tbtp;
         } *isp_head, *isp;

         isp_head = isp = NULL;
# endif

         for(flags &= a_READ_LOOP_MASK;;){
# ifdef mx_HAVE_KEY_BINDINGS
            if(!(flags & a_BUFMODE) && tlp->tl_bind_takeover != '\0'){
               wc = tlp->tl_bind_takeover;
               tlp->tl_bind_takeover = '\0';
            }else
# endif
            {
               if(!(flags & a_BUFMODE)){
                  /* Let me at least once dream of iomon(itor), timer with
                   * one-shot, enwrapped with key_event and key_sequence_event,
                   * all driven by an event_loop */
                  /* TODO v15 Until we have SysV signal handling all through we
                   * TODO need to temporarily adjust our BSD signal handler
                   * TODO with a SysV one, here */
                  n_sighdl_t otstp, ottin, ottou;

                  otstp = n_signal(SIGTSTP, &a_tty_signal);
                  ottin = n_signal(SIGTTIN, &a_tty_signal);
                  ottou = n_signal(SIGTTOU, &a_tty_signal);
# ifdef mx_HAVE_KEY_BINDINGS
                  flags &= ~a_TIMEOUT_MASK;
                  if(isp != NULL && (tbtp = isp->tbtp)->tbt_isseq &&
                        !tbtp->tbt_isseq_trail){
                     a_tty_term_rawmode_timeout(tlp, TRU1);
                     flags |= a_TIMEOUT;
                  }
# endif

                  while((rv = read(STDIN_FILENO, cbufp, 1)) < 1){
                     if(rv == -1){
                        if(su_err_no() == su_ERR_INTR){
                           if((tlp->tl_vi_flags & a_TTY_VF_MOD_DIRTY) &&
                                 !a_tty_vi_refresh(tlp))
                              break;
                           continue;
                        }
                        break;
                     }

# ifdef mx_HAVE_KEY_BINDINGS
                     /* Timeout expiration */
                     if(rv == 0){
                        assert(flags & a_TIMEOUT);
                        assert(isp != NULL);
                        a_tty_term_rawmode_timeout(tlp, FAL0);

                        /* Something "atomic" broke.  Maybe the current one can
                         * also be terminated already, itself? xxx really? */
                        if((tbtp = isp->tbtp)->tbt_bind != NULL){
                           tlp->tl_bind_takeover = wc;
                           goto jhave_bind;
                        }

                        /* Or, maybe there is a second path without a timeout;
                         * this should be covered by .tbt_isseq_trail, but then
                         * again a single-layer implementation cannot "know" */
                        for(xtbtp = tbtp;
                              (xtbtp = xtbtp->tbt_sibling) != NULL;)
                           if(xtbtp->tbt_char == tbtp->tbt_char){
                              assert(!xtbtp->tbt_isseq);
                              break;
                           }
                        /* Lay down on read(2)? */
                        if(xtbtp != NULL)
                           continue;
                        goto jtake_over;
                     }
# endif /* mx_HAVE_KEY_BINDINGS */
                  }

# ifdef mx_HAVE_KEY_BINDINGS
                  if(flags & a_TIMEOUT)
                     a_tty_term_rawmode_timeout(tlp, FAL0);
# endif
                  safe_signal(SIGTSTP, otstp);
                  safe_signal(SIGTTIN, ottin);
                  safe_signal(SIGTTOU, ottou);
                  if(rv < 0)
                     goto jleave;

                  /* As a special case, simulate EOF via EOT (which can happen
                   * via type-ahead as when typing "yes\n^@" during sleep of
                   *    $ sleep 5; mail -s byjove $LOGNAME */
                  if(*cbufp == '\0'){
                     assert((n_psonce & n_PSO_INTERACTIVE) &&
                        !(n_pstate & n_PS_ROBOT));
                     *cbuf = '\x04';
                  }
                  ++cbufp;
               }

               rv = (ssize_t)mbrtowc(&wc, cbuf, PTR2SIZE(cbufp - cbuf), &ps[0]);
               if(rv <= 0){
                  /* Any error during buffer mode can only result in a hard
                   * reset;  Otherwise, if it's a hard error, or if too many
                   * redundant shift sequences overflow our buffer: perform
                   * hard reset */
                  if((flags & a_BUFMODE) || rv == -1 ||
                        sizeof cbuf_base == PTR2SIZE(cbufp - cbuf)){
                     a_tty_fun(tlp, a_TTY_BIND_FUN_FULLRESET, &len);
                     goto jrestart;
                  }
                  /* Otherwise, due to the way we deal with the buffer, we need
                   * to restore the mbstate_t from before this conversion */
                  ps[0] = ps[1];
                  continue;
               }
               cbufp = cbuf;
               ps[1] = ps[0];
            }

            /* Normal read(2)ing is subject to detection of key-bindings */
# ifdef mx_HAVE_KEY_BINDINGS
            if(!(flags & a_BUFMODE)){
               /* Check for special bypass functions before we try to embed
                * this character into the tree */
               if(su_cs_is_ascii(wc)){
                  char c;
                  char const *cp;

                  for(c = (char)wc, cp = &(*tlp->tl_bind_shcut_prompt_char)[0];
                        *cp != '\0'; ++cp){
                     if(c == *cp){
                        wc = a_tty_vinuni(tlp);
                        break;
                     }
                  }
                  if(wc == '\0'){
                     tlp->tl_vi_flags |= a_TTY_VF_BELL;
                     goto jinput_loop;
                  }
               }
               if(su_cs_is_ascii(wc))
                  flags |= a_MAYBEFUN;
               else
                  flags &= ~a_MAYBEFUN;

               /* Search for this character in the bind tree */
               tbtp = (isp != NULL) ? isp->tbtp->tbt_childs
                     : (*tlp->tl_bind_tree_hmap)[wc % HSHSIZE];
               for(; tbtp != NULL; tbtp = tbtp->tbt_sibling){
                  if(tbtp->tbt_char == wc){
                     struct inseq *nisp;

                     /* If this one cannot continue we're likely finished! */
                     if(tbtp->tbt_childs == NULL){
                        assert(tbtp->tbt_bind != NULL);
                        tbf = tbtp->tbt_bind->tbc_flags;
                        goto jmle_fun;
                     }

                     /* This needs to read more characters */
                     nisp = n_autorec_alloc(sizeof *nisp);
                     if((nisp->last = isp) == NULL)
                        isp_head = nisp;
                     else
                        isp->next = nisp;
                     nisp->next = NULL;
                     nisp->tbtp = tbtp;
                     isp = nisp;
                     flags &= ~a_WAS_HERE;
                     break;
                  }
               }
               if(tbtp != NULL)
                  continue;

               /* Was there a binding active, but couldn't be continued? */
               if(isp != NULL){
                  /* A binding had a timeout, it didn't expire, but we saw
                   * something non-expected.  Something "atomic" broke.
                   * Maybe there is a second path without a timeout, that
                   * continues like we've seen it.  I.e., it may just have been
                   * the user, typing too fast.  We definitely want to allow
                   * bindings like \e,d etc. to succeed: users are so used to
                   * them that a timeout cannot be the mechanism to catch up!
                   * A single-layer implementation cannot "know" */
                  if((tbtp = isp->tbtp)->tbt_isseq && (isp->last == NULL ||
                        !(xtbtp = isp->last->tbtp)->tbt_isseq ||
                        xtbtp->tbt_isseq_trail)){
                     for(xtbtp = (tbtp = isp->tbtp);
                           (xtbtp = xtbtp->tbt_sibling) != NULL;)
                        if(xtbtp->tbt_char == tbtp->tbt_char){
                           assert(!xtbtp->tbt_isseq);
                           break;
                        }
                     if(xtbtp != NULL){
                        isp->tbtp = xtbtp;
                        tlp->tl_bind_takeover = wc;
                        continue;
                     }
                  }

                  /* Check for CANCEL shortcut now */
                  if(flags & a_MAYBEFUN){
                     char c;
                     char const *cp;

                     for(c = (char)wc, cp = &(*tlp->tl_bind_shcut_cancel)[0];
                           *cp != '\0'; ++cp)
                        if(c == *cp){
                           tbf = a_TTY_BIND_FUN_INTERNAL |
                                 a_TTY_BIND_FUN_CANCEL;
                           goto jmle_fun;
                        }
                  }

                  /* So: maybe the current sequence can be terminated here? */
                  if((tbtp = isp->tbtp)->tbt_bind != NULL){
jhave_bind:
                     tbf = tbtp->tbt_bind->tbc_flags;
jmle_fun:
                     if(tbf & a_TTY_BIND_FUN_INTERNAL){
                        switch(a_tty_fun(tlp, tbf, &len)){
                        case a_TTY_FUN_STATUS_OK:
                           goto jinput_loop;
                        case a_TTY_FUN_STATUS_COMMIT:
                           goto jdone;
                        case a_TTY_FUN_STATUS_RESTART:
                           goto jrestart;
                        case a_TTY_FUN_STATUS_END:
                           rv = -1;
                           goto jleave;
                        }
                        assert(0);
                     }else if(tbtp->tbt_bind->tbc_flags & a_TTY_BIND_NOCOMMIT){
                        struct a_tty_bind_ctx *tbcp;

                        tbcp = tbtp->tbt_bind;
                        memcpy(tlp->tl_defc.s = n_autorec_alloc(
                              (tlp->tl_defc.l = len = tbcp->tbc_exp_len) +1),
                           tbcp->tbc_exp, tbcp->tbc_exp_len +1);
                        goto jrestart;
                     }else{
                        cbufp = tbtp->tbt_bind->tbc_exp;
                        goto jinject_input;
                     }
                  }
               }

               /* Otherwise take over all chars "as is" */
jtake_over:
               for(; isp_head != NULL; isp_head = isp_head->next)
                  if(a_tty_kother(tlp, isp_head->tbtp->tbt_char)){
                     /* FIXME */
                  }
               /* And the current one too */
               goto jkother;
            }
# endif /* mx_HAVE_KEY_BINDINGS */

            if((flags & a_BUFMODE) && (len -= (size_t)rv) == 0){
               /* Buffer mode completed */
               tlp->tl_defc.s = NULL;
               tlp->tl_defc.l = 0;
               flags &= ~a_BUFMODE;
            }
            break;
         }

# ifndef mx_HAVE_KEY_BINDINGS
         /* Don't interpret control bytes during buffer mode.
          * Otherwise, if it's a control byte check whether it is a MLE
          * function.  Remarks: initially a complete duplicate to be able to
          * switch(), later converted to simply iterate over (an #ifdef'd
          * subset of) the MLE base_tuple table in order to have "a SPOF" */
         if(cbuf == cbuf_base && su_cs_is_ascii(wc) &&
               su_cs_is_cntrl((unsigned char)wc)){
            struct a_tty_bind_builtin_tuple const *tbbtp, *tbbtp_max;
            char c;

            c = (char)wc ^ 0x40;
            tbbtp = a_tty_bind_base_tuples;
            tbbtp_max = &tbbtp[n_NELEM(a_tty_bind_base_tuples)];
jbuiltin_redo:
            for(; tbbtp < tbbtp_max; ++tbbtp){
               /* Assert default_tuple table is properly subset'ed */
               assert(tbbtp->tbdt_iskey);
               if(tbbtp->tbbt_ckey == c){
                  if(tbbtp->tbbt_exp[0] == '\0'){
                     tbf = a_TTY_BIND_FUN_EXPAND((ui8_t)tbbtp->tbbt_exp[1]);
                     switch(a_tty_fun(tlp, tbf, &len)){
                     case a_TTY_FUN_STATUS_OK:
                        goto jinput_loop;
                     case a_TTY_FUN_STATUS_COMMIT:
                        goto jdone;
                     case a_TTY_FUN_STATUS_RESTART:
                        goto jrestart;
                     case a_TTY_FUN_STATUS_END:
                        rv = -1;
                        goto jleave;
                     }
                     assert(0);
                  }else{
                     cbufp = n_UNCONST(tbbtp->tbbt_exp);
                     goto jinject_input;
                  }
               }
            }
            if(tbbtp ==
                  &a_tty_bind_base_tuples[n_NELEM(a_tty_bind_base_tuples)]){
               tbbtp = a_tty_bind_default_tuples;
               tbbtp_max = &tbbtp[n_NELEM(a_tty_bind_default_tuples)];
               goto jbuiltin_redo;
            }
         }
#  endif /* !mx_HAVE_KEY_BINDINGS */

# ifdef mx_HAVE_KEY_BINDINGS
jkother:
# endif
         if(a_tty_kother(tlp, wc)){
            /* Don't clear the history during buffer mode.. */
# ifdef mx_HAVE_HISTORY
            if(!(flags & a_BUFMODE) && cbuf == cbuf_base)
               tlp->tl_hist = NULL;
# endif
         }
      }
   }

   /* We have a completed input line, convert the struct cell data to its
    * plain character equivalent */
jdone:
   rv = a_tty_cell2dat(tlp);
jleave:
   putc('\n', n_tty_fp);
   fflush(n_tty_fp);
   n_NYD_OU;
   return rv;

jinject_input:{
   size_t i;

   hold_all_sigs(); /* XXX v15 drop */
   i = a_tty_cell2dat(tlp);
   n_go_input_inject(n_GO_INPUT_INJECT_NONE, tlp->tl_line.cbuf, i);
   i = su_cs_len(cbufp) +1;
   if(i >= *tlp->tl_x_bufsize){
      *tlp->tl_x_buf = su_MEM_REALLOC_LOCOR(*tlp->tl_x_buf, i,
               su_DBG_LOC_ARGS_ORUSE);
      *tlp->tl_x_bufsize = i;
   }
   memcpy(*tlp->tl_x_buf, cbufp, i);
   rele_all_sigs(); /* XXX v15 drop */
   if(histok_or_null != NULL)
      *histok_or_null = FAL0;
   rv = (ssize_t)--i;
   }
   goto jleave;
}

# ifdef mx_HAVE_KEY_BINDINGS
static enum n_go_input_flags
a_tty_bind_ctx_find(char const *name){
   enum n_go_input_flags rv;
   struct a_tty_input_ctx_map const *ticmp;
   n_NYD2_IN;

   ticmp = a_tty_input_ctx_maps;
   do if(!su_cs_cmp_case(ticmp->ticm_name, name)){
      rv = ticmp->ticm_ctx;
      goto jleave;
   }while(PTRCMP(++ticmp, <,
      &a_tty_input_ctx_maps[n_NELEM(a_tty_input_ctx_maps)]));

   rv = (enum n_go_input_flags)-1;
jleave:
   n_NYD2_OU;
   return rv;
}

static bool_t
a_tty_bind_create(struct a_tty_bind_parse_ctx *tbpcp, bool_t replace){
   struct a_tty_bind_ctx *tbcp;
   bool_t rv;
   n_NYD2_IN;

   rv = FAL0;

   if(!a_tty_bind_parse(TRU1, tbpcp))
      goto jleave;

   /* Since we use a single buffer for it all, need to replace as such */
   if(tbpcp->tbpc_tbcp != NULL){
      if(!replace)
         goto jleave;
      a_tty_bind_del(tbpcp);
   }else if(a_tty.tg_bind_cnt == UI32_MAX){
      n_err(_("`bind': maximum number of bindings already established\n"));
      goto jleave;
   }

   /* C99 */{
      size_t i, j;

      tbcp = n_alloc(n_VSTRUCT_SIZEOF(struct a_tty_bind_ctx, tbc__buf) +
            tbpcp->tbpc_seq_len + tbpcp->tbpc_exp.l +2 +
            tbpcp->tbpc_cnv_align_mask + 1 + tbpcp->tbpc_cnv_len);
      if(tbpcp->tbpc_ltbcp != NULL){
         tbcp->tbc_next = tbpcp->tbpc_ltbcp->tbc_next;
         tbpcp->tbpc_ltbcp->tbc_next = tbcp;
      }else{
         enum n_go_input_flags gif;

         gif = tbpcp->tbpc_flags & n__GO_INPUT_CTX_MASK;
         tbcp->tbc_next = a_tty.tg_bind[gif];
         a_tty.tg_bind[gif] = tbcp;
      }
      memcpy(tbcp->tbc_seq = &tbcp->tbc__buf[0],
         tbpcp->tbpc_seq, i = (tbcp->tbc_seq_len = tbpcp->tbpc_seq_len) +1);
      memcpy(tbcp->tbc_exp = &tbcp->tbc__buf[i],
         tbpcp->tbpc_exp.s, j = (tbcp->tbc_exp_len = tbpcp->tbpc_exp.l) +1);
      i += j;
      i = (i + tbpcp->tbpc_cnv_align_mask) & ~tbpcp->tbpc_cnv_align_mask;
      memcpy(tbcp->tbc_cnv = &tbcp->tbc__buf[i],
         tbpcp->tbpc_cnv, (tbcp->tbc_cnv_len = tbpcp->tbpc_cnv_len));
      tbcp->tbc_flags = tbpcp->tbpc_flags;
   }

   /* Directly resolve any termcap(5) symbol if we are already setup */
   if((n_psonce & n_PSO_STARTED) &&
         (tbcp->tbc_flags & (a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT)) ==
          a_TTY_BIND_RESOLVE)
      a_tty_bind_resolve(tbcp);

   ++a_tty.tg_bind_cnt;
   /* If this binding is usable invalidate the key input lookup trees */
   if(!(tbcp->tbc_flags & a_TTY_BIND_DEFUNCT))
      a_tty.tg_bind_isdirty = TRU1;
   rv = TRU1;
jleave:
   n_NYD2_OU;
   return rv;
}

static bool_t
a_tty_bind_parse(bool_t isbindcmd, struct a_tty_bind_parse_ctx *tbpcp){
   enum{a_TRUE_RV = a_TTY__BIND_LAST<<1};

   struct n_visual_info_ctx vic;
   struct str shin_save, shin;
   struct n_string shou, *shoup;
   size_t i;
   struct kse{
      struct kse *next;
      char *seq_dat;
      wc_t *cnv_dat;
      ui32_t seq_len;
      ui32_t cnv_len;      /* High bit set if a termap to be resolved */
      ui32_t calc_cnv_len; /* Ditto, but aligned etc. */
      ui8_t kse__dummy[4];
   } *head, *tail;
   ui32_t f;
   n_NYD2_IN;
   n_LCTA(UICMP(64, a_TRUE_RV, <, UI32_MAX),
      "Flag bits excess storage datatype");

   f = n_GO_INPUT_NONE;
   shoup = n_string_creat_auto(&shou);
   head = tail = NULL;

   /* Parse the key-sequence */
   for(shin.s = n_UNCONST(tbpcp->tbpc_in_seq), shin.l = UIZ_MAX;;){
      struct kse *ep;
      enum n_shexp_state shs;

      shin_save = shin;
      shs = n_shexp_parse_token((n_SHEXP_PARSE_TRUNC |
            n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_IGNORE_EMPTY |
            n_SHEXP_PARSE_IFS_IS_COMMA), shoup, &shin, NULL);
      if(shs & n_SHEXP_STATE_ERR_UNICODE){
         f |= a_TTY_BIND_DEFUNCT;
         if(isbindcmd && (n_poption & n_PO_D_V))
            n_err(_("`%s': \\uNICODE not available in locale: %s\n"),
               tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
      }
      if((shs & n_SHEXP_STATE_ERR_MASK) & ~n_SHEXP_STATE_ERR_UNICODE){
         n_err(_("`%s': failed to parse key-sequence: %s\n"),
            tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
         goto jleave;
      }
      if((shs & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_STOP)) ==
            n_SHEXP_STATE_STOP)
         break;

      ep = n_autorec_alloc(sizeof *ep);
      if(head == NULL)
         head = ep;
      else
         tail->next = ep;
      tail = ep;
      ep->next = NULL;
      if(!(shs & n_SHEXP_STATE_ERR_UNICODE)){
         i = su_cs_len(ep->seq_dat =
               n_shexp_quote_cp(n_string_cp(shoup), TRU1));
         if(i >= SI32_MAX - 1)
            goto jelen;
         ep->seq_len = (ui32_t)i;
      }else{
         /* Otherwise use the original buffer, _we_ can only quote it the wrong
          * way (e.g., an initial $'\u3a' becomes '\u3a'), _then_ */
         if((i = shin_save.l - shin.l) >= SI32_MAX - 1)
            goto jelen;
         ep->seq_len = (ui32_t)i;
         ep->seq_dat = savestrbuf(shin_save.s, i);
      }

      memset(&vic, 0, sizeof vic);
      vic.vic_inlen = shoup->s_len;
      vic.vic_indat = shoup->s_dat;
      if(!n_visual_info(&vic,
            n_VISUAL_INFO_WOUT_CREATE | n_VISUAL_INFO_WOUT_SALLOC)){
         n_err(_("`%s': key-sequence seems to contain invalid "
            "characters: %s: %s\n"),
            tbpcp->tbpc_cmd, n_string_cp(shoup), tbpcp->tbpc_in_seq);
         f |= a_TTY_BIND_DEFUNCT;
         goto jleave;
      }else if(vic.vic_woulen == 0 ||
            vic.vic_woulen >= (SI32_MAX - 2) / sizeof(wc_t)){
jelen:
         n_err(_("`%s': length of key-sequence unsupported: %s: %s\n"),
            tbpcp->tbpc_cmd, n_string_cp(shoup), tbpcp->tbpc_in_seq);
         f |= a_TTY_BIND_DEFUNCT;
         goto jleave;
      }
      ep->cnv_dat = vic.vic_woudat;
      ep->cnv_len = (ui32_t)vic.vic_woulen;

      /* A termcap(5)/terminfo(5) identifier? */
      if(ep->cnv_len > 1 && ep->cnv_dat[0] == ':'){
         i = --ep->cnv_len, ++ep->cnv_dat;
#  if 0 /* ndef mx_HAVE_TERMCAP xxx User can, via *termcap*! */
         if(n_poption & n_PO_D_V)
            n_err(_("`%s': no termcap(5)/terminfo(5) support: %s: %s\n"),
               tbpcp->tbpc_cmd, ep->seq_dat, tbpcp->tbpc_in_seq);
         f |= a_TTY_BIND_DEFUNCT;
#  endif
         if(i > a_TTY_BIND_CAPNAME_MAX){
            n_err(_("`%s': termcap(5)/terminfo(5) name too long: %s: %s\n"),
               tbpcp->tbpc_cmd, ep->seq_dat, tbpcp->tbpc_in_seq);
            f |= a_TTY_BIND_DEFUNCT;
         }
         while(i > 0)
            /* (We store it as char[]) */
            if((ui32_t)ep->cnv_dat[--i] & ~0x7Fu){
               n_err(_("`%s': invalid termcap(5)/terminfo(5) name content: "
                  "%s: %s\n"),
                  tbpcp->tbpc_cmd, ep->seq_dat, tbpcp->tbpc_in_seq);
               f |= a_TTY_BIND_DEFUNCT;
               break;
            }
         ep->cnv_len |= SI32_MIN; /* Needs resolve */
         f |= a_TTY_BIND_RESOLVE;
      }

      if(shs & n_SHEXP_STATE_STOP)
         break;
   }

   if(head == NULL){
jeempty:
      n_err(_("`%s': effectively empty key-sequence: %s\n"),
         tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
      goto jleave;
   }

   if(isbindcmd) /* (Or always, just "1st time init") */
      tbpcp->tbpc_cnv_align_mask = n_MAX(sizeof(si32_t), sizeof(wc_t)) - 1;

   /* C99 */{
      struct a_tty_bind_ctx *ltbcp, *tbcp;
      char *cpbase, *cp, *cnv;
      size_t sl, cl;

      /* Unite the parsed sequence(s) into single string representations */
      for(sl = cl = 0, tail = head; tail != NULL; tail = tail->next){
         sl += tail->seq_len + 1;

         if(!isbindcmd)
            continue;

         /* Preserve room for terminal capabilities to be resolved.
          * Above we have ensured the buffer will fit in these calculations */
         if((i = tail->cnv_len) & SI32_MIN){
            /* For now
             * struct{si32_t buf_len_iscap; si32_t cap_len; wc_t name[]+NUL;}
             * later
             * struct{si32_t buf_len_iscap; si32_t cap_len; char buf[]+NUL;} */
            n_LCTAV(n_ISPOW2(a_TTY_BIND_CAPEXP_ROUNDUP));
            n_LCTA(a_TTY_BIND_CAPEXP_ROUNDUP >= sizeof(wc_t),
               "Aligning on this constant does not properly align wc_t");
            i &= SI32_MAX;
            i *= sizeof(wc_t);
            i += sizeof(si32_t);
            if(i < a_TTY_BIND_CAPEXP_ROUNDUP)
               i = (i + (a_TTY_BIND_CAPEXP_ROUNDUP - 1)) &
                     ~(a_TTY_BIND_CAPEXP_ROUNDUP - 1);
         }else
            /* struct{si32_t buf_len_iscap; wc_t buf[]+NUL;} */
            i *= sizeof(wc_t);
         i += sizeof(si32_t) + sizeof(wc_t); /* (buf_len_iscap, NUL) */
         cl += i;
         if(tail->cnv_len & SI32_MIN){
            tail->cnv_len &= SI32_MAX;
            i |= SI32_MIN;
         }
         tail->calc_cnv_len = (ui32_t)i;
      }
      --sl;

      tbpcp->tbpc_seq_len = sl;
      tbpcp->tbpc_cnv_len = cl;
      /* C99 */{
         size_t j;

         j = i = sl + 1; /* Room for comma separator */
         if(isbindcmd){
            i = (i + tbpcp->tbpc_cnv_align_mask) & ~tbpcp->tbpc_cnv_align_mask;
            j = i;
            i += cl;
         }
         tbpcp->tbpc_seq = cp = cpbase = n_autorec_alloc(i);
         tbpcp->tbpc_cnv = cnv = &cpbase[j];
      }

      for(tail = head; tail != NULL; tail = tail->next){
         memcpy(cp, tail->seq_dat, tail->seq_len);
         cp += tail->seq_len;
         *cp++ = ',';

         if(isbindcmd){
            char * const save_cnv = cnv;

            n_UNALIGN(si32_t*,cnv)[0] = (si32_t)(i = tail->calc_cnv_len);
            cnv += sizeof(si32_t);
            if(i & SI32_MIN){
               /* For now
                * struct{si32_t buf_len_iscap; si32_t cap_len; wc_t name[];}
                * later
                * struct{si32_t buf_len_iscap; si32_t cap_len; char buf[];} */
               n_UNALIGN(si32_t*,cnv)[0] = tail->cnv_len;
               cnv += sizeof(si32_t);
            }
            i = tail->cnv_len * sizeof(wc_t);
            memcpy(cnv, tail->cnv_dat, i);
            cnv += i;
            *n_UNALIGN(wc_t*,cnv) = '\0';

            cnv = save_cnv + (tail->calc_cnv_len & SI32_MAX);
         }
      }
      *--cp = '\0';

      /* Search for a yet existing identical mapping */
      /* C99 */{
         enum n_go_input_flags gif;

         gif = tbpcp->tbpc_flags & n__GO_INPUT_CTX_MASK;

         for(ltbcp = NULL, tbcp = a_tty.tg_bind[gif]; tbcp != NULL;
               ltbcp = tbcp, tbcp = tbcp->tbc_next)
            if(tbcp->tbc_seq_len == sl && !memcmp(tbcp->tbc_seq, cpbase, sl)){
               tbpcp->tbpc_tbcp = tbcp;
               break;
            }
      }
      tbpcp->tbpc_ltbcp = ltbcp;
      tbpcp->tbpc_flags |= (f & a_TTY__BIND_MASK);
   }

   /* Create single string expansion if so desired */
   if(isbindcmd){
      char *exp;

      exp = tbpcp->tbpc_exp.s;

      i = tbpcp->tbpc_exp.l;
      if(i > 0 && exp[i - 1] == '@'){
#if 0 /* xxx no: allow trailing whitespace, as in 'echo du @' .. */
         while(--i > 0)
            if(!su_cs_is_space(exp[i - 1]))
               break;
#else
         --i;
#endif
         if(i == 0)
            goto jeempty;

         exp[tbpcp->tbpc_exp.l = i] = '\0';
         tbpcp->tbpc_flags |= a_TTY_BIND_NOCOMMIT;
      }

      /* Reverse solidus cannot be placed last in expansion to avoid (at the
       * time of this writing) possible problems with newline escaping.
       * Don't care about (un)even number thereof */
      if(i > 0 && exp[i - 1] == '\\'){
         n_err(_("`%s': reverse solidus cannot be last in expansion: %s\n"),
            tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
         goto jleave;
      }

      if(i == 0)
         goto jeempty;

      /* It may map to an internal MLE command! */
      for(i = 0; i < n_NELEM(a_tty_bind_fun_names); ++i)
         if(!su_cs_cmp_case(exp, a_tty_bind_fun_names[i])){
            tbpcp->tbpc_flags |= a_TTY_BIND_FUN_EXPAND(i) |
                  a_TTY_BIND_FUN_INTERNAL |
                  (head->next == NULL ? a_TTY_BIND_MLE1CNTRL : 0);
            if((n_poption & n_PO_D_V) &&
                  (tbpcp->tbpc_flags & a_TTY_BIND_NOCOMMIT))
               n_err(_("%s: MLE commands cannot be made editable via @: %s\n"),
                  tbpcp->tbpc_cmd, exp);
            tbpcp->tbpc_flags &= ~a_TTY_BIND_NOCOMMIT;
            break;
         }
   }

  f |= a_TRUE_RV; /* TODO because we only now true and false; DEFUNCT.. */
jleave:
   n_string_gut(shoup);
   n_NYD2_OU;
   return (f & a_TRUE_RV) != 0;
}

static void
a_tty_bind_resolve(struct a_tty_bind_ctx *tbcp){
   char capname[a_TTY_BIND_CAPNAME_MAX +1];
   struct n_termcap_value tv;
   size_t len;
   bool_t isfirst; /* TODO For now: first char must be control! */
   char *cp, *next;
   n_NYD2_IN;

   n_UNINIT(next, NULL);
   for(cp = tbcp->tbc_cnv, isfirst = TRU1, len = tbcp->tbc_cnv_len;
         len > 0; isfirst = FAL0, cp = next){
      /* C99 */{
         si32_t i, j;

         i = n_UNALIGN(si32_t*,cp)[0];
         j = i & SI32_MAX;
         next = &cp[j];
         len -= j;
         if(i == j)
            continue;

         /* struct{si32_t buf_len_iscap; si32_t cap_len; wc_t name[];} */
         cp += sizeof(si32_t);
         i = n_UNALIGN(si32_t*,cp)[0];
         cp += sizeof(si32_t);
         for(j = 0; j < i; ++j)
            capname[j] = n_UNALIGN(wc_t*,cp)[j];
         capname[j] = '\0';
      }

      /* Use generic lookup mechanism if not a known query */
      /* C99 */{
         si32_t tq;

         tq = n_termcap_query_for_name(capname, n_TERMCAP_CAPTYPE_STRING);
         if(tq == -1){
            tv.tv_data.tvd_string = capname;
            tq = n__TERMCAP_QUERY_MAX1;
         }

         if(tq < 0 || !n_termcap_query(tq, &tv)){
            if(n_poption & n_PO_D_V)
               n_err(_("`bind': unknown or unsupported capability: %s: %s\n"),
                  capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }
      }

      /* struct{si32_t buf_len_iscap; si32_t cap_len; char buf[]+NUL;} */
      /* C99 */{
         size_t i;

         i = su_cs_len(tv.tv_data.tvd_string);
         if(/*i > SI32_MAX ||*/ i >= PTR2SIZE(next - cp)){
            if(n_poption & n_PO_D_V)
               n_err(_("`bind': capability expansion too long: %s: %s\n"),
                  capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }else if(i == 0){
            if(n_poption & n_PO_D_V)
               n_err(_("`bind': empty capability expansion: %s: %s\n"),
                  capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }else if(isfirst && !su_cs_is_cntrl(*tv.tv_data.tvd_string)){
            if(n_poption & n_PO_D_V)
               n_err(_("`bind': capability expansion does not start with "
                  "control: %s: %s\n"), capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }
         n_UNALIGN(si32_t*,cp)[-1] = (si32_t)i;
         memcpy(cp, tv.tv_data.tvd_string, i);
         cp[i] = '\0';
      }
   }
   n_NYD2_OU;
}

static void
a_tty_bind_del(struct a_tty_bind_parse_ctx *tbpcp){
   struct a_tty_bind_ctx *ltbcp, *tbcp;
   n_NYD2_IN;

   tbcp = tbpcp->tbpc_tbcp;

   if((ltbcp = tbpcp->tbpc_ltbcp) != NULL)
      ltbcp->tbc_next = tbcp->tbc_next;
   else
      a_tty.tg_bind[tbpcp->tbpc_flags & n__GO_INPUT_CTX_MASK] = tbcp->tbc_next;
   n_free(tbcp);

   --a_tty.tg_bind_cnt;
   a_tty.tg_bind_isdirty = TRU1;
   n_NYD2_OU;
}

static void
a_tty_bind_tree_build(void){
   size_t i;
   n_NYD2_IN;

   for(i = 0; i < n__GO_INPUT_CTX_MAX1; ++i){
      struct a_tty_bind_ctx *tbcp;
      n_LCTAV(n_GO_INPUT_CTX_BASE == 0);

      /* Somewhat wasteful, but easier to handle: simply clone the entire
       * primary key onto the secondary one, then only modify it */
      for(tbcp = a_tty.tg_bind[n_GO_INPUT_CTX_BASE]; tbcp != NULL;
            tbcp = tbcp->tbc_next)
         if(!(tbcp->tbc_flags & a_TTY_BIND_DEFUNCT))
            a_tty__bind_tree_add(n_GO_INPUT_CTX_BASE,
               &a_tty.tg_bind_tree[i][0], tbcp);

      if(i != n_GO_INPUT_CTX_BASE)
         for(tbcp = a_tty.tg_bind[i]; tbcp != NULL; tbcp = tbcp->tbc_next)
            if(!(tbcp->tbc_flags & a_TTY_BIND_DEFUNCT))
               a_tty__bind_tree_add(i, &a_tty.tg_bind_tree[i][0], tbcp);
   }

   a_tty.tg_bind_isbuild = TRU1;
   n_NYD2_OU;
}

static void
a_tty_bind_tree_teardown(void){
   size_t i, j;
   n_NYD2_IN;

   memset(&a_tty.tg_bind_shcut_cancel[0], 0,
      sizeof(a_tty.tg_bind_shcut_cancel));
   memset(&a_tty.tg_bind_shcut_prompt_char[0], 0,
      sizeof(a_tty.tg_bind_shcut_prompt_char));

   for(i = 0; i < n__GO_INPUT_CTX_MAX1; ++i)
      for(j = 0; j < HSHSIZE; ++j)
         a_tty__bind_tree_free(a_tty.tg_bind_tree[i][j]);
   memset(&a_tty.tg_bind_tree[0], 0, sizeof(a_tty.tg_bind_tree));

   a_tty.tg_bind_isdirty = a_tty.tg_bind_isbuild = FAL0;
   n_NYD2_OU;
}

static void
a_tty__bind_tree_add(ui32_t hmap_idx, struct a_tty_bind_tree *store[HSHSIZE],
      struct a_tty_bind_ctx *tbcp){
   ui32_t cnvlen;
   char const *cnvdat;
   struct a_tty_bind_tree *ntbtp;
   n_NYD2_IN;
   n_UNUSED(hmap_idx);

   ntbtp = NULL;

   for(cnvdat = tbcp->tbc_cnv, cnvlen = tbcp->tbc_cnv_len; cnvlen > 0;){
      union {wchar_t const *wp; char const *cp;} u;
      si32_t entlen;

      /* {si32_t buf_len_iscap;} */
      entlen = *n_UNALIGN(si32_t const*,cnvdat);

      if(entlen & SI32_MIN){
         /* struct{si32_t buf_len_iscap; si32_t cap_len; char buf[]+NUL;}
          * Note that empty capabilities result in DEFUNCT */
         for(u.cp = (char const*)&n_UNALIGN(si32_t const*,cnvdat)[2];
               *u.cp != '\0'; ++u.cp)
            ntbtp = a_tty__bind_tree_add_wc(store, ntbtp, *u.cp, TRU1);
         assert(ntbtp != NULL);
         ntbtp->tbt_isseq_trail = TRU1;
         entlen &= SI32_MAX;
      }else{
         /* struct{si32_t buf_len_iscap; wc_t buf[]+NUL;} */
         bool_t isseq;

         u.wp = (wchar_t const*)&n_UNALIGN(si32_t const*,cnvdat)[1];

         /* May be a special shortcut function? */
         if(ntbtp == NULL && (tbcp->tbc_flags & a_TTY_BIND_MLE1CNTRL)){
            char *cp;
            ui32_t ctx, fun;

            ctx = tbcp->tbc_flags & n__GO_INPUT_CTX_MASK;
            fun = tbcp->tbc_flags & a_TTY__BIND_FUN_MASK;

            if(fun == a_TTY_BIND_FUN_CANCEL){
               for(cp = &a_tty.tg_bind_shcut_cancel[ctx][0];
                     PTRCMP(cp, <, &a_tty.tg_bind_shcut_cancel[ctx]
                        [n_NELEM(a_tty.tg_bind_shcut_cancel[ctx]) - 1]); ++cp)
                  if(*cp == '\0'){
                     *cp = (char)*u.wp;
                     break;
                  }
            }else if(fun == a_TTY_BIND_FUN_PROMPT_CHAR){
               for(cp = &a_tty.tg_bind_shcut_prompt_char[ctx][0];
                     PTRCMP(cp, <, &a_tty.tg_bind_shcut_prompt_char[ctx]
                        [n_NELEM(a_tty.tg_bind_shcut_prompt_char[ctx]) - 1]);
                     ++cp)
                  if(*cp == '\0'){
                     *cp = (char)*u.wp;
                     break;
                  }
            }
         }

         isseq = (u.wp[1] != '\0');
         for(; *u.wp != '\0'; ++u.wp)
            ntbtp = a_tty__bind_tree_add_wc(store, ntbtp, *u.wp, isseq);
         if(isseq){
            assert(ntbtp != NULL);
            ntbtp->tbt_isseq_trail = TRU1;
         }
      }

      cnvlen -= entlen;
      cnvdat += entlen;
   }

   /* Should have been rendered defunctional at first instead */
   assert(ntbtp != NULL);
   ntbtp->tbt_bind = tbcp;
   n_NYD2_OU;
}

static struct a_tty_bind_tree *
a_tty__bind_tree_add_wc(struct a_tty_bind_tree **treep,
      struct a_tty_bind_tree *parentp, wchar_t wc, bool_t isseq){
   struct a_tty_bind_tree *tbtp, *xtbtp;
   n_NYD2_IN;

   if(parentp == NULL){
      treep += wc % HSHSIZE;

      /* Having no parent also means that the tree slot is possibly empty */
      for(tbtp = *treep; tbtp != NULL;
            parentp = tbtp, tbtp = tbtp->tbt_sibling){
         if(tbtp->tbt_char != wc)
            continue;
         if(tbtp->tbt_isseq == isseq)
            goto jleave;
         /* isseq MUST be linked before !isseq, so record this "parent"
          * sibling, but continue searching for now.
          * Otherwise it is impossible that we'll find what we look for */
         if(isseq){
#ifdef mx_HAVE_DEBUG
            while((tbtp = tbtp->tbt_sibling) != NULL)
               assert(tbtp->tbt_char != wc);
#endif
            break;
         }
      }

      tbtp = n_alloc(sizeof *tbtp);
      memset(tbtp, 0, sizeof *tbtp);
      tbtp->tbt_char = wc;
      tbtp->tbt_isseq = isseq;

      if(parentp == NULL){
         tbtp->tbt_sibling = *treep;
         *treep = tbtp;
      }else{
         tbtp->tbt_sibling = parentp->tbt_sibling;
         parentp->tbt_sibling = tbtp;
      }
   }else{
      if((tbtp = *(treep = &parentp->tbt_childs)) != NULL){
         for(;; tbtp = xtbtp){
            if(tbtp->tbt_char == wc){
               if(tbtp->tbt_isseq == isseq)
                  goto jleave;
               /* isseq MUST be linked before, so it is impossible that we'll
                * find what we look for */
               if(isseq){
#ifdef mx_HAVE_DEBUG
                  while((tbtp = tbtp->tbt_sibling) != NULL)
                     assert(tbtp->tbt_char != wc);
#endif
                  tbtp = NULL;
                  break;
               }
            }

            if((xtbtp = tbtp->tbt_sibling) == NULL){
               treep = &tbtp->tbt_sibling;
               break;
            }
         }
      }

      xtbtp = n_alloc(sizeof *xtbtp);
      memset(xtbtp, 0, sizeof *xtbtp);
      xtbtp->tbt_parent = parentp;
      xtbtp->tbt_char = wc;
      xtbtp->tbt_isseq = isseq;
      tbtp = xtbtp;
      *treep = tbtp;
   }
jleave:
   n_NYD2_OU;
   return tbtp;
}

static void
a_tty__bind_tree_free(struct a_tty_bind_tree *tbtp){
   n_NYD2_IN;
   while(tbtp != NULL){
      struct a_tty_bind_tree *tmp;

      if((tmp = tbtp->tbt_childs) != NULL)
         a_tty__bind_tree_free(tmp);

      tmp = tbtp->tbt_sibling;
      n_free(tbtp);
      tbtp = tmp;
   }
   n_NYD2_OU;
}
# endif /* mx_HAVE_KEY_BINDINGS */

FL void
n_tty_init(void){
   n_NYD_IN;

   if(ok_blook(line_editor_disable))
      goto jleave;

   /* Load the history file */
# ifdef mx_HAVE_HISTORY
   a_tty_hist_load();
# endif

   /* Force immediate resolve for anything which follows */
   n_psonce |= n_PSO_LINE_EDITOR_INIT;

# ifdef mx_HAVE_KEY_BINDINGS
   /* `bind's (and `unbind's) done from within resource files couldn't be
    * performed for real since our termcap driver wasn't yet loaded, and we
    * can't perform automatic init since the user may have disallowed so */
   /* C99 */{ /* TODO outsource into own file */
      struct a_tty_bind_ctx *tbcp;
      enum n_go_input_flags gif;

      for(gif = 0; gif < n__GO_INPUT_CTX_MAX1; ++gif)
         for(tbcp = a_tty.tg_bind[gif]; tbcp != NULL; tbcp = tbcp->tbc_next)
            if((tbcp->tbc_flags & (a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT)) ==
                  a_TTY_BIND_RESOLVE)
               a_tty_bind_resolve(tbcp);
   }

   /* And we want to (try to) install some default key bindings */
   if(!ok_blook(line_editor_no_defaults)){
      char buf[8];
      struct a_tty_bind_parse_ctx tbpc;
      struct a_tty_bind_builtin_tuple const *tbbtp, *tbbtp_max;
      ui32_t flags;

      buf[0] = '$', buf[1] = '\'', buf[2] = '\\', buf[3] = 'c',
         buf[5] = '\'', buf[6] = '\0';

      tbbtp = a_tty_bind_base_tuples;
      tbbtp_max = &tbbtp[n_NELEM(a_tty_bind_base_tuples)];
      flags = n_GO_INPUT_CTX_BASE;
jbuiltin_redo:
      for(; tbbtp < tbbtp_max; ++tbbtp){
         memset(&tbpc, 0, sizeof tbpc);
         tbpc.tbpc_cmd = "bind";
         if(tbbtp->tbbt_iskey){
            buf[4] = tbbtp->tbbt_ckey;
            tbpc.tbpc_in_seq = buf;
         }else
            tbpc.tbpc_in_seq = savecatsep(":", '\0',
               n_termcap_name_of_query(tbbtp->tbbt_query));
         tbpc.tbpc_exp.s = n_UNCONST(tbbtp->tbbt_exp[0] == '\0'
               ? a_tty_bind_fun_names[(ui8_t)tbbtp->tbbt_exp[1]]
               : tbbtp->tbbt_exp);
         tbpc.tbpc_exp.l = su_cs_len(tbpc.tbpc_exp.s);
         tbpc.tbpc_flags = flags;
         /* ..but don't want to overwrite any user settings */
         a_tty_bind_create(&tbpc, FAL0);
      }
      if(flags == n_GO_INPUT_CTX_BASE){
         tbbtp = a_tty_bind_default_tuples;
         tbbtp_max = &tbbtp[n_NELEM(a_tty_bind_default_tuples)];
         flags = n_GO_INPUT_CTX_DEFAULT;
         goto jbuiltin_redo;
      }
   }
# endif /* mx_HAVE_KEY_BINDINGS */

jleave:
   n_NYD_OU;
}

FL void
n_tty_destroy(bool_t xit_fastpath){
   n_NYD_IN;

   if(!(n_psonce & n_PSO_LINE_EDITOR_INIT))
      goto jleave;

   /* Write the history file */
# ifdef mx_HAVE_HISTORY
   if(!xit_fastpath)
      a_tty_hist_save();
# endif

# if defined mx_HAVE_KEY_BINDINGS && defined mx_HAVE_DEBUG
   if(!xit_fastpath)
      n_go_command(n_GO_INPUT_NONE, "unbind * *");
# endif

# ifdef mx_HAVE_DEBUG
   memset(&a_tty, 0, sizeof a_tty);

   n_psonce &= ~n_PSO_LINE_EDITOR_INIT;
# endif
jleave:
   n_NYD_OU;
}

FL int
(n_tty_readline)(enum n_go_input_flags gif, char const *prompt,
      char **linebuf, size_t *linesize, size_t n, bool_t *histok_or_null
      su_DBG_LOC_ARGS_DECL){
   struct a_tty_line tl;
   struct n_string xprompt;
# ifdef mx_HAVE_COLOUR
   char *posbuf, *pos;
# endif
   ssize_t nn;
   n_NYD_IN;

   assert(!ok_blook(line_editor_disable));
   if(!(n_psonce & n_PSO_LINE_EDITOR_INIT))
      n_tty_init();
   assert(n_psonce & n_PSO_LINE_EDITOR_INIT);

# ifdef mx_HAVE_COLOUR
   n_colour_env_create(n_COLOUR_CTX_MLE, n_tty_fp, FAL0);

   /* .tl_pos_buf is a hack */
   posbuf = pos = NULL;

   if(n_COLOUR_IS_ACTIVE()){
      char const *ccol;
      struct n_colour_pen *ccp;
      struct str const *sp;

      if((ccp = n_colour_pen_create(n_COLOUR_ID_MLE_POSITION, NULL)) != NULL &&
            (sp = n_colour_pen_to_str(ccp)) != NULL){
         ccol = sp->s;
         if((sp = n_colour_reset_to_str()) != NULL){
            size_t l1, l2;

            l1 = su_cs_len(ccol);
            l2 = su_cs_len(sp->s);
            posbuf = n_autorec_alloc(l1 + 4 + l2 +1);
            memcpy(posbuf, ccol, l1);
            pos = &posbuf[l1];
            memcpy(&pos[4], sp->s, ++l2);
         }
      }
   }

   if(posbuf == NULL){
      posbuf = pos = n_autorec_alloc(4 +1);
      pos[4] = '\0';
   }
# endif /* mx_HAVE_COLOUR */

   memset(&tl, 0, sizeof tl);
   tl.tl_goinflags = gif;

# ifdef mx_HAVE_KEY_BINDINGS
   /* C99 */{
      char const *cp;

      if((cp = ok_vlook(bind_timeout)) != NULL){
         ui64_t uib;

         su_idec_u64_cp(&uib, cp, 0, NULL);

         if(uib > 0 &&
               /* Convert to tenths of a second, unfortunately */
               (uib = (uib + 99) / 100) <= a_TTY_BIND_TIMEOUT_MAX)
            tl.tl_bind_timeout = (ui8_t)uib;
         else if(n_poption & n_PO_D_V)
            n_err(_("Ignoring invalid *bind-timeout*: %s\n"), cp);
      }
   }

   if(a_tty.tg_bind_isdirty)
      a_tty_bind_tree_teardown();
   if(a_tty.tg_bind_cnt > 0 && !a_tty.tg_bind_isbuild)
      a_tty_bind_tree_build();
   tl.tl_bind_tree_hmap = &a_tty.tg_bind_tree[gif & n__GO_INPUT_CTX_MASK];
   tl.tl_bind_shcut_cancel =
         &a_tty.tg_bind_shcut_cancel[gif & n__GO_INPUT_CTX_MASK];
   tl.tl_bind_shcut_prompt_char =
         &a_tty.tg_bind_shcut_prompt_char[gif & n__GO_INPUT_CTX_MASK];
# endif /* mx_HAVE_KEY_BINDINGS */

# ifdef mx_HAVE_COLOUR
   tl.tl_pos_buf = posbuf;
   tl.tl_pos = pos;
# endif

   if(!(gif & n_GO_INPUT_PROMPT_NONE)){
      n_string_creat_auto(&xprompt);

      if((tl.tl_prompt_width = n_tty_create_prompt(&xprompt, prompt, gif)
               ) > 0){
         tl.tl_prompt = n_string_cp_const(&xprompt);
         tl.tl_prompt_length = (ui32_t)xprompt.s_len;
      }
   }

   tl.tl_line.cbuf = *linebuf;
   if(n != 0){
      tl.tl_defc.s = savestrbuf(*linebuf, n);
      tl.tl_defc.l = n;
   }
   tl.tl_x_buf = linebuf;
   tl.tl_x_bufsize = linesize;

   a_tty.tg_line = &tl;
   a_tty_sigs_up();
   n_TERMCAP_RESUME(FAL0);
   a_tty_term_mode(TRU1);
   nn = a_tty_readline(&tl, n, histok_or_null  su_DBG_LOC_ARGS_USE);
   n_COLOUR( n_colour_env_gut(); )
   a_tty_term_mode(FAL0);
   n_TERMCAP_SUSPEND(FAL0);
   a_tty_sigs_down();
   a_tty.tg_line = NULL;

   n_NYD_OU;
   return (int)nn;
}

FL void
n_tty_addhist(char const *s, enum n_go_input_flags gif){
   n_NYD_IN;
   n_UNUSED(s);
   n_UNUSED(gif);

# ifdef mx_HAVE_HISTORY
   if(*s != '\0' && (n_psonce & n_PSO_LINE_EDITOR_INIT) &&
         a_tty.tg_hist_size_max > 0 &&
         (!(gif & n_GO_INPUT_HIST_GABBY) || ok_blook(history_gabby)) &&
          !ok_blook(line_editor_disable)){
      struct a_tty_input_ctx_map const *ticmp;

      ticmp = &a_tty_input_ctx_maps[gif & a_TTY_HIST_CTX_MASK];

      if(temporary_addhist_hook(ticmp->ticm_name,
            ((gif & n_GO_INPUT_HIST_GABBY) != 0), s)){
         hold_all_sigs();
         a_tty_hist_add(s, gif);
         rele_all_sigs();
      }
   }
# endif
   n_NYD_OU;
}

# ifdef mx_HAVE_HISTORY
FL int
c_history(void *v){
   siz_t entry;
   struct a_tty_hist *thp;
   char **argv;
   n_NYD_IN;

   if(ok_blook(line_editor_disable)){
      n_err(_("history: *line-editor-disable* is set\n"));
      goto jerr;
   }

   if(!(n_psonce & n_PSO_LINE_EDITOR_INIT)){
      n_tty_init();
      assert(n_psonce & n_PSO_LINE_EDITOR_INIT);
   }

   if(*(argv = v) == NULL)
      goto jlist;
   if(argv[1] != NULL)
      goto jerr;
   if(!su_cs_cmp_case(*argv, "show"))
      goto jlist;
   if(!su_cs_cmp_case(*argv, "clear"))
      goto jclear;

   if(!su_cs_cmp_case(*argv, "load")){
      if(!a_tty_hist_load())
         v = NULL;
      goto jleave;
   }
   if(!su_cs_cmp_case(*argv, "save")){
      if(!a_tty_hist_save())
         v = NULL;
      goto jleave;
   }

   if((su_idec_sz_cp(&entry, *argv, 10, NULL
            ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) == su_IDEC_STATE_CONSUMED)
      goto jentry;
jerr:
   n_err(_("Synopsis: history: %s\n"),
      /* Same string as in cmd-tab.h, still hoping...) */
      _("<show (default)|load|save|clear> or select history <NO>"));
   v = NULL;
jleave:
   n_NYD_OU;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */

jlist:{
   size_t no, l, b;
   FILE *fp;

   if(a_tty.tg_hist == NULL)
      goto jleave;

   if((fp = Ftmp(NULL, "hist", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("tmpfile"), 0);
      v = NULL;
      goto jleave;
   }

   no = a_tty.tg_hist_size;
   l = b = 0;

   for(thp = a_tty.tg_hist; thp != NULL;
         --no, ++l, thp = thp->th_older){
      char c1, c2;

      b += thp->th_len;

      switch(thp->th_flags & a_TTY_HIST_CTX_MASK){
      default:
      case a_TTY_HIST_CTX_DEFAULT:
         c1 = 'd';
         break;
      case a_TTY_HIST_CTX_COMPOSE:
         c1 = 'c';
         break;
      }
      c2 = (thp->th_flags & a_TTY_HIST_GABBY) ? '*' : ' ';

      if(n_poption & n_PO_D_V)
         fprintf(fp, "# Length +%" PRIu32 ", total %" PRIuZ "\n",
            thp->th_len, b);
      fprintf(fp, "%c%c%4" PRIuZ "\t%s\n", c1, c2, no, thp->th_dat);
   }

   page_or_print(fp, l);
   Fclose(fp);
   }
   goto jleave;

jclear:
   while((thp = a_tty.tg_hist) != NULL){
      a_tty.tg_hist = thp->th_older;
      n_free(thp);
   }
   a_tty.tg_hist_tail = NULL;
   a_tty.tg_hist_size = 0;
   goto jleave;

jentry:{
   siz_t ep;

   ep = (entry < 0) ? -entry : entry;

   if(ep != 0 && UICMP(z, ep, <=, a_tty.tg_hist_size)){
      if(ep != entry)
         --ep;
      else
         ep = (siz_t)a_tty.tg_hist_size - ep;
      for(thp = a_tty.tg_hist;; thp = thp->th_older){
         assert(thp != NULL);
         if(ep-- == 0){
            n_go_input_inject((n_GO_INPUT_INJECT_COMMIT |
               n_GO_INPUT_INJECT_HISTORY), v = thp->th_dat, thp->th_len);
            break;
         }
      }
   }else{
      n_err(_("`history': no such entry: %" PRIdZ "\n"), entry);
      v = NULL;
   }
   }
   goto jleave;
}
# endif /* mx_HAVE_HISTORY */

# ifdef mx_HAVE_KEY_BINDINGS
FL int
c_bind(void *v){
   struct a_tty_bind_ctx *tbcp;
   enum n_go_input_flags gif;
   bool_t aster, show;
   union {char const *cp; char *p; char c;} c;
   struct n_cmd_arg_ctx *cacp;
   n_NYD_IN;

   cacp = v;

   c.cp = cacp->cac_arg->ca_arg.ca_str.s;
   if(cacp->cac_no == 1)
      show = TRU1;
   else
      show = !su_cs_cmp_case(cacp->cac_arg->ca_next->ca_arg.ca_str.s, "show");
   aster = FAL0;

   if((gif = a_tty_bind_ctx_find(c.cp)) == (enum n_go_input_flags)-1){
      if(!(aster = n_is_all_or_aster(c.cp)) || !show){
         n_err(_("`bind': invalid context: %s\n"), c.cp);
         v = NULL;
         goto jleave;
      }
      gif = 0;
   }

   if(show){
      ui32_t lns;
      FILE *fp;

      if((fp = Ftmp(NULL, "bind", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
         n_perr(_("tmpfile"), 0);
         v = NULL;
         goto jleave;
      }

      lns = 0;
      for(;;){
         for(tbcp = a_tty.tg_bind[gif]; tbcp != NULL;
               ++lns, tbcp = tbcp->tbc_next){
            /* Print the bytes of resolved terminal capabilities, then */
            if((n_poption & n_PO_D_V) &&
                  (tbcp->tbc_flags & (a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT)
                  ) == a_TTY_BIND_RESOLVE){
               char cbuf[8];
               union {wchar_t const *wp; char const *cp;} u;
               si32_t entlen;
               ui32_t cnvlen;
               char const *cnvdat, *bsep, *cbufp;

               putc('#', fp);
               putc(' ', fp);

               cbuf[0] = '=', cbuf[2] = '\0';
               for(cnvdat = tbcp->tbc_cnv, cnvlen = tbcp->tbc_cnv_len;
                     cnvlen > 0;){
                  if(cnvdat != tbcp->tbc_cnv)
                     putc(',', fp);

                  /* {si32_t buf_len_iscap;} */
                  entlen = *n_UNALIGN(si32_t const*,cnvdat);
                  if(entlen & SI32_MIN){
                     /* struct{si32_t buf_len_iscap; si32_t cap_len;
                      * char buf[]+NUL;} */
                     for(bsep = n_empty,
                              u.cp = (char const*)
                                    &n_UNALIGN(si32_t const*,cnvdat)[2];
                           (c.c = *u.cp) != '\0'; ++u.cp){
                        if(su_cs_is_ascii(c.c) && !su_cs_is_cntrl(c.c))
                           cbuf[1] = c.c, cbufp = cbuf;
                        else
                           cbufp = n_empty;
                        fprintf(fp, "%s\\x%02X%s",
                           bsep, (ui32_t)(ui8_t)c.c, cbufp);
                        bsep = " ";
                     }
                     entlen &= SI32_MAX;
                  }else
                     putc('-', fp);

                  cnvlen -= entlen;
                  cnvdat += entlen;
               }

               fputs("\n  ", fp);
               ++lns;
            }

            fprintf(fp, "%sbind %s %s %s%s%s\n",
               ((tbcp->tbc_flags & a_TTY_BIND_DEFUNCT)
               /* I18N: `bind' sequence not working, either because it is
                * I18N: using Unicode and that is not available in the locale,
                * I18N: or a termcap(5)/terminfo(5) sequence won't work out */
                  ? _("# <Defunctional> ") : n_empty),
               a_tty_input_ctx_maps[gif].ticm_name, tbcp->tbc_seq,
               n_shexp_quote_cp(tbcp->tbc_exp, TRU1),
               (tbcp->tbc_flags & a_TTY_BIND_NOCOMMIT ? n_at : n_empty),
               (!(n_poption & n_PO_D_VV) ? n_empty
                  : (tbcp->tbc_flags & a_TTY_BIND_FUN_INTERNAL
                     ? _(" # MLE internal") : n_empty))
               );
         }
         if(!aster || ++gif >= n__GO_INPUT_CTX_MAX1)
            break;
      }
      page_or_print(fp, lns);

      Fclose(fp);
   }else{
      struct a_tty_bind_parse_ctx tbpc;
      struct n_cmd_arg *cap;

      memset(&tbpc, 0, sizeof tbpc);
      tbpc.tbpc_cmd = cacp->cac_desc->cad_name;
      tbpc.tbpc_in_seq = (cap = cacp->cac_arg->ca_next)->ca_arg.ca_str.s;
      if((cap = cap->ca_next) != NULL){
         tbpc.tbpc_exp.s = cap->ca_arg.ca_str.s;
         tbpc.tbpc_exp.l = cap->ca_arg.ca_str.l;
      }
      tbpc.tbpc_flags = gif;
      if(!a_tty_bind_create(&tbpc, TRU1))
         v = NULL;
   }
jleave:
   n_NYD_OU;
   return (v != NULL) ? n_EXIT_OK : n_EXIT_ERR;
}

FL int
c_unbind(void *v){
   struct a_tty_bind_parse_ctx tbpc;
   struct a_tty_bind_ctx *tbcp;
   enum n_go_input_flags gif;
   bool_t aster;
   union {char const *cp; char *p;} c;
   struct n_cmd_arg_ctx *cacp;
   n_NYD_IN;

   cacp = v;
   c.cp = cacp->cac_arg->ca_arg.ca_str.s;
   aster = FAL0;

   if((gif = a_tty_bind_ctx_find(c.cp)) == (enum n_go_input_flags)-1){
      if(!(aster = n_is_all_or_aster(c.cp))){
         n_err(_("`unbind': invalid context: %s\n"), c.cp);
         v = NULL;
         goto jleave;
      }
      gif = 0;
   }

   c.cp = cacp->cac_arg->ca_next->ca_arg.ca_str.s;
jredo:
   if(n_is_all_or_aster(c.cp)){
      while((tbcp = a_tty.tg_bind[gif]) != NULL){
         memset(&tbpc, 0, sizeof tbpc);
         tbpc.tbpc_tbcp = tbcp;
         tbpc.tbpc_flags = gif;
         a_tty_bind_del(&tbpc);
      }
   }else{
      memset(&tbpc, 0, sizeof tbpc);
      tbpc.tbpc_cmd = cacp->cac_desc->cad_name;
      tbpc.tbpc_in_seq = c.cp;
      tbpc.tbpc_flags = gif;

      if(n_UNLIKELY(!a_tty_bind_parse(FAL0, &tbpc)))
         v = NULL;
      else if(n_UNLIKELY((tbcp = tbpc.tbpc_tbcp) == NULL)){
         n_err(_("`unbind': no such `bind'ing: %s  %s\n"),
            a_tty_input_ctx_maps[gif].ticm_name, c.cp);
         v = NULL;
      }else
         a_tty_bind_del(&tbpc);
   }

   if(aster && ++gif < n__GO_INPUT_CTX_MAX1)
      goto jredo;
jleave:
   n_NYD_OU;
   return (v != NULL) ? n_EXIT_OK : n_EXIT_ERR;
}
# endif /* mx_HAVE_KEY_BINDINGS */

#else /* mx_HAVE_MLE */
/*
 * The really-nothing-at-all implementation
 */

# ifdef a_TTY_SIGNALS
static void
a_tty_signal(int sig){
   /* Prototype at top */
#  ifdef mx_HAVE_TERMCAP
   sigset_t nset, oset;
#  endif
   n_NYD_X; /* Signal handler */
   n_UNUSED(sig);

#  ifdef mx_HAVE_TERMCAP
   n_TERMCAP_SUSPEND(TRU1);
   a_tty_sigs_down();

   sigemptyset(&nset);
   sigaddset(&nset, sig);
   sigprocmask(SIG_UNBLOCK, &nset, &oset);
   n_raise(sig);
   /* When we come here we'll continue editing, so reestablish */
   sigprocmask(SIG_BLOCK, &oset, (sigset_t*)NULL);

   a_tty_sigs_up();
   n_TERMCAP_RESUME(TRU1);
#  endif /* mx_HAVE_TERMCAP */
}
# endif /* a_TTY_SIGNALS */

# if 0
FL void
n_tty_init(void){
   n_NYD_IN;
   n_NYD_OU;
}

FL void
n_tty_destroy(bool_t xit_fastpath){
   n_NYD_IN;
   n_UNUSED(xit_fastpath);
   n_NYD_OU;
}
# endif /* 0 */

FL int
(n_tty_readline)(enum n_go_input_flags gif, char const *prompt,
      char **linebuf, size_t *linesize, size_t n, bool_t *histok_or_null
      su_DBG_LOC_ARGS_DECL){
   struct n_string xprompt;
   int rv;
   n_NYD_IN;
   n_UNUSED(histok_or_null);

   if(!(gif & n_GO_INPUT_PROMPT_NONE)){
      if(n_tty_create_prompt(n_string_creat_auto(&xprompt), prompt, gif) > 0){
         fwrite(xprompt.s_dat, 1, xprompt.s_len, n_tty_fp);
         fflush(n_tty_fp);
      }
   }

# ifdef mx_HAVE_TERMCAP
   a_tty_sigs_up();
   n_TERMCAP_RESUME(FAL0);
# endif
   rv = (readline_restart)(n_stdin, linebuf, linesize, n  su_DBG_LOC_ARGS_USE);
# ifdef mx_HAVE_TERMCAP
   n_TERMCAP_SUSPEND(FAL0);
   a_tty_sigs_down();
# endif
   n_NYD_OU;
   return rv;
}

FL void
n_tty_addhist(char const *s, enum n_go_input_flags gif){
   n_NYD_IN;
   n_UNUSED(s);
   n_UNUSED(gif);
   n_NYD_OU;
}
#endif /* nothing at all */

#undef a_TTY_SIGNALS
/* s-it-mode */
