/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Startup -- interface with user.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
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
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
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

/*
 * Most strcpy/sprintf functions have been changed to strncpy/snprintf to
 * correct several buffer overruns (at least one ot them was exploitable).
 * Sat Jun 20 04:58:09 CEST 1998 Alvaro Martinez Echevarria <alvaro@lander.es>
 * ---
 * Note: We set egid to realgid ... and only if we need the egid we will
 *       switch back temporary.  Nevertheless, I do not like seg faults.
 *       Werner Fink, <werner@suse.de>
 */

#ifndef HAVE_AMALGAMATION
# define _MAIN_SOURCE
# include "nail.h"
#endif

#include <sys/ioctl.h>

#include <fcntl.h>
#include <pwd.h>

#ifdef HAVE_NL_LANGINFO
# include <langinfo.h>
#endif
#ifdef HAVE_SETLOCALE
# include <locale.h>
#endif

#include "version.h"

struct a_arg {
   struct a_arg   *aa_next;
   char           *aa_file;
};

/* (extern, but not with amalgamation, so define here) */
VL char const        weekday_names[7 + 1][4] = {
   "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", ""
};
VL char const        month_names[12 + 1][4] = {
   "Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", ""
};
VL char const        uagent[] = UAGENT;
VL char const        version[] = VERSION;
VL sighandler_type   dflpipe = SIG_DFL;

/* getopt(3) fallback implementation */
#ifdef HAVE_GETOPT
# define _oarg       optarg
# define _oind       optind
/*# define _oerr     opterr*/
# define _oopt       optopt
#else
static char          *_oarg;
static int           _oind, /*_oerr,*/ _oopt;
#endif

/* getopt(3) fallback implementation */
#ifdef HAVE_GETOPT
# define _getopt     getopt
#else
static int  _getopt(int argc, char *const argv[], const char *optstring);
#endif

/* Perform basic startup initialization */
static void _startup(void);

/* Grow a char** */
static int  _grow_cpp(char const ***cpp, int size, int cnt);

/* Initialize *tempdir*, *myname*, *homedir* */
static void _setup_vars(void);

/* We're in an interactive session - compute what the screen size for printing
 * headers etc. should be; notify tty upon resize if *is_sighdl* is not 0.
 * We use the following algorithm for the height:
 * If baud rate < 1200, use  9
 * If baud rate = 1200, use 14
 * If baud rate > 1200, use 24 or ws_row
 * Width is either 80 or ws_col */
static void _setscreensize(int is_sighdl);

/* Ok, we are reading mail.  Decide whether we are editing a mailbox or reading
 * the system mailbox, and open up the right stuff */
static int  _rcv_mode(char const *folder);

/* Interrupt printing of the headers */
static void _hdrstop(int signo);

#ifndef HAVE_GETOPT
static int
_getopt(int argc, char * const argv[], char const *optstring)
{
   static char const *lastp;
   int rv = -1, colon;
   char const *curp;

   if ((colon = (optstring[0] == ':')))
      ++optstring;
   if (lastp != NULL) {
      curp = lastp;
      lastp = 0;
   } else {
      if (_oind >= argc || argv[_oind] == 0 || argv[_oind][0] != '-' ||
            argv[_oind][1] == '\0')
         goto jleave;
      if (argv[_oind][1] == '-' && argv[_oind][2] == '\0') {
         ++_oind;
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
            _oarg = UNCONST(&curp[1]);
            ++_oind;
         } else {
            if ((_oind += 2) > argc) {
               if (!colon /*&& _oerr*/) {
                  fprintf(stderr,
                     tr(79, "%s: option requires an argument -- %c\n"),
                     argv[0], (char)_oopt);
               }
               rv = (colon ? ':' : '?');
               goto jleave;
            }
            _oarg = argv[_oind - 1];
         }
      } else {
         if (curp[1] != '\0')
            lastp = &curp[1];
         else
            ++_oind;
         _oarg = NULL;
      }
      rv = _oopt;
      goto jleave;
   }

   if (!colon /*&& opterr*/)
      fprintf(stderr, tr(78, "%s: illegal option -- %c\n"), argv[0], _oopt);
   if (curp[1] != '\0')
      lastp = &curp[1];
   else
      ++_oind;
   _oarg = 0;
   rv = '?';
jleave:
   return rv;
}
#endif /* !HAVE_GETOPT */

static void
_startup(void)
{
   char *cp;

   /* Absolutely the first thing we do is save our egid
    * and set it to the rgid, so that we can safely run
    * setgid.  We use the sgid (saved set-gid) to allow ourselves
    * to revert to the egid if we want (temporarily) to become
    * privileged XXX (s-nail-)*dotlock(-program)* */
   effectivegid = getegid();
   realgid = getgid();
   if (setgid(realgid) < 0) {
      perror("setgid");
      exit(1);
   }

   image = -1;
   dflpipe = SIG_DFL;
#ifndef HAVE_GETOPT
   _oind = /*_oerr =*/ 1;
#endif

   if ((cp = strrchr(progname, '/')) != NULL)
      progname = ++cp;

   /* Set up a reasonable environment.
    * Figure out whether we are being run interactively,
    * start the SIGCHLD catcher, and so forth */
   safe_signal(SIGCHLD, sigchild);

   if (isatty(STDIN_FILENO)) /* TODO should be isatty(0) && isatty(2)?? */
      options |= OPT_TTYIN | OPT_INTERACTIVE;
   if (isatty(STDOUT_FILENO))
      options |= OPT_TTYOUT;
   if (IS_TTY_SESSION())
      safe_signal(SIGPIPE, dflpipe = SIG_IGN);

   /* Define defaults for internal variables, based on POSIX 2008/Cor 1-2013 */
   /* noallnet */
   /* noappend */
   assign("asksub", "");
   /* noaskbcc */
   /* noaskcc */
   /* noautoprint */
   /* nobang */
   /* nocmd */
   /* nocrt */
   /* nodebug */
   /* nodot */
   /* assign("escape", "~"); TODO non-compliant */
   /* noflipr */
   /* nofolder */
   assign("header", "");
   /* nohold */
   /* noignore */
   /* noignoreeof */
   /* nokeep */
   /* nokeepsave */
   /* nometoo */
   /* noonehop -- Note: we ignore this one */
   /* nooutfolder */
   /* nopage */
   assign("prompt", "\\& "); /* POSIX "? " unless *bsdcompat*, then "& " */
   /* noquiet */
   /* norecord */
   assign("save", "");
   /* nosendwait */
   /* noshowto */
   /* nosign */
   /* noSign */
   /* assign("toplines", "5"); XXX somewhat hmm */

#ifdef HAVE_SETLOCALE
   setlocale(LC_ALL, "");
   mb_cur_max = MB_CUR_MAX;
# if defined HAVE_NL_LANGINFO && defined CODESET
   if (voption("ttycharset") == NULL && (cp = nl_langinfo(CODESET)) != NULL)
      assign("ttycharset", cp);
# endif

# if defined HAVE_MBTOWC && defined HAVE_WCTYPE_H
   if (mb_cur_max > 1) {
      wchar_t  wc;
      if (mbtowc(&wc, "\303\266", 2) == 2 && wc == 0xF6 &&
            mbtowc(&wc, "\342\202\254", 3) == 3 && wc == 0x20AC)
         utf8 = 1;
      /* Reset state - it may have been messed up; luckily this also
       * gives us an indication wether the encoding has locking shift
       * state sequences */
      /* TODO temporary - use option bits! */
      enc_has_state = mbtowc(&wc, NULL, mb_cur_max);
   }
# endif
#else
   mb_cur_max = 1;
#endif

#ifdef HAVE_CATGETS
# ifdef NL_CAT_LOCALE
   catd = catopen(CATNAME, NL_CAT_LOCALE);
# else
   catd = catopen(CATNAME, 0);
# endif
#endif

#ifdef HAVE_ICONV
   iconvd = (iconv_t)-1;
#endif
}

static int
_grow_cpp(char const ***cpp, int size, int cnt)
{
   /* Before spreserve(): use our string pool instead of LibC heap;
    * Increment *size* by at least 5! */
   char const **newcpp = salloc(sizeof(char*) * (size += 8));

   if (cnt > 0)
      memcpy(newcpp, *cpp, (size_t)cnt * sizeof(char*));
   *cpp = newcpp;
   return size;
}

static void
_setup_vars(void)
{
   /* Before spreserve(): use our string pool instead of LibC heap */
   /* XXX further check paths? */
   char const *cp;
   uid_t uid;
   struct passwd *pwuid, *pw;

   tempdir = ((cp = getenv("TMPDIR")) != NULL) ? savestr(cp) : TMPDIR_FALLBACK;

   cp = (myname == NULL) ? getenv("USER") : myname;
   uid = getuid();
   if ((pwuid = getpwuid(uid)) == NULL)
      panic(tr(201, "Cannot associate a name with uid %lu"), (ul_it)uid);
   if (cp == NULL)
      myname = pwuid->pw_name;
   else if ((pw = getpwnam(cp)) == NULL)
      panic(tr(236, "`%s' is not a user of this system"), cp);
   else {
      myname = pw->pw_name;
      if (pw->pw_uid != uid)
         options |= OPT_u_FLAG;
   }
   myname = savestr(myname);
   /* XXX myfullname = pw->pw_gecos[OPTIONAL!] -> GUT THAT; TODO pw_shell */

   if ((cp = getenv("HOME")) == NULL)
      cp = "."; /* XXX User and Login objects; Login: pw->pw_dir */
   homedir = savestr(cp);
}

static void
_setscreensize(int is_sighdl)
{
   struct termios tbuf;
#ifdef TIOCGWINSZ
   struct winsize ws;
#elif defined TIOCGSIZE
   struct ttysize ts;
#endif

   scrnheight = realscreenheight = scrnwidth = 0;

   /* (Also) POSIX: LINES and COLUMNS always override.  Adjust this
    * a little bit to be able to honour resizes during our lifetime and
    * only honour it upon first run; abuse *is_sighdl* as an indicator */
   if (!is_sighdl) {
      char *cp;
      long i;

      if ((cp = getenv("LINES")) != NULL && (i = strtol(cp, NULL, 10)) > 0)
         scrnheight = realscreenheight = (int)i;
      if ((cp = getenv("COLUMNS")) != NULL && (i = strtol(cp, NULL, 10)) > 0)
         scrnwidth = (int)i;

      if (scrnwidth != 0 && scrnheight != 0)
         goto jleave;
   }

#ifdef TIOCGWINSZ
   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGSIZE
   if (ioctl(STDOUT_FILENO, TIOCGSIZE, &ws) < 0)
      ts.ts_lines = ts.ts_cols = 0;
#endif

   if (scrnheight == 0) {
      speed_t ospeed = ((tcgetattr(STDOUT_FILENO, &tbuf) < 0)
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
   if (is_sighdl && IS_TTY_SESSION())
      tty_signal(SIGWINCH);
#endif
}

static sigjmp_buf __hdrjmp; /* XXX */

static int
_rcv_mode(char const *folder)
{
   char *cp;
   int i;
   sighandler_type prevint;

   if (folder == NULL)
      folder = "%";
   else if (*folder == '@') {
      /* This must be treated specially to make invocation like
       * -A imap -f @mailbox work */
      if ((cp = value("folder")) != NULL && which_protocol(cp) == PROTO_IMAP)
         (void)n_strlcpy(mailname, cp, MAXPATHLEN);
   }

   i = setfile(folder, 0);
   if (i < 0)
      exit(1);    /* error already reported */
   if (options & OPT_EXISTONLY)
      exit(i);

   if (options & OPT_HEADERSONLY) {
#ifdef HAVE_IMAP
      if (mb.mb_type == MB_IMAP)
         imap_getheaders(1, msgCount);
#endif
      time_current_update(&time_current, FAL0);
      for (i = 1; i <= msgCount; ++i)
         printhead(i, stdout, 0);
      exit(exit_status);
   }

   callhook(mailname, 0);
   if (i > 0 && !boption("emptystart"))
      exit(1);

   if (sigsetjmp(__hdrjmp, 1) == 0) {
      if ((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, _hdrstop);
      if (!(options & OPT_N_FLAG)) {
         if (!value("quiet"))
            printf(tr(140, "%s version %s.  Type ? for help.\n"),
               (boption("bsdcompat") ? "Mail" : uagent), version);
         announce(1);
         fflush(stdout);
      }
      safe_signal(SIGINT, prevint);
   }

   /* Enter the command loop */
   if (options & OPT_INTERACTIVE)
      tty_init();
   commands();
   if (options & OPT_INTERACTIVE)
      tty_destroy();

   if (mb.mb_type == MB_FILE || mb.mb_type == MB_MAILDIR) {
      safe_signal(SIGHUP, SIG_IGN);
      safe_signal(SIGINT, SIG_IGN);
      safe_signal(SIGQUIT, SIG_IGN);
   }
   save_mbox_for_possible_quitstuff();
   quit();
   return exit_status;
}

static void
_hdrstop(int signo)
{
   UNUSED(signo);

   fflush(stdout);
   fprintf(stderr, tr(141, "\nInterrupt\n"));
   siglongjmp(__hdrjmp, 1);
}

int
main(int argc, char *argv[])
{
   static char const optstr[] = "A:a:Bb:c:DdEeFfHiNnO:q:Rr:S:s:tu:Vv~#",
      usagestr[] =
         "Synopsis:\n"
         "  %s [-BDdEFintv~] [-A acc] [-a attachment] "
            "[-b bcc-addr] [-c cc-addr]\n"
         "\t  [-O mtaopt [-O mtaopt-arg]] [-q file] [-r from-addr] "
            "[-S var[=value]]\n"
         "\t  [-s subject] to-addr...\n"
         "  %s [-BDdEeHiNnRv~#] [-A acct] "
            "[-S var[=value]] -f [file]\n"
         "  %s [-BDdEeiNnRv~#] [-A acc] [-S var[=value]] [-u user]\n";

   struct a_arg *a_head = NULL, *a_curr = /* silence CC */ NULL;
   struct name *to = NULL, *cc = NULL, *bcc = NULL;
   struct attachment *attach = NULL;
   char *cp = NULL, *subject = NULL, *qf = NULL, *Aflag = NULL;
   char const *okey, **oargs = NULL, *folder = NULL;
   int oargs_size = 0, oargs_count = 0, smopts_size = 0, i;

   /*
    * Start our lengthy setup
    */

   starting =
   unset_allow_undefined = TRU1;

   progname = argv[0];
   _startup();

   /* Command line parsing */
   while ((i = _getopt(argc, argv, optstr)) >= 0) {
      switch (i) {
      case 'A':
         /* Execute an account command later on */
         Aflag = _oarg;
         break;
      case 'a':
         {  struct a_arg *nap = ac_alloc(sizeof(struct a_arg));
            if (a_head == NULL)
               a_head = nap;
            else
               a_curr->aa_next = nap;
            nap->aa_next = NULL;
            nap->aa_file = _oarg;
            a_curr = nap;
         }
         options |= OPT_SENDMODE;
         break;
      case 'B':
         /* Make 0/1 line buffered */
         setvbuf(stdin, NULL, _IOLBF, 0);
         setvbuf(stdout, NULL, _IOLBF, 0);
         break;
      case 'b':
         /* Get Blind Carbon Copy Recipient list */
         bcc = cat(bcc, checkaddrs(lextract(_oarg, GBCC | GFULL)));
         options |= OPT_SENDMODE;
         break;
      case 'c':
         /* Get Carbon Copy Recipient list */
         cc = cat(cc, checkaddrs(lextract(_oarg, GCC | GFULL)));
         options |= OPT_SENDMODE;
         break;
      case 'D':
#ifdef HAVE_IMAP
         okey = "disconnected";
         goto joarg;
#else
         break;
#endif
      case 'd':
         okey = "debug";
#ifdef HAVE_DEBUG
         assign(okey, "");
#endif
         goto joarg;
      case 'E':
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
      case 'i':
         /* Ignore interrupts */
         okey = "ignore";
         goto joarg;
      case 'N':
         /* Avoid initial header printing */
         okey = "noheader";
         goto joarg;
      case 'n':
         /* Don't source "unspecified system start-up file" */
         options |= OPT_NOSRC;
         break;
      case 'O':
         /* Additional options to pass-through to MTA */
         if (smopts_count == (size_t)smopts_size)
            smopts_size = _grow_cpp(&smopts, smopts_size, (int)smopts_count);
         smopts[smopts_count++] = skin(_oarg);
         break;
      case 'q':
         /* Quote file TODO drop? -Q with real quote?? what ? */
         if (*_oarg != '-')
            qf = _oarg;
         options |= OPT_SENDMODE;
         break;
      case 'R':
         /* Open folders read-only */
         options |= OPT_R_FLAG;
         break;
      case 'r':
         /* Set From address. */
         options |= OPT_r_FLAG;
         if ((option_r_arg = _oarg)[0] != '\0') {
            struct name *fa = nalloc(_oarg, GFULL);

            if (is_addr_invalid(fa, 1) || is_fileorpipe_addr(fa)) {
               fprintf(stderr, tr(271, "Invalid address argument with -r\n"));
               goto jusage;
            }
            option_r_arg = fa->n_name;
            /* TODO -r options is set in smopts, but may
             * TODO be overwritten by setting from= in
             * TODO an interactive session!
             * TODO Maybe disable setting of from?
             * TODO Warn user?  Update manual!! */
            okey = savecat("from=", fa->n_fullname);
            goto joarg;
            /* ..and fa goes even though it is ready :/ */
         }
         break;
      case 'S':
         /* Set variable.  We need to do this twice, since the
          * user surely wants the setting to take effect
          * immediately, but also doesn't want it to be
          * overwritten from within resource files */
         {  char *a[2];
            okey = a[0] = _oarg;
            a[1] = NULL;
            set(a);
         }
joarg:
         if (oargs_count == oargs_size)
            oargs_size = _grow_cpp(&oargs, oargs_size, oargs_count);
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
         puts(version);
         exit(0);
         /* NOTREACHED */
      case 'v':
         /* Be verbose */
         okey = "verbose";
#ifdef HAVE_DEBUG
         assign(okey, "");
#endif
         goto joarg;
      case '~':
         /* Enable tilde escapes even in non-interactive mode */
         options |= OPT_TILDE_FLAG;
         break;
      case '#':
         /* Work in batch mode, even if non-interactive */
         if (oargs_count + 3 >= oargs_size)
            oargs_size = _grow_cpp(&oargs, oargs_size, oargs_count);
         oargs[oargs_count++] = "dot";
         oargs[oargs_count++] = "emptystart";
         oargs[oargs_count++] = "noheader";
         oargs[oargs_count++] = "sendwait";
         options |= OPT_TILDE_FLAG | OPT_BATCH_FLAG;
         break;
      case '?':
jusage:
         fprintf(stderr, tr(135, usagestr), progname, progname, progname);
         exit(2);
      }
   }

   if (folder != NULL) {
      if (_oind < argc) {
         if (_oind + 1 < argc) {
            fprintf(stderr, tr(205, "More than one file given with -f\n"));
            goto jusage;
         }
         folder = argv[_oind];
      }
   } else {
      for (i = _oind; argv[i]; ++i)
         to = cat(to, checkaddrs(lextract(argv[i], GTO | GFULL)));
      if (to != NULL)
         options |= OPT_SENDMODE;
   }

   /* Check for inconsistent arguments */
   if (folder != NULL && to != NULL) {
      fprintf(stderr, tr(137, "Cannot give -f and people to send to.\n"));
      goto jusage;
   }
   if ((options & (OPT_SENDMODE | OPT_t_FLAG)) == OPT_SENDMODE && to == NULL) {
      fprintf(stderr, tr(138,
         "Send options without primary recipient specified.\n"));
      goto jusage;
   }
   if ((options & OPT_R_FLAG) && to != NULL) {
      fprintf(stderr, tr(235, "The -R option is meaningless in send mode.\n"));
      goto jusage;
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

   if (!(options & OPT_NOSRC) && getenv("NAIL_NO_SYSTEM_RC") == NULL)
      load(SYSCONFRC);
   /* *expand() returns a savestr(), but load only uses the file name for
    * fopen(), so it's safe to do this */
   if ((cp = getenv("MAILRC")) != NULL)
      load(file_expand(cp));
   else if ((cp = getenv("NAILRC")) != NULL)
      load(file_expand(cp));
   else
      load(file_expand(MAILRC));
   if (getenv("NAIL_EXTRA_RC") == NULL && (cp = value("NAIL_EXTRA_RC")) != NULL)
      load(file_expand(cp));

   /* Now we can set the account */
   if (Aflag != NULL) {
      char *a[2];
      a[0] = Aflag;
      a[1] = NULL;
      c_account(a);
   }

   /* Ensure the -S and other command line options take precedence over
    * anything that may have been placed in resource files */
   for (i = 0; i < oargs_count; ++i) {
      char const *a[2];
      a[0] = oargs[i];
      a[1] = NULL;
      set(a);
   }

   /*
    * We're finally completely setup and ready to go
    */

   starting =
   unset_allow_undefined = FAL0;

   if (options & OPT_DEBUG)
      fprintf(stderr, tr(199, "user = %s, homedir = %s\n"), myname, homedir);

   if (!(options & OPT_SENDMODE)) {
      exit_status = _rcv_mode(folder);
      goto jleave;
   }

   /* Now that full mailx(1)-style file expansion is possible handle the
    * attachments which we had delayed due to this.
    * This may use savestr(), but since we won't enter the command loop we
    * don't need to care about that */
   while (a_head != NULL) {
      attach = add_attachment(attach, a_head->aa_file, NULL);
      if (attach != NULL) {
         a_curr = a_head;
         a_head = a_head->aa_next;
         ac_free(a_curr);
      } else {
         perror(a_head->aa_file);
         exit(1);
      }
   }

   /* xxx exit_status = EXIT_OK; */
   if (options & OPT_INTERACTIVE)
      tty_init();
   mail(to, cc, bcc, subject, attach, qf, ((options & OPT_F_FLAG) != 0));
   if (options & OPT_INTERACTIVE)
      tty_destroy();
jleave:
   return exit_status;
}

/* vim:set fenc=utf-8:s-it-mode */
