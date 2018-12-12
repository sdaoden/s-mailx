/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Startup and initialization.
 *@ This file is also used to materialize externals.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
VL char const n_unirepl[sizeof n_UNIREPL] = n_UNIREPL;
VL char const n_0[2] = "0";
VL char const n_1[2] = "1";
VL char const n_m1[3] = "-1";
VL char const n_em[2] = "!";
VL char const n_ns[2] = "#";
VL char const n_star[2] = "*";
VL char const n_hy[2] = "-";
VL char const n_qm[2] = "?";
VL char const n_at[2] = "@";
VL ui16_t const n_class_char[1 + 0x7F] = {
#define a_BC C_BLANK | C_CNTRL
#define a_SC C_SPACE | C_CNTRL
#define a_WC C_WHITE | C_CNTRL
/* 000 nul  001 soh  002 stx  003 etx  004 eot  005 enq  006 ack  007 bel */
   C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL,
/* 010 bs   011 ht   012 nl   013 vt   014 np   015 cr   016 so   017 si */
   C_CNTRL, a_BC,    a_WC,    a_SC,    a_SC,    a_SC,    C_CNTRL, C_CNTRL,
/* 020 dle  021 dc1  022 dc2  023 dc3  024 dc4  025 nak  026 syn  027 etb */
   C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL,
/* 030 can  031 em   032 sub  033 esc  034 fs   035 gs   036 rs   037 us */
   C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL,
#undef a_WC
#undef a_SC
#undef a_BC
/* 040 sp   041  !   042  "   043  #   044  $   045  %   046  &   047  ' */
   C_BLANK, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT,
/* 050  (   051  )   052  *   053  +   054  ,    055  -   056  .   057  / */
   C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT,
/* 060  0   061  1   062  2   063  3   064  4   065  5   066  6   067  7 */
   C_OCTAL, C_OCTAL, C_OCTAL, C_OCTAL, C_OCTAL, C_OCTAL, C_OCTAL, C_OCTAL,
/* 070  8   071  9   072  :   073  ;   074  <   075  =   076  >   077  ? */
   C_DIGIT, C_DIGIT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT,
/* 100  @   101  A   102  B   103  C   104  D   105  E   106  F   107  G */
   C_PUNCT, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER,
/* 110  H   111  I   112  J   113  K   114  L   115  M   116  N   117  O */
   C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER,
/* 120  P   121  Q   122  R   123  S   124  T   125  U   126  V   127  W */
   C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER, C_UPPER,
/* 130  X   131  Y   132  Z   133  [   134  \   135  ]   136  ^   137  _ */
   C_UPPER, C_UPPER, C_UPPER, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT,
/* 140  `   141  a   142  b   143  c   144  d   145  e   146  f   147  g */
   C_PUNCT, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER,
/* 150  h   151  i   152  j   153  k   154  l   155  m   156  n   157  o */
   C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER,
/* 160  p   161  q   162  r   163  s   164  t   165  u   166  v   167  w */
   C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER, C_LOWER,
/* 170  x   171  y   172  z   173  {   174  |   175  }   176  ~   177 del */
   C_LOWER, C_LOWER, C_LOWER, C_PUNCT, C_PUNCT, C_PUNCT, C_PUNCT, C_CNTRL
};

/* Our own little getopt(3) */
static char const *a_main_oarg;
static int a_main_oind, /*_oerr,*/ a_main_oopt;

/* A little getopt(3).  Note: --help/--version == -h/-v */
static int a_main_getopt(int argc, char * const argv[], char const *optstring);

/* */
static void a_main_usage(FILE *fp);

/* Perform basic startup initialization */
static void a_main_startup(void);

/* Grow a char** */
static size_t a_main_grow_cpp(char const ***cpp, size_t newsize, size_t oldcnt);

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
static int a_main_rcv_mode(bool_t had_A_arg, char const *folder,
            char const *Larg, char const **Yargs, size_t Yargs_cnt);

/* Interrupt printing of the headers */
static void a_main_hdrstop(int signo);

static int
a_main_getopt(int argc, char * const argv[], char const *optstring){
   static char const *lastp;
   char const *curp;
   int rv/*, colon*/;
   n_NYD2_IN;

   a_main_oarg = NULL;
   rv = -1;

   /*if((colon = (optstring[0] == ':')))
      ++optstring;*/

   if(lastp != NULL){
      curp = lastp;
      lastp = NULL;
   }else{
      if(a_main_oind >= argc || argv[a_main_oind] == NULL ||
            argv[a_main_oind][0] != '-' || argv[a_main_oind][1] == '\0')
         goto jleave;
      if(argv[a_main_oind][1] == '-' && argv[a_main_oind][2] == '\0'){
         /* We need this in for MTA arg detection (easier) ++a_main_oind;*/
         goto jleave;
      }
      curp = &argv[a_main_oind][1];
   }

   for(a_main_oopt = curp[0]; optstring[0] != '\0';){
      if(optstring[0] != a_main_oopt){
         optstring += 1 + (optstring[1] == ':');
         continue;
      }

      if(optstring[1] == ':'){
         if(curp[1] != '\0'){
            a_main_oarg = n_UNCONST(curp + 1);
            ++a_main_oind;
         }else{
            if((a_main_oind += 2) > argc){
               /*if(!colon *//*&& _oerr*//*)*/{
                  n_err(_("%s: option requires an argument -- %c\n"),
                     argv[0], (char)a_main_oopt);
               }
               rv = (/*colon ? ':' :*/ '?');
               goto jleave;
            }
            a_main_oarg = argv[a_main_oind - 1];
         }
      }else{
         if(curp[1] != '\0')
            lastp = curp + 1;
         else
            ++a_main_oind;
      }
      rv = a_main_oopt;
      goto jleave;
   }

   /* Special support for --help and --version, which are quite common */
   if(a_main_oopt == '-' && &curp[-1] == argv[a_main_oind]){
      ++a_main_oind;
      rv = 'h';
      if(!strcmp(curp, "-help"))
         goto jleave;
      rv = 'V';
      if(!strcmp(curp, "-version"))
         goto jleave;
      --a_main_oind;
   }

   /* Definitive error */
   /*if(!colon *//*&& opterr*//*)*/
      n_err(_("%s: invalid option -- %c\n"), argv[0], a_main_oopt);
   if(curp[1] != '\0')
      lastp = curp + 1;
   else
      ++a_main_oind;
   a_main_oarg = NULL;
   rv = '?';
jleave:
   n_NYD2_OU;
   return rv;
}

static void
a_main_usage(FILE *fp){
   /* Stay in VAL_HEIGHT lines; On buf length change: verify visual output! */
   char buf[7];
   size_t i;
   n_NYD2_IN;

   i = strlen(su_program);
   i = n_MIN(i, sizeof(buf) -1);
   if(i > 0)
      memset(buf, ' ', i);
   buf[i] = '\0';

   fprintf(fp, _("%s (%s %s): send and receive Internet mail\n"),
      su_program, n_uagent, ok_vlook(version));
   if(fp != n_stderr)
      putc('\n', fp);

   fprintf(fp, _(
      "Send-only mode: send mail \"to-address\" receiver(s):\n"
      "  %s [-DdEFinv~#] [-: spec] [-A account] [:-C \"custom: header\":]\n"
      "  %s [:-a attachment:] [:-b bcc-address:] [:-c cc-address:]\n"
      "  %s [-M type | -m file | -q file | -t] [-r from-address]\n"
      "  %s [:-S var[=value]:] [-s subject] [:-X/Y cmd:] [-.] :to-address:\n"),
      su_program, buf, buf, buf);
   if(fp != n_stderr)
      putc('\n', fp);

   fprintf(fp, _(
      "\"Receive\" mode, starting on [-u user], primary *inbox* or [$MAIL]:\n"
      "  %s [-DdEeHiNnRv~#] [-: spec] [-A account] "
         "[:-C \"custom: header\":]\n"
      "  %s [-L spec] [-r from-address] [:-S var[=value]:] [-u user] "
         "[:-X/Y cmd:]\n"),
      su_program, buf);
   if(fp != n_stderr)
      putc('\n', fp);

   fprintf(fp, _(
      "\"Receive\" mode, starting on -f (secondary $MBOX or [file]):\n"
      "  %s [-DdEeHiNnRv~#] [-: spec] [-A account] "
         "[:-C \"custom: header\":] -f\n"
      "  %s [-L spec] [-r from-address] [:-S var[=value]:] [:-X/Y cmd:] "
         "[file]\n"),
      su_program, buf);
   if(fp != n_stderr)
      putc('\n', fp);

   fprintf(fp, _(
         ". -d sandbox, -:/ no .rc files, -. end options and force send-mode\n"
         ". -a attachment[=input-charset[#output-charset]]\n"
         ". -b, -c, to-address, (-r): ex@am.ple or '(Lovely) Ex <am@p.le>'\n"
         ". -[Mmqt]: special input data (-t: template message on stdin)\n"
         ". -e only mail check, -H header summary; "
            "both: message specification via -L\n"
         ". -S (un)sets variable, -X and -Y execute commands early/late, "
            "-#: batch mode\n"
         ". Features via \"$ %s -Xversion -Xx\"\n"
         ". Bugs/Contact via "
            "\"$ %s -Sexpandaddr=shquote '\\$contact-mail'\"\n"),
         su_program, su_program);
   n_NYD2_OU;
}

static void
a_main_startup(void){
   char *cp;
   n_NYD2_IN;

   n_stdin = stdin;
   n_stdout = stdout;
   n_stderr = stderr;
   dflpipe = SIG_DFL;

   a_main_oind = /*_oerr =*/ 1;

   if((cp = strrchr(su_program, '/')) != NULL)
      su_program = ++cp;
   /* XXX Somewhen: su_state_set(su_STATE_ERR_NOMEM | su_STATE_ERR_OVERFLOW);*/
   su_log_set_level(n_LOG_LEVEL); /* XXX _EMERG is 0.. */

#ifdef mx_HAVE_n_NYD
   safe_signal(SIGABRT, &_nyd_oncrash);
# ifdef SIGBUS
   safe_signal(SIGBUS, &_nyd_oncrash);
# endif
   safe_signal(SIGFPE, &_nyd_oncrash);
   safe_signal(SIGILL, &_nyd_oncrash);
   safe_signal(SIGSEGV, &_nyd_oncrash);
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

   /* Ensure some variables get loaded and/or verified */

   (void)ok_blook(POSIXLY_CORRECT);
   n_NYD2_OU;
}

static size_t
a_main_grow_cpp(char const ***cpp, size_t newsize, size_t oldcnt){
   /* Just use auto-reclaimed storage, it will be preserved */
   char const **newcpp;
   n_NYD2_IN;

   newcpp = n_autorec_alloc(sizeof(char*) * (newsize + 1));

   if(oldcnt > 0)
      memcpy(newcpp, *cpp, oldcnt * sizeof(char*));
   *cpp = newcpp;
   n_NYD2_OU;
   return newsize;
}

static void
a_main_setup_vars(void){
   struct passwd *pwuid;
   char const *cp;
   n_NYD2_IN;

   /* Detect, verify and fixate our invoking user (environment) */
   n_group_id = getgid();
   if((pwuid = getpwuid(n_user_id = getuid())) == NULL)
      n_panic(_("Cannot associate a name with uid %lu"), (ul_i)n_user_id);
   else{
      char const *ep;
      bool_t doenv;

      if(!(doenv = (ep = ok_vlook(LOGNAME)) == NULL) &&
            (doenv = (strcmp(pwuid->pw_name, ep) != 0)))
         n_err(_("Warning: $LOGNAME (%s) not identical to user (%s)!\n"),
            ep, pwuid->pw_name);
      if(doenv){
         n_pstate |= n_PS_ROOT;
         ok_vset(LOGNAME, pwuid->pw_name);
         n_pstate &= ~n_PS_ROOT;
      }

      /* BSD compat */
      if((ep = ok_vlook(USER)) != NULL && strcmp(pwuid->pw_name, ep)){
         n_err(_("Warning: $USER (%s) not identical to user (%s)!\n"),
            ep, pwuid->pw_name);
         n_pstate |= n_PS_ROOT;
         ok_vset(USER, pwuid->pw_name);
         n_pstate &= ~n_PS_ROOT;
      }

      /* XXX myfullname = pw->pw_gecos[OPTIONAL!] -> GUT THAT; TODO pw_shell */
   }

   /* Ensure some variables get loaded and/or verified.
    * While doing so, take special care for invocations as root */

   /* This is not automated just as $TMPDIR is for the initial setting, since
    * we have the pwuid at hand and can simply use it!  See accmacvar.c! */
   if(n_user_id == 0 || (cp = ok_vlook(HOME)) == NULL){
      cp = pwuid->pw_dir;
      n_pstate |= n_PS_ROOT;
      ok_vset(HOME, cp);
      n_pstate &= ~n_PS_ROOT;
   }

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
   n_NYD2_OU;
}

su_SINLINE void
a_main_setup_screen(void){
   /* Problem: VAL_ configuration values are strings, we need numbers */
   n_LCTAV(VAL_HEIGHT[0] != '\0' && (VAL_HEIGHT[1] == '\0' ||
      VAL_HEIGHT[2] == '\0' || VAL_HEIGHT[3] == '\0'));
#define a_HEIGHT \
   (VAL_HEIGHT[1] == '\0' ? (VAL_HEIGHT[0] - '0') \
   : (VAL_HEIGHT[2] == '\0' \
      ? ((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) \
      : (((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) * 10 + \
         (VAL_HEIGHT[2] - '0'))))
   n_LCTAV(VAL_WIDTH[0] != '\0' &&
      (VAL_WIDTH[1] == '\0' || VAL_WIDTH[2] == '\0' || VAL_WIDTH[3] == '\0'));
#define a_WIDTH \
   (VAL_WIDTH[1] == '\0' ? (VAL_WIDTH[0] - '0') \
   : (VAL_WIDTH[2] == '\0' \
      ? ((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) \
      : (((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) * 10 + \
         (VAL_WIDTH[2] - '0'))))

   n_NYD2_IN;

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
   n_NYD2_OU;
}

static void
a_main__scrsz(int is_sighdl){
   struct termios tbuf;
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
   struct winsize ws;
#elif defined TIOCGSIZE
   struct ttysize ts;
#endif
   n_NYD2_IN;
   assert((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_BATCH_FLAG));

   n_scrnheight = n_realscreenheight = n_scrnwidth = 0;

   /* (Also) POSIX: LINES and COLUMNS always override.  Adjust this
    * a little bit to be able to honour resizes during our lifetime and
    * only honour it upon first run; abuse *is_sighdl* as an indicator */
   if(!is_sighdl){
      char const *cp;
      bool_t hadl, hadc;

      if((hadl = ((cp = ok_vlook(LINES)) != NULL))){
         n_idec_ui32_cp(&n_scrnheight, cp, 0, NULL);
         n_realscreenheight = n_scrnheight;
      }
      if((hadc = ((cp = ok_vlook(COLUMNS)) != NULL)))
         n_idec_ui32_cp(&n_scrnwidth, cp, 0, NULL);

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
   n_NYD2_OU;

#undef a_HEIGHT
#undef a_WIDTH
}

static sigjmp_buf a_main__hdrjmp; /* XXX */

static int
a_main_rcv_mode(bool_t had_A_arg, char const *folder, char const *Larg,
      char const **Yargs, size_t Yargs_cnt){
   /* XXX a_main_rcv_mode(): use argument carrier */
   sighandler_type prevint;
   int i;
   n_NYD_IN;

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
         n_strscpy(mailname, cp, sizeof mailname);
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
   n_NYD_OU;
   return n_exit_status;
}

static void
a_main_hdrstop(int signo){
   n_NYD_X; /* Signal handler */
   n_UNUSED(signo);

   fflush(n_stdout);
   n_err_sighdl(_("\nInterrupt\n"));
   siglongjmp(a_main__hdrjmp, 1);
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
   static char const optstr[] =
         "A:a:Bb:C:c:DdEeFfHhiL:M:m:NnO:q:Rr:S:s:tu:VvX:Y:::~#.";
   int i;
   char *cp;
   enum{
      a_RF_NONE = 0,
      a_RF_SET = 1<<0,
      a_RF_SYSTEM = 1<<1,
      a_RF_USER = 1<<2,
      a_RF_ALL = a_RF_SYSTEM | a_RF_USER
   } resfiles;
   size_t Xargs_size, Xargs_cnt, Yargs_size, Yargs_cnt, smopts_size;
   char const *Aarg, *emsg, *folder, *Larg, *okey, *qf,
      *subject, *uarg, **Xargs, **Yargs;
   struct attachment *attach;
   struct name *to, *cc, *bcc;
   struct a_arg *a_head, *a_curr;
   n_NYD_IN;

   a_head = NULL;
   n_UNINIT(a_curr, NULL);
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

   /* Command line parsing
    * -S variable settings need to be done twice, since the user surely wants
    * the setting to take effect immediately, but also doesn't want it to be
    * overwritten from within resource files */
   while((i = a_main_getopt(argc, argv, optstr)) >= 0){
      switch(i){
      case 'A':
         /* Execute an account command later on */
         Aarg = a_main_oarg;
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
         nap->aa_file = a_main_oarg;
         a_curr = nap;
         }break;
      case 'B':
         n_OBSOLETE(_("-B is obsolete, please use -# as necessary"));
         break;
      case 'b':
         /* Add (a) blind carbon copy recipient (list) */
         n_psonce |= n_PSO_SENDMODE;
         bcc = cat(bcc, lextract(a_main_oarg,
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
         if(!n_header_add_custom(hflpp, a_main_oarg, FAL0)){
            emsg = N_("Invalid custom header data with -C");
            goto jusage;
         }
         }break;
      case 'c':
         /* Add (a) carbon copy recipient (list) */
         n_psonce |= n_PSO_SENDMODE;
         cc = cat(cc, lextract(a_main_oarg, GCC | GFULL | GSHEXP_PARSE_HACK));
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
         a_main_usage(n_stdout);
         goto j_leave;
      case 'i':
         /* Ignore interrupts */
         ok_bset(ignore);
         break;
      case 'L':
         /* Display summary of headers which match given spec, exit.
          * In conjunction with -e, only test the given spec for existence */
         Larg = a_main_oarg;
         n_poption |= n_PO_HEADERLIST;
         if(*Larg == '"' || *Larg == '\''){ /* TODO list.c:listspec_check() */
            size_t j;

            j = strlen(++Larg);
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
         n_poption_arg_Mm = a_main_oarg;
         qf = (char*)-1;
         if(0){
            /* FALLTHRU*/
      case 'm':
            /* Flag the given file with MIME type and use as message body */
            if(qf != NULL && (!(n_poption & n_PO_Mm_FLAG) || qf == (char*)-1))
               goto jeMmq;
            qf = a_main_oarg;
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
         n_smopts[n_smopts_cnt++] = a_main_oarg;
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
         /* Allow for now, we have to special check validity of -q- later on! */
         qf = (a_main_oarg[0] == '-' && a_main_oarg[1] == '\0')
               ? (char*)-1 : a_main_oarg;
         break;
      case 'R':
         /* Open folders read-only */
         n_poption |= n_PO_R_FLAG;
         break;
      case 'r':
         /* Set From address. */
         n_poption |= n_PO_r_FLAG;
         if(a_main_oarg[0] == '\0')
            break;
         else{
            struct name *fa;

            fa = nalloc(a_main_oarg, GSKIN | GFULL | GFULLEXTRA);
            if(is_addr_invalid(fa, EACM_STRICT | EACM_NOLOG)){
               emsg = N_("Invalid address argument with -r");
               goto jusage;
            }
            n_poption_arg_r = fa;
            /* TODO -r options is set in n_smopts, but may
             * TODO be overwritten by setting from= in
             * TODO an interactive session!
             * TODO Maybe disable setting of from?
             * TODO Warn user?  Update manual!! */
            a_main_oarg = savecat("from=", fa->n_fullname);
         }
         /* FALLTHRU */
      case 'S':
         {  struct str sin;
            struct n_string s, *sp;
            char const *a[2];
            bool_t b;

            if(!ok_blook(v15_compat)){
               okey = a[0] = a_main_oarg;
               sp = NULL;
            }else{
               enum n_shexp_state shs;

               n_autorec_relax_create();
               sp = n_string_creat_auto(&s);
               sin.s = n_UNCONST(a_main_oarg);
               sin.l = UIZ_MAX;
               shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
                     n_SHEXP_PARSE_IGNORE_EMPTY |
                     n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
                     n_SHEXP_PARSE_QUOTE_AUTO_DSQ), sp, &sin, NULL);
               if((shs & n_SHEXP_STATE_ERR_MASK) ||
                     !(shs & n_SHEXP_STATE_STOP)){
                  n_autorec_relax_gut();
                  goto je_S;
               }
               okey = a[0] = n_string_cp_const(sp);
            }

            a[1] = NULL;
            n_poption |= n_PO_S_FLAG_TEMPORARY;
            n_pstate |= n_PS_ROBOT;
            b = (c_set(a) == 0);
            n_pstate &= ~n_PS_ROBOT;
            n_poption &= ~n_PO_S_FLAG_TEMPORARY;

            if(sp != NULL)
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
         if(n_anyof_cp("\n\r", subject = a_main_oarg)){
            n_err(_("-s: normalizing away invalid ASCII NL / CR bytes\n"));
            for(subject = cp = savestr(a_main_oarg); *cp != '\0'; ++cp)
               if(*cp == '\n' || *cp == '\r')
                  *cp = ' ';
         }
         n_psonce |= n_PSO_SENDMODE;
         break;
      case 't':
         /* Use the given message as send template */
         n_poption |= n_PO_t_FLAG;
         n_psonce |= n_PSO_SENDMODE;
         break;
      case 'u':
         /* Open primary mailbox of the given user */
         uarg = savecat("%", a_main_oarg);
         break;
      case 'V':
         fprintf(n_stdout, _("%s version %s\n"), n_uagent, ok_vlook(version));
         n_exit_status = n_EXIT_OK;
         goto j_leave;
      case 'v':
         /* Be verbose */
         ok_bset(verbose);
         break;
      case 'X':
         /* Add to list of commands to exec before entering normal operation */
         if(Xargs_cnt == Xargs_size)
            Xargs_size = a_main_grow_cpp(&Xargs, Xargs_size + 8, Xargs_cnt);
         Xargs[Xargs_cnt++] = a_main_oarg;
         break;
      case 'Y':
         /* Add to list of commands to exec after entering normal operation */
         if(Yargs_cnt == Yargs_size)
            Yargs_size = a_main_grow_cpp(&Yargs, Yargs_size + 8, Yargs_cnt);
         Yargs[Yargs_cnt++] = a_main_oarg;
         break;
      case ':':
         /* Control which resource files shall be loaded */
         if(!(resfiles & (a_RF_SET | a_RF_SYSTEM))){
            emsg = N_("-n cannot be used in conjunction with -:");
            goto jusage;
         }
         resfiles = a_RF_SET;
         while((i = *a_main_oarg++) != '\0')
            switch(i){
            case 'S': case 's': resfiles |= a_RF_SYSTEM; break;
            case 'U': case 'u': resfiles |= a_RF_USER; break;
            case ':': case '/': resfiles &= ~a_RF_ALL; break;
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
         n_pstate |= n_PS_ROBOT; /* (be silent unsetting undefined variables) */
         ok_vset(MAIL, folder);
         ok_vset(MBOX, folder);
         ok_bset(emptystart);
         ok_bclear(errexit);
         ok_bclear(header);
         ok_vset(inbox, folder);
         ok_bclear(posix);
         ok_bset(quiet);
         ok_bset(sendwait);
         ok_bset(typescript_mode);
         n_pstate &= ~n_PS_ROBOT;
         break;
      case '.':
         /* Enforce send mode */
         n_psonce |= n_PSO_SENDMODE;
         goto jgetopt_done;
      case '?':
jusage:
         if(emsg != NULL)
            n_err("%s\n", V_(emsg));
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
   if((cp = argv[i = a_main_oind]) == NULL)
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
   a_main_oind = i;

   /* ...BUT, since we use n_autorec_alloc() for the MTA n_smopts storage we
    * need to allocate the space for them before we fixate that storage! */
   while(argv[i] != NULL)
      ++i;
   if(n_smopts_cnt + i > smopts_size)
      DBG(smopts_size =)
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
   n_memory_pool_fixate();

   /* load() any resource files */
   if(resfiles & a_RF_ALL){
      /* *expand() returns a savestr(), but load() only uses the file name
       * for fopen(), so it is safe to do this */
      if(resfiles & a_RF_SYSTEM){
         bool_t nload;

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
   i = a_main_oind;
   if((cp = ok_vlook(expandargv)) != NULL){
      bool_t isfail, isrestrict;

      isfail = !asccasecmp(cp, "fail");
      isrestrict = (!isfail && !asccasecmp(cp, "restrict"));

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
            assert(n_smopts_cnt + 1 <= smopts_size);
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
      }else if(!asccasecmp(n_poption_arg_Mm, "text/plain")) /* TODO no: magic */
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
#if defined mx_HAVE_MEMORY_DEBUG || defined mx_HAVE_NOMEMDBG
   n_memory_pool_pop(NULL, TRU1);
#endif
#if defined mx_HAVE_DEBUG || defined mx_HAVE_DEVEL || defined mx_HAVE_NOMEMDBG
   n_memory_reset();
#endif
   n_NYD_OU;
   return n_exit_status;
}

/* Source the others in that case! */
#ifdef mx_HAVE_AMALGAMATION
# include <mx/gen-config.h>
#endif

/* s-it-mode */
