/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Privilege-separated dot file lock program (WANT_PRIVSEP=yes)
 *@ that is capable of calling setgid(2) and change its group identity
 *@ to the configured PRIVSEP_GROUP (usually "mail").
 *@ It should be started when chdir(2)d to the lock file's directory,
 *@ and SIGPIPE should be ignored.
 *
 * Copyright (c) 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#define n_FILE privsep

#include "nail.h"

#include "dotlock.h"

static void _ign_signal(int signum);

static void
_ign_signal(int signum)
{
   struct sigaction nact, oact;

   nact.sa_handler = SIG_IGN;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = 0;
   sigaction(signum, &nact, &oact);
}

int
main(int argc, char **argv)
{
   struct stat stb;
   sigset_t nset, oset;
   char const *name, *hostname, *randstr;
   size_t pollmsecs;
   enum dotlock_state dls;
   bool_t anyid;

   /* We're a dumb helper, ensure as much as we can noone else uses us */
   if (argc != 10 ||
         strcmp(argv[0], PRIVSEP) ||
         strcmp(argv[1], "dotlock") ||
         strcmp(argv[2], "name") ||
         strcmp(argv[4], "hostname") ||
         strcmp(argv[6], "randstr") ||
         strcmp(argv[8], "pollmsecs") ||
         fstat(0, &stb) == -1 || !S_ISFIFO(stb.st_mode) ||
         fstat(1, &stb) == -1 || !S_ISFIFO(stb.st_mode)) {
      fprintf(stderr,
         "This is a helper program of \"" UAGENT "\" (in " BINDIR ").\n"
         "  It is capable of gaining more privileges than \"" UAGENT "\"\n"
         "  and will be used to create lock files.\n"
         "  It's sole purpose is outsourcing of high privileges into\n"
         "  fewest lines of code in order to reduce attack surface.\n"
         "  It cannot be run by itself.\n");
      exit(EXIT_USE);
   }

   close(STDERR_FILENO);

   name = argv[3];
   hostname = argv[5];
   randstr = argv[7];
   pollmsecs = (size_t)strtoul(argv[9], NULL, 10);

   /* Try to change our identity.
    * Don't bail if UID==EUID or setuid() fails, but simply continue,
    * don't bail if GID==EGID or setgid() fails, but simply continue */
   anyid = FAL0;

#if 0 /* TODO PRIVSEP_USER disabled, won't setuid(2) for now, see make.rc! */
   if (PRIVSEP_USER[0] != '\0') {
      uid_t uid = getuid(), euid = geteuid();

      if (uid != euid) {
         if (setuid(euid)) {
            dls = DLS_PRIVFAILED;
            if (UICMP(z, write(STDOUT_FILENO, &dls, sizeof dls), !=,
                  sizeof dls))
               goto jerr;
         }
         anyid = TRU1;
      }
   }
#endif

   if (PRIVSEP_GROUP[0] != '\0') {
      gid_t gid = getgid(), egid = getegid();

      if (gid != egid) {
         if (setgid(egid)) {
            dls = DLS_PRIVFAILED;
            if (UICMP(z, write(STDOUT_FILENO, &dls, sizeof dls), !=,
                  sizeof dls))
               goto jerr;
         }
         anyid = TRU1;
      }
   }

   if (!anyid) {
      dls = DLS_PRIVFAILED;
      if (UICMP(z, write(STDOUT_FILENO, &dls, sizeof dls), !=, sizeof dls))
         goto jerr;
   }

   /* In order to prevent stale lock files at all cost block any signals until
    * we have unlinked the lock file.
    * It is still not safe because we may be SIGKILLed and may linger around
    * because we have been SIGSTOPped, but unfortunately the standard doesn't
    * give any option a.k.a. atcrash() --- and then again we should not
    * unlink(2) the lock file unless our parent has finalized the
    * synchronization!  While at it, let me rant about the default action of
    * realtime signals is to terminate the program */
   _ign_signal(SIGPIPE); /* (Inherited, though) */
   sigfillset(&nset);
   sigdelset(&nset, SIGCONT); /* (Rather redundant, though) */
   sigprocmask(SIG_BLOCK, &nset, &oset);

   dls = DLS_NOPERM;
   dls = _dotlock_create(name, hostname, randstr, pollmsecs);

   /* Finally: notify our parent about the actual lock state.. */
   write(STDOUT_FILENO, &dls, sizeof dls);
   close(STDOUT_FILENO);

   /* ..then eventually wait until we shall remove the lock again, which will
    * be notified via the read returning */
   if (dls == DLS_NONE) {
      read(STDIN_FILENO, &dls, sizeof dls);

      unlink(name);
   }

   sigprocmask(SIG_SETMASK, &oset, NULL);
   return EXIT_OK;
jerr:
   return EXIT_ERR;
}

/* s-it-mode */
