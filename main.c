/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Startup and initialization.
 *@ This file is also used to materialize externals.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE main
#define n_MAIN_SOURCE

#include "nail.h"

#include <sys/ioctl.h>

#include <pwd.h>

#ifdef HAVE_NL_LANGINFO
# include <langinfo.h>
#endif
#ifdef HAVE_SETLOCALE
# include <locale.h>
#endif

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
VL char const n_error[sizeof n_ERROR] = N_(n_ERROR);
VL char const n_unirepl[sizeof n_UNIREPL] = n_UNIREPL;
VL char const n_empty[1] = "";
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

/* Our own little getopt(3); note --help is special-treated as 'h' */
static int a_main_getopt(int argc, char * const argv[], char const *optstring);

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
 * If baud rate > 1200, use 24 or ws_row
 * Width is either 80 or ws_col */
static void a_main_setscreensize(int is_sighdl);

/* Ok, we are reading mail.  Decide whether we are editing a mailbox or reading
 * the system mailbox, and open up the right stuff */
static int a_main_rcv_mode(char const *folder, char const *Larg);

/* Interrupt printing of the headers */
static void a_main_hdrstop(int signo);

static int
a_main_getopt(int argc, char * const argv[], char const *optstring){
   static char const *lastp;

   char const *curp;
   int rv/*, colon*/;
   NYD_ENTER;

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

   /* Special support for --help, which is quite common */
   if(a_main_oopt == '-' && !strcmp(curp, "-help") &&
         &curp[-1] == argv[a_main_oind]){
      ++a_main_oind;
      rv = 'h';
      goto jleave;
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
   NYD_LEAVE;
   return rv;
}

static void
a_main_startup(void){
   char *cp;
   NYD_ENTER;

   n_stdin = stdin;
   n_stdout = stdout;
   n_stderr = stderr;
   image = -1;
   dflpipe = SIG_DFL;
   a_main_oind = /*_oerr =*/ 1;

   if((cp = strrchr(n_progname, '/')) != NULL)
      n_progname = ++cp;

   /* Set up a reasonable environment */

#ifdef HAVE_NYD
   safe_signal(SIGABRT, &_nyd_oncrash);
# ifdef SIGBUS
   safe_signal(SIGBUS, &_nyd_oncrash);
# endif
   safe_signal(SIGFPE, &_nyd_oncrash);
   safe_signal(SIGILL, &_nyd_oncrash);
   safe_signal(SIGSEGV, &_nyd_oncrash);
#endif
   command_manager_start();

   /* TODO This is wrong: interactive is STDIN/STDERR for a POSIX sh(1).
    * TODO For now we get this wrong, all over the place, as this software
    * TODO has always been developed with stdout as an output channel.
    * TODO Start doing it right for at least explicit terminal-related things,
    * TODO but v15 should use ONLY this, also for terminal input! */
   if(isatty(STDIN_FILENO)){
      n_psonce |= n_PSO_TTYIN;
#if defined HAVE_MLE || defined HAVE_TERMCAP
      n_tty_fp = fdopen(fileno(n_stdin), "w");
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

#ifndef HAVE_SETLOCALE
   n_mb_cur_max = 1;
#else
   setlocale(LC_ALL, n_empty);
   n_mb_cur_max = MB_CUR_MAX;
# ifdef HAVE_NL_LANGINFO
   if((cp = nl_langinfo(CODESET)) != NULL)
      ok_vset(ttycharset, cp);
# endif

# ifdef HAVE_C90AMEND1
   if(n_mb_cur_max > 1){
#  ifdef HAVE_ALWAYS_UNICODE_LOCALE
      n_psonce |= n_PSO_UNICODE;
#  else
      wchar_t wc;
      if(mbtowc(&wc, "\303\266", 2) == 2 && wc == 0xF6 &&
            mbtowc(&wc, "\342\202\254", 3) == 3 && wc == 0x20AC)
         n_psonce |= n_PSO_UNICODE;
      /* Reset possibly messed up state; luckily this also gives us an
       * indication whether the encoding has locking shift state sequences */
      if(mbtowc(&wc, NULL, n_mb_cur_max))
         n_psonce |= n_PSO_ENC_MBSTATE;
#  endif
   }
# endif
#endif

#ifdef HAVE_ICONV
   iconvd = (iconv_t)-1;
#endif
   NYD_LEAVE;
}

static size_t
a_main_grow_cpp(char const ***cpp, size_t newsize, size_t oldcnt){
   /* Just use auto-reclaimed storage, it will be preserved */
   char const **newcpp;
   NYD_ENTER;

   newcpp = salloc(sizeof(char*) * newsize);

   if(oldcnt > 0)
      memcpy(newcpp, *cpp, oldcnt * sizeof(char*));
   *cpp = newcpp;
   NYD_LEAVE;
   return newsize;
}

static void
a_main_setup_vars(void){
   struct passwd *pwuid;
   char const *cp;
   NYD_ENTER;

   n_group_id = getgid();
   if((pwuid = getpwuid(n_user_id = getuid())) == NULL)
      n_panic(_("Cannot associate a name with uid %lu"), (ul_i)n_user_id);

   /* C99 */{
      char const *ep;
      bool_t doenv;

      if(!(doenv = (ep = ok_vlook(LOGNAME)) == NULL) &&
            (doenv = strcmp(pwuid->pw_name, ep)))
         n_err(_("Warning: $LOGNAME (%s) not identical to user (%s)!\n"),
            ep, pwuid->pw_name);
      if(doenv){
         n_pstate |= n_PS_ROOT;
         ok_vset(LOGNAME, pwuid->pw_name);
         n_pstate &= ~n_PS_ROOT;
      }

      if((ep = ok_vlook(USER)) != NULL && strcmp(pwuid->pw_name, ep)){
         n_err(_("Warning: $USER (%s) not identical to user (%s)!\n"),
            ep, pwuid->pw_name);
         n_pstate |= n_PS_ROOT;
         ok_vset(USER, pwuid->pw_name);
         n_pstate &= ~n_PS_ROOT;
      }
   }

   /* XXX myfullname = pw->pw_gecos[OPTIONAL!] -> GUT THAT; TODO pw_shell */

   /* */
   if((cp = ok_vlook(HOME)) == NULL ||
         !is_dir(cp) || access(cp, R_OK | W_OK | X_OK)){
      cp = pwuid->pw_dir;
      n_pstate |= n_PS_ROOT;
      ok_vset(HOME, cp);
      n_pstate &= ~n_PS_ROOT;
   }

   cp = ok_vlook(TMPDIR);
   assert(cp != NULL);
   if(!is_dir(cp) || access(cp, R_OK | W_OK | X_OK))
      ok_vclear(TMPDIR);

   /* Ensure some variables get loaded */
   (void)ok_blook(POSIXLY_CORRECT);
   NYD_LEAVE;
}

static void
a_main_setscreensize(int is_sighdl){/* TODO globl policy; int wraps; minvals! */
   struct termios tbuf;
#ifdef TIOCGWINSZ
   struct winsize ws;
#elif defined TIOCGSIZE
   struct ttysize ts;
#endif
   NYD_ENTER;

   n_scrnheight = n_realscreenheight = n_scrnwidth = 0;

   /* (Also) POSIX: LINES and COLUMNS always override.  Adjust this
    * a little bit to be able to honour resizes during our lifetime and
    * only honour it upon first run; abuse *is_sighdl* as an indicator */
   if(!is_sighdl){
      char const *cp;

      /* We manage those variables for our child processes, so ensure they
       * are up to date, always */
      if(n_psonce & n_PSO_INTERACTIVE)
         n_pstate |= n_PS_SIGWINCH_PEND;

      if((cp = ok_vlook(LINES)) != NULL)
         n_scrnheight = n_realscreenheight = (int)strtoul(cp, NULL, 0); /*TODO*/
      if((cp = ok_vlook(COLUMNS)) != NULL)
         n_scrnwidth = (int)strtoul(cp, NULL, 0); /* TODO posui32= not posnum */

      if(n_scrnwidth != 0 && n_scrnheight != 0)
         goto jleave;
   }

#ifdef TIOCGWINSZ
   if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGSIZE
   if(ioctl(STDOUT_FILENO, TIOCGSIZE, &ws) == -1)
      ts.ts_lines = ts.ts_cols = 0;
#endif

   if(n_scrnheight == 0){
      speed_t ospeed;

      ospeed = ((tcgetattr(fileno(n_tty_fp), &tbuf) == -1)
            ? B9600 : cfgetospeed(&tbuf));

      if(ospeed < B1200)
         n_scrnheight = 9;
      else if(ospeed == B1200)
         n_scrnheight = 14;
#ifdef TIOCGWINSZ
      else if(ws.ws_row != 0)
         n_scrnheight = ws.ws_row;
#elif defined TIOCGSIZE
      else if(ts.ts_lines != 0)
         n_scrnheight = ts.ts_lines;
#endif
      else
         n_scrnheight = 24;

#if defined TIOCGWINSZ || defined TIOCGSIZE
      if(0 ==
# ifdef TIOCGWINSZ
            (n_realscreenheight = ws.ws_row)
# else
            (n_realscreenheight = ts.ts_lines)
# endif
      )
         n_realscreenheight = 24;
#endif
   }

   if(n_scrnwidth == 0 && 0 ==
#ifdef TIOCGWINSZ
         (n_scrnwidth = ws.ws_col)
#elif defined TIOCGSIZE
         (n_scrnwidth = ts.ts_cols)
#endif
   )
      n_scrnwidth = 80;

jleave:
#ifdef SIGWINCH
   if(is_sighdl){
      n_pstate |= n_PS_SIGWINCH_PEND; /* XXX Not atomic */
      if(n_psonce & n_PSO_INTERACTIVE)
         n_tty_signal(SIGWINCH);
   }
#endif
   NYD_LEAVE;
}

static sigjmp_buf a_main__hdrjmp; /* XXX */

static int
a_main_rcv_mode(char const *folder, char const *Larg){
   int i;
   sighandler_type prevint;
   NYD_ENTER;

   if(folder == NULL)
      folder = "%";

   i = (n_poption & n_PO_QUICKRUN_MASK) ? FEDIT_RDONLY : FEDIT_NONE;
   i = setfile(folder, i);
   if(i < 0){
      n_exit_status = n_EXIT_ERR; /* error already reported */
      goto jquit;
   }
   if(n_poption & n_PO_QUICKRUN_MASK){
      n_exit_status = i;
      if(i == n_EXIT_OK && (!(n_poption & n_PO_EXISTONLY) ||
            (n_poption & n_PO_HEADERLIST)))
         print_header_summary(Larg);
      goto jquit;
   }
   temporary_folder_hook_check(FAL0);

   if(i > 0 && !ok_blook(emptystart)){
      n_exit_status = n_EXIT_ERR;
      goto jleave;
   }

   if(sigsetjmp(a_main__hdrjmp, 1) == 0){
      if((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &a_main_hdrstop);
      if(!(n_poption & n_PO_N_FLAG)){
         if(!ok_blook(quiet))
            fprintf(n_stdout, _("%s version %s.  Type `?' for help\n"),
               (ok_blook(bsdcompat) ? "Mail" : n_uagent), ok_vlook(version));
         announce(1);
         fflush(n_stdout);
      }
      safe_signal(SIGINT, prevint);
   }

   /* Enter the command loop */
   if(n_psonce & n_PSO_INTERACTIVE)
      n_tty_init();
   n_commands();
   if(n_psonce & n_PSO_INTERACTIVE)
      n_tty_destroy();

   if(mb.mb_type == MB_FILE || mb.mb_type == MB_MAILDIR){
      safe_signal(SIGHUP, SIG_IGN);
      safe_signal(SIGINT, SIG_IGN);
      safe_signal(SIGQUIT, SIG_IGN);
   }
jquit:
   save_mbox_for_possible_quitstuff();
   quit(FAL0);
jleave:
   NYD_LEAVE;
   return n_exit_status;
}

static void
a_main_hdrstop(int signo){
   NYD_X; /* Signal handler */
   n_UNUSED(signo);

   fflush(n_stdout);
   n_err_sighdl(_("\nInterrupt\n"));
   siglongjmp(a_main__hdrjmp, 1);
}

int
main(int argc, char *argv[]){
   /* Keep in SYNC: ./nail.1:"SYNOPSIS, main() */
   static char const
      optstr[] = "A:a:Bb:c:dEeFfHhiL:M:m:NnO:q:Rr:S:s:tu:VvX:::~#.",
      usagestr[] = N_(
         "Synopsis:\n"
         "  %s -h\n"

         "  %s [-BdEFintv~] [-: spec] [-A account]\n"
         "\t [-a attachment] [-b bcc-addr] [-c cc-addr]\n"
         "\t [-M type | -m file | -q file | -t]\n"
         "\t [-r from-addr] [-S var[=value]..]\n"
         "\t [-s subject] [-X cmd] [-.] to-addr.. [-- mta-option..]\n"

         "  %s [-BdEeHiNnRv~] [-: spec] [-A account]\n"
         "\t [-L spec-list] [-r from-addr] [-S var[=value]..]\n"
         "\t [-u user] [-X cmd] [-- mta-option..]\n"

         "  %s [-BdEeHiNnRv~#] [-: spec] [-A account] -f\n"
         "\t [-L spec-list] [-r from-addr] [-S var[=value]..]\n"
         "\t [-X cmd] [file] [-- mta-option..]\n"
      );
#define _USAGE_ARGS , n_progname, n_progname, n_progname, n_progname

   int i;
   char *cp;
   enum{
      a_RF_NONE = 0,
      a_RF_SET = 1<<0,
      a_RF_SYSTEM = 1<<1,
      a_RF_USER = 1<<2,
      a_RF_ALL = a_RF_SYSTEM | a_RF_USER
   } resfiles;
   size_t oargs_size, oargs_cnt, Xargs_size, Xargs_cnt, smopts_size;
   char const *Aarg, *emsg, *folder, *Larg, *okey, **oargs, *qf,
      *subject, *uarg, **Xargs;
   struct attachment *attach;
   struct name *to, *cc, *bcc;
   struct a_arg *a_head, *a_curr;
   NYD_ENTER;

   a_head = NULL;
   n_UNINIT(a_curr, NULL);
   to = cc = bcc = NULL;
   attach = NULL;
   Aarg = emsg = folder = Larg = okey = qf = subject = uarg = NULL;
   oargs = Xargs = NULL;
   oargs_size = oargs_cnt = Xargs_size = Xargs_cnt = smopts_size = 0;
   resfiles = a_RF_ALL;

   /*
    * Start our lengthy setup, finalize by setting n_PSO_STARTED
    */

   n_progname = argv[0];
   a_main_startup();

   /* Command line parsing
    * Variable settings need to be done twice, since the user surely wants the
    * setting to take effect immediately, but also doesn't want it to be
    * overwritten from within resource files */
   while((i = a_main_getopt(argc, argv, optstr)) >= 0){
      switch(i){
      case 'A':
         /* Execute an account command later on */
         Aarg = a_main_oarg;
         break;
      case 'a':{
         struct a_arg *nap;

         n_psonce |= n_PSO_SENDMODE;
         nap = salloc(sizeof(struct a_arg));
         if(a_head == NULL)
            a_head = nap;
         else
            a_curr->aa_next = nap;
         nap->aa_next = NULL;
         nap->aa_file = a_main_oarg;
         a_curr = nap;
      }  break;
      case 'B':
         n_OBSOLETE(_("-B is obsolete, please use -# as necessary"));
         break;
      case 'b':
         /* Get Blind Carbon Copy Recipient list */
         n_psonce |= n_PSO_SENDMODE;
         bcc = cat(bcc, lextract(a_main_oarg, GBCC | GFULL));
         break;
      case 'c':
         /* Get Carbon Copy Recipient list */
         n_psonce |= n_PSO_SENDMODE;
         cc = cat(cc, lextract(a_main_oarg, GCC | GFULL));
         break;
      case 'd':
         ok_bset(debug);
         okey = "debug";
         goto joarg;
      case 'E':
         n_OBSOLETE(_("-E will be removed, please use \"-Sskipemptybody\""));
         ok_bset(skipemptybody);
         okey = "skipemptybody";
         goto joarg;
      case 'e':
         n_poption |= n_PO_EXISTONLY;
         break;
      case 'F':
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
         n_poption |= n_PO_HEADERSONLY;
         break;
      case 'h':
         n_err(V_(usagestr) _USAGE_ARGS);
         goto j_leave;
      case 'i':
         /* Ignore interrupts */
         ok_bset(ignore);
         okey = "ignore";
         goto joarg;
      case 'L':
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
         if(qf != NULL && (!(n_poption & n_PO_Mm_FLAG) || qf != (char*)-1))
            goto jeMmq;
         n_poption_arg_Mm = a_main_oarg;
         qf = (char*)-1;
         if(0){
            /* FALLTHRU*/
      case 'm':
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
         okey = "noheader";
         goto joarg;
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
         if(qf != NULL && (n_poption & n_PO_Mm_FLAG)){
jeMmq:
            emsg = N_("Only one of -M, -m or -q may be given");
            goto jusage;
         }
         /* Quote file TODO drop? -Q with real quote?? what ? */
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
         if(a_main_oarg[0] != '\0'){
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
            okey = savecat("from=", fa->n_fullname);
            goto joarg;
         }
         break;
      case 'S':
         /* Set variable (TODO twice [only if successful]) */
         {  char const *a[2];
            bool_t b;

            okey = a[0] = a_main_oarg;
            a[1] = NULL;
            n_pstate |= n_PS_ROBOT;
            b = (c_set(a) == 0);
            n_pstate &= ~n_PS_ROBOT;
            if(!b)
               break;
         }
joarg:
         if(oargs_cnt == oargs_size)
            oargs_size = a_main_grow_cpp(&oargs, oargs_size + 8, oargs_cnt);
         oargs[oargs_cnt++] = okey;
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
         /* Read defined set of headers from mail to be sent */
         n_poption |= n_PO_t_FLAG;
         n_psonce |= n_PSO_SENDMODE;
         break;
      case 'u':
         uarg = savecat("%", a_main_oarg);
         break;
      case 'V':
         fprintf(n_stdout, _("%s version %s\n"), n_uagent, ok_vlook(version));
         n_exit_status = n_EXIT_OK;
         goto j_leave;
      case 'v':
         /* Be verbose */
         ok_bset(verbose);
         okey = "verbose";
         goto joarg;
      case 'X':
         /* Add to list of commands to exec before entering normal operation */
         if(Xargs_cnt == Xargs_size)
            Xargs_size = a_main_grow_cpp(&Xargs, Xargs_size + 8, Xargs_cnt);
         Xargs[Xargs_cnt++] = a_main_oarg;
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
         if (oargs_cnt + 8 >= oargs_size)
            oargs_size = a_main_grow_cpp(&oargs, oargs_size + 9, oargs_cnt);
         folder = "/dev/null";
         ok_vset(MAIL, folder);
         ok_vset(MBOX, folder);
         ok_bset(emptystart);
         ok_bclear(header);
         ok_vset(inbox, folder);
         ok_bset(quiet);
         ok_bset(sendwait);
         ok_bset(typescript_mode);
         oargs[oargs_cnt + 0] = "MAIL=/dev/null";
         oargs[oargs_cnt + 1] = "MBOX=/dev/null";
         oargs[oargs_cnt + 2] = "emptystart";
         oargs[oargs_cnt + 3] = "noheader";
         oargs[oargs_cnt + 4] = "inbox=/dev/null";
         oargs[oargs_cnt + 5] = "quiet";
         oargs[oargs_cnt + 6] = "sendwait";
         oargs[oargs_cnt + 7] = "typescript-mode";
         oargs_cnt += 8;
         break;
      case '.':
         n_psonce |= n_PSO_SENDMODE;
         goto jgetopt_done;
      case '?':
jusage:
         if(emsg != NULL)
            n_err("%s\n", V_(emsg));
         n_err(V_(usagestr) _USAGE_ARGS);
#undef _USAGE_ARGS
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
         to = cat(to, lextract(cp, GTO | GFULL));
         if((cp = argv[++i]) == NULL)
            break;
         if(cp[0] == '-' && cp[1] == '-' && cp[2] == '\0'){
            ++i;
            break;
         }
      }
   }
   a_main_oind = i;

   /* ...BUT, since we use salloc() for the MTA n_smopts storage we need to
    * allocate the necessary space for them before we fixate that storage! */
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
    * Likely to go, perform more setup
    */

   a_main_setup_vars();

   if(n_psonce & n_PSO_INTERACTIVE){
      a_main_setscreensize(0);
#ifdef SIGWINCH
# ifndef TTY_WANTS_SIGWINCH
      if(safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
# endif
         safe_signal(SIGWINCH, &a_main_setscreensize);
#endif
   }else
      n_scrnheight = n_realscreenheight = 24, n_scrnwidth = 80;

   /* Fixate the current snapshot of our global auto-reclaimed storage instance.
    * Memory is auto-reclaimed from now on */
   n_memory_autorec_fixate();

   /* load() any resource files */
   if(resfiles & a_RF_ALL){
      /* *expand() returns a savestr(), but load only uses the file name for
       * fopen(), so it's safe to do this */
      if(resfiles & a_RF_SYSTEM){
         bool_t nload;

         if((nload = ok_blook(NAIL_NO_SYSTEM_RC)))
            n_OBSOLETE(_("Please use $MAILX_NO_SYSTEM_RC instead of "
               "$NAIL_NO_SYSTEM_RC"));
         if(!nload && !ok_blook(MAILX_NO_SYSTEM_RC))
            n_load(VAL_SYSCONFDIR "/" VAL_SYSCONFRC);
      }

      if(resfiles & a_RF_USER)
         n_load(fexpand(ok_vlook(MAILRC), FEXP_LOCAL | FEXP_NOPROTO));

      if((cp = ok_vlook(NAIL_EXTRA_RC)) != NULL)
         n_OBSOLETE(_("Please use *mailx-extra-rc*, not *NAIL_EXTRA_RC*"));
      if(cp != NULL || (cp = ok_vlook(mailx_extra_rc)) != NULL)
         n_load(fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO));
   }

   /* Ensure the -S and other command line options take precedence over
    * anything that may have been placed in resource files.
    * Our "ternary binary" option *verbose* needs special treament */
   if((n_poption & (n_PO_VERB | n_PO_VERBVERB)) == n_PO_VERB)
      n_poption &= ~n_PO_VERB;
   /* ..and be silent when unsetting an undefined variable */
   n_pstate |= n_PS_ROBOT;
   for(i = 0; UICMP(z, i, <, oargs_cnt); ++i){
      char const *a[2];

      a[0] = oargs[i];
      a[1] = NULL;
      c_set(a);
   }
   n_pstate &= ~n_PS_ROBOT;

   /* Cause possible umask(2), now that any setting is established, and before
    * we change accounts, evaluate commands etc. */
   (void)ok_vlook(umask);

   /* Additional options to pass-through to MTA, and allowed to do so? */
   if((cp = ok_vlook(expandargv)) != NULL){
      bool_t isfail, isrestrict;

      isfail = !asccasecmp(cp, "fail");
      isrestrict = (!isfail && !asccasecmp(cp, "restrict"));

      if((n_poption & n_PO_D_V) && !isfail && !isrestrict && *cp != '\0')
         n_err(_("Unknown *expandargv* value: %s\n"), cp);

      if((cp = argv[i = a_main_oind]) != NULL){
         if(isfail || (isrestrict && (!(n_poption & n_PO_TILDE_FLAG) ||
                  !(n_psonce & n_PSO_INTERACTIVE)))){
            n_err(_("*expandargv* doesn't allow MTA arguments; consider "
               "using *sendmail-arguments*\n"));
            n_exit_status = n_EXIT_USE | n_EXIT_SEND_ERROR;
            goto jleave;
         }
         do{
            assert(n_smopts_cnt + 1 <= smopts_size);
            n_smopts[n_smopts_cnt++] = cp;
         }while((cp = argv[++i]) != NULL);
      }
   }

   /* We had to wait until the resource files are loaded and any command line
    * setting has been restored, but get the termcap up and going before we
    * switch account or running commands */
#ifdef n_HAVE_TCAP
   if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK))
      n_termcap_init();
#endif

   /* Now we can set the account */
   if(Aarg != NULL){
      char const *a[2];

      a[0] = Aarg;
      a[1] = NULL;
      c_account(a);
   }

   /* "load()" commands given on command line */
   if(Xargs_cnt > 0)
      n_load_Xargs(Xargs, Xargs_cnt);

   /* Final tests */
   if(n_poption & n_PO_Mm_FLAG){
      if(qf == (char*)-1){
         if(!mime_type_check_mtname(n_poption_arg_Mm)){
            n_err(_("Could not find `mimetype' for -M argument: %s\n"),
               n_poption_arg_Mm);
            n_exit_status = n_EXIT_ERR;
            goto jleave;
         }
      }else if((n_poption_arg_Mm = mime_type_classify_filename(qf)) == NULL){
         n_err(_("Could not `mimetype'-classify -m argument: %s\n"),
            n_shexp_quote_cp(qf, FAL0));
         n_exit_status = n_EXIT_ERR;
         goto jleave;
      }else if(!asccasecmp(n_poption_arg_Mm, "text/plain")) /* TODO no: magic */
         n_poption_arg_Mm = NULL;
   }

   /*
    * We're finally completely setup and ready to go
    */
   n_psonce |= n_PSO_STARTED;

   if(!(n_psonce & n_PSO_SENDMODE)){
      n_exit_status = a_main_rcv_mode(folder, Larg);
      goto jleave;
   }

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
   mail(to, cc, bcc, subject, attach, qf, ((n_poption & n_PO_F_FLAG) != 0));
   if(n_psonce & n_PSO_INTERACTIVE)
      n_tty_destroy();

jleave:
  /* Be aware of identical code for `exit' command! */
#ifdef n_HAVE_TCAP
   if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK))
      n_termcap_destroy();
#endif

j_leave:
#ifdef HAVE_MEMORY_DEBUG
   n_memory_autorec_pop(NULL);
#endif
#if (defined HAVE_DEBUG || defined HAVE_DEVEL)
   n_memory_reset();
#endif
   NYD_LEAVE;
   return n_exit_status;
}

/* Source the others in that case! */
#ifdef HAVE_AMALGAMATION
# include "config.h"
#endif

/* s-it-mode */
