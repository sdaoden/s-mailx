/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ TTY (command line) editing interaction.
 *@ Because we have multiple line-editor implementations, including our own
 *@ M(ailx) L(ine) E(ditor), change the file layout a bit and place those
 *@ one after the other below the other externals.
 *
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#undef n_FILE
#define n_FILE tty

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#ifdef HAVE_READLINE
# include <readline/readline.h>
# ifdef HAVE_HISTORY
#  include <readline/history.h>
# endif
#endif

#if defined HAVE_READLINE || defined HAVE_MLE || defined HAVE_TERMCAP
# define a_TTY_SIGNALS
#endif

/* Shared history support macros */
#ifdef HAVE_HISTORY
# define a_TTY_HISTFILE(S) \
do{\
   char const *__hist_obsolete = ok_vlook(NAIL_HISTFILE);\
   if(__hist_obsolete != NULL)\
      OBSOLETE(_("please use *history-file* instead of *NAIL_HISTFILE*"));\
   S = ok_vlook(history_file);\
   if((S) == NULL)\
      (S) = __hist_obsolete;\
   if((S) != NULL)\
      S = fexpand(S, FEXP_LOCAL | FEXP_NSHELL);\
}while(0)

# define a_TTY_HISTSIZE(V) \
do{\
   char const *__hist_obsolete = ok_vlook(NAIL_HISTSIZE);\
   char const *__sv = ok_vlook(history_size);\
   long __rv;\
   if(__hist_obsolete != NULL)\
      OBSOLETE(_("please use *history-size* instead of *NAIL_HISTSIZE*"));\
   if(__sv == NULL)\
      __sv = __hist_obsolete;\
   if(__sv == NULL || *__sv == '\0' || (__rv = strtol(__sv, NULL, 10)) == 0)\
      (V) = HIST_SIZE;\
   else if(__rv < 0)\
      (V) = 0;\
   else\
      (V) = __rv;\
}while(0)

# define a_TTY_CHECK_ADDHIST(S,NOACT) \
do{\
   switch(*(S)){\
   case '\0':\
   case ' ':\
   case '\t':\
      NOACT;\
   default:\
      break;\
   }\
}while(0)

# define C_HISTORY_SHARED \
   char **argv = v;\
   long entry;\
   NYD_ENTER;\
\
   if(*argv == NULL)\
      goto jlist;\
   if(argv[1] != NULL)\
      goto jerr;\
   if(!asccasecmp(*argv, "show"))\
      goto jlist;\
   if(!asccasecmp(*argv, "clear"))\
      goto jclear;\
   if((entry = strtol(*argv, argv, 10)) > 0 && **argv == '\0')\
      goto jentry;\
jerr:\
   n_err(_("Synopsis: history: %s\n" \
      "<show> (default), <clear> or select <NO> from editor history"));\
   v = NULL;\
jleave:\
   NYD_LEAVE;\
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
#endif /* HAVE_HISTORY */

/* fexpand() flags for expand-on-tab */
#define a_TTY_TAB_FEXP_FL (FEXP_FULL | FEXP_SILENT | FEXP_MULTIOK)

#ifdef a_TTY_SIGNALS
static sighandler_type a_tty_oint, a_tty_oquit, a_tty_oterm,
   a_tty_ohup,
   a_tty_otstp, a_tty_ottin, a_tty_ottou;
#endif

#ifdef a_TTY_SIGNALS
static void a_tty_sigs_up(void), a_tty_sigs_down(void);
#endif

#ifdef a_TTY_SIGNALS
static void
a_tty_sigs_up(void){
   sigset_t nset, oset;
   NYD2_ENTER;

   sigfillset(&nset);

   sigprocmask(SIG_BLOCK, &nset, &oset);
   a_tty_oint = safe_signal(SIGINT, &n_tty_signal);
   a_tty_oquit = safe_signal(SIGQUIT, &n_tty_signal);
   a_tty_oterm = safe_signal(SIGTERM, &n_tty_signal);
   a_tty_ohup = safe_signal(SIGHUP, &n_tty_signal);
   a_tty_otstp = safe_signal(SIGTSTP, &n_tty_signal);
   a_tty_ottin = safe_signal(SIGTTIN, &n_tty_signal);
   a_tty_ottou = safe_signal(SIGTTOU, &n_tty_signal);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD2_LEAVE;
}

static void
a_tty_sigs_down(void){
   sigset_t nset, oset;
   NYD2_ENTER;

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
   NYD2_LEAVE;
}
#endif /* a_TTY_SIGNALS */

static sigjmp_buf a_tty__actjmp; /* TODO someday, we won't need it no more */
static void
a_tty__acthdl(int s) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   termios_state_reset();
   siglongjmp(a_tty__actjmp, s);
}

FL bool_t
getapproval(char const * volatile prompt, bool_t noninteract_default)
{
   sighandler_type volatile oint, ohup;
   bool_t volatile rv;
   int volatile sig;
   NYD_ENTER;

   if (!(options & OPT_INTERACTIVE)) {
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

   if (n_lex_input(prompt, TRU1, &termios_state.ts_linebuf,
         &termios_state.ts_linesize, NULL) >= 0)
      rv = (boolify(termios_state.ts_linebuf, UIZ_MAX,
            noninteract_default) > 0);
jrestore:
   termios_state_reset();

   safe_signal(SIGHUP, ohup);
   safe_signal(SIGINT, oint);
jleave:
   NYD_LEAVE;
   if (sig != 0)
      n_raise(sig);
   return rv;
}

#ifdef HAVE_SOCKETS
FL char *
getuser(char const * volatile query) /* TODO v15-compat obsolete */
{
   sighandler_type volatile oint, ohup;
   char * volatile user = NULL;
   int volatile sig;
   NYD_ENTER;

   if (query == NULL)
      query = _("User: ");

   oint = safe_signal(SIGINT, SIG_IGN);
   ohup = safe_signal(SIGHUP, SIG_IGN);
   if ((sig = sigsetjmp(a_tty__actjmp, 1)) != 0)
      goto jrestore;
   safe_signal(SIGINT, &a_tty__acthdl);
   safe_signal(SIGHUP, &a_tty__acthdl);

   if (n_lex_input(query, TRU1, &termios_state.ts_linebuf,
         &termios_state.ts_linesize, NULL) >= 0)
      user = termios_state.ts_linebuf;
jrestore:
   termios_state_reset();

   safe_signal(SIGHUP, ohup);
   safe_signal(SIGINT, oint);
   NYD_LEAVE;
   if (sig != 0)
      n_raise(sig);
   return user;
}

FL char *
getpassword(char const *query)
{
   sighandler_type volatile oint, ohup;
   struct termios tios;
   char * volatile pass = NULL;
   int volatile sig;
   NYD_ENTER;

   if (query == NULL)
      query = _("Password: ");
   fputs(query, stdout);
   fflush(stdout);

   /* FIXME everywhere: tcsetattr() generates SIGTTOU when we're not in
    * FIXME foreground pgrp, and can fail with EINTR!! also affects
    * FIXME termios_state_reset() */
   if (options & OPT_TTYIN) {
      tcgetattr(STDIN_FILENO, &termios_state.ts_tios);
      memcpy(&tios, &termios_state.ts_tios, sizeof tios);
      termios_state.ts_needs_reset = TRU1;
      tios.c_iflag &= ~(ISTRIP);
      tios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
   }

   oint = safe_signal(SIGINT, SIG_IGN);
   ohup = safe_signal(SIGHUP, SIG_IGN);
   if ((sig = sigsetjmp(a_tty__actjmp, 1)) != 0)
      goto jrestore;
   safe_signal(SIGINT, &a_tty__acthdl);
   safe_signal(SIGHUP, &a_tty__acthdl);

   if (options & OPT_TTYIN)
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &tios);

   if (readline_restart(stdin, &termios_state.ts_linebuf,
         &termios_state.ts_linesize, 0) >= 0)
      pass = termios_state.ts_linebuf;
jrestore:
   termios_state_reset();
   if (options & OPT_TTYIN)
      putc('\n', stdout);

   safe_signal(SIGHUP, ohup);
   safe_signal(SIGINT, oint);
   NYD_LEAVE;
   if (sig != 0)
      n_raise(sig);
   return pass;
}
#endif /* HAVE_SOCKETS */

/*
 * readline(3)
 */
#ifdef HAVE_READLINE

static char *a_tty_rl_buf;    /* pre_input() hook: initial line */
static int a_tty_rl_buflen;   /* content, and its length */

/* Our rl_pre_input_hook */
static int a_tty_rl_pre_input(void);

static int
a_tty_rl_pre_input(void){
   NYD2_ENTER;
   /* Handle leftover data from \ escaped former line */
   rl_extend_line_buffer(a_tty_rl_buflen + 10);
   memcpy(rl_line_buffer, a_tty_rl_buf, a_tty_rl_buflen +1);
   rl_point = rl_end = a_tty_rl_buflen;
   rl_pre_input_hook = (rl_hook_func_t*)NULL;
   rl_redisplay();
   NYD2_LEAVE;
   return 0;
}

FL void
n_tty_init(void){
# ifdef HAVE_HISTORY
   long hs;
   char const *v;
# endif
   NYD_ENTER;

   rl_readline_name = UNCONST(uagent);
# ifdef HAVE_HISTORY
   a_TTY_HISTSIZE(hs);
   using_history();
   stifle_history((int)hs);
# endif
   rl_read_init_file(NULL);

   /* Because rl_read_init_file() may have introduced yet a different
    * history size limit, simply load and incorporate the history, leave
    * it up to readline(3) to do the rest */
# ifdef HAVE_HISTORY
   a_TTY_HISTFILE(v);
   if(v != NULL)
      read_history(v);
# endif
   NYD_LEAVE;
}

FL void
n_tty_destroy(void){
# ifdef HAVE_HISTORY
   char const *v;
# endif
   NYD_ENTER;

# ifdef HAVE_HISTORY
   a_TTY_HISTFILE(v);
   if(v != NULL)
      write_history(v);
# endif
   NYD_LEAVE;
}

FL void
n_tty_signal(int sig){
   NYD_X; /* Signal handler */

   /* WINCH comes from main.c */
   switch(sig){
# ifdef SIGWINCH
   case SIGWINCH:
      break;
# endif
   default:{
      sigset_t nset, oset;

      /* readline(3) doesn't catch SIGHUP :( */
      if(sig == SIGHUP){
         rl_free_line_state();
         rl_cleanup_after_signal();
      }
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
      if(sig == SIGHUP)
         rl_reset_after_signal();
      break;
      }
   }
}

FL int
(n_tty_readline)(char const *prompt, char **linebuf, size_t *linesize, size_t n
      SMALLOC_DEBUG_ARGS){
   int nn;
   char *line;
   NYD_ENTER;

   if(n > 0){
      a_tty_rl_buf = *linebuf;
      a_tty_rl_buflen = (int)n;
      rl_pre_input_hook = &a_tty_rl_pre_input;
   }

   a_tty_sigs_up();
   n_TERMCAP_SUSPEND(FAL0);
   line = readline(prompt != NULL ? prompt : "");
   n_TERMCAP_RESUME(FAL0);
   a_tty_sigs_down();

   if(line == NULL){
      nn = -1;
      goto jleave;
   }
   n = strlen(line);

   if(n >= *linesize){
      *linesize = LINESIZE + n +1;
      *linebuf = (srealloc)(*linebuf, *linesize SMALLOC_DEBUG_ARGSCALL);
   }
   memcpy(*linebuf, line, n);
   (free)(line);
   (*linebuf)[n] = '\0';
   nn = (int)n;
jleave:
   NYD_LEAVE;
   return nn;
}

FL void
n_tty_addhist(char const *s, bool_t isgabby){
   NYD_ENTER;
   UNUSED(s);
   UNUSED(isgabby);

# ifdef HAVE_HISTORY
   if(isgabby && !ok_blook(history_gabby))
      goto jleave;
   a_TTY_CHECK_ADDHIST(s, goto jleave);
   hold_all_sigs();  /* XXX too heavy */
   add_history(s);   /* XXX yet we jump away! */
   rele_all_sigs();  /* XXX remove jumps */
jleave:
# endif
   NYD_LEAVE;
}

# ifdef HAVE_HISTORY
FL int
c_history(void *v){
   C_HISTORY_SHARED;

jlist:{
   FILE *fp;
   HISTORY_STATE *hs;
   HIST_ENTRY **hl;
   ul_i i, b;

   if((fp = Ftmp(NULL, "hist", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("tmpfile"), 0);
      v = NULL;
      goto jleave;
   }

   hs = history_get_history_state();

   for(i = (ul_i)hs->length, hl = hs->entries + i, b = 0; i > 0; --i){
      char *cp = (*--hl)->line;
      size_t sl = strlen(cp);

      fprintf(fp, "%4lu. %-50.50s (%4lu+%2lu B)\n", i, cp, b, sl);
      b += sl;
   }

   page_or_print(fp, (size_t)hs->length);
   Fclose(fp);
   }
   goto jleave;

jclear:
   clear_history();
   goto jleave;

jentry:{
   HISTORY_STATE *hs = history_get_history_state();

   if(UICMP(z, entry, <=, hs->length))
      v = temporary_arg_v_store = hs->entries[entry - 1]->line;
   else
      v = NULL;
   }
   goto jleave;
}
# endif /* HAVE_HISTORY */
#endif /* HAVE_READLINE */

/*
 * MLE: the Mailx-Line-Editor, our homebrew editor
 * (inspired from NetBSD sh(1) / dash(1)s hetio.c).
 *
 * Only used in interactive mode, simply use STDIN_FILENO as point of interest.
 * TODO . After I/O layer rewrite, also "output to STDIN_FILENO".
 * TODO . We work with wide characters, but not for buffer takeovers and
 * TODO   cell2save()ings.  This should be changed.  For the former the buffer
 * TODO   thus needs to be converted to wide first, and then simply be fed in.
 * TODO . No BIDI support.
 * TODO . We repaint too much.  To overcome this use the same approach that my
 * TODO   terminal library uses, add a true "virtual screen line" that stores
 * TODO   the actually visible content, keep a notion of "first modified slot"
 * TODO   and "last modified slot" (including "unknown" and "any" specials),
 * TODO   update that virtual instead, then synchronize what has truly changed.
 * TODO   I.e., add an indirection layer.
 */
#ifdef HAVE_MLE
/* To avoid memory leaks etc. with the current codebase that simply longjmp(3)s
 * we're forced to use the very same buffer--the one that is passed through to
 * us from the outside--to store anything we need, i.e., a "struct cell[]", and
 * convert that on-the-fly back to the plain char* result once we're done.
 * To simplify our live, use savestr() buffers for all other needed memory */

/* Columns to ripoff: outermost may not be touched, plus position indicator.
 * Must thus be at least 1, but should be >= 1+4 to dig the position indicator
 * that we place (if there is sufficient space) */
# define a_TTY_WIDTH_RIPOFF 5

/* When shall the visual screen be scrolled, in % of usable screen width */
# define a_TTY_SCROLL_MARGIN_LEFT 15
# define a_TTY_SCROLL_MARGIN_RIGHT 10

/* The maximum size (of a_tty_cell's) in a line */
# define a_TTY_LINE_MAX SI32_MAX

/* (Some more CTAs around) */
n_CTA(a_TTY_LINE_MAX <= SI32_MAX,
   "a_TTY_LINE_MAX larger than SI32_MAX, but the MLE uses 32-bit arithmetic");

enum a_tty_visual_flags{
   a_TTY_VF_NONE,
   a_TTY_VF_MOD_CURSOR = 1<<0,   /* Cursor moved */
   a_TTY_VF_MOD_CONTENT = 1<<1,  /* Content modified */
   a_TTY_VF_MOD_DIRTY = 1<<2,    /* Needs complete repaint */
   a_TTY_VF_MOD_SINGLE = 1<<3,   /* TODO Drop when indirection as above comes */
   a_TTY_VF_REFRESH = a_TTY_VF_MOD_DIRTY | a_TTY_VF_MOD_CURSOR |
         a_TTY_VF_MOD_CONTENT | a_TTY_VF_MOD_SINGLE,
   a_TTY_VF_BELL = 1<<8,         /* Ring the bell */
   a_TTY_VF_SYNC = 1<<9,         /* Flush/Sync I/O channel */

   a_TTY_VF_ALL_MASK = a_TTY_VF_REFRESH | a_TTY_VF_BELL | a_TTY_VF_SYNC,
   a_TTY__VF_LAST = a_TTY_VF_SYNC
};

struct a_tty_global{
# ifdef HAVE_HISTORY
   struct a_tty_hist *tg_hist;
   struct a_tty_hist *tg_hist_tail;
   size_t tg_hist_size;
   size_t tg_hist_size_max;
# endif
   struct termios tg_tios_old;
   struct termios tg_tios_new;
};

struct a_tty_cell{
   wchar_t tc_wc;
   ui16_t tc_count;  /* ..of bytes */
   ui8_t tc_width;   /* Visual width; TAB==UI8_MAX! */
   bool_t tc_novis;  /* Don't display visually as such (control character) */
   char tc_cbuf[MB_LEN_MAX * 2]; /* .. plus reset shift sequence */
};

struct a_tty_line{
   /* Caller pointers */
   char **tl_x_buf;
   size_t *tl_x_bufsize;
   /* Input processing */
   char const *tl_reenter_after_cmd; /* `bind' cmd to exec, then re-readline */
   /* Line data / content handling */
   ui32_t tl_count;              /* ..of a_tty_cell's (<= a_TTY_LINE_MAX) */
   ui32_t tl_cursor;             /* Current a_tty_cell insertion point */
   union{
      char *cbuf;                /* *.tl_x_buf */
      struct a_tty_cell *cells;
   } tl_line;
   struct str tl_defc;           /* Current default content */
   struct str tl_savec;          /* Saved default content */
   struct str tl_yankbuf;        /* Last yanked data */
# ifdef HAVE_HISTORY
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
   ui32_t tl_prompt_length;      /* Preclassified (TODO needed as a_tty_cell) */
   ui32_t tl_prompt_width;
   ui8_t tl__dummy[4];
   char const *tl_prompt;        /* Preformatted prompt (including colours) */
   /* .tl_pos_buf is a hack */
# ifdef HAVE_COLOUR
   char *tl_pos_buf;             /* mle-position colour-on, [4], reset seq. */
   char *tl_pos;                 /* Address of the [4] */
# endif
};

# ifdef HAVE_HISTORY
struct a_tty_hist{
   struct a_tty_hist *th_older;
   struct a_tty_hist *th_younger;
   ui32_t th_isgabby : 1;
   ui32_t th_len : 31;
   char th_dat[VFIELD_SIZE(sizeof(ui32_t))];
};
# endif

static struct a_tty_global a_tty;

/**/
static void a_tty_term_mode(bool_t raw);

/* 0-X (2), UI8_MAX == \t / TAB */
static ui8_t a_tty_wcwidth(wchar_t wc);

/* Memory / cell / word generics */
static void a_tty_check_grow(struct a_tty_line *tlp, ui32_t no
               SMALLOC_DEBUG_ARGS);
static ssize_t a_tty_cell2dat(struct a_tty_line *tlp);
static void a_tty_cell2save(struct a_tty_line *tlp);

/* Save away data bytes of given range (max = non-inclusive) */
static void a_tty_yank(struct a_tty_line *tlp, struct a_tty_cell *tcpmin,
               struct a_tty_cell *tcpmax);

/* Ask user for hexadecimal number, interpret as UTF-32 */
static wchar_t a_tty_vinuni(struct a_tty_line *tlp);

/* Visual screen synchronization */
static bool_t a_tty_vi_refresh(struct a_tty_line *tlp);

/* Search for word boundary, starting at tl_cursor, in "dir"ection (<> 0).
 * Return <0 when moving is impossible (backward direction but in position 0,
 * forward direction but in outermost column), and relative distance to
 * tl_cursor otherwise */
static si32_t a_tty_wboundary(struct a_tty_line *tlp, si32_t dir);

/* Key actions */
static void a_tty_khome(struct a_tty_line *tlp, bool_t dobell);
static void a_tty_kend(struct a_tty_line *tlp);
static void a_tty_kbs(struct a_tty_line *tlp);
static void a_tty_kkill(struct a_tty_line *tlp, bool_t dobell);
static si32_t a_tty_kdel(struct a_tty_line *tlp);
static void a_tty_kleft(struct a_tty_line *tlp);
static void a_tty_kright(struct a_tty_line *tlp);
static void a_tty_kbwddelw(struct a_tty_line *tlp);
static void a_tty_kgow(struct a_tty_line *tlp, si32_t dir);
static void a_tty_kother(struct a_tty_line *tlp, wchar_t wc);
static ui32_t a_tty_kht(struct a_tty_line *tlp);
# ifdef HAVE_HISTORY
static ui32_t a_tty__khist_shared(struct a_tty_line *tlp,
                  struct a_tty_hist *thp);
static ui32_t a_tty_khist(struct a_tty_line *tlp, bool_t backwd);
static ui32_t a_tty_krhist(struct a_tty_line *tlp);
# endif

/* Readline core */
static ssize_t a_tty_readline(struct a_tty_line *tlp, size_t len
                  SMALLOC_DEBUG_ARGS);

static void
a_tty_term_mode(bool_t raw){
   struct termios *tiosp;
   NYD2_ENTER;

   tiosp = &a_tty.tg_tios_old;
   if(!raw)
      goto jleave;

   /* Always requery the attributes, in case we've been moved from background
    * to foreground or however else in between sessions */
   /* XXX Always enforce ECHO and ICANON in the OLD attributes - do so as long
    * XXX as we don't properly deal with TTIN and TTOU etc. */
   tcgetattr(STDIN_FILENO, tiosp);
   tiosp->c_lflag |= ECHO | ICANON;

   memcpy(&a_tty.tg_tios_new, tiosp, sizeof *tiosp);
   tiosp = &a_tty.tg_tios_new;
   tiosp->c_cc[VMIN] = 1;
   tiosp->c_cc[VTIME] = 0;
   tiosp->c_iflag &= ~(ISTRIP);
   tiosp->c_lflag &= ~(ECHO /*| ECHOE | ECHONL */| ICANON | IEXTEN);
jleave:
   tcsetattr(STDIN_FILENO, TCSADRAIN, tiosp);
   NYD2_LEAVE;
}

static ui8_t
a_tty_wcwidth(wchar_t wc){
   ui8_t rv;
   NYD2_ENTER;

   /* Special case the backslash at first */
   if(wc == '\t')
      rv = UI8_MAX;
   else{
      int i;

# ifdef HAVE_WCWIDTH
      rv = ((i = wcwidth(wc)) > 0) ? (ui8_t)i : 0;
# else
      rv = iswprint(wc) ? 1 + (wc >= 0x1100u) : 0; /* TODO use S-CText */
# endif
   }
   NYD2_LEAVE;
   return rv;
}

static void
a_tty_check_grow(struct a_tty_line *tlp, ui32_t no SMALLOC_DEBUG_ARGS){
   ui32_t cmax;
   NYD2_ENTER;

   if(UNLIKELY((cmax = tlp->tl_count + no) > tlp->tl_count_max)){
      size_t i;

      i = cmax * sizeof(struct a_tty_cell) + 2 * sizeof(struct a_tty_cell);
      if(LIKELY(i >= *tlp->tl_x_bufsize)){
         hold_all_sigs(); /* XXX v15 drop */
         i <<= 1;
         tlp->tl_line.cbuf =
         *tlp->tl_x_buf = (srealloc)(*tlp->tl_x_buf, i SMALLOC_DEBUG_ARGSCALL);
         rele_all_sigs(); /* XXX v15 drop */
      }
      tlp->tl_count_max = cmax;
      *tlp->tl_x_bufsize = i;
   }
   NYD2_LEAVE;
}

static ssize_t
a_tty_cell2dat(struct a_tty_line *tlp){
   size_t len, i;
   NYD2_ENTER;

   len = 0;

   if(LIKELY((i = tlp->tl_count) > 0)){
      struct a_tty_cell const *tcap;

      tcap = tlp->tl_line.cells;
      do{
         memcpy(tlp->tl_line.cbuf + len, tcap->tc_cbuf, tcap->tc_count);
         len += tcap->tc_count;
      }while(++tcap, --i > 0);
   }

   tlp->tl_line.cbuf[len] = '\0';
   NYD2_LEAVE;
   return (ssize_t)len;
}

static void
a_tty_cell2save(struct a_tty_line *tlp){
   size_t len, i;
   struct a_tty_cell *tcap;
   NYD2_ENTER;

   tlp->tl_savec.s = NULL;
   tlp->tl_savec.l = 0;

   if(UNLIKELY(tlp->tl_count == 0))
      goto jleave;

   for(tcap = tlp->tl_line.cells, len = 0, i = tlp->tl_count; i > 0;
         ++tcap, --i)
      len += tcap->tc_count;

   tlp->tl_savec.s = salloc((tlp->tl_savec.l = len) +1);

   for(tcap = tlp->tl_line.cells, len = 0, i = tlp->tl_count; i > 0;
         ++tcap, --i){
      memcpy(tlp->tl_savec.s + len, tcap->tc_cbuf, tcap->tc_count);
      len += tcap->tc_count;
   }
   tlp->tl_savec.s[len] = '\0';
jleave:
   NYD2_LEAVE;
}

static void
a_tty_yank(struct a_tty_line *tlp, struct a_tty_cell *tcpmin,
      struct a_tty_cell *tcpmax){
   char *cp;
   struct a_tty_cell *tcp;
   size_t l;
   NYD2_ENTER;

   l = 0;
   for(tcp = tcpmin; tcp < tcpmax; ++tcp)
      l += tcp->tc_count;

   tlp->tl_yankbuf.s = cp = salloc((tlp->tl_yankbuf.l = l) +1);

   l = 0;
   for(tcp = tcpmin; tcp < tcpmax; cp += l, ++tcp)
      memcpy(cp, tcp->tc_cbuf, l = tcp->tc_count);
   *cp = '\0';
   NYD2_LEAVE;
}

static wchar_t
a_tty_vinuni(struct a_tty_line *tlp){
   char buf[16], *eptr;
   union {size_t i; long l;} u;
   wchar_t wc;
   NYD2_ENTER;

   wc = 0;

   tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
   if(!n_termcap_cmdx(n_TERMCAP_CMD_cr) ||
         !n_termcap_cmd(n_TERMCAP_CMD_ce, 0, -1))
      goto jleave;

   /* C99 */{
      struct str const *cpre, *csuf;
#ifdef HAVE_COLOUR
      struct n_colour_pen *cpen;

      cpen = n_colour_pen_create(n_COLOUR_ID_MLE_PROMPT, NULL);
      if((cpre = n_colour_pen_to_str(cpen)) != NULL)
         csuf = n_colour_reset_to_str();
      else
         csuf = NULL;
#else
      cpre = csuf = NULL;
#endif
      printf(_("%sPlease enter Unicode code point:%s "),
         (cpre != NULL ? cpre->s : ""), (csuf != NULL ? csuf->s : ""));
   }
   fflush(stdout);

   buf[sizeof(buf) -1] = '\0';
   for(u.i = 0;;){
      if(read(STDIN_FILENO, &buf[u.i], 1) != 1){
         if(errno == EINTR) /* xxx #if !SA_RESTART ? */
            continue;
         goto jleave;
      }
      if(buf[u.i] == '\n')
         break;
      if(!hexchar(buf[u.i])){
         char const emsg[] = "[0-9a-fA-F]";

         LCTA(sizeof emsg <= sizeof(buf));
         memcpy(buf, emsg, sizeof emsg);
         goto jerr;
      }

      putc(buf[u.i], stdout);
      fflush(stdout);
      if(++u.i == sizeof buf)
         goto jerr;
   }
   buf[u.i] = '\0';

   u.l = strtol(buf, &eptr, 16);
   if(u.l <= 0 || u.l >= 0x10FFFF/* XXX magic; CText */ || *eptr != '\0'){
jerr:
      n_err(_("\nInvalid input: %s\n"), buf);
      goto jleave;
   }

   wc = (wchar_t)u.l;
jleave:
   NYD2_LEAVE;
   return wc;
}

static bool_t
a_tty_vi_refresh(struct a_tty_line *tlp){
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

   ui32_t f, w, phy_wid_base, phy_wid, phy_base, phy_cur, cnt, lstcur, cur,
      vi_left, vi_right, phy_nxtcur;
   struct a_tty_cell const *tccp, *tcp_left, *tcp_right, *tcxp;
   NYD2_ENTER;
   n_LCTA(UICMP(64, a__LAST, <, UI32_MAX), "Flag bits excess storage datatype");

   f = tlp->tl_vi_flags;
   tlp->tl_vi_flags = (f & ~(a_TTY_VF_REFRESH | a_PERSIST_MASK)) |
         a_TTY_VF_SYNC;
   f |= a_TRUE_RV;
   if((w = tlp->tl_prompt_length) > 0)
      f |= a_HAVE_PROMPT;
   f |= a_HAVE_POSITION;

   /* XXX We don't have a OnTerminalResize event (see main.c) yet, so we need
    * XXX to reevaluate our circumstances over and over again */
   /* Don't display prompt or position indicator on very small screens */
   if((phy_wid_base = (ui32_t)scrnwidth) <= a_TTY_WIDTH_RIPOFF)
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
   lstcur = tlp->tl_lst_cursor;

   /* XXX Assume dirty screen if shrunk */
   if(cnt < tlp->tl_lst_count)
      f |= a_TTY_VF_MOD_DIRTY;

   /* TODO Without HAVE_TERMCAP, it would likely be much cheaper to simply
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
         if(fputs(tlp->tl_prompt, stdout) == EOF)
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
   if(w == UI8_MAX) /* TODO yet TAB == SPC */
      w = 1;
   while(tcp_left > tlp->tl_line.cells){
      ui16_t cw = tcp_left[-1].tc_width;

      if(cw == UI8_MAX) /* TODO yet TAB == SPC */
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

   /* Then search for right boundary.  We always leave the rightmost column
    * empty because some terminals [cw]ould wrap the line if we write into
    * that.  XXX terminfo(5)/termcap(5) have the semi_auto_right_margin/sam/YE
    * XXX capability to indicate this, but we don't look at that */
   phy_wid = phy_wid_base - phy_base;
   tcp_right = tlp->tl_line.cells + cnt;

   while(&tccp[1] < tcp_right){
      ui16_t cw = tccp[1].tc_width;
      ui32_t i;

      if(cw == UI8_MAX) /* TODO yet TAB == SPC */
         cw = 1;
      i = w + cw;
      if(i > phy_wid)
         break;
      w = i;
      ++tccp;
   }
   vi_right = w - vi_left;

   /* If the complete line including prompt fits on the screen, show prompt */
   if(--tcp_right == tccp){
      f |= a_RIGHT_MAX;

      /* Since we did brute-force walk also for the left boundary we may end up
       * in a situation were anything effectively fits on the screen, including
       * the prompt that is, but were we don't recognize this since we
       * restricted the search to fit in some visual viewpoint.  Therefore try
       * again to extend the left boundary to overcome that */
      if(!(f & a_LEFT_MIN)){
         struct a_tty_cell const *tc1p = tlp->tl_line.cells;
         ui32_t vil1 = vi_left;

         assert(!(f & a_SHOW_PROMPT));
         w += tlp->tl_prompt_width;
         for(tcxp = tcp_left;;){
            ui32_t i = tcxp[-1].tc_width;

            if(i == UI8_MAX) /* TODO yet TAB == SPC */
               i = 1;
            vil1 += i;
            i += w;
            if(i > phy_wid)
               break;
            w = i;
            if(--tcxp == tc1p){
               tcp_left = tc1p;
               vi_left = vil1;
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
      if(fputs(tlp->tl_prompt, stdout) == EOF)
         goto jerr;
      phy_cur = phy_nxtcur;
      f |= a_VISIBLE_PROMPT;
   }else
      f &= ~a_VISIBLE_PROMPT;

/* FIXME reposition cursor for paint */
   for(w = phy_nxtcur; tcp_left <= tcp_right; ++tcp_left){
      ui16_t cw;

      cw = tcp_left->tc_width;

      if(LIKELY(!tcp_left->tc_novis)){
         if(fwrite(tcp_left->tc_cbuf, sizeof *tcp_left->tc_cbuf,
               tcp_left->tc_count, stdout) != tcp_left->tc_count)
            goto jerr;
      }else{ /* XXX Shouldn't be here <-> CText, ui_str.c */
         char wbuf[8]; /* XXX magic */

         if(options & OPT_UNICODE){
            ui32_t wc;

            wc = (ui32_t)tcp_left->tc_wc;
            if((wc & ~0x1Fu) == 0)
               wc |= 0x2400;
            else if(wc == 0x7F)
               wc = 0x2421;
            else
               wc = 0x2426;
            n_utf32_to_utf8(wc, wbuf);
         }else
            wbuf[0] = '?', wbuf[1] = '\0';

         if(fputs(wbuf, stdout) == EOF)
            goto jerr;
         cw = 1;
      }

      if(cw == UI8_MAX) /* TODO yet TAB == SPC */
         cw = 1;
      w += cw;
      if(tcp_left == tccp)
         phy_nxtcur = w;
      phy_cur += cw;
   }

   /* Write something position marker alike if it doesn't fit on screen */
   if((f & a_HAVE_POSITION) &&
         ((f & (a_LEFT_MIN | a_RIGHT_MAX)) != (a_LEFT_MIN | a_RIGHT_MAX) ||
          ((f & a_HAVE_PROMPT) && !(f & a_SHOW_PROMPT)))){
# ifdef HAVE_COLOUR
      char *posbuf = tlp->tl_pos_buf, *pos = tlp->tl_pos;
# else
      char posbuf[5], *pos = posbuf;

      pos[4] = '\0';
# endif

      if(phy_cur != (w = phy_wid_base) &&
            !n_termcap_cmd(n_TERMCAP_CMD_ch, phy_cur = w, 0))
         goto jerr;

      *pos++ = '|';
      if((f & a_LEFT_MIN) && (!(f & a_HAVE_PROMPT) || (f & a_SHOW_PROMPT)))
         memcpy(pos, "^.+", 3);
      else if(f & a_RIGHT_MAX)
         memcpy(pos, ".+$", 3);
      else{
         /* Theoretical line length limit a_TTY_LINE_MAX, choose next power of
          * ten (10 ** 10) to represent 100 percent, since we don't have a macro
          * that generates a constant, and i don't trust the standard "u type
          * suffix automatically scales" calculate the large number */
         static char const itoa[] = "0123456789";

         ui64_t const fact100 = (ui64_t)0x3B9ACA00u * 10u, fact = fact100 / 100;
         ui32_t i = (ui32_t)(((fact100 / cnt) * tlp->tl_cursor) / fact);
         n_LCTA(a_TTY_LINE_MAX <= SI32_MAX, "a_TTY_LINE_MAX too large");

         if(i < 10)
            pos[0] = ' ', pos[1] = itoa[i];
         else
            pos[1] = itoa[i % 10], pos[0] = itoa[i / 10];
         pos[2] = '%';
      }

      if(fputs(posbuf, stdout) == EOF)
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

      if(cw == UI8_MAX) /* TODO yet TAB == SPC */
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

   NYD2_LEAVE;
   return ((f & a_TRUE_RV) != 0);
jerr:
   f &= ~a_TRUE_RV;
   goto jleave;
}

static si32_t
a_tty_wboundary(struct a_tty_line *tlp, si32_t dir){
   bool_t anynon;
   struct a_tty_cell *tcap;
   ui32_t cur, cnt;
   si32_t rv;
   NYD2_ENTER;

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
      if(iswblank(wc) || iswpunct(wc)){
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
   NYD2_LEAVE;
   return rv;
}

static void
a_tty_khome(struct a_tty_line *tlp, bool_t dobell){
   ui32_t f;
   NYD2_ENTER;

   if(LIKELY(tlp->tl_cursor > 0)){
      tlp->tl_cursor = 0;
      f = a_TTY_VF_MOD_CURSOR;
   }else if(dobell)
      f = a_TTY_VF_BELL;
   else
      f = a_TTY_VF_NONE;

   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
}

static void
a_tty_kend(struct a_tty_line *tlp){
   ui32_t f;
   NYD2_ENTER;

   if(LIKELY(tlp->tl_cursor < tlp->tl_count)){
      tlp->tl_cursor = tlp->tl_count;
      f = a_TTY_VF_MOD_CURSOR;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
}

static void
a_tty_kbs(struct a_tty_line *tlp){
   ui32_t f, cur, cnt;
   NYD2_ENTER;

   cur = tlp->tl_cursor;
   cnt = tlp->tl_count;

   if(LIKELY(cur > 0)){
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
   NYD2_LEAVE;
}

static void
a_tty_kkill(struct a_tty_line *tlp, bool_t dobell){
   ui32_t i;
   NYD2_ENTER;

   if(LIKELY((i = tlp->tl_cursor) < tlp->tl_count)){
      struct a_tty_cell *tcap;

      tcap = &tlp->tl_line.cells[0];
      a_tty_yank(tlp, &tcap[i], &tcap[tlp->tl_count]);
      tlp->tl_count = i;
      i = a_TTY_VF_MOD_CONTENT;
   }else if(dobell)
      i = a_TTY_VF_BELL;
   else
      i = a_TTY_VF_NONE;

   tlp->tl_vi_flags |= i;
   NYD2_LEAVE;
}

static si32_t
a_tty_kdel(struct a_tty_line *tlp){
   ui32_t cur, cnt, f;
   si32_t i;
   NYD2_ENTER;

   cur = tlp->tl_cursor;
   cnt = tlp->tl_count;
   i = (si32_t)(cnt - cur);

   if(LIKELY(i > 0)){
      tlp->tl_count = --cnt;

      if(LIKELY(--i > 0)){
         struct a_tty_cell *tcap;

         tcap = &tlp->tl_line.cells[cur];
         memmove(tcap, &tcap[1], (ui32_t)i * sizeof(*tcap));
      }
      f = a_TTY_VF_MOD_CONTENT;
   }else if(cnt == 0 && !ok_blook(ignoreeof)){
      putchar('^');
      putchar('D');
      i = -1;
      f = a_TTY_VF_NONE;
   }else{
      i = 0;
      f = a_TTY_VF_BELL;
   }

   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
   return i;
}

static void
a_tty_kleft(struct a_tty_line *tlp){
   ui32_t f;
   NYD2_ENTER;

   if(LIKELY(tlp->tl_cursor > 0)){
      --tlp->tl_cursor;
      f = a_TTY_VF_MOD_CURSOR;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
}

static void
a_tty_kright(struct a_tty_line *tlp){
   ui32_t i;
   NYD2_ENTER;

   if(LIKELY((i = tlp->tl_cursor + 1) <= tlp->tl_count)){
      tlp->tl_cursor = i;
      i = a_TTY_VF_MOD_CURSOR;
   }else
      i = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= i;
   NYD2_LEAVE;
}

static void
a_tty_kbwddelw(struct a_tty_line *tlp){
   struct a_tty_cell *tcap;
   ui32_t cnt, cur, f;
   si32_t i;
   NYD2_ENTER;

   if(UNLIKELY((i = a_tty_wboundary(tlp, -1)) <= 0)){
      f = (i < 0) ? a_TTY_VF_BELL : a_TTY_VF_NONE;
      goto jleave;
   }

   cnt = tlp->tl_count - (ui32_t)i;
   cur = tlp->tl_cursor - (ui32_t)i;
   tcap = &tlp->tl_line.cells[cur];

   a_tty_yank(tlp, &tcap[0], &tcap[i]);

   if((tlp->tl_count = cnt) != (tlp->tl_cursor = cur)){
      cnt -= cur;
      memmove(&tcap[0], &tcap[i], cnt * sizeof(*tcap)); /* FIXME*/
   }

   f = a_TTY_VF_MOD_CURSOR | a_TTY_VF_MOD_CONTENT;
jleave:
   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
}

static void
a_tty_kgow(struct a_tty_line *tlp, si32_t dir){
   ui32_t f;
   si32_t i;
   NYD2_ENTER;

   if(UNLIKELY((i = a_tty_wboundary(tlp, dir)) <= 0))
      f = (i < 0) ? a_TTY_VF_BELL : a_TTY_VF_NONE;
   else{
      if(dir < 0)
         i = -i;
      tlp->tl_cursor += (ui32_t)i;
      f = a_TTY_VF_MOD_CURSOR;
   }

   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
}

static void
a_tty_kother(struct a_tty_line *tlp, wchar_t wc){
   /* Append if at EOL, insert otherwise;
    * since we may move around character-wise, always use a fresh ps */
   mbstate_t ps;
   struct a_tty_cell tc, *tcap;
   ui32_t f, cur, cnt;
   NYD2_ENTER;

   f = a_TTY_VF_NONE;

   n_LCTA(a_TTY_LINE_MAX <= SI32_MAX, "a_TTY_LINE_MAX too large");
   if(tlp->tl_count + 1 >= a_TTY_LINE_MAX){
      n_err(_("Stop here, we can't extend line beyond size limit\n"));
      goto jleave;
   }

   /* First init a cell and see wether we'll really handle this wc */
   memset(&ps, 0, sizeof ps);
   /* C99 */{
      size_t l;

      l = wcrtomb(tc.tc_cbuf, tc.tc_wc = wc, &ps);
      if(UNLIKELY(l > MB_LEN_MAX)){
jemb:
         n_err(_("wcrtomb(3) error: too many multibyte character bytes\n"));
         goto jleave;
      }
      tc.tc_count = (ui16_t)l;

      if(UNLIKELY((options & OPT_ENC_MBSTATE) != 0)){
         l = wcrtomb(&tc.tc_cbuf[l], L'\0', &ps);
         if(LIKELY(l == 1))
            /* Only NUL terminator */;
         else if(LIKELY(--l < MB_LEN_MAX))
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
jleave:
   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
}

static ui32_t
a_tty_kht(struct a_tty_line *tlp){
   struct str orig, bot, topp, sub, exp;
   struct a_tty_cell *cword, *ctop, *cx;
   bool_t set_savec = FAL0;
   ui32_t f, rv;
   NYD2_ENTER;

   /* We cannot expand an empty line */
   if(UNLIKELY(tlp->tl_count == 0)){
      rv = 0;
      f = a_TTY_VF_BELL; /* xxx really bell if no expansion is possible? */
      goto jleave;
   }

   /* Get plain line data; if this is the first expansion/xy, update the
    * very original content so that ^G gets the origin back */
   orig = tlp->tl_savec;
   a_tty_cell2save(tlp);
   exp = tlp->tl_savec;
   if(orig.s != NULL)
      tlp->tl_savec = orig;
   else
      set_savec = TRU1;
   orig = exp;

   /* Find the word to be expanded */

   cword = tlp->tl_line.cells;
   ctop = cword + tlp->tl_cursor;
   cx = cword + tlp->tl_count;

   /* topp: separate data right of cursor */
   if(cx > ctop){
      for(rv = 0; --cx > ctop; --cx)
         rv += cx->tc_count;
      topp.l = rv;
      topp.s = orig.s + orig.l - rv;
   }else
      topp.s = NULL, topp.l = 0;

   /* bot, sub: we cannot expand the entire data left of cursor, but only
    * the last "word", so separate them */
   /* TODO Context-sensitive completion: stop for | too, try expand shell?
    * TODO Ditto, "cx==cword(+space)": try mail command expansion? */
   while(cx > cword && !iswspace(cx[-1].tc_wc))
      --cx;
   for(rv = 0; cword < cx; ++cword)
      rv += cword->tc_count;
   sub =
   bot = orig;
   bot.l = rv;
   sub.s += rv;
   sub.l -= rv;
   sub.l -= topp.l;

   /* Leave room for "implicit asterisk" expansion, as below */
   if(sub.l == 0){
      sub.s = UNCONST("*");
      sub.l = 1;
   }else{
      exp.s = salloc(sub.l + 1 +1);
      memcpy(exp.s, sub.s, sub.l);
      exp.s[sub.l] = '\0';
      sub.s = exp.s;
   }

   /* TODO there is a TODO note upon fexpand() with multi-return;
    * TODO if that will change, the if() below can be simplified.
    * TODO Also: iff multireturn, offer a numbered list of possible
    * TODO expansions, 0 meaning "none" and * (?) meaning "all",
    * TODO go over the pager as necesary (use *crt*, generalized) */
   /* Super-Heavy-Metal: block all sigs, avoid leaks on jump */
jredo:
   hold_all_sigs();
   exp.s = fexpand(sub.s, a_TTY_TAB_FEXP_FL);
   rele_all_sigs();

   if(exp.s == NULL || (exp.l = strlen(exp.s)) == 0)
      goto jnope;
   /* xxx That is not really true since the limit counts characters not bytes */
   n_LCTA(a_TTY_LINE_MAX <= SI32_MAX, "a_TTY_LINE_MAX too large");
   if(exp.l + 1 >= a_TTY_LINE_MAX){
      n_err(_("Tabulator expansion would extend beyond line size limit\n"));
      goto jnope;
   }

   /* If the expansion equals the original string, assume the user wants what
    * is usually known as tab completion, append `*' and restart */
   if(exp.l == sub.l && !strcmp(exp.s, sub.s)){
      if(sub.s[sub.l - 1] == '*')
         goto jnope;
      sub.s[sub.l++] = '*';
      sub.s[sub.l] = '\0';
      goto jredo;
   }

   orig.l = bot.l + exp.l + topp.l;
   orig.s = salloc(orig.l + 5 +1);
   if((rv = (ui32_t)bot.l) > 0)
      memcpy(orig.s, bot.s, rv);
   memcpy(orig.s + rv, exp.s, exp.l);
   rv += exp.l;
   if(topp.l > 0){
      memcpy(orig.s + rv, topp.s, topp.l);
      rv += topp.l;
   }
   orig.s[rv] = '\0';

   tlp->tl_defc = orig;
   tlp->tl_count = tlp->tl_cursor = 0;
   f = a_TTY_VF_MOD_DIRTY;
jleave:
   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
   return rv;

jnope:
   /* If we've provided a default content, but failed to expand, there is
    * nothing we can "revert to": drop that default again */
   if(set_savec){
      tlp->tl_savec.s = NULL;
      tlp->tl_savec.l = 0;
   }
   rv = 0;
   f = a_TTY_VF_NONE;
   goto jleave;
}

# ifdef HAVE_HISTORY
static ui32_t
a_tty__khist_shared(struct a_tty_line *tlp, struct a_tty_hist *thp){
   ui32_t f, rv;
   NYD2_ENTER;

   if(LIKELY((tlp->tl_hist = thp) != NULL)){
      tlp->tl_defc.s = savestrbuf(thp->th_dat, thp->th_len);
      rv = tlp->tl_defc.l = thp->th_len;
      f = (tlp->tl_count > 0) ? a_TTY_VF_MOD_DIRTY : a_TTY_VF_NONE;
      tlp->tl_count = tlp->tl_cursor = 0;
   }else{
      f = a_TTY_VF_BELL;
      rv = 0;
   }

   tlp->tl_vi_flags |= f;
   NYD2_LEAVE;
   return rv;
}

static ui32_t
a_tty_khist(struct a_tty_line *tlp, bool_t backwd){
   struct a_tty_hist *thp;
   ui32_t rv;
   NYD2_ENTER;

   /* If we're not in history mode yet, save line content;
    * also, disallow forward search, then, and, of course, bail unless we
    * do have any history at all */
   if((thp = tlp->tl_hist) == NULL){
      if(!backwd)
         goto jleave;
      if((thp = a_tty.tg_hist) == NULL)
         goto jleave;
      a_tty_cell2save(tlp);
      goto jleave;
   }

   thp = backwd ? thp->th_older : thp->th_younger;
jleave:
   rv = a_tty__khist_shared(tlp, thp);
   NYD2_LEAVE;
   return rv;
}

static ui32_t
a_tty_krhist(struct a_tty_line *tlp){
   struct str orig_savec;
   struct a_tty_hist *thp;
   ui32_t rv;
   NYD2_ENTER;

   thp = NULL;

   /* We cannot complete an empty line */
   if(UNLIKELY(tlp->tl_count == 0)){
      /* XXX The upcoming hard reset would restore a set savec buffer,
       * XXX so forcefully reset that.  A cleaner solution would be to
       * XXX reset it whenever a restore is no longer desired */
      tlp->tl_savec.s = NULL;
      tlp->tl_savec.l = 0;
      goto jleave;
   }

   if((thp = tlp->tl_hist) == NULL){
      if((thp = a_tty.tg_hist) == NULL)
         goto jleave;
      orig_savec.s = NULL;
      orig_savec.l = 0; /* silence CC */
   }else if((thp = thp->th_older) == NULL)
      goto jleave;
   else
      orig_savec = tlp->tl_savec;

   if(orig_savec.s == NULL)
      a_tty_cell2save(tlp);

   for(; thp != NULL; thp = thp->th_older)
      if(is_prefix(tlp->tl_savec.s, thp->th_dat))
         break;

   if(orig_savec.s != NULL)
      tlp->tl_savec = orig_savec;
jleave:
   rv = a_tty__khist_shared(tlp, thp);
   NYD2_LEAVE;
   return rv;
}
# endif /* HAVE_HISTORY */

static ssize_t
a_tty_readline(struct a_tty_line *tlp, size_t len SMALLOC_DEBUG_ARGS){
   /* We want to save code, yet we may have to incorporate a lines'
    * default content and / or default input to switch back to after some
    * history movement; let "len > 0" mean "have to display some data
    * buffer", and only otherwise read(2) it */
   mbstate_t ps[2];
   char cbuf_base[MB_LEN_MAX * 2], *cbuf, *cbufp, cursor_maybe, cursor_store;
   wchar_t wc;
   ssize_t rv;
   NYD_ENTER;

jrestart:
   memset(ps, 0, sizeof ps);
   tlp->tl_vi_flags |= a_TTY_VF_REFRESH | a_TTY_VF_SYNC;

   for(cursor_maybe = cursor_store = 0;;){
      /* Ensure we have valid pointers, and room for grow */
      a_tty_check_grow(tlp, (len == 0 ? 1 : (ui32_t)len)
         SMALLOC_DEBUG_ARGSCALL);

      /* Handle visual state flags, except in buffer take-over mode */
      if(len == 0){
         if(tlp->tl_vi_flags & a_TTY_VF_BELL){
            tlp->tl_vi_flags |= a_TTY_VF_SYNC;
            putchar('\a');
         }

         if(tlp->tl_vi_flags & a_TTY_VF_REFRESH){
            if(!a_tty_vi_refresh(tlp)){
               clearerr(stdout); /* xxx I/O layer rewrite */
               n_err(_("Visual refresh failed!  Is $TERM set correctly?\n"
                  "  Setting *line-editor-disable* to get us through!\n"));
               ok_bset(line_editor_disable, TRU1);
               rv = -1;
               goto jleave;
            }
         }

         if(tlp->tl_vi_flags & a_TTY_VF_SYNC){
            tlp->tl_vi_flags &= ~a_TTY_VF_SYNC;
            fflush(stdout);
         }

         tlp->tl_vi_flags &= ~a_TTY_VF_ALL_MASK;
      }

      /* Ready for messing around.
       * Normal read(2)?  Else buffer-takeover: speed this one up */
      if(len == 0){
         cbufp =
         cbuf = cbuf_base;
      }else{
         assert(tlp->tl_defc.l > 0 && tlp->tl_defc.s != NULL);
         cbufp =
         cbuf = tlp->tl_defc.s + (tlp->tl_defc.l - len);
         cbufp += len;
      }

      /* Read in the next complete multibyte character */
      for(;;){
         if(len == 0){
            /* Let me at least once dream of iomon(itor), timer with one-shot,
             * enwrapped with key_event and key_sequence_event, all driven by
             * an event_loop */
            if((rv = read(STDIN_FILENO, cbufp, 1)) < 1){
               if(errno == EINTR) /* xxx #if !SA_RESTART ? */
                  continue;
               goto jleave;
            }
            ++cbufp;
         }

         rv = (ssize_t)mbrtowc(&wc, cbuf, PTR2SIZE(cbufp - cbuf), &ps[0]);
         if(rv <= 0){
            /* Any error during take-over can only result in a hard reset;
             * Otherwise, if it's a hard error, or if too many redundant shift
             * sequences overflow our buffer, also perform a hard reset */
            if(len != 0 || rv == -1 ||
                  sizeof cbuf_base == PTR2SIZE(cbufp - cbuf)){
               tlp->tl_savec.s = tlp->tl_defc.s = NULL;
               tlp->tl_savec.l = tlp->tl_defc.l = len = 0;
               tlp->tl_vi_flags |= a_TTY_VF_BELL;
               goto jreset;
            }
            /* Otherwise, due to the way we deal with the buffer, we need to
             * restore the mbstate_t from before this conversion */
            ps[0] = ps[1];
            continue;
         }

         /* Buffer takeover completed? */
         if(len != 0 && (len -= (size_t)rv) == 0){
            tlp->tl_defc.s = NULL;
            tlp->tl_defc.l = 0;
         }
         ps[1] = ps[0];
         break;
      }

      /* Don't interpret control bytes during buffer take-over */
      if(cbuf != cbuf_base)
         goto jprint;
      switch(wc){
      case 'A' ^ 0x40: /* cursor home */
         a_tty_khome(tlp, TRU1);
         break;
      case 'B' ^ 0x40: /* backward character */
j_b:
         a_tty_kleft(tlp);
         break;
      /* 'C': interrupt (CTRL-C) */
      case 'D' ^ 0x40: /* delete char forward if any, else EOF */
         if((rv = a_tty_kdel(tlp)) < 0)
            goto jleave;
         break;
      case 'E' ^ 0x40: /* end of line */
         a_tty_kend(tlp);
         break;
      case 'F' ^ 0x40: /* forward character */
j_f:
         a_tty_kright(tlp);
         break;
      /* 'G' below */
      case 'H' ^ 0x40: /* backspace */
      case '\177':
         a_tty_kbs(tlp);
         break;
      case 'I' ^ 0x40: /* horizontal tab */
         if((len = a_tty_kht(tlp)) > 0)
            goto jrestart;
         goto jbell;
      case 'J' ^ 0x40: /* NL (\n) */
         goto jdone;
      case 'G' ^ 0x40: /* full reset */
jreset:
         /* FALLTHRU */
      case 'U' ^ 0x40: /* ^U: ^A + ^K */
         a_tty_khome(tlp, FAL0);
         /* FALLTHRU */
      case 'K' ^ 0x40: /* kill from cursor to end of line */
         a_tty_kkill(tlp, (wc == ('K' ^ 0x40) || tlp->tl_count == 0));
         /* (Handle full reset?) */
         if(wc == ('G' ^ 0x40)) {
# ifdef HAVE_HISTORY
            tlp->tl_hist = NULL;
# endif
            if((len = tlp->tl_savec.l) != 0){
               tlp->tl_defc = tlp->tl_savec;
               tlp->tl_savec.s = NULL;
               tlp->tl_savec.l = 0;
            }else
               len = tlp->tl_defc.l;
         }
         goto jrestart;
      case 'L' ^ 0x40: /* repaint line */
         tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
         break;
      /* 'M': CR (\r) */
      case 'N' ^ 0x40: /* history next */
j_n:
# ifdef HAVE_HISTORY
         if(tlp->tl_hist == NULL)
            goto jbell;
         if((len = a_tty_khist(tlp, FAL0)) > 0)
            goto jrestart;
         wc = 'G' ^ 0x40;
         goto jreset;
# else
         goto jbell;
# endif
      case 'O' ^ 0x40:
         tlp->tl_reenter_after_cmd = "dp";
         goto jdone;
      case 'P' ^ 0x40: /* history previous */
j_p:
# ifdef HAVE_HISTORY
         if((len = a_tty_khist(tlp, TRU1)) > 0)
            goto jrestart;
         wc = 'G' ^ 0x40;
         goto jreset;
# else
         goto jbell;
# endif
      /* 'Q': no code */
      case 'R' ^ 0x40: /* reverse history search */
# ifdef HAVE_HISTORY
         if((len = a_tty_krhist(tlp)) > 0)
            goto jrestart;
         wc = 'G' ^ 0x40;
         goto jreset;
# else
         goto jbell;
# endif
      /* 'S': no code */
      /* 'U' above */
      case 'V' ^ 0x40: /* insert (Unicode) character */
         if((wc = a_tty_vinuni(tlp)) > 0)
            goto jprint;
         goto jbell;
      case 'W' ^ 0x40: /* backward delete "word" */
         a_tty_kbwddelw(tlp);
         break;
      case 'X' ^ 0x40: /* move cursor forward "word" */
         a_tty_kgow(tlp, +1);
         break;
      case 'Y' ^ 0x40: /* move cursor backward "word" */
         a_tty_kgow(tlp, -1);
         break;
      /* 'Z': suspend (CTRL-Z) */
      case 0x1B:
         if(cursor_maybe++ != 0)
            goto jreset;
         continue;
      default:
         /* XXX Handle usual ^[[[ABCD1456] cursor keys: UGLY,"MAGIC",INFLEX */
         if(cursor_maybe > 0){
            if(++cursor_maybe == 2){
               if(wc == L'[')
                  continue;
               cursor_maybe = 0;
            }else if(cursor_maybe == 3){
               cursor_maybe = 0;
               switch(wc){
               default:    break;
               case L'A':  goto j_p;
               case L'B':  goto j_n;
               case L'C':  goto j_f;
               case L'D':  goto j_b;
               case L'H':
                  cursor_store = '0';
                  goto J_xterm_noapp;
               case L'F':
                  cursor_store = '$';
                  goto J_xterm_noapp;
               case L'1':
               case L'4':
               case L'5':
               case L'6':
                  cursor_store = ((wc == L'1') ? '0' :
                        (wc == L'4' ? '$' : (wc == L'5' ? '-' : '+')));
                  cursor_maybe = 3;
                  continue;
               }
               a_tty_kother(tlp, L'[');
            }else{
               cursor_maybe = 0;
               if(wc == L'~') J_xterm_noapp:{
                  char *cp = salloc(3);

                  cp[0] = 'z';
                  cp[1] = cursor_store;
                  cp[2] = '\0';
                  tlp->tl_reenter_after_cmd = cp;
                  goto jdone;
               }else if(cursor_store == '-' && (wc == L'A' || wc == L'B')){
                  char const cmd[] = "dotmove";
                  char *cp = salloc(sizeof(cmd) -1 + 1 +1);

                  memcpy(cp, cmd, sizeof(cmd) -1);
                  cp[sizeof(cmd) -1] = (wc != L'A') ? '+' : cursor_store;
                  cp[sizeof(cmd)] = '\0';
                  tlp->tl_reenter_after_cmd = cp;
                  goto jdone;
               }
               a_tty_kother(tlp, L'[');
               a_tty_kother(tlp, (wchar_t)cursor_store);
               cursor_store = 0;
            }
         }
jprint:
         if(iswprint(wc)){
            a_tty_kother(tlp, wc);
            /* Don't clear the history during takeover..
             * ..and also avoid fflush()ing unless we've worked entire buffer */
            if(len > 0)
               continue;
# ifdef HAVE_HISTORY
            if(cbuf == cbuf_base)
               tlp->tl_hist = NULL;
# endif
         }else{
jbell:
            tlp->tl_vi_flags |= a_TTY_VF_BELL;
         }
         break;
      }
      tlp->tl_vi_flags |= a_TTY_VF_SYNC;
   }

   /* We have a completed input line, convert the struct cell data to its
    * plain character equivalent */
jdone:
   rv = a_tty_cell2dat(tlp);
jleave:
   putchar('\n');
   fflush(stdout);
   NYD_LEAVE;
   return rv;
}

FL void
n_tty_init(void){
# ifdef HAVE_HISTORY
   long hs;
   char const *v;
   char *lbuf;
   FILE *f;
   size_t lsize, cnt, llen;
# endif
   NYD_ENTER;

   /* Load the history file */
# ifdef HAVE_HISTORY
   a_TTY_HISTSIZE(hs);
   a_tty.tg_hist_size = 0;
   a_tty.tg_hist_size_max = (size_t)hs;
   if(hs == 0)
      goto jleave;

   a_TTY_HISTFILE(v);
   if(v == NULL)
      goto jleave;

   hold_all_sigs(); /* TODO too heavy, yet we may jump even here!? */
   f = fopen(v, "r"); /* TODO HISTFILE LOAD: use linebuf pool */
   if(f == NULL)
      goto jdone;
   (void)n_file_lock(fileno(f), FLT_READ, 0,0, 500);

   lbuf = NULL;
   lsize = 0;
   cnt = (size_t)fsize(f);
   while(fgetline(&lbuf, &lsize, &cnt, &llen, f, FAL0) != NULL){
      if(llen > 0 && lbuf[llen - 1] == '\n')
         lbuf[--llen] = '\0';
      if(llen == 0 || lbuf[0] == '#') /* xxx comments? noone! */
         continue;
      else{
         bool_t isgabby = (lbuf[0] == '*');

         n_tty_addhist(lbuf + isgabby, isgabby);
      }
   }
   if(lbuf != NULL)
      free(lbuf);

   fclose(f);
jdone:
   rele_all_sigs(); /* XXX remove jumps */
jleave:
# endif /* HAVE_HISTORY */
   pstate |= PS_HISTORY_LOADED;
   NYD_LEAVE;
}

FL void
n_tty_destroy(void){
# ifdef HAVE_HISTORY
   long hs;
   char const *v;
   struct a_tty_hist *thp;
   bool_t dogabby;
   FILE *f;
# endif
   NYD_ENTER;

# ifdef HAVE_HISTORY
   a_TTY_HISTSIZE(hs);
   if(hs == 0)
      goto jleave;

   a_TTY_HISTFILE(v);
   if(v == NULL)
      goto jleave;

   dogabby = ok_blook(history_gabby_persist);

   if((thp = a_tty.tg_hist) != NULL)
      for(; thp->th_older != NULL; thp = thp->th_older)
         if((dogabby || !thp->th_isgabby) && --hs == 0)
            break;

   hold_all_sigs(); /* TODO too heavy, yet we may jump even here!? */
   f = fopen(v, "w"); /* TODO temporary + rename?! */
   if(f == NULL)
      goto jdone;
   (void)n_file_lock(fileno(f), FLT_WRITE, 0,0, 500);
   if (fchmod(fileno(f), S_IRUSR | S_IWUSR) != 0)
      goto jclose;

   for(; thp != NULL; thp = thp->th_younger){
      if(dogabby || !thp->th_isgabby){
         if(thp->th_isgabby)
            putc('*', f);
         fwrite(thp->th_dat, sizeof *thp->th_dat, thp->th_len, f);
         putc('\n', f);
      }
   }
jclose:
   fclose(f);
jdone:
   rele_all_sigs(); /* XXX remove jumps */
jleave:
# endif /* HAVE_HISTORY */
   NYD_LEAVE;
}

FL void
n_tty_signal(int sig){
   sigset_t nset, oset;
   NYD_X; /* Signal handler */

   switch(sig){
   case SIGWINCH:
      /* We don't deal with SIGWINCH, yet get called from main.c */
      break;
   default:
      a_tty_term_mode(FAL0);
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
      a_tty_term_mode(TRU1);
      break;
   }
}

FL int
(n_tty_readline)(char const *prompt, char **linebuf, size_t *linesize, size_t n
      SMALLOC_DEBUG_ARGS){
   struct a_tty_line tl;
# ifdef HAVE_COLOUR
   char *posbuf, *pos;
# endif
   ui32_t plen, pwidth;
   ssize_t nn;
   NYD_ENTER;

# ifdef HAVE_COLOUR
   n_colour_env_create(n_COLOUR_CTX_MLE, FAL0);
# endif

   /* Classify prompt */
   UNINIT(plen, 0);
   UNINIT(pwidth, 0);
   if(prompt != NULL){
      size_t i = strlen(prompt);

      if(i == 0 || i >= UI32_MAX)
         prompt = NULL;
      else{
         plen = (ui32_t)i;
         /* TODO *prompt* is in multibyte and not in a_tty_cell, therefore
          * TODO we cannot handle it in parts, it's all or nothing.
          * TODO Later (S-CText, SysV signals) the prompt should be some global
          * TODO carrier thing, fully evaluated and passed around as UI-enabled
          * TODO string, then we can print it character by character */
         if((i = field_detect_width(prompt, i)) != (size_t)-1)
            pwidth = (ui32_t)i;
         else{
            n_err(_("Character set error in evaluation of prompt\n"));
            prompt = NULL;
         }
      }
   }

# ifdef HAVE_COLOUR
   /* C99 */{
      struct n_colour_pen *ccp;
      struct str const *sp;

      if(prompt != NULL &&
            (ccp = n_colour_pen_create(n_COLOUR_ID_MLE_PROMPT, NULL)) != NULL &&
            (sp = n_colour_pen_to_str(ccp)) != NULL){
         char const *ccol = sp->s;

         if((sp = n_colour_reset_to_str()) != NULL){
            size_t l1 = strlen(ccol), l2 = strlen(sp->s);
            ui32_t nplen = (ui32_t)(l1 + plen + l2);
            char *nprompt = salloc(nplen +1);

            memcpy(nprompt, ccol, l1);
            memcpy(&nprompt[l1], prompt, plen);
            memcpy(&nprompt[l1 += plen], sp->s, ++l2);

            prompt = nprompt;
            plen = nplen;
         }
      }

      /* .tl_pos_buf is a hack */
      posbuf = pos = NULL;
      if((ccp = n_colour_pen_create(n_COLOUR_ID_MLE_POSITION, NULL)) != NULL &&
            (sp = n_colour_pen_to_str(ccp)) != NULL){
         char const *ccol = sp->s;

         if((sp = n_colour_reset_to_str()) != NULL){
            size_t l1 = strlen(ccol), l2 = strlen(sp->s);

            posbuf = salloc(l1 + 4 + l2 +1);
            memcpy(posbuf, ccol, l1);
            pos = &posbuf[l1];
            memcpy(&pos[4], sp->s, ++l2);
         }
      }
      if(posbuf == NULL){
         posbuf = pos = salloc(4 +1);
         pos[4] = '\0';
      }
   }
# endif /* HAVE_COLOUR */

jredo:
   memset(&tl, 0, sizeof tl);

   if((tl.tl_prompt = prompt) != NULL){ /* XXX not re-evaluated */
      tl.tl_prompt_length = plen;
      tl.tl_prompt_width = pwidth;
   }
# ifdef HAVE_COLOUR
   tl.tl_pos_buf = posbuf;
   tl.tl_pos = pos;
# endif

   tl.tl_line.cbuf = *linebuf;
   if(n != 0){
      tl.tl_defc.s = savestrbuf(*linebuf, n);
      tl.tl_defc.l = n;
   }
   tl.tl_x_buf = linebuf;
   tl.tl_x_bufsize = linesize;

   a_tty_sigs_up();
   a_tty_term_mode(TRU1);
   nn = a_tty_readline(&tl, n SMALLOC_DEBUG_ARGSCALL);
   a_tty_term_mode(FAL0);
   a_tty_sigs_down();

   if(tl.tl_reenter_after_cmd != NULL){
      n = 0;
      n_source_command(tl.tl_reenter_after_cmd);
      goto jredo;
   }

# ifdef HAVE_COLOUR
   n_colour_env_gut(stdout);
# endif
   NYD_LEAVE;
   return (int)nn;
}

FL void
n_tty_addhist(char const *s, bool_t isgabby){
# ifdef HAVE_HISTORY
   /* Super-Heavy-Metal: block all sigs, avoid leaks+ on jump */
   ui32_t l;
   struct a_tty_hist *thp, *othp, *ythp;
# endif
   NYD_ENTER;
   UNUSED(s);
   UNUSED(isgabby);

# ifdef HAVE_HISTORY
   if(isgabby && !ok_blook(history_gabby))
      goto j_leave;

   if(a_tty.tg_hist_size_max == 0)
      goto j_leave;
   a_TTY_CHECK_ADDHIST(s, goto j_leave);

   l = (ui32_t)strlen(s);

   /* Eliminating duplicates is expensive, but simply inacceptable so
    * during the load of a potentially large history file! */
   if(pstate & PS_HISTORY_LOADED)
      for(thp = a_tty.tg_hist; thp != NULL; thp = thp->th_older)
         if(thp->th_len == l && !strcmp(thp->th_dat, s)){
            hold_all_sigs(); /* TODO */
            if(thp->th_isgabby)
               thp->th_isgabby = !!isgabby;
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
   hold_all_sigs();

   ++a_tty.tg_hist_size;
   if((pstate & PS_HISTORY_LOADED) &&
         a_tty.tg_hist_size > a_tty.tg_hist_size_max){
      --a_tty.tg_hist_size;
      if((thp = a_tty.tg_hist_tail) != NULL){
         if((a_tty.tg_hist_tail = thp->th_younger) == NULL)
            a_tty.tg_hist = NULL;
         else
            a_tty.tg_hist_tail->th_older = NULL;
         free(thp);
      }
   }

   thp = smalloc((sizeof(struct a_tty_hist) -
         VFIELD_SIZEOF(struct a_tty_hist, th_dat)) + l +1);
   thp->th_isgabby = !!isgabby;
   thp->th_len = l;
   memcpy(thp->th_dat, s, l +1);
jleave:
   if((thp->th_older = a_tty.tg_hist) != NULL)
      a_tty.tg_hist->th_younger = thp;
   else
      a_tty.tg_hist_tail = thp;
   thp->th_younger = NULL;
   a_tty.tg_hist = thp;

   rele_all_sigs();
j_leave:
# endif
   NYD_LEAVE;
}

# ifdef HAVE_HISTORY
FL int
c_history(void *v){
   C_HISTORY_SHARED;

jlist:{
   FILE *fp;
   size_t i, b;
   struct a_tty_hist *thp;

   if(a_tty.tg_hist == NULL)
      goto jleave;

   if((fp = Ftmp(NULL, "hist", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("tmpfile"), 0);
      v = NULL;
      goto jleave;
   }

   i = a_tty.tg_hist_size;
   b = 0;
   for(thp = a_tty.tg_hist; thp != NULL;
         --i, b += thp->th_len, thp = thp->th_older)
      fprintf(fp,
         "%c%4" PRIuZ ". %-50.50s (%4" PRIuZ "+%2" PRIu32 " B)\n",
         (thp->th_isgabby ? '*' : ' '), i, thp->th_dat, b, thp->th_len);

   page_or_print(fp, i);
   Fclose(fp);
   }
   goto jleave;

jclear:{
   struct a_tty_hist *thp;

   while((thp = a_tty.tg_hist) != NULL){
      a_tty.tg_hist = thp->th_older;
      free(thp);
   }
   a_tty.tg_hist_tail = NULL;
   a_tty.tg_hist_size = 0;
   }
   goto jleave;

jentry:{
   struct a_tty_hist *thp;

   if(UICMP(z, entry, <=, a_tty.tg_hist_size)){
      entry = (long)a_tty.tg_hist_size - entry;
      for(thp = a_tty.tg_hist;; thp = thp->th_older)
         if(thp == NULL)
            break;
         else if(entry-- != 0)
            continue;
         else{
            v = temporary_arg_v_store = thp->th_dat;
            goto jleave;
         }
   }
   v = NULL;
   }
   goto jleave;
}
# endif /* HAVE_HISTORY */
#endif /* HAVE_MLE */

/*
 * The really-nothing-at-all implementation
 */
#if !defined HAVE_READLINE && !defined HAVE_MLE

FL void
n_tty_init(void){
   NYD_ENTER;
   NYD_LEAVE;
}

FL void
n_tty_destroy(void){
   NYD_ENTER;
   NYD_LEAVE;
}

FL void
n_tty_signal(int sig){
   NYD_X; /* Signal handler */
   UNUSED(sig);

# ifdef HAVE_TERMCAP
   switch(sig){
   default:{
      sigset_t nset, oset;

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
      break;
   }
   }
# endif /* HAVE_TERMCAP */
}

FL int
(n_tty_readline)(char const *prompt, char **linebuf, size_t *linesize, size_t n
      SMALLOC_DEBUG_ARGS){
   int rv;
   NYD_ENTER;

   if(prompt != NULL){
      if(*prompt != '\0')
         fputs(prompt, stdout);
      fflush(stdout);
   }
# ifdef HAVE_TERMCAP
   a_tty_sigs_up();
# endif
   rv = (readline_restart)(stdin, linebuf, linesize,n SMALLOC_DEBUG_ARGSCALL);
# ifdef HAVE_TERMCAP
   a_tty_sigs_down();
# endif
   NYD_LEAVE;
   return rv;
}

FL void
n_tty_addhist(char const *s, bool_t isgabby){
   NYD_ENTER;
   UNUSED(s);
   UNUSED(isgabby);
   NYD_LEAVE;
}
#endif /* nothing at all */

#undef a_TTY_SIGNALS
/* s-it-mode */
