/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Creation of an exclusive "dotlock" file.  This is (potentially) shared
 *@ in between dotlock() and the privilege-separated "dotlocker"..
 *@ The code assumes it has been chdir(2)d into the target directory and
 *@ that SIGPIPE is ignored (we react upon EPIPE).
 *
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Actual logic: fname is the XY.lock file, hostname and randstr have been
 * created in the parent (to avoid useless page-ins etc.).
 * Callee has to ensure strlen(fname)+1+1 fits in NAME_MAX */
static enum dotlock_state  _dotlock_create(char const *fname,
                              char const *hostname, char const *randstr,
                              size_t pollmsecs);

/* Create a unique file. O_EXCL does not really work over NFS so we follow
 * the following trick (inspired by S.R. van den Berg):
 * - make a mostly unique filename and try to create it
 * - link the unique filename to our target
 * - get the link count of the target
 * - unlink the mostly unique filename
 * - if the link count was 2, then we are ok; else we've failed */
static enum dotlock_state  __dotlock_create_excl(char const *fname,
                              char const *lname);

static enum dotlock_state
_dotlock_create(char const *fname, char const *hostname, char const *randstr,
   size_t pollmsecs)
{
   char lname[NAME_MAX];
   sigset_t nset, oset;
   size_t tries;
   ssize_t w;
   enum dotlock_state rv, xrv;

   /* See _create_excl() */
   snprintf(lname, sizeof lname, "%s.%s.%s", fname, hostname, randstr);

   sigemptyset(&nset);
   sigaddset(&nset, SIGHUP);
   sigaddset(&nset, SIGINT);
   sigaddset(&nset, SIGQUIT);
   sigaddset(&nset, SIGTERM);
   sigaddset(&nset, SIGTTIN);
   sigaddset(&nset, SIGTTOU);
   sigaddset(&nset, SIGTSTP);
   sigaddset(&nset, SIGCHLD);

   for (tries = 0;; ++tries) {
      sigprocmask(SIG_BLOCK, &nset, &oset);
      rv = __dotlock_create_excl(fname, lname);
      sigprocmask(SIG_SETMASK, &oset, NULL);

      if (rv == DLS_NONE || (rv & DLS_ABANDON))
         break;
      if (pollmsecs == 0 || tries >= DOTLOCK_TRIES) {
         rv |= DLS_ABANDON;
         break;
      }

      xrv = DLS_PING;
      w = write(STDOUT_FILENO, &xrv, sizeof xrv);
      if (w == -1 && errno == EPIPE) {
         rv = DLS_DUNNO | DLS_ABANDON;
         break;
      }
      sleep(1); /* TODO pollmsecs -> use finer grain */
   }
   return rv;
}

static enum dotlock_state
__dotlock_create_excl(char const *fname, char const *lname)
{
   struct stat stb;
   int fd;
   size_t tries;
   enum dotlock_state rv = DLS_NONE;

   /* We try to create the unique filename */
   for (tries = 0;; ++tries) {
      fd = open(lname,
#ifdef O_SYNC
               (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL | O_SYNC),
#else
               (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL),
#endif
            0);
      if (fd != -1) {
         close(fd);
         break;
      } else if (errno != EEXIST) {
         rv = DLS_NOPERM | DLS_ABANDON;
         goto jleave;
      } else if (tries >= DOTLOCK_TRIES) {
         rv = DLS_EXIST;
         goto jleave;
      }
   }

   /* We link the name to the fname */
   if (link(lname, fname) == -1)
      goto jbados;

   /* Note that we stat our own exclusively created name, not the
    * destination, since the destination can be affected by others */
   if (stat(lname, &stb) == -1)
      goto jbados;

   unlink(lname);

   /* If the number of links was two (one for the unique file and one for
    * the lock), we've won the race */
   if (stb.st_nlink != 2)
      rv = DLS_EXIST;

jleave:
   return rv;
jbados:
   rv = (errno == EEXIST) ? DLS_EXIST : DLS_NOPERM | DLS_ABANDON;
   unlink(lname);
   goto jleave;
}

/* s-it-mode */
