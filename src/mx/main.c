/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Startup and initialization.
 *@ This file is also used to materialize externals.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
#undef su_FILE
#define su_FILE main
#define mx_SOURCE
#define mx_SOURCE_MASTER

#include "mx/nail.h"

#include <sys/ioctl.h>

#include <pwd.h>

#include <su/avopt.h>
#include <su/cs.h>
#include <su/icodec.h>

#include "mx/iconv.h"
#include "mx/names.h"

/* TODO fake */
#include "su/code-in.h"

struct a_arg{
   struct a_arg *aa_next;
   char const *aa_file;
};

/* (extern, but not with amalgamation, so define here) */
VL char const n_weekday_names[7 + 1][4] = {
   "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", ""
};
VL char const n_month_names[12 + 1][4] = {
   "Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", ""
};
VL char const n_uagent[sizeof VAL_UAGENT] = VAL_UAGENT;
#ifdef mx_HAVE_UISTRINGS
VL char const n_error[sizeof n_ERROR] = N_(n_ERROR);
#endif
VL char const n_path_devnull[sizeof n_PATH_DEVNULL] = n_PATH_DEVNULL;
VL char const n_0[2] = "0";
VL char const n_1[2] = "1";
VL char const n_m1[3] = "-1";
VL char const n_em[2] = "!";
VL char const n_ns[2] = "#";
VL char const n_star[2] = "*";
VL char const n_hy[2] = "-";
VL char const n_qm[2] = "?";
VL char const n_at[2] = "@";

/* Perform basic startup initialization */
static void a_main_startup(void);

/* Grow a char** */
static uz a_main_grow_cpp(char const ***cpp, uz newsize, uz oldcnt);

/* Setup some variables which we require to be valid / verified */
static void a_main_setup_vars(void);

/* We're in an interactive session - compute what the screen size for printing
 * headers etc. should be; notify tty upon resize if *is_sighdl* is not 0.
 * We use the following algorithm for the height:
 * If baud rate < 1200, use  9
 * If baud rate = 1200, use 14
 * If baud rate > 1200, use VAL_HEIGHT or ws_row
 * Width is either VAL_WIDTH or ws_col */
su_SINLINE void a_main_setup_screen(void);
static void a_main__scrsz(int is_sighdl);

/* Ok, we are reading mail.  Decide whether we are editing a mailbox or reading
 * the system mailbox, and open up the right stuff */
static int a_main_rcv_mode(boole had_A_arg, char const *folder,
            char const *Larg, char const **Yargs, uz Yargs_cnt);

/* Interrupt printing of the headers */
static void a_main_hdrstop(int signo);

/* */
static void a_main_usage(FILE *fp);
static boole a_main_dump_doc(up cookie, boole has_arg, char const *sopt,
      char const *lopt, char const *doc);

static void
a_main_startup(void){
   struct passwd *pwuid;
   char *cp;
   NYD2_IN;

   n_stdin = stdin;
   n_stdout = stdout;
   n_stderr = stderr;
   dflpipe = SIG_DFL;

   if((cp = su_cs_rfind_c(su_program, '/')) != NULL)
      su_program = ++cp;
   /* XXX Due to n_err() mess the su_log config only applies to EMERG yet! */
   su_state_set(su_STATE_LOG_SHOW_LEVEL | su_STATE_LOG_SHOW_PID
         /* XXX | su_STATE_ERR_NOMEM | su_STATE_ERR_OVERFLOW */
   );
   su_log_set_level(n_LOG_LEVEL); /* XXX _EMERG is 0.. */

#if su_DVLOR(1, 0)
   safe_signal(SIGABRT, &mx__nyd_oncrash);
# ifdef SIGBUS
   safe_signal(SIGBUS, &mx__nyd_oncrash);
# endif
   safe_signal(SIGFPE, &mx__nyd_oncrash);
   safe_signal(SIGILL, &mx__nyd_oncrash);
   safe_signal(SIGSEGV, &mx__nyd_oncrash);
#endif

   /* Initialize our input, loop and command machinery */
   n_go_init();

   /* Set up a reasonable environment */

   /* TODO This is wrong: interactive is STDIN/STDERR for a POSIX sh(1).
    * TODO For now we get this wrong, all over the place, as this software
    * TODO has always been developed with stdout as an output channel.
    * TODO Start doing it right for at least explicit terminal-related things,
    * TODO but v15 should use ONLY this, also for terminal input! */
   if(isatty(STDIN_FILENO)){
      n_psonce |= n_PSO_TTYIN;
#if defined mx_HAVE_MLE || defined mx_HAVE_TERMCAP
      if((n_tty_fp = fdopen(fileno(n_stdin), "w")) != NULL)
         setvbuf(n_tty_fp, NULL, _IOLBF, 0);
#endif
   }
   if(isatty(STDOUT_FILENO))
      n_psonce |= n_PSO_TTYOUT;
   if((n_psonce & (n_PSO_TTYIN | n_PSO_TTYOUT)) ==
         (n_PSO_TTYIN | n_PSO_TTYOUT)){
      n_psonce |= n_PSO_INTERACTIVE;
      safe_signal(SIGPIPE, dflpipe = SIG_IGN);
   }

   /* STDOUT is always line buffered from our point of view */
   setvbuf(n_stdout, NULL, _IOLBF, 0);
   if(n_tty_fp == NULL)
      n_tty_fp = n_stdout;

   /*  --  >8  --  8<  --  */

   n_locale_init();

#ifdef mx_HAVE_ICONV
   iconvd = (iconv_t)-1;
#endif

   /*
    * Ensure some variables get loaded and/or verified, I. (pre-getopt)
    */

   /* Detect, verify and fixate our invoking user (environment) */
   n_group_id = getgid();
   if((pwuid = getpwuid(n_user_id = getuid())) == NULL)
      n_panic(_("Cannot associate a name with uid %lu"), (ul)n_user_id);
   else{
      char const *ep;
      boole doenv;

      if(!(doenv = (ep = ok_vlook(LOGNAME)) == NULL) &&
            (doenv = (su_cs_cmp(pwuid->pw_name, ep) != 0)))
         n_err(_("Warning: $LOGNAME (%s) not identical to user (%s)!\n"),
            ep, pwuid->pw_name);
      if(doenv){
         n_pstate |= n_PS_ROOT;
         ok_vset(LOGNAME, pwuid->pw_name);
         n_pstate &= ~n_PS_ROOT;
      }

      /* BSD compat */
      if((ep = ok_vlook(USER)) != NULL && su_cs_cmp(pwuid->pw_name, ep)){
         n_err(_("Warning: $USER (%s) not identical to user (%s)!\n"),
            ep, pwuid->pw_name);
         n_pstate |= n_PS_ROOT;
         ok_vset(USER, pwuid->pw_name);
         n_pstate &= ~n_PS_ROOT;
      }

      /* XXX myfullname = pw->pw_gecos[OPTIONAL!] -> GUT THAT; TODO pw_shell */
   }

   /* This is not automated just as $TMPDIR is for the initial setting, since
    * we have the pwuid at hand and can simply use it!  See accmacvar.c! */
   if(n_user_id == 0 || (cp = ok_vlook(HOME)) == NULL){
      cp = pwuid->pw_dir;
      n_pstate |= n_PS_ROOT;
      ok_vset(HOME, cp);
      n_pstate &= ~n_PS_ROOT;
   }

   (void)ok_blook(POSIXLY_CORRECT);
   NYD2_OU;
}

static uz
a_main_grow_cpp(char const ***cpp, uz newsize, uz oldcnt){
   /* Just use auto-reclaimed storage, it will be preserved */
   char const **newcpp;
   NYD2_IN;

   newcpp = n_autorec_alloc(sizeof(char*) * (newsize + 1));

   if(oldcnt > 0)
      su_mem_copy(newcpp, *cpp, oldcnt * sizeof(char*));
   *cpp = newcpp;
   NYD2_OU;
   return newsize;
}

static void
a_main_setup_vars(void){
   char const *cp;
   NYD2_IN;

   /*
    * Ensure some variables get loaded and/or verified, II. (post getopt).
    */

   /* Do not honour TMPDIR if root */
   if(n_user_id == 0)
      ok_vset(TMPDIR, NULL);
   else
      (void)ok_vlook(TMPDIR);

   /* Are we in a reproducible-builds.org environment?
    * That special mode bends some settings (again) */
   if(ok_vlook(SOURCE_DATE_EPOCH) != NULL){
      su_state_set(su_STATE_REPRODUCIBLE);
      su_program = su_reproducible_build;
      n_pstate |= n_PS_ROOT;
      ok_vset(LOGNAME, su_reproducible_build);
      /* Do not care about USER at all in this special mode! */
      n_pstate &= ~n_PS_ROOT;
      cp = savecat(su_reproducible_build, ": ");
      ok_vset(log_prefix, cp);
   }
   NYD2_OU;
}

su_SINLINE void
a_main_setup_screen(void){
   /* Problem: VAL_ configuration values are strings, we need numbers */
   LCTAV(VAL_HEIGHT[0] != '\0' && (VAL_HEIGHT[1] == '\0' ||
      VAL_HEIGHT[2] == '\0' || VAL_HEIGHT[3] == '\0'));
#define a_HEIGHT \
   (VAL_HEIGHT[1] == '\0' ? (VAL_HEIGHT[0] - '0') \
   : (VAL_HEIGHT[2] == '\0' \
      ? ((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) \
      : (((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) * 10 + \
         (VAL_HEIGHT[2] - '0'))))
   LCTAV(VAL_WIDTH[0] != '\0' &&
      (VAL_WIDTH[1] == '\0' || VAL_WIDTH[2] == '\0' || VAL_WIDTH[3] == '\0'));
#define a_WIDTH \
   (VAL_WIDTH[1] == '\0' ? (VAL_WIDTH[0] - '0') \
   : (VAL_WIDTH[2] == '\0' \
      ? ((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) \
      : (((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) * 10 + \
         (VAL_WIDTH[2] - '0'))))

   NYD2_IN;

   if(!su_state_has(su_STATE_REPRODUCIBLE) &&
         ((n_psonce & n_PSO_INTERACTIVE) ||
            ((n_psonce & (n_PSO_TTYIN | n_PSO_TTYOUT)) &&
            (n_poption & n_PO_BATCH_FLAG)))){
      a_main__scrsz(FAL0);
      if(n_psonce & n_PSO_INTERACTIVE){
         /* XXX Yet WINCH after SIGWINCH/SIGCONT, but see POSIX TOSTOP flag */
#ifdef SIGWINCH
# ifndef TTY_WANTS_SIGWINCH
         if(safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
# endif
            safe_signal(SIGWINCH, &a_main__scrsz);
#endif
#ifdef SIGCONT
         safe_signal(SIGCONT, &a_main__scrsz);
#endif
      }
   }else
      /* $COLUMNS and $LINES defaults as documented in the manual */
      n_scrnheight = n_realscreenheight = a_HEIGHT,
      n_scrnwidth = a_WIDTH;
   NYD2_OU;
}

static void
a_main__scrsz(int is_sighdl){
   struct termios tbuf;
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
   struct winsize ws;
#elif defined TIOCGSIZE
   struct ttysize ts;
#endif
   NYD2_IN;
   ASSERT((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_BATCH_FLAG));

   n_scrnheight = n_realscreenheight = n_scrnwidth = 0;

   /* (Also) POSIX: LINES and COLUMNS always override.  Adjust this
    * a little bit to be able to honour resizes during our lifetime and
    * only honour it upon first run; abuse *is_sighdl* as an indicator */
   if(!is_sighdl){
      char const *cp;
      boole hadl, hadc;

      if((hadl = ((cp = ok_vlook(LINES)) != NULL))){
         su_idec_u32_cp(&n_scrnheight, cp, 0, NULL);
         n_realscreenheight = n_scrnheight;
      }
      if((hadc = ((cp = ok_vlook(COLUMNS)) != NULL)))
         su_idec_u32_cp(&n_scrnwidth, cp, 0, NULL);

      if(n_scrnwidth != 0 && n_scrnheight != 0)
         goto jleave;

      /* In non-interactive mode, stop now, except for the documented case that
       * both are set but not both have been usable */
      if(!(n_psonce & n_PSO_INTERACTIVE) && (!hadl || !hadc)){
         n_scrnheight = n_realscreenheight = a_HEIGHT;
         n_scrnwidth = a_WIDTH;
         goto jleave;
      }
   }

#ifdef mx_HAVE_TCGETWINSIZE
   if(tcgetwinsize(fileno(n_tty_fp), &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGWINSZ
   if(ioctl(fileno(n_tty_fp), TIOCGWINSZ, &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGSIZE
   if(ioctl(fileno(n_tty_fp), TIOCGSIZE, &ws) == -1)
      ts.ts_lines = ts.ts_cols = 0;
#endif

   if(n_scrnheight == 0){
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
      if(ws.ws_row != 0)
         n_scrnheight = ws.ws_row;
#elif defined TIOCGSIZE
      if(ts.ts_lines != 0)
         n_scrnheight = ts.ts_lines;
#endif
      else{
         speed_t ospeed;

         ospeed = ((tcgetattr(fileno(n_tty_fp), &tbuf) == -1)
               ? B9600 : cfgetospeed(&tbuf));

         if(ospeed < B1200)
            n_scrnheight = 9;
         else if(ospeed == B1200)
            n_scrnheight = 14;
         else
            n_scrnheight = a_HEIGHT;
      }

#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ || defined TIOCGSIZE
      if(0 ==
# if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
            (n_realscreenheight = ws.ws_row)
# else
            (n_realscreenheight = ts.ts_lines)
# endif
      )
         n_realscreenheight = a_HEIGHT;
#endif
   }

   if(n_scrnwidth == 0 && 0 ==
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
         (n_scrnwidth = ws.ws_col)
#elif defined TIOCGSIZE
         (n_scrnwidth = ts.ts_cols)
#endif
   )
      n_scrnwidth = a_WIDTH;

   /**/
   n_pstate |= n_PS_SIGWINCH_PEND;
jleave:
   /* Note: for the first invocation this will always trigger.
    * If we have termcap support then main() will undo this if FULLWIDTH is set
    * after termcap is initialized.
    * We had to evaluate screen size now since cmds may run pre-termcap ... */
/*#ifdef mx_HAVE_TERMCAP*/
   if(n_scrnwidth > 1 && !(n_psonce & n_PSO_TERMCAP_FULLWIDTH))
      --n_scrnwidth;
/*#endif*/
   NYD2_OU;

#undef a_HEIGHT
#undef a_WIDTH
}

static sigjmp_buf a_main__hdrjmp; /* XXX */

static int
a_main_rcv_mode(boole had_A_arg, char const *folder, char const *Larg,
      char const **Yargs, uz Yargs_cnt){
   /* XXX a_main_rcv_mode(): use argument carrier */
   n_sighdl_t prevint;
   int i;
   NYD_IN;

   i = had_A_arg ? FEDIT_ACCOUNT : FEDIT_NONE;
   if(n_poption & n_PO_QUICKRUN_MASK)
      i |= FEDIT_RDONLY;

   if(folder == NULL){
      folder = "%";
      if(had_A_arg)
         i |= FEDIT_SYSBOX;
   }
#ifdef mx_HAVE_IMAP
   else if(*folder == '@'){
      /* This must be treated specially to make possible invocation like
       * -A imap -f @mailbox */
      char const *cp;

      cp = n_folder_query();
      if(which_protocol(cp, FAL0, FAL0, NULL) == PROTO_IMAP)
         su_cs_pcopy_n(mailname, cp, sizeof mailname);
   }
#endif

   i = setfile(folder, i);
   if(i < 0){
      n_exit_status = n_EXIT_ERR; /* error already reported */
      goto jquit;
   }
   temporary_folder_hook_check(FAL0);
   if(n_poption & n_PO_QUICKRUN_MASK){
      n_exit_status = i;
      if(i == n_EXIT_OK && (!(n_poption & n_PO_EXISTONLY) ||
            (n_poption & n_PO_HEADERLIST)))
         print_header_summary(Larg);
      goto jquit;
   }

   if(i > 0 && !ok_blook(emptystart)){
      n_exit_status = n_EXIT_ERR;
      goto jleave;
   }

   if(sigsetjmp(a_main__hdrjmp, 1) == 0){
      if((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &a_main_hdrstop);
      if(!ok_blook(quiet))
         fprintf(n_stdout, _("%s version %s.  Type `?' for help\n"),
            n_uagent,
            (su_state_has(su_STATE_REPRODUCIBLE)
               ? su_reproducible_build : ok_vlook(version)));
      n_folder_announce(n_ANNOUNCE_MAIN_CALL | n_ANNOUNCE_CHANGE);
      safe_signal(SIGINT, prevint);
   }

   /* Enter the command loop */
   if(n_psonce & n_PSO_INTERACTIVE)
      n_tty_init();
   /* "load()" more commands given on command line */
   if(Yargs_cnt > 0 && !n_go_XYargs(TRU1, Yargs, Yargs_cnt))
      n_exit_status = n_EXIT_ERR;
   else
      n_go_main_loop();
   if(n_psonce & n_PSO_INTERACTIVE)
      n_tty_destroy((n_psonce & n_PSO_XIT) != 0);

   if(!(n_psonce & n_PSO_XIT)){
      if(mb.mb_type == MB_FILE || mb.mb_type == MB_MAILDIR){
         safe_signal(SIGHUP, SIG_IGN);
         safe_signal(SIGINT, SIG_IGN);
         safe_signal(SIGQUIT, SIG_IGN);
      }
jquit:
      save_mbox_for_possible_quitstuff();
      quit(FAL0);
   }
jleave:
   NYD_OU;
   return n_exit_status;
}

static void
a_main_hdrstop(int signo){
   NYD; /* Signal handler */
   UNUSED(signo);

   fflush(n_stdout);
   n_err_sighdl(_("\nInterrupt\n"));
   siglongjmp(a_main__hdrjmp, 1);
}

static void
a_main_usage(FILE *fp){
   /* Stay in VAL_HEIGHT lines; On buf length change: verify visual output! */
   char buf[7];
   uz i;
   NYD2_IN;

   i = su_cs_len(su_program);
   i = MIN(i, sizeof(buf) -1);
   if(i > 0)
      su_mem_set(buf, ' ', i);
   buf[i] = '\0';

   fprintf(fp, _("%s (%s %s): send and receive Internet mail\n"),
      su_program, n_uagent, ok_vlook(version));
   if(fp != n_stderr)
      putc('\n', fp);

   fprintf(fp, _(
      "Send-only mode: send mail \"to-addr\"(ess) receiver(s):\n"
      "  %s [-DdEFinv~#] [-: spec] [-A account] [:-C \"field: body\":]\n"
      "  %s [:-a attachment:] [:-b bcc-addr:] [:-c cc-addr:]\n"
      "  %s [-M type | -m file | -q file | -t] [-r from-addr] "
         "[:-S var[=value]:]\n"
      "  %s [-s subject] [-T \"arget: addr\"] [:-X/Y cmd:] [-.] :to-addr:\n"),
      su_program, buf, buf, buf);
   if(fp != n_stderr)
      putc('\n', fp);

   fprintf(fp, _(
      "\"Receive\" mode, starting on [-u user], primary *inbox* or [$MAIL]:\n"
      "  %s [-DdEeHiNnRv~#] [-: spec] [-A account] [:-C \"field: body\":]\n"
      "  %s [-L spec] [-r from-addr] [:-S var[=value]:] [-u user] "
         "[:-X/Y cmd:]\n"),
      su_program, buf);
   if(fp != n_stderr)
      putc('\n', fp);

   fprintf(fp, _(
      "\"Receive\" mode, starting on -f (secondary $MBOX or [file]):\n"
      "  %s [-DdEeHiNnRv~#] [-: spec] [-A account] [:-C \"field: body\":] -f\n"
      "  %s [-L spec] [-r from-addr] [:-S var[=value]:] [:-X/Y cmd:] "
         "[file]\n"),
      su_program, buf);
   if(fp != n_stderr)
      putc('\n', fp);

   /* (ISO C89 string length) */
   fprintf(fp, _(
         ". -d sandbox, -:/ no .rc files, -. end options and force send-mode\n"
         ". -a attachment[=input-charset[#output-charset]]\n"
         ". -[bcrT], to-addr: ex@am.ple or '(Lovely) Ex <am@p.le>'\n"
         ". -[Mmqt]: special input data (-t: template message on stdin)\n"
         ". -e only mail check, -H header summary; "
            "both: message specification via -L\n"
         ". -S (un)sets variable, -X/-Y execute commands pre/post startup, "
            "-#: batch mode\n"));
   fprintf(fp, _(
         ". Features via \"$ %s -Xversion -Xx\"; there is --long-help\n"
         ". Bugs/Contact via "
            "\"$ %s -Sexpandaddr=shquote '\\$contact-mail'\"\n"),
         su_program, su_program);
   NYD2_OU;
}

static boole
a_main_dump_doc(up cookie, boole has_arg, char const *sopt, char const *lopt,
      char const *doc){
   char const *x1, *x2;
   NYD2_IN;

   if(has_arg)
      /* I18N: describing arguments to command line options */
      x1 = (sopt[0] != '\0' ? _(" ARG, ") : sopt), x2 = _("=ARG");
   else
      /* I18N: separating command line options */
      x1 = (sopt[0] != '\0' ? _(", ") : sopt), x2 = su_empty;
   /* I18N: short option, "[ ARG], " separator, long option [=ARG], doc */
   fprintf(S(FILE*,cookie), _("%s%s%s%s: %s\n"), sopt, x1, lopt, x2, doc);
   NYD2_OU;
   return TRU1;
}

int
main(int argc, char *argv[]){
   /* TODO Once v15 control flow/carrier rewrite took place main() should
    * TODO be rewritten and option parsing++ should be outsourced.
    * TODO Like so we can get rid of some stack locals etc.
    * TODO Furthermore: the locals should be in a carrier, and once there
    * TODO is the memory pool+page cache, that should go in LOFI memory,
    * TODO and there should be two pools: one which is fixated() and remains,
    * TODO and one with throw away data (-X, -Y args, temporary allocs, e.g.,
    * TODO redo -S like so, etc.) */
   /* Keep in SYNC: ./nail.1:"SYNOPSIS, main() */
   static char const a_sopts[] =
         "::A:a:Bb:C:c:DdEeFfHhiL:M:m:NnO:q:Rr:S:s:T:tu:VvX:Y:~#.";
   static char const * const a_lopts[] = {
      "resource-files:;:;" N_("control loading of resource files"),
      "account:;A;" N_("execute an `account command'"),
         "attach:;a;" N_("attach a file to message to be sent"),
      "bcc:;b;" N_("add blind carbon copy recipient"),
      "custom-header:;C;" N_("create custom header (\"header-field: body\")"),
         "cc:;c;" N_("add carbon copy recipient"),
      "disconnected;D;" N_("identical to -Sdisconnected"),
         "debug;d;" N_("identical to -Sdebug"),
      "discard-empty-messages;E;" N_("identical to -Sskipemptybody"),
         "check-and-exit;e;" N_("note mail presence (of -L) via exit status"),
      "file;f;" N_("open secondary mailbox (or \"file\" last on command line"),
      "header-summary;H;" N_("is to be displayed (for given file) only"),
         "help;h;" N_("short help"),
      "header-search:;L;" N_("like -H (or -e) for the given \"spec\" only"),
      "no-header-summary;N;" N_("identical to -Snoheader"),
      "quote-file:;q;" N_("initialize body of message to be sent with a file"),
      "read-only;R;" N_("any mailbox file will be opened read-only"),
         "from-address:;r;" N_("set source address used by MTAs (+ -Sfrom)"),
      "set:;S;" N_("set one of the INTERNAL VARIABLES (unset via \"noARG\")"),
         "subject:;s;" N_("specify subject of message to be sent"),
      "target:;T;" N_("add single receiver via \"header-field: address\""),
      "template;t;" N_("message to be sent is read from standard input"),
      "inbox-of:;u;" N_("initially open primary mailbox of the given user"),
      "version;V;" N_("print version (more so with \"[-v] -Xversion -Xx\")"),
         "verbose;v;" N_("identical to -Sverbose (twice for more verbosity)"),
      "startup-cmd:;X;" N_("to be executed before normal operation"),
      "cmd:;Y;" N_("to be executed under normal operation (is \"input\")"),
      "enable-cmd-escapes;~;" N_("even in non-interactive compose mode"),
      "batch-mode;#;" N_("more confined non-interactive setup"),
      "end-options;.;" N_("force the end of options, and send mode"),
      "long-help;\201;" N_("this listing"),
      NULL
   };
   struct su_avopt avo;
   int i;
   char *cp;
   uz Xargs_size, Xargs_cnt, Yargs_size, Yargs_cnt, smopts_size;
   char const *Aarg, *emsg, *folder, *Larg, *okey, *qf,
      *subject, *uarg, **Xargs, **Yargs;
   struct attachment *attach;
   struct mx_name *to, *cc, *bcc;
   struct a_arg *a_head, *a_curr;
   enum{
      a_RF_NONE = 0,
      a_RF_SET = 1<<0,
      a_RF_SYSTEM = 1<<1,
      a_RF_USER = 1<<2,
      a_RF_ALL = a_RF_SYSTEM | a_RF_USER
   } resfiles;
   NYD_IN;

   a_head = NULL;
   UNINIT(a_curr, NULL);
   to = cc = bcc = NULL;
   attach = NULL;
   Aarg = emsg = folder = Larg = okey = qf = subject = uarg = NULL;
   Xargs = Yargs = NULL;
   Xargs_size = Xargs_cnt = Yargs_size = Yargs_cnt = smopts_size = 0;
   resfiles = a_RF_ALL;

   /*
    * Start our lengthy setup, finalize by setting n_PSO_STARTED
    */

   su_program = argv[0];
   a_main_startup();

   /* Command line parsing.
    * XXX We could parse silently to grasp the actual mode (send, receive
    * XXX with/out -f, then use an according option array.  This would ease
    * XXX the interdependency checking necessities! */
   su_avopt_setup(&avo, --argc, su_C(char const*const*,++argv),
      a_sopts, a_lopts);
   while((i = su_avopt_parse(&avo)) != su_AVOPT_STATE_DONE){
      switch(i){
      case 'A':
         /* Execute an account command later on */
         Aarg = avo.avo_current_arg;
         break;
      case 'a':{
         /* Add an attachment */
         struct a_arg *nap;

         n_psonce |= n_PSO_SENDMODE;
         nap = n_autorec_alloc(sizeof(struct a_arg));
         if(a_head == NULL)
            a_head = nap;
         else
            a_curr->aa_next = nap;
         nap->aa_next = NULL;
         nap->aa_file = avo.avo_current_arg;
         a_curr = nap;
         }break;
      case 'B':
         n_OBSOLETE(_("-B is obsolete, please use -# as necessary"));
         break;
      case 'b':
         /* Add (a) blind carbon copy recipient (list) */
         n_psonce |= n_PSO_SENDMODE;
         bcc = cat(bcc, lextract(avo.avo_current_arg,
               GBCC | GFULL | GSHEXP_PARSE_HACK));
         break;
      case 'C':{
         /* Create custom header (at list tail) */
         struct n_header_field **hflpp;

         if(*(hflpp = &n_poption_arg_C) != NULL){
            while((*hflpp)->hf_next != NULL)
               *hflpp = (*hflpp)->hf_next;
            hflpp = &(*hflpp)->hf_next;
         }
         if(!n_header_add_custom(hflpp, avo.avo_current_arg, FAL0)){
            emsg = N_("Invalid custom header data with -C");
            goto jusage;
         }
         }break;
      case 'c':
         /* Add (a) carbon copy recipient (list) */
         n_psonce |= n_PSO_SENDMODE;
         cc = cat(cc, lextract(avo.avo_current_arg,
               GCC | GFULL | GSHEXP_PARSE_HACK));
         break;
      case 'D':
#ifdef mx_HAVE_IMAP
         ok_bset(disconnected);
#endif
         break;
      case 'd':
         ok_bset(debug);
         break;
      case 'E':
         ok_bset(skipemptybody);
         break;
      case 'e':
         /* Check if mail (matching -L) exists in given box, exit status */
         n_poption |= n_PO_EXISTONLY;
         break;
      case 'F':
         /* Save msg in file named after local part of first recipient */
         n_poption |= n_PO_F_FLAG;
         n_psonce |= n_PSO_SENDMODE;
         break;
      case 'f':
         /* User is specifying file to "edit" with Mail, as opposed to reading
          * system mailbox.  If no argument is given, we read his mbox file.
          * Check for remaining arguments later */
         folder = "&";
         break;
      case 'H':
         /* Display summary of headers, exit */
         n_poption |= n_PO_HEADERSONLY;
         break;
      case 'h':
      case (char)(su_u8)'\201':
         a_main_usage(n_stdout);
         if(i != 'h'){
            fprintf(n_stdout, "\nLong options:\n");
            (void)su_avopt_dump_doc(&avo, &a_main_dump_doc,
               su_S(su_up,n_stdout));
         }
         goto j_leave;
      case 'i':
         /* Ignore interrupts */
         ok_bset(ignore);
         break;
      case 'L':
         /* Display summary of headers which match given spec, exit.
          * In conjunction with -e, only test the given spec for existence */
         Larg = avo.avo_current_arg;
         n_poption |= n_PO_HEADERLIST;
         if(*Larg == '"' || *Larg == '\''){ /* TODO list.c:listspec_check() */
            uz j;

            j = su_cs_len(++Larg);
            if(j > 0){
               cp = savestrbuf(Larg, --j);
               Larg = cp;
            }
         }
         break;
      case 'M':
         /* Flag message body (standard input) with given MIME type */
         if(qf != NULL && (!(n_poption & n_PO_Mm_FLAG) || qf != (char*)-1))
            goto jeMmq;
         n_poption_arg_Mm = avo.avo_current_arg;
         qf = (char*)-1;
         if(0){
            /* FALLTHRU*/
      case 'm':
            /* Flag the given file with MIME type and use as message body */
            if(qf != NULL && (!(n_poption & n_PO_Mm_FLAG) || qf == (char*)-1))
               goto jeMmq;
            qf = avo.avo_current_arg;
         }
         n_poption |= n_PO_Mm_FLAG;
         n_psonce |= n_PSO_SENDMODE;
         break;
      case 'N':
         /* Avoid initial header printing */
         ok_bclear(header);
         break;
      case 'n':
         /* Don't source "unspecified system start-up file" */
         if(resfiles & a_RF_SET){
            emsg = N_("-n cannot be used in conjunction with -:");
            goto jusage;
         }
         resfiles = a_RF_USER;
         break;
      case 'O':
         /* Additional options to pass-through to MTA TODO v15-compat legacy */
         if(n_smopts_cnt == smopts_size)
            smopts_size = a_main_grow_cpp(&n_smopts, smopts_size + 8,
                  n_smopts_cnt);
         n_smopts[n_smopts_cnt++] = avo.avo_current_arg;
         break;
      case 'q':
         /* "Quote" file: use as message body (-t without headers etc.) */
         /* XXX Traditional.  Add -Q to initialize as *quote*d content? */
         if(qf != NULL && (n_poption & n_PO_Mm_FLAG)){
jeMmq:
            emsg = N_("Only one of -M, -m or -q may be given");
            goto jusage;
         }
         n_psonce |= n_PSO_SENDMODE;
         /* Allow, we have to special check validity of -q- later on! */
         qf = (avo.avo_current_arg[0] == '-' && avo.avo_current_arg[1] == '\0')
               ? (char*)-1 : avo.avo_current_arg;
         break;
      case 'R':
         /* Open folders read-only */
         n_poption |= n_PO_R_FLAG;
         break;
      case 'r':
         /* Set From address. */
         n_poption |= n_PO_r_FLAG;
         if(avo.avo_current_arg[0] == '\0')
            break;
         else{
            struct mx_name *fa;

            fa = nalloc(avo.avo_current_arg, GSKIN | GFULL | GFULLEXTRA |
                  GNOT_A_LIST | GNULL_OK);
            if(fa == NULL || is_addr_invalid(fa, EACM_STRICT | EACM_NOLOG)){
               emsg = N_("Invalid address argument with -r");
               goto jusage;
            }
            n_poption_arg_r = fa;
            /* TODO -r options is set in n_smopts, but may
             * TODO be overwritten by setting from= in
             * TODO an interactive session!
             * TODO Maybe disable setting of from?
             * TODO Warn user?  Update manual!! */
            avo.avo_current_arg = savecat("from=", fa->n_fullname);
         }
         /* FALLTHRU */
      case 'S':
         {  struct str sin;
            struct n_string s_b, *s;
            char const *a[2];
            boole b;

            if(ok_vlook(v15_compat) == su_NIL){
               okey = a[0] = avo.avo_current_arg;
               s = NIL;
            }else{
               enum n_shexp_state shs;

               n_autorec_relax_create();
               s = n_string_creat_auto(&s_b);
               sin.s = n_UNCONST(avo.avo_current_arg);
               sin.l = UZ_MAX;
               shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
                     n_SHEXP_PARSE_IGNORE_EMPTY |
                     n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
                     n_SHEXP_PARSE_QUOTE_AUTO_DSQ), s, &sin, NULL);
               if((shs & n_SHEXP_STATE_ERR_MASK) ||
                     !(shs & n_SHEXP_STATE_STOP)){
                  n_autorec_relax_gut();
                  goto je_S;
               }
               okey = a[0] = n_string_cp_const(s);
            }

            a[1] = NIL;
            n_poption |= n_PO_S_FLAG_TEMPORARY;
            n_pstate |= n_PS_ROBOT;
            b = (c_set(a) == 0);
            n_pstate &= ~n_PS_ROBOT;
            n_poption &= ~n_PO_S_FLAG_TEMPORARY;

            if(s != NIL)
               n_autorec_relax_gut();
            if(!b && (ok_blook(errexit) || ok_blook(posix))){
je_S:
               emsg = N_("-S failed to set variable");
               goto jusage;
            }
         }
         break;
      case 's':
         /* Subject:; take care for Debian #419840 and strip any \r and \n */
         if(su_cs_first_of(subject = avo.avo_current_arg, "\n\r"
               ) != su_UZ_MAX){
            n_err(_("-s: normalizing away invalid ASCII NL / CR bytes\n"));
            for(subject = cp = savestr(avo.avo_current_arg); *cp != '\0'; ++cp)
               if(*cp == '\n' || *cp == '\r')
                  *cp = ' ';
         }
         n_psonce |= n_PSO_SENDMODE;
         break;

      case 'T':{
         /* Target mode: `digmsg header insert' from command line.
          * TODO So far code cannot be shared since `digmsg' backing objects
          * TODO do not exist yet (stack local for compose mode right now).
          * TODO Thus need to unroll and can offer header subset only yet */
         struct mx_name **npp, *np;
         enum gfield gf;

         cp = UNCONST(char*,avo.avo_current_arg); /* (logical) */

         if(!su_cs_cmp_case_n(cp, "bcc", i = 3))
            gf = GBCC, npp = &bcc;
         else if(!su_cs_cmp_case_n(cp, "cc", i = 2))
            gf = GCC, npp = &cc;
         else if(!su_cs_cmp_case_n(cp, "fcc", i = 3))
            gf = GBCC_IS_FCC, npp = &bcc;
         else if(!su_cs_cmp_case_n(cp, "to", i = 2))
            gf = GTO, npp = &to;
         else{
            emsg = N_("-T: supports only to,cc,bcc and fcc for now");
            goto jusage;
         }
         cp += i;

         gf |= GSHEXP_PARSE_HACK | GFULL | GNULL_OK | GNOT_A_LIST;

         if(*cp == '?'){
            char c;

            for(i = 0; (c = *++cp) != '\0'; ++i)
               if(su_cs_is_blank(c) || c == ':')
                  break;
            if(i > 0 && !su_cs_starts_with_case_n("list", &cp[-i], i)){
               emsg = N_("-T: invalid modifier");
               goto jusage;
            }
            gf &= ~GNOT_A_LIST;
         }

         while(su_cs_is_blank(*cp))
            ++cp;
         if(*cp == ':')
            ++cp;
         while(su_cs_is_blank(*cp))
            ++cp;
         if(*cp == '\0')
            goto jt_err;

         if(!(gf & GBCC_IS_FCC))
            np = lextract(cp, gf);
         else if(gf & GNOT_A_LIST)
            np = nalloc_fcc(cp);
         else
            goto jt_err;
         if(np == su_NIL){
jt_err:
            emsg = N_("-T: invalid format or addressee");
            goto jusage;
         }
         *npp = cat(*npp, np);
         }break;

      case 't':
         /* Use the given message as send template */
         n_poption |= n_PO_t_FLAG;
         n_psonce |= n_PSO_SENDMODE;
         break;
      case 'u':
         /* Open primary mailbox of the given user */
         uarg = savecat("%", avo.avo_current_arg);
         break;
      case 'V':{
         struct n_string s;

         fputs(n_string_cp_const(n_version(
            n_string_book(n_string_creat_auto(&s), 120))), n_stdout);
         n_exit_status = n_EXIT_OK;
         }goto j_leave;
      case 'v':
         /* Be verbose */
         ok_bset(verbose);
         break;
      case 'X':
         /* Add to list of commands to exec before entering normal operation */
         if(Xargs_cnt == Xargs_size)
            Xargs_size = a_main_grow_cpp(&Xargs, Xargs_size + 8, Xargs_cnt);
         Xargs[Xargs_cnt++] = avo.avo_current_arg;
         break;
      case 'Y':
         /* Add to list of commands to exec after entering normal operation */
         if(Yargs_cnt == Yargs_size)
            Yargs_size = a_main_grow_cpp(&Yargs, Yargs_size + 8, Yargs_cnt);
         Yargs[Yargs_cnt++] = avo.avo_current_arg;
         break;
      case ':':
         /* Control which resource files shall be loaded */
         if(!(resfiles & (a_RF_SET | a_RF_SYSTEM))){
            emsg = N_("-n cannot be used in conjunction with -:");
            goto jusage;
         }
         resfiles = a_RF_SET;
         while((i = *avo.avo_current_arg++) != '\0')
            switch(i){
            case 'S': case 's': resfiles |= a_RF_SYSTEM; break;
            case 'U': case 'u': resfiles |= a_RF_USER; break;
            case '-': case '/': resfiles &= ~a_RF_ALL; break;
            default:
               emsg = N_("Invalid argument of -:");
               goto jusage;
            }
         break;
      case '~':
         /* Enable command escapes even in non-interactive mode */
         n_poption |= n_PO_TILDE_FLAG;
         break;
      case '#':
         /* Work in batch mode, even if non-interactive */
         if(!(n_psonce & n_PSO_INTERACTIVE))
            setvbuf(n_stdin, NULL, _IOLBF, 0);
         n_poption |= n_PO_TILDE_FLAG | n_PO_BATCH_FLAG;
         folder = n_path_devnull;
         n_var_setup_batch_mode();
         break;
      case '.':
         /* Enforce send mode */
         n_psonce |= n_PSO_SENDMODE;
         goto jgetopt_done;
      case su_AVOPT_STATE_ERR_ARG:
         emsg = su_avopt_fmt_err_arg;
         if(0){
            /* FALLTHRU */
      case su_AVOPT_STATE_ERR_OPT:
            emsg = su_avopt_fmt_err_opt;
         }
         n_err(emsg, avo.avo_current_err_opt);
         if(0){
jusage:
            if(emsg != NULL)
               n_err("%s\n", V_(emsg));
         }
         a_main_usage(n_stderr);
         n_exit_status = n_EXIT_USE;
         goto j_leave;
      }
   }
jgetopt_done:
   ;

   /* The normal arguments may be followed by MTA arguments after a "--";
    * however, -f may take off an argument, too, and before that.
    * Since MTA arguments after "--" require *expandargv*, delay parsing off
    * those options until after the resource files are loaded... */
   argc = avo.avo_argc;
   argv = su_C(char**,avo.avo_argv);
   if((cp = argv[i = 0]) == NULL)
      ;
   else if(cp[0] == '-' && cp[1] == '-' && cp[2] == '\0')
      ++i;
   /* n_PO_BATCH_FLAG sets to /dev/null, but -f can still be used and sets & */
   else if(folder != NULL && /*folder[0] == '&' &&*/ folder[1] == '\0'){
      folder = cp;
      if((cp = argv[++i]) != NULL){
         if(cp[0] != '-' || cp[1] != '-' || cp[2] != '\0'){
            emsg = N_("More than one file given with -f");
            goto jusage;
         }
         ++i;
      }
   }else{
      n_psonce |= n_PSO_SENDMODE;
      for(;;){
         to = cat(to, lextract(cp, GTO | GFULL | GSHEXP_PARSE_HACK));
         if((cp = argv[++i]) == NULL)
            break;
         if(cp[0] == '-' && cp[1] == '-' && cp[2] == '\0'){
            ++i;
            break;
         }
      }
   }
   argc = i;

   /* ...BUT, since we use n_autorec_alloc() for the MTA n_smopts storage we
    * need to allocate the space for them before we fixate that storage! */
   while(argv[i] != NULL)
      ++i;
   if(n_smopts_cnt + i > smopts_size)
      su_DBG(smopts_size =)
      a_main_grow_cpp(&n_smopts, n_smopts_cnt + i + 1, n_smopts_cnt);

   /* Check for inconsistent arguments, fix some temporaries */
   if(n_psonce & n_PSO_SENDMODE){
      /* XXX This is only because BATCH_FLAG sets *folder*=/dev/null
       * XXX in order to function.  Ideally that would not be needed */
      if(folder != NULL && !(n_poption & n_PO_BATCH_FLAG)){
         emsg = N_("Cannot give -f and people to send to.");
         goto jusage;
      }
      if(uarg != NULL){
         emsg = N_("The -u option cannot be used in send mode");
         goto jusage;
      }
      if(!(n_poption & n_PO_t_FLAG) && to == NULL){
         emsg = N_("Send options without primary recipient specified.");
         goto jusage;
      }
      if((n_poption & n_PO_t_FLAG) && qf != NULL){
         emsg = N_("The -M, -m, -q and -t options are mutual exclusive.");
         goto jusage;
      }
      if(n_poption & (n_PO_EXISTONLY | n_PO_HEADERSONLY | n_PO_HEADERLIST)){
         emsg = N_("The -e, -H and -L options cannot be used in send mode.");
         goto jusage;
      }
      if(n_poption & n_PO_R_FLAG){
         emsg = N_("The -R option is meaningless in send mode.");
         goto jusage;
      }

      if(n_psonce & n_PSO_INTERACTIVE){
         if(qf == (char*)-1){
            if(!(n_poption & n_PO_Mm_FLAG))
               emsg = N_("-q can't use standard input when interactive.\n");
            goto jusage;
         }
      }
   }else{
      if(uarg != NULL && folder != NULL){
         emsg = N_("The options -u and -f (and -#) are mutually exclusive");
         goto jusage;
      }
      if((n_poption & (n_PO_EXISTONLY | n_PO_HEADERSONLY)) ==
            (n_PO_EXISTONLY | n_PO_HEADERSONLY)){
         emsg = N_("The options -e and -H are mutual exclusive");
         goto jusage;
      }
      if((n_poption & (n_PO_HEADERSONLY | n_PO_HEADERLIST) /* TODO OBSOLETE */
            ) == (n_PO_HEADERSONLY | n_PO_HEADERLIST))
         n_OBSOLETE(_("please use \"-e -L xy\" instead of \"-H -L xy\""));

      if(uarg != NULL)
         folder = uarg;
   }

   /*
    * We have reached our second program state, the command line options have
    * been worked and verified a bit, we are likely to go, perform more setup
    */
   n_psonce |= n_PSO_STARTED_GETOPT;

   a_main_setup_vars();
   a_main_setup_screen();

   /* Create memory pool snapshot; Memory is auto-reclaimed from now on */
   su_mem_bag_fixate(n_go_data->gdc_membag);

   /* load() any resource files */
   if(resfiles & a_RF_ALL){
      /* *expand() returns a savestr(), but load() only uses the file name
       * for fopen(), so it is safe to do this */
      if(resfiles & a_RF_SYSTEM){
         boole nload;

         if((nload = ok_blook(NAIL_NO_SYSTEM_RC)))
            n_OBSOLETE(_("Please use $MAILX_NO_SYSTEM_RC instead of "
               "$NAIL_NO_SYSTEM_RC"));
         if(!nload && !ok_blook(MAILX_NO_SYSTEM_RC) &&
               !n_go_load(ok_vlook(system_mailrc)))
            goto j_leave;
      }

      if((resfiles & a_RF_USER) &&
            !n_go_load(fexpand(ok_vlook(MAILRC), FEXP_LOCAL | FEXP_NOPROTO)))
         goto j_leave;

      if((cp = ok_vlook(NAIL_EXTRA_RC)) != NULL)
         n_OBSOLETE(_("Please use *mailx-extra-rc*, not *NAIL_EXTRA_RC*"));
      if((cp != NULL || (cp = ok_vlook(mailx_extra_rc)) != NULL) &&
            !n_go_load(fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)))
         goto j_leave;
   }

   /* Cause possible umask(2) to be applied, now that any setting is
    * established, and before we change accounts, evaluate commands etc. */
   (void)ok_vlook(umask);

   /* Additional options to pass-through to MTA, and allowed to do so? */
   i = argc;
   if((cp = ok_vlook(expandargv)) != NULL){
      boole isfail, isrestrict;

      isfail = !su_cs_cmp_case(cp, "fail");
      isrestrict = (!isfail && !su_cs_cmp_case(cp, "restrict"));

      if((n_poption & n_PO_D_V) && !isfail && !isrestrict && *cp != '\0')
         n_err(_("Unknown *expandargv* value: %s\n"), cp);

      if((cp = argv[i]) != NULL){
         if(isfail || (isrestrict && (!(n_poption & n_PO_TILDE_FLAG) ||
                  !(n_psonce & n_PSO_INTERACTIVE)))){
je_expandargv:
            n_err(_("*expandargv* doesn't allow MTA arguments; consider "
               "using *mta-arguments*\n"));
            n_exit_status = n_EXIT_USE | n_EXIT_SEND_ERROR;
            goto jleave;
         }
         do{
            ASSERT(n_smopts_cnt + 1 <= smopts_size);
            n_smopts[n_smopts_cnt++] = cp;
         }while((cp = argv[++i]) != NULL);
      }
   }else if(argv[i] != NULL)
      goto je_expandargv;

   /* We had to wait until the resource files are loaded and any command line
    * setting has been restored, but get the termcap up and going before we
    * switch account or running commands */
#ifdef n_HAVE_TCAP
   if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK)){
      n_termcap_init();
      /* Undo the decrement from a_main__scrsz()s first invocation */
      if(n_scrnwidth > 0 && (n_psonce & n_PSO_TERMCAP_FULLWIDTH))
         ++n_scrnwidth;
   }
#endif

   /* Now we can set the account */
   if(Aarg != NULL){
      char const *a[2];

      a[0] = Aarg;
      a[1] = NULL;
      if(c_account(a) && (!(n_psonce & n_PSO_INTERACTIVE) ||
            ok_blook(errexit) || ok_blook(posix))){
         n_exit_status = n_EXIT_USE | n_EXIT_SEND_ERROR;
         goto jleave;
      }
   }

   /*
    * Almost setup, only -X commands are missing!
    */
   n_psonce |= n_PSO_STARTED_CONFIG;

   /* "load()" commands given on command line */
   if(Xargs_cnt > 0 && !n_go_XYargs(FAL0, Xargs, Xargs_cnt))
      goto jleave;

   /* Final tests */
   if(n_poption & n_PO_Mm_FLAG){
      if(qf == (char*)-1){
         if(!n_mimetype_check_mtname(n_poption_arg_Mm)){
            n_err(_("Could not find `mimetype' for -M argument: %s\n"),
               n_poption_arg_Mm);
            n_exit_status = n_EXIT_ERR;
            goto jleave;
         }
      }else if(/* XXX only to satisfy Coverity! */qf != NULL &&
            (n_poption_arg_Mm = n_mimetype_classify_filename(qf)) == NULL){
         n_err(_("Could not `mimetype'-classify -m argument: %s\n"),
            n_shexp_quote_cp(qf, FAL0));
         n_exit_status = n_EXIT_ERR;
         goto jleave;
      }else if(!su_cs_cmp_case(n_poption_arg_Mm, "text/plain")) /* TODO magic*/
         n_poption_arg_Mm = NULL;
   }

   /*
    * We're finally completely setup and ready to go!
    */
   n_psonce |= n_PSO_STARTED;

   if(!(n_psonce & n_PSO_SENDMODE))
      n_exit_status = a_main_rcv_mode((Aarg != NULL), folder, Larg,
            Yargs, Yargs_cnt);
   else{
      /* Now that full mailx(1)-style file expansion is possible handle the
       * attachments which we had delayed due to this.
       * This may use savestr(), but since we won't enter the command loop we
       * don't need to care about that */
      for(; a_head != NULL; a_head = a_head->aa_next){
         enum n_attach_error aerr;

         attach = n_attachment_append(attach, a_head->aa_file, &aerr, NULL);
         if(aerr != n_ATTACH_ERR_NONE){
            n_exit_status = n_EXIT_ERR;
            goto jleave;
         }
      }

      if(n_psonce & n_PSO_INTERACTIVE)
         n_tty_init();
      /* "load()" more commands given on command line */
      if(Yargs_cnt > 0 && !n_go_XYargs(TRU1, Yargs, Yargs_cnt))
         n_exit_status = n_EXIT_ERR;
      else
         n_mail((n_poption & n_PO_F_FLAG ? n_MAILSEND_RECORD_RECIPIENT : 0),
            to, cc, bcc, subject, attach, qf);
      if(n_psonce & n_PSO_INTERACTIVE)
         n_tty_destroy((n_psonce & n_PSO_XIT) != 0);
   }

jleave:
  /* Be aware of identical code for `exit' command! */
#ifdef n_HAVE_TCAP
   if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK))
      n_termcap_destroy();
#endif

j_leave:
#ifdef su_HAVE_DEBUG
   su_mem_bag_gut(n_go_data->gdc_membag); /* Was init in go_init() */
   su_mem_set_conf(su_MEM_CONF_LINGER_FREE_RELEASE, 0);
#endif
   NYD_OU;
   return n_exit_status;
}

#include "su/code-ou.h"

/* Source the others in that case! */
#ifdef mx_HAVE_AMALGAMATION
# include <mx/gen-config.h>
#endif

/* s-it-mode */
