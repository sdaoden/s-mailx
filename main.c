/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Startup and initialization.
 *@ This file is also used to materialize externals.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

struct a_arg {
   struct a_arg   *aa_next;
   char           *aa_file;
};

struct X_arg {
   struct X_arg   *xa_next;
   size_t         xa_cmd_len;
   char           xa_cmd_buf[VFIELD_SIZE(sizeof(size_t))];
};

/* (extern, but not with amalgamation, so define here) */
VL char const        weekday_names[7 + 1][4] = {
   "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", ""
};
VL char const        month_names[12 + 1][4] = {
   "Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", ""
};
VL char const        uagent[sizeof UAGENT] = UAGENT;
VL uc_i const        class_char[1 + 0x7F] = {
/* 000 nul  001 soh  002 stx  003 etx  004 eot  005 enq  006 ack  007 bel */
   C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL,
/* 010 bs   011 ht   012 nl   013 vt   014 np   015 cr   016 so   017 si */
   C_CNTRL, C_BLANK, C_WHITE, C_SPACE, C_SPACE, C_SPACE, C_CNTRL, C_CNTRL,
/* 020 dle  021 dc1  022 dc2  023 dc3  024 dc4  025 nak  026 syn  027 etb */
   C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL,
/* 030 can  031 em   032 sub  033 esc  034 fs   035 gs   036 rs   037 us */
   C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL, C_CNTRL,
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
static char          *_oarg;
static int           _oind, /*_oerr,*/ _oopt;

/* Our own little getopt(3); note --help is special-treated as 'h' */
static int     _getopt(int argc, char * const argv[], char const *optstring);

/* Perform basic startup initialization */
static void    _startup(void);

/* Grow a char** */
static size_t  _grow_cpp(char const ***cpp, size_t newsize, size_t oldcnt);

/* Initialize *tempdir*, *myname*, *homedir* */
static void    _setup_vars(void);

/* We're in an interactive session - compute what the screen size for printing
 * headers etc. should be; notify tty upon resize if *is_sighdl* is not 0.
 * We use the following algorithm for the height:
 * If baud rate < 1200, use  9
 * If baud rate = 1200, use 14
 * If baud rate > 1200, use 24 or ws_row
 * Width is either 80 or ws_col */
static void    _setscreensize(int is_sighdl);

/* Ok, we are reading mail.  Decide whether we are editing a mailbox or reading
 * the system mailbox, and open up the right stuff */
static int     _rcv_mode(char const *folder, char const *Larg,
                  struct X_arg *xhp);

/* Interrupt printing of the headers */
static void    _hdrstop(int signo);

/* -X arg given at least once, evaluate the list in order */
static bool_t  _X_arg_eval(struct X_arg *xhp);

static int
_getopt(int argc, char * const argv[], char const *optstring)
{
   static char const *lastp;

   int rv = -1, colon;
   char const *curp;
   NYD_ENTER;

   _oarg = NULL;

   if ((colon = (optstring[0] == ':')))
      ++optstring;
   if (lastp != NULL) {
      curp = lastp;
      lastp = 0;
   } else {
      if (_oind >= argc || argv[_oind] == NULL || argv[_oind][0] != '-' ||
            argv[_oind][1] == '\0')
         goto jleave;
      if (argv[_oind][1] == '-' && argv[_oind][2] == '\0') {
         /* We need this in for MTA arg detection (easier) ++_oind;*/
         goto jleave;
      }
      curp = &argv[_oind][1];
   }

   _oopt = curp[0];
   while (optstring[0] != '\0') {
      if (optstring[0] == ':' || optstring[0] != _oopt) {
         ++optstring;
         continue;
      }
      if (optstring[1] == ':') {
         if (curp[1] != '\0') {
            _oarg = UNCONST(curp + 1);
            ++_oind;
         } else {
            if ((_oind += 2) > argc) {
               if (!colon /*&& _oerr*/) {
                  n_err(_("%s: option requires an argument -- %c\n"),
                     argv[0], (char)_oopt);
               }
               rv = (colon ? ':' : '?');
               goto jleave;
            }
            _oarg = argv[_oind - 1];
         }
      } else {
         if (curp[1] != '\0')
            lastp = curp + 1;
         else
            ++_oind;
      }
      rv = _oopt;
      goto jleave;
   }

   /* Special support for --help, which is quite common */
   if (_oopt == '-' && !strcmp(curp, "-help") && curp - 1 == argv[_oind]) {
      ++_oind;
      rv = 'h';
      goto jleave;
   }

   /* Definitive error */
   if (!colon /*&& opterr*/)
      n_err(_("%s: invalid option -- %c\n"), argv[0], _oopt);
   if (curp[1] != '\0')
      lastp = curp + 1;
   else
      ++_oind;
   _oarg = 0;
   rv = '?';
jleave:
   NYD_LEAVE;
   return rv;
}

static void
_startup(void)
{
   char *cp;
   NYD_ENTER;

   image = -1;
   dflpipe = SIG_DFL;
   _oind = /*_oerr =*/ 1;

   if ((cp = strrchr(progname, '/')) != NULL)
      progname = ++cp;

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

   if (isatty(STDIN_FILENO)) /* TODO should be isatty(0) && isatty(2)?? */
      options |= OPT_TTYIN;
   if (isatty(STDOUT_FILENO))
      options |= OPT_TTYOUT;
   if ((options & (OPT_TTYIN | OPT_TTYOUT)) == (OPT_TTYIN | OPT_TTYOUT)) {
      options |= OPT_INTERACTIVE;
      safe_signal(SIGPIPE, dflpipe = SIG_IGN);
   }

   /*  --  >8  --  8<  --  */

   /* Define defaults for internal variables, based on POSIX 2008/Cor 1-2013 */
   /* (Keep in sync:
    * ./main.c:_startup(), ./nail.rc, ./nail.1:"Initial settings") */
   /* noallnet */
   /* noappend */
   ok_bset(asksub, TRU1);
   /* noaskbcc */
   /* noaskcc */
   /* noautoprint */
   /* nobang */
   /* nocmd */
   /* nocrt */
   /* nodebug */
   /* nodot */
   /* ok_vset(escape, ESCAPE *"~"*); TODO non-compliant */
   /* noflipr */
   /* nofolder */
   ok_bset(header, TRU1);
   /* nohold */
   /* noignore */
   /* noignoreeof */
   /* nokeep */
   /* nokeepsave */
   /* nometoo */
   /* noonehop -- Note: we ignore this one */
   /* nooutfolder */
   /* nopage */
   ok_vset(prompt, "\\& "); /* POSIX "? " unless *bsdcompat*, then "& " */
   /* noquiet */
   /* norecord */
   ok_bset(save, TRU1);
   /* nosendwait */
   /* noshowto */
   /* nosign */
   /* noSign */
   /* ok_vset(toplines, "5"); XXX somewhat hmm */

   /* TODO until we have an automatic mechanism for that, set some more
    * TODO variables so that users see the internal fallback settings
    * TODO (something like "defval=X,notempty=1") */
   do {
      char const *vp;

      vp = env_vlook("SHELL", TRU1);
      ok_vset(SHELL, (vp != NULL ? vp : XSHELL));

      vp = env_vlook("LISTER", TRU1);
      ok_vset(LISTER, (vp != NULL ? vp : XLISTER));

      vp = env_vlook("PAGER", TRU1);
      ok_vset(PAGER, (vp != NULL ? vp : XPAGER));

      ok_vset(sendmail, SENDMAIL);
      ok_vset(sendmail_progname, SENDMAIL_PROGNAME);
   } while (0);

   /*  --  >8  --  8<  --  */

#ifdef HAVE_SETLOCALE
   setlocale(LC_ALL, "");
   mb_cur_max = MB_CUR_MAX;
# ifdef HAVE_NL_LANGINFO
   if (ok_vlook(ttycharset) == NULL && (cp = nl_langinfo(CODESET)) != NULL)
      ok_vset(ttycharset, cp);
# endif

# ifdef HAVE_C90AMEND1
   if (mb_cur_max > 1) {
      wchar_t  wc;
      if (mbtowc(&wc, "\303\266", 2) == 2 && wc == 0xF6 &&
            mbtowc(&wc, "\342\202\254", 3) == 3 && wc == 0x20AC)
         options |= OPT_UNICODE;
      /* Reset possibly messed up state; luckily this also gives us an
       * indication wether the encoding has locking shift state sequences */
      /* TODO temporary - use option bits! */
      if (mbtowc(&wc, NULL, mb_cur_max))
         options |= OPT_ENC_MBSTATE;
   }
# endif
#else
   mb_cur_max = 1;
#endif

#ifdef HAVE_ICONV
   iconvd = (iconv_t)-1;
#endif
   NYD_LEAVE;
}

static size_t
_grow_cpp(char const ***cpp, size_t newsize, size_t oldcnt)
{
   /* Before spreserve(): use our string pool instead of LibC heap */
   char const **newcpp;
   NYD_ENTER;

   newcpp = salloc(sizeof(char*) * newsize);

   if (oldcnt > 0)
      memcpy(newcpp, *cpp, oldcnt * sizeof(char*));
   *cpp = newcpp;
   NYD_LEAVE;
   return newsize;
}

static void
_setup_vars(void)
{
   /* Before spreserve(): use our string pool instead of LibC heap */
   /* XXX further check paths? */
   char const *cp;
   uid_t uid;
   struct passwd *pwuid, *pw;
   NYD_ENTER;

   /* Verify and fixate user identification */
   if (myname != NULL)
      cp = myname;
   else if ((cp = env_vlook("LOGNAME", TRU1)) == NULL)
      cp = env_vlook("USER", TRU1);

   group_id = getgid();
   user_id = uid = getuid();
   if ((pwuid = getpwuid(uid)) == NULL)
      n_panic(_("Cannot associate a name with uid %u"), user_id);

   if (cp == NULL || *cp == '\0')
      myname = pwuid->pw_name;
   else if ((pw = getpwnam(cp)) == NULL) {
      n_alert(_("\"%s\" is not a user of this system"), cp);
      exit(EXIT_NOUSER);
   } else {
      myname = pw->pw_name;
      if (pw->pw_uid != uid)
         options |= OPT_u_FLAG;
   }
   myname = savestr(myname);
   /* XXX myfullname = pw->pw_gecos[OPTIONAL!] -> GUT THAT; TODO pw_shell */

   /* */
   if ((cp = env_vlook("HOME", TRU1)) == NULL)
      cp = "."; /* XXX User and Login objects; Login: pw->pw_dir */
   homedir = savestr(cp);

   tempdir = ((cp = env_vlook("TMPDIR", TRU1)) != NULL)
         ? savestr(cp) : TMPDIR_FALLBACK;
   NYD_LEAVE;
}

static void
_setscreensize(int is_sighdl) /* TODO global policy; int wraps; minvals! */
{
   struct termios tbuf;
#ifdef TIOCGWINSZ
   struct winsize ws;
#elif defined TIOCGSIZE
   struct ttysize ts;
#endif
   NYD_ENTER;

   scrnheight = realscreenheight = scrnwidth = 0;

   /* (Also) POSIX: LINES and COLUMNS always override.  Adjust this
    * a little bit to be able to honour resizes during our lifetime and
    * only honour it upon first run; abuse *is_sighdl* as an indicator */
   if (!is_sighdl) {
      char *cp;
      long i;

      if ((cp = env_vlook("LINES", FAL0)) != NULL &&
            (i = strtol(cp, NULL, 10)) > 0 && i < INT_MAX)
         scrnheight = realscreenheight = (int)i;
      if ((cp = env_vlook("COLUMNS", FAL0)) != NULL &&
            (i = strtol(cp, NULL, 10)) > 0 && i < INT_MAX)
         scrnwidth = (int)i;

      if (scrnwidth != 0 && scrnheight != 0)
         goto jleave;
   }

#ifdef TIOCGWINSZ
   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGSIZE
   if (ioctl(STDOUT_FILENO, TIOCGSIZE, &ws) == -1)
      ts.ts_lines = ts.ts_cols = 0;
#endif

   if (scrnheight == 0) {
      speed_t ospeed = ((tcgetattr(STDOUT_FILENO, &tbuf) == -1)
            ? B9600 : cfgetospeed(&tbuf));

      if (ospeed < B1200)
         scrnheight = 9;
      else if (ospeed == B1200)
         scrnheight = 14;
#ifdef TIOCGWINSZ
      else if (ws.ws_row != 0)
         scrnheight = ws.ws_row;
#elif defined TIOCGSIZE
      else if (ts.ts_lines != 0)
         scrnheight = ts.ts_lines;
#endif
      else
         scrnheight = 24;

#if defined TIOCGWINSZ || defined TIOCGSIZE
      if (0 ==
# ifdef TIOCGWINSZ
            (realscreenheight = ws.ws_row)
# else
            (realscreenheight = ts.ts_lines)
# endif
      )
         realscreenheight = 24;
#endif
   }

   if (scrnwidth == 0 && 0 ==
#ifdef TIOCGWINSZ
         (scrnwidth = ws.ws_col)
#elif defined TIOCGSIZE
         (scrnwidth = ts.ts_cols)
#endif
   )
      scrnwidth = 80;

jleave:
#ifdef SIGWINCH
   if (is_sighdl && (options & OPT_INTERACTIVE))
      n_tty_signal(SIGWINCH);
#endif
   NYD_LEAVE;
}

static sigjmp_buf __hdrjmp; /* XXX */

static int
_rcv_mode(char const *folder, char const *Larg, struct X_arg *xhp)
{
   int i;
   sighandler_type prevint;
   NYD_ENTER;

   if (folder == NULL)
      folder = "%";

   if (options & OPT_QUICKRUN_MASK)
      i = FEDIT_RDONLY;
   else
      i = FEDIT_NONE;
   i = setfile(folder, i);
   if (i < 0) {
      exit_status = EXIT_ERR; /* error already reported */
      goto jquit;
   }
   if (options & OPT_QUICKRUN_MASK) {
      exit_status = i;
      if (i == EXIT_OK && (!(options & OPT_EXISTONLY) ||
            (options & OPT_HEADERLIST)))
         print_header_summary(Larg);
      goto jquit;
   }
   check_folder_hook(FAL0);

   if (i > 0 && !ok_blook(emptystart)) {
      exit_status = EXIT_ERR;
      goto jleave;
   }

   if (sigsetjmp(__hdrjmp, 1) == 0) {
      if ((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, _hdrstop);
      if (!(options & OPT_N_FLAG)) {
         if (!ok_blook(quiet))
            printf(_("%s version %s.  Type ? for help.\n"),
               (ok_blook(bsdcompat) ? "Mail" : uagent), ok_vlook(version));
         announce(1);
         fflush(stdout);
      }
      safe_signal(SIGINT, prevint);
   }

   /* Enter the command loop */
   if (xhp == NULL || _X_arg_eval(xhp)) {
      if (options & OPT_INTERACTIVE)
         n_tty_init();
      commands();
      if (options & OPT_INTERACTIVE)
         n_tty_destroy();
   }

   if (mb.mb_type == MB_FILE || mb.mb_type == MB_MAILDIR) {
      safe_signal(SIGHUP, SIG_IGN);
      safe_signal(SIGINT, SIG_IGN);
      safe_signal(SIGQUIT, SIG_IGN);
   }
jquit:
   save_mbox_for_possible_quitstuff();
   quit();
jleave:
   NYD_LEAVE;
   return exit_status;
}

static void
_hdrstop(int signo)
{
   NYD_X; /* Signal handler */
   UNUSED(signo);

   fflush(stdout);
   n_err_sighdl(_("\nInterrupt\n"));
   siglongjmp(__hdrjmp, 1);
}

static bool_t
_X_arg_eval(struct X_arg *xhp) /* TODO error handling not right */
{
   struct eval_ctx ev;
   struct X_arg *xp;
   bool_t rv = TRU1;
   NYD_ENTER;

   while ((xp = xhp) != NULL) {
      xhp = xp->xa_next;

      memset(&ev, 0, sizeof ev);
      ev.ev_line.s = xp->xa_cmd_buf;
      ev.ev_line.l = xp->xa_cmd_len;
      ev.ev_is_recursive = TRU1;
      pstate &= ~PS_HOOK_MASK;
      rv = (evaluate(&ev) == 0);
      free(xp);

      if (!rv) {
         if (exit_status == EXIT_OK)
            exit_status = EXIT_ERR;
         break;
      }
      if ((options & OPT_BATCH_FLAG) && ok_blook(batch_exit_on_error)) {
         if (exit_status != EXIT_OK)
            break;
         if (pstate & PS_EVAL_ERROR) {
            exit_status = EXIT_ERR;
            break;
         }
      }
   }
   NYD_LEAVE;
   return rv;
}

int
main(int argc, char *argv[])
{
   static char const optstr[] = "A:a:Bb:c:dEeFfHhiL:NnO:q:Rr:S:s:tu:VvX:::~#.",
      usagestr[] = N_(
         " Synopsis:\n"
         "  %s -h | --help\n"
         "  %s [-BdEFintv~] [-: spec] [-A account]\n"
         "\t [-a attachment] [-b bcc-address] [-c cc-address]\n"
         "\t [-q file] [-r from-address] [-S var[=value]...]\n"
         "\t [-s subject] [-X cmd] [-.] to-address... [-- mta-option...]\n"
         "  %s [-BdEeHiNnRv~#] [-: spec] [-A account]\n"
         "\t [-L spec-list] [-r from-address] [-S var[=value]...]\n"
         "\t [-X cmd] -f [file] [-- mta-option...]\n"
         "  %s [-BdEeHiNnRv~#] [-: spec] [-A account]\n"
         "\t [-L spec-list] [-r from-address] [-S var[=value]...]\n"
         "\t [-u user] [-X cmd] [-- mta-option...]\n"
      );
#define _USAGE_ARGS , progname, progname, progname, progname

   struct X_arg *X_head = NULL, *X_curr = /* silence CC */ NULL;
   struct a_arg *a_head = NULL, *a_curr = /* silence CC */ NULL;
   struct name *to = NULL, *cc = NULL, *bcc = NULL;
   struct attachment *attach = NULL;
   char *cp = NULL, *subject = NULL, *qf = NULL, *Aarg = NULL, *Larg = NULL;
   char const *okey, **oargs = NULL, *folder = NULL, *emsg = NULL;
   size_t oargs_size = 0, oargs_count = 0, smopts_size = 0;
   enum{
      a_RF_NONE = 0,
      a_RF_SET = 1<<0,
      a_RF_SYSTEM = 1<<1,
      a_RF_USER = 1<<2,
      a_RF_ALL = a_RF_SYSTEM | a_RF_USER
   } resfiles;
   int i;
   NYD_ENTER;

   resfiles = a_RF_ALL;

   /*
    * Start our lengthy setup, finalize by setting PS_STARTED
    */

   progname = argv[0];
   _startup();

   /* Command line parsing
    * Variable settings need to be done twice, since the user surely wants the
    * setting to take effect immediately, but also doesn't want it to be
    * overwritten from within resource files */
   while ((i = _getopt(argc, argv, optstr)) >= 0) {
      switch (i) {
      case 'A':
         /* Execute an account command later on */
         Aarg = _oarg;
         break;
      case 'a':
         options |= OPT_SENDMODE;
         {  struct a_arg *nap = salloc(sizeof(struct a_arg));
            if (a_head == NULL)
               a_head = nap;
            else
               a_curr->aa_next = nap;
            nap->aa_next = NULL;
            nap->aa_file = _oarg;
            a_curr = nap;
         }
         break;
      case 'B':
         /* Make 0/1 line buffered */
         setvbuf(stdin, NULL, _IOLBF, 0);
         setvbuf(stdout, NULL, _IOLBF, 0);
         break;
      case 'b':
         /* Get Blind Carbon Copy Recipient list */
         options |= OPT_SENDMODE;
         bcc = cat(bcc, lextract(_oarg, GBCC | GFULL));
         break;
      case 'c':
         /* Get Carbon Copy Recipient list */
         options |= OPT_SENDMODE;
         cc = cat(cc, lextract(_oarg, GCC | GFULL));
         break;
      case 'd':
         ok_bset(debug, TRU1);
         okey = "debug";
         goto joarg;
      case 'E':
         ok_bset(skipemptybody, TRU1);
         okey = "skipemptybody";
         goto joarg;
      case 'e':
         options |= OPT_EXISTONLY;
         break;
      case 'F':
         options |= OPT_F_FLAG | OPT_SENDMODE;
         break;
      case 'f':
         /* User is specifying file to "edit" with Mail, as opposed to reading
          * system mailbox.  If no argument is given, we read his mbox file.
          * Check for remaining arguments later */
         folder = "&";
         break;
      case 'H':
         options |= OPT_HEADERSONLY;
         break;
      case 'h':
         n_err(V_(usagestr) _USAGE_ARGS);
         goto jleave;
      case 'i':
         /* Ignore interrupts */
         ok_bset(ignore, TRU1);
         okey = "ignore";
         goto joarg;
      case 'L':
         Larg = _oarg;
         options |= OPT_HEADERLIST;
         if (*Larg == '"' || *Larg == '\'') { /* TODO list.c:listspec_check() */
            size_t j = strlen(++Larg);
            if (j > 0)
               Larg[j - 1] = '\0';
         }
         break;
      case 'N':
         /* Avoid initial header printing */
         ok_bset(header, FAL0);
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
         if (smopts_count == (size_t)smopts_size)
            smopts_size = _grow_cpp(&smopts, smopts_size + 8, smopts_count);
         smopts[smopts_count++] = _oarg;
         break;
      case 'q':
         /* Quote file TODO drop? -Q with real quote?? what ? */
         options |= OPT_SENDMODE;
         if (*_oarg != '-')
            qf = _oarg;
         break;
      case 'R':
         /* Open folders read-only */
         options |= OPT_R_FLAG;
         break;
      case 'r':
         /* Set From address. */
         options |= OPT_r_FLAG;
         if (_oarg[0] != '\0') {
            struct name *fa = nalloc(_oarg, GSKIN | GFULL | GFULLEXTRA);

            if (is_addr_invalid(fa, EACM_STRICT | EACM_NOLOG)) {
               emsg = N_("Invalid address argument with -r");
               goto jusage;
            }
            option_r_arg = fa;
            /* TODO -r options is set in smopts, but may
             * TODO be overwritten by setting from= in
             * TODO an interactive session!
             * TODO Maybe disable setting of from?
             * TODO Warn user?  Update manual!! */
            okey = savecat("from=", fa->n_fullname);
            goto joarg;
         }
         break;
      case 'S':
         /* Set variable (twice) */
         {  char *a[2];
            okey = a[0] = _oarg;
            a[1] = NULL;
            c_set(a);
         }
joarg:
         if (oargs_count == oargs_size)
            oargs_size = _grow_cpp(&oargs, oargs_size + 8, oargs_count);
         oargs[oargs_count++] = okey;
         break;
      case 's':
         /* Subject: */
         subject = _oarg;
         options |= OPT_SENDMODE;
         break;
      case 't':
         /* Read defined set of headers from mail to be send */
         options |= OPT_SENDMODE | OPT_t_FLAG;
         break;
      case 'u':
         /* Set user name to pretend to be; don't set OPT_u_FLAG yet, this is
          * done as necessary in _setup_vars() above */
         myname = _oarg;
         break;
      case 'V':
         puts(ok_vlook(version));
         exit(EXIT_OK);
         /* NOTREACHED */
      case 'v':
         /* Be verbose */
         ok_bset(verbose, TRU1);
         okey = "verbose";
         goto joarg;
      case 'X': {
            /* Add to list of commands to exec before entering normal operation */
            size_t l = strlen(_oarg);
            struct X_arg *nxp = smalloc(sizeof(*nxp) -
                  VFIELD_SIZEOF(struct X_arg, xa_cmd_buf) + l +1);
            if (X_head == NULL)
               X_head = nxp;
            else
               X_curr->xa_next = nxp;
            X_curr = nxp;
            nxp->xa_next = NULL;
            nxp->xa_cmd_len = l;
            memcpy(nxp->xa_cmd_buf, _oarg, l +1);
         }
         break;
      case ':':
         /* Control which resource files shall be loaded */
         if(!(resfiles & (a_RF_SET | a_RF_SYSTEM))){
            emsg = N_("-n cannot be used in conjunction with -:");
            goto jusage;
         }
         resfiles = a_RF_SET;
         while((i = *_oarg++) != '\0')
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
         /* Enable tilde escapes even in non-interactive mode */
         options |= OPT_TILDE_FLAG;
         break;
      case '#':
         /* Work in batch mode, even if non-interactive */
         if (oargs_count + 5 >= oargs_size)
            oargs_size = _grow_cpp(&oargs, oargs_size + 8, oargs_count);
         /* xxx Setting most of the -# options immediately is useless, so be
          * selective in what is set immediately */
         options |= OPT_TILDE_FLAG | OPT_BATCH_FLAG;
         folder = "/dev/null";
         ok_bset(emptystart, TRU1);
         ok_bset(header, FAL0);
         ok_bset(quiet, TRU1);
         ok_bset(sendwait, TRU1);
         ok_vset(MBOX, folder);
         oargs[oargs_count + 0] = "emptystart";
         oargs[oargs_count + 1] = "noheader";
         oargs[oargs_count + 2] = "quiet";
         oargs[oargs_count + 3] = "sendwait";
         oargs[oargs_count + 4] = "MBOX=/dev/null";
         oargs_count += 5;
         break;
      case '.':
         options |= OPT_SENDMODE;
         goto jgetopt_done;
      case '?':
jusage:
         if (emsg != NULL)
            n_err("%s\n", V_(emsg));
         n_err(V_(usagestr) _USAGE_ARGS);
#undef _USAGE_ARGS
         exit_status = EXIT_USE;
         goto jleave;
      }
   }
jgetopt_done:
   ;

   /* The normal arguments may be followed by MTA arguments after a "--";
    * however, -f may take off an argument, too, and before that.
    * Since MTA arguments after "--" require *expandargv*, delay parsing off
    * those options until after the resource files are loaded... */
   if ((cp = argv[i = _oind]) == NULL)
      ;
   else if (cp[0] == '-' && cp[1] == '-' && cp[2] == '\0')
      ++i;
   /* OPT_BATCH_FLAG sets to /dev/null, but -f can still be used and sets & */
   else if (folder != NULL && folder[1] == '\0') {
      folder = cp;
      if ((cp = argv[++i]) != NULL) {
         if (cp[0] != '-' || cp[1] != '-' || cp[2] != '\0') {
            emsg = N_("More than one file given with -f");
            goto jusage;
         }
         ++i;
      }
   } else {
      options |= OPT_SENDMODE;
      for (;;) {
         to = cat(to, lextract(cp, GTO | GFULL));
         if ((cp = argv[++i]) == NULL)
            break;
         if (cp[0] == '-' && cp[1] == '-' && cp[2] == '\0') {
            ++i;
            break;
         }
      }
   }
   _oind = i;

   /* ...BUT, since we use salloc() for the MTA smopts storage we need to
    * allocate the necessary space for them before we call spreserve()! */
   while (argv[i] != NULL)
      ++i;
   if (smopts_count + i > smopts_size)
      DBG(smopts_size =) _grow_cpp(&smopts, smopts_count + i + 1, smopts_count);

   /* Check for inconsistent arguments */
   if (options & OPT_SENDMODE) {
      if (folder != NULL && !(options & OPT_BATCH_FLAG)) {
         emsg = N_("Cannot give -f and people to send to.");
         goto jusage;
      }
      if (myname != NULL) {
         emsg = N_("The -u option cannot be used in send mode");
         goto jusage;
      }
      if (!(options & OPT_t_FLAG) && to == NULL) {
         emsg = N_("Send options without primary recipient specified.");
         goto jusage;
      }
      if (options & (OPT_EXISTONLY | OPT_HEADERSONLY | OPT_HEADERLIST)) {
         emsg = N_("The -e, -H and -L options cannot be used in send mode.");
         goto jusage;
      }
      if (options & OPT_R_FLAG) {
         emsg = N_("The -R option is meaningless in send mode.");
         goto jusage;
      }
   } else {
      if (folder != NULL && myname != NULL) {
         emsg = N_("The options -f and -u are mutually exclusive");
         goto jusage;
      }
      if ((options & (OPT_EXISTONLY | OPT_HEADERSONLY)) ==
            (OPT_EXISTONLY | OPT_HEADERSONLY)) {
         emsg = N_("The options -e and -H are mutual exclusive");
         goto jusage;
      }
      if ((options & (OPT_HEADERSONLY | OPT_HEADERLIST)) == /* TODO OBSOLETE */
            (OPT_HEADERSONLY | OPT_HEADERLIST))
         OBSOLETE(_("please use \"-e -L xy\" instead of \"-H -L xy\""));
   }

   /*
    * Likely to go, perform more setup
    */

   _setup_vars();

   if (options & OPT_INTERACTIVE) {
      _setscreensize(0);
#ifdef SIGWINCH
# ifndef TTY_WANTS_SIGWINCH
      if (safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
# endif
         safe_signal(SIGWINCH, _setscreensize);
#endif
   } else
      scrnheight = realscreenheight = 24, scrnwidth = 80;

   /* Snapshot our string pools.  Memory is auto-reclaimed from now on */
   spreserve();

   /* load() any resource files */
   if(resfiles & a_RF_ALL){
      /* *expand() returns a savestr(), but load only uses the file name for
       * fopen(), so it's safe to do this */
      if((resfiles & a_RF_SYSTEM) && !env_blook("NAIL_NO_SYSTEM_RC", TRU1))
         load(SYSCONFDIR "/" SYSCONFRC);
      if(resfiles & a_RF_USER){
         if((cp = env_vlook("MAILRC", TRU1)) == NULL)
            cp = UNCONST(MAILRC);
         load(file_expand(cp));
      }
      if(env_vlook("NAIL_EXTRA_RC", TRU1) == NULL &&
            (cp = ok_vlook(NAIL_EXTRA_RC)) != NULL)
         load(file_expand(cp));
   }

   /* We had to wait until the resource files are loaded but it is time to get
    * the termcap going, so that *term-ca-mode* won't hide our output for us */
#ifdef HAVE_TERMCAP
   if ((options & (OPT_INTERACTIVE | OPT_QUICKRUN_MASK)) == OPT_INTERACTIVE)
      termcap_init(); /* TODO program state machine */
#endif

   /* Now we can set the account */
   if (Aarg != NULL) {
      char const *a[2];
      a[0] = Aarg;
      a[1] = NULL;
      c_account(a);
   }

   /* Ensure the -S and other command line options take precedence over
    * anything that may have been placed in resource files.
    * Our "ternary binary" option *verbose* needs special treament */
   if ((options & (OPT_VERB | OPT_VERBVERB)) == OPT_VERB)
      options &= ~OPT_VERB;
   for (i = 0; UICMP(z, i, <, oargs_count); ++i) {
      char const *a[2];
      a[0] = oargs[i];
      a[1] = NULL;
      c_set(a);
   }

   /* Additional options to pass-through to MTA, and allowed to do so? */
   if ((cp = ok_vlook(expandargv)) != NULL) {
      bool_t isfail = !asccasecmp(cp, "fail"),
         isrestrict = (!isfail && !asccasecmp(cp, "restrict"));

      if ((cp = argv[i = _oind]) != NULL) {
         if (isfail ||
               (isrestrict && !(options & (OPT_INTERACTIVE |OPT_TILDE_FLAG)))) {
            n_err(_("*expandargv* doesn't allow MTA arguments; consider "
               "using *sendmail-arguments*\n"));
            exit_status = EXIT_USE | EXIT_SEND_ERROR;
            goto jleave;
         }
         do {
            assert(smopts_count + 1 <= smopts_size);
            smopts[smopts_count++] = cp;
         } while ((cp = argv[++i]) != NULL);
      }
   }

   /*
    * We're finally completely setup and ready to go
    */

   pstate |= PS_STARTED;

   if (options & OPT_DEBUG)
      n_err(_("user = %s, homedir = %s\n"), myname, homedir);

   if (!(options & OPT_SENDMODE)) {
      exit_status = _rcv_mode(folder, Larg, X_head);
      goto jleave;
   }

   /* xxx exit_status = EXIT_OK; */
   if (X_head != NULL && !_X_arg_eval(X_head))
      goto jleave;

   /* Now that full mailx(1)-style file expansion is possible handle the
    * attachments which we had delayed due to this.
    * This may use savestr(), but since we won't enter the command loop we
    * don't need to care about that */
   for (cp = NULL; a_head != NULL;) {
      struct attachment *nahp, *nap;

      if ((nahp = add_attachment(attach, a_head->aa_file, &nap)) != NULL) {
         attach = nahp;
         /* Did we split a charset set name for fixation purposes? */
         if (cp != NULL) {
            nap->a_conv = AC_FIX_INCS;
            nap->a_input_charset = cp;
            cp = NULL;
         }
         a_head = a_head->aa_next;
         continue;
      }
      i = errno;

      /* It may not have worked because of an appended character set name, so
       * try to split name and charset and retry once */
      if (cp == NULL && (cp = strrchr(a_head->aa_file, '=')) != NULL) {
         char *ncp, *nfp = savestrbuf(a_head->aa_file,
               PTR2SIZE(cp - a_head->aa_file));

         for (ncp = ++cp; *ncp != '\0'; ++ncp)
            if (!alnumchar(*ncp) && !punctchar(*ncp))
               break;
         if (*ncp == '\0') {
            a_head->aa_file = nfp;
            continue;
         }
      }

      n_perr(a_head->aa_file, i);
      exit_status = EXIT_ERR;
      goto jleave;
   }

   if (options & OPT_INTERACTIVE)
      n_tty_init();
   mail(to, cc, bcc, subject, attach, qf, ((options & OPT_F_FLAG) != 0));
   if (options & OPT_INTERACTIVE)
      n_tty_destroy();

jleave:
#ifdef HAVE_TERMCAP
   if (options & OPT_INTERACTIVE)
      termcap_destroy();
#endif
#ifdef HAVE_DEBUG
   sreset(FAL0);
   smemcheck();
#endif
   NYD_LEAVE;
   return exit_status;
}

FL int
c_rexit(void *v) /* TODO program state machine */
{
   UNUSED(v);
   NYD_ENTER;

   if (!(pstate & PS_SOURCING)) {
#ifdef HAVE_TERMCAP
      if (options & OPT_INTERACTIVE)
         termcap_destroy();
#endif
      exit(EXIT_OK);
   }
   NYD_LEAVE;
   return 1;
}

/* Source the others in that case! */
#ifdef HAVE_AMALGAMATION
# include "config.h"
#endif

/* s-it-mode */
