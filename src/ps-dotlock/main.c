/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Privilege-separated dot file lock program (OPT_DOTLOCK=yes)
 *@ that is capable of calling setuid(2), and change its user identity
 *@ to the VAL_PS_DOTLOCK_USER (usually "root") in order to create a
 *@ dotlock file with the same UID/GID as the mailbox to be locked.
 *@ It should be started when chdir(2)d to the lock file's directory,
 *@ with a symlink-resolved target and with SIGPIPE being ignored.
 *
 * Copyright (c) 2015 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE ps_dotlock_main
#define mx_SOURCE
#define mx_SOURCE_PS_DOTLOCK_MAIN

#define su_ASSERT_EXPAND_NOTHING

#include "mx/nail.h"

#include <errno.h>
#include <string.h>

#if defined mx_HAVE_PRCTL_DUMPABLE
# include <sys/prctl.h>
#elif defined mx_HAVE_PTRACE_DENY
# include <sys/ptrace.h>
#elif defined mx_HAVE_SETPFLAGS_PROTECT
# include <priv.h>
#endif

/* TODO fake */
#include "su/code-in.h"

/* TODO Avoid linkage errors, instantiate what is needed;
 * TODO SU needs to be available as a library to overcome this,
 * TODO or a compiler capable of inlining can only be used */
uz su__state;
#ifdef su_MEM_ALLOC_DEBUG
boole su__mem_check(su_DBG_LOC_ARGS_DECL_SOLE) {return FAL0;}
boole su__mem_trace(su_DBG_LOC_ARGS_DECL_SOLE) {return FAL0;}
#endif
#define su_err_no() errno
#define su_err_set_no(X) (errno = X)

static void _ign_signal(int signum);
static uz n_msleep(uz millis, boole ignint);

#include "mx/dotlock.h" /* $(PS_DOTLOCK_SRCDIR) */

static void
_ign_signal(int signum){
   struct sigaction nact, oact;

   nact.sa_handler = SIG_IGN;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = 0;
   sigaction(signum, &nact, &oact);
}

static uz
n_msleep(uz millis, boole ignint){
   uz rv;

#ifdef mx_HAVE_NANOSLEEP
   /* C99 */{
      struct timespec ts, trem;
      int i;

      ts.tv_sec = millis / 1000;
      ts.tv_nsec = (millis %= 1000) * 1000 * 1000;

      while((i = nanosleep(&ts, &trem)) != 0 && ignint)
         ts = trem;
      rv = (i == 0) ? 0
            : (trem.tv_sec * 1000) + (trem.tv_nsec / (1000 * 1000));
   }

#elif defined mx_HAVE_SLEEP
   if((millis /= 1000) == 0)
      millis = 1;
   while((rv = sleep(S(ui,millis))) != 0 && ignint)
      millis = rv;
#else
# error Configuration should have detected a function for sleeping.
#endif
   return rv;
}

int
main(int argc, char **argv){
   char hostbuf[64];
   struct n_dotlock_info di;
   struct stat stb;
   sigset_t nset, oset;
   enum n_dotlock_state dls;

   /* We're a dumb helper, ensure as much as we can noone else uses us */
   if(argc != 12 ||
         strcmp(argv[ 0], VAL_PS_DOTLOCK) ||
         (argv[1][0] != 'r' && argv[1][0] != 'w') ||
         strcmp(argv[ 1] + 1, "dotlock") ||
         strcmp(argv[ 2], "mailbox") ||
         strchr(argv[ 3], '/') != NULL /* Seal path injection.. */ ||
         strcmp(argv[ 4], "name") ||
         strcmp(argv[ 6], "hostname") ||
         strcmp(argv[ 8], "randstr") ||
         strchr(argv[ 9], '/') != NULL /* ..attack vector */ ||
         strcmp(argv[10], "pollmsecs") ||
         fstat(STDIN_FILENO, &stb) == -1 || !S_ISFIFO(stb.st_mode) ||
         fstat(STDOUT_FILENO, &stb) == -1 || !S_ISFIFO(stb.st_mode)){
jeuse:
      fprintf(stderr,
         "This is a helper program of " VAL_UAGENT " (in " VAL_BINDIR ").\n"
         "  It is capable of gaining more privileges than " VAL_UAGENT "\n"
         "  and will be used to create lock files.\n"
         "  The sole purpose is outsourcing of high privileges into\n"
         "  fewest lines of code in order to reduce attack surface.\n"
         "  This program cannot be run by itself.\n");
      exit(n_EXIT_USE);
   }else{
      /* Prevent one more path injection attack vector, but be friendly */
      char const *ccp;
      size_t i;
      char *cp, c;

      for(ccp = argv[7], cp = hostbuf, i = 0; (c = *ccp) != '\0'; ++cp, ++ccp){
         *cp = (c == '/' ? '_' : c);
         if(++i == sizeof(hostbuf) -1)
            break;
      }
      *cp = '\0';
      if(cp == hostbuf)
         goto jeuse;
      argv[7] = hostbuf;
   }

   di.di_file_name = argv[3];
   di.di_lock_name = argv[5];
   di.di_hostname = argv[7];
   di.di_randstr = argv[9];
   di.di_pollmsecs = (size_t)strtoul(argv[11], NULL, 10);

   /* Ensure the lock name and the file name are identical */
   /* C99 */{
      size_t i;

      i = strlen(di.di_file_name);
      if(i == 0 || strncmp(di.di_file_name, di.di_lock_name, i) ||
            di.di_lock_name[i] == '\0' || strcmp(di.di_lock_name + i, ".lock"))
         goto jeuse;
   }

   /* Ensure that we got some random string, and some hostname.
    * a_dotlock_create() will later ensure that it will produce some string
    * not-equal to .di_lock_name if it is called by us */
   if(di.di_hostname[0] == '\0' || di.di_randstr[0] == '\0')
      goto jeuse;

   close(STDERR_FILENO);

   /* In order to prevent stale lock files at all cost block any signals until
    * we have unlinked the lock file.
    * It is still not safe because we may be SIGKILLed and may linger around
    * because we have been SIGSTOPped, but unfortunately the standard doesn't
    * give any option, e.g. atcrash() or open(O_TEMPORARY_KEEP_NAME) or so, ---
    * and then again we should not unlink(2) the lock file unless our parent
    * has finalized the synchronization!  While at it, let me rant about the
    * default action of realtime signals, program termination */
   _ign_signal(SIGPIPE); /* (Inherited, though) */
   sigfillset(&nset);
   sigdelset(&nset, SIGCONT); /* (Rather redundant, though) */
   sigprocmask(SIG_BLOCK, &nset, &oset);

   dls = n_DLS_NOPERM | n_DLS_ABANDON;

   /* First of all: we only dotlock when the executing user has the necessary
    * rights to access the mailbox */
   if(access(di.di_file_name, (argv[1][0] == 'r' ? R_OK : R_OK | W_OK)))
      goto jmsg;

   /* We need UID and GID information about the mailbox to lock */
   if(stat(di.di_file_name, di.di_stb = &stb) == -1)
      goto jmsg;

   dls = n_DLS_PRIVFAILED | n_DLS_ABANDON;

   /* We are SETUID and do not want to become traced or being attached to */
#if defined mx_HAVE_PRCTL_DUMPABLE
   if(prctl(PR_SET_DUMPABLE, 0))
      goto jmsg;
#elif defined mx_HAVE_PTRACE_DENY
   if(ptrace(PT_DENY_ATTACH, 0, 0, 0) == -1)
      goto jmsg;
#elif defined mx_HAVE_SETPFLAGS_PROTECT
   if(setpflags(__PROC_PROTECT, 1))
      goto jmsg;
#endif

   /* This helper is only executed when really needed, it thus doesn't make
    * sense to try to continue with initial privileges */
   if(setuid(geteuid()))
      goto jmsg;

   dls = a_dotlock_create(&di);

   /* Finally: notify our parent about the actual lock state.. */
jmsg:
   write(STDOUT_FILENO, &dls, sizeof dls);
   close(STDOUT_FILENO);

   /* ..then eventually wait until we shall remove the lock again, which will
    * be notified via the read returning */
   if(dls == n_DLS_NONE){
      read(STDIN_FILENO, &dls, sizeof dls);

      unlink(di.di_lock_name);
   }

   sigprocmask(SIG_SETMASK, &oset, NULL);
   return (dls == n_DLS_NONE ? n_EXIT_OK : n_EXIT_ERR);
}

#include "su/code-ou.h"
/* s-it-mode */
