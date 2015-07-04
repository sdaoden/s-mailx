/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Privilege-separated dot file lock program (WANT_PRIVSEP=yes)
 *@ that is capable of calling setgid(2) and change its group identity
 *@ to the configured PRIVSEP_GROUP (usually "mail").
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
#ifdef SA_RESTART
   nact.sa_flags |= SA_RESTART;
#endif
   sigaction(signum, &nact, &oact);
}

int
main(int argc, char **argv)
{
   struct stat stb;
   char const *name, *hostname, *randstr;
   size_t pollmsecs;
   enum dotlock_state dls;
   gid_t gid, egid;

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

   name = argv[3];
   hostname = argv[5];
   randstr = argv[7];
   pollmsecs = (size_t)strtoul(argv[9], NULL, 10);

   /* Try to change our group identity; to catch faulty installations etc.
    * don't baild if GID==EGID or setgid() fails, but simply continue */
   gid = getgid();
   egid = getegid();
   if (gid == egid || setgid(egid)) {
      dls = DLS_PRIVFAILED;
      if (UICMP(z, write(STDOUT_FILENO, &dls, sizeof dls), !=, sizeof dls))
         goto jerr;
   }

   _ign_signal(SIGHUP);
   _ign_signal(SIGINT);
   _ign_signal(SIGPIPE); /* (Inherited, though) */
   _ign_signal(SIGQUIT);
   _ign_signal(SIGTERM);

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
   return EXIT_OK;
jerr:
   return EXIT_ERR;
}

/* s-it-mode */
