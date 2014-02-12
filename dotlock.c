/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Mailbox file locking.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/utsname.h>

#include <fcntl.h>

#define APID_SZ        40  /* sufficient for 128 bits pids XXX nail.h */
#define CREATE_RETRIES  5  /* XXX nail.h */
#define DOTLOCK_RETRIES 15 /* XXX nail.h */

#ifdef O_SYNC
# define O_BITS         (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL | O_SYNC)
#else
# define O_BITS         (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL)
#endif

/* TODO Allow safe setgid, optional: check on startup wether in receive mode,
 * TODO start helper process that is setgid and only does dotlocking.
 * TODO Approach two, also optional: use a configurable setgid dotlock prog */
#define GID_MAYBESET(P) \
do if (realgid != effectivegid && !_maybe_setgid(P, effectivegid)) {\
   perror("setgid");\
   exit(1);\
} while (0)
#define GID_RESET() \
do if (realgid != effectivegid && setgid(realgid) == -1) {\
   perror("setgid");\
   exit(1);\
} while (0)

/* GID_*() helper: set the gid if the path is in the normal mail spool */
static bool_t  _maybe_setgid(char const *name, gid_t gid);

/* Check if we can write a lock file at all */
static int     maildir_access(char const *fname);

/* Create a unique file. O_EXCL does not really work over NFS so we follow
 * the following trick (inspired by  S.R. van den Berg):
 * - make a mostly unique filename and try to create it
 * - link the unique filename to our target
 * - get the link count of the target
 * - unlink the mostly unique filename
 * - if the link count was 2, then we are ok; else we've failed */
static int     create_exclusive(char const *fname);

static bool_t
_maybe_setgid(char const *name, gid_t gid)
{
   char const safepath[] = MAILSPOOL;
   bool_t rv;
   NYD_ENTER;

   if (strncmp(name, safepath, sizeof(safepath) - 1) ||
         strchr(name + sizeof(safepath), '/') != NULL)
      rv = TRU1;
   else
      rv = (setgid(gid) != -1);
   NYD_LEAVE;
   return rv;
}

static int
maildir_access(char const *fname)
{
   char *path, *p;
   int i;
   NYD_ENTER;

   i = (int)strlen(fname);
   path = ac_alloc(i + 2);
   memcpy(path, fname, i + 1);
   p = strrchr(path, '/');
   if (p != NULL)
      *p = '\0';
   if (p == NULL || *path == '\0') {
      path[0] = '.';
      path[1] = '\0';
   }
   i = access(path, R_OK | W_OK | X_OK);
   ac_free(path);
   NYD_LEAVE;
   return i;
}

static int
create_exclusive(char const *fname) /* TODO revisit! */
{
   char path[PATH_MAX], apid[APID_SZ], *hostname;
   struct stat st;
   struct utsname ut;
   char const *ptr;
   time_t t;
   size_t ntries;
   int pid, fd, serrno, cc;
   NYD_ENTER;

   time(&t);
   uname(&ut);
   hostname = ut.nodename;
   pid = (int)getpid();

   /* We generate a semi-unique filename, from hostname.(pid ^ usec) */
   if ((ptr = strrchr(fname, '/')) == NULL)
      ptr = fname;
   else
      ++ptr;

   snprintf(path, sizeof path, "%.*s.%s.%x",
       (int)PTR2SIZE(ptr - fname), fname, hostname, (pid ^ (int)t));

   /* We try to create the unique filename */
   for (ntries = 0; ntries < CREATE_RETRIES; ++ntries) {
      GID_MAYBESET(path);
      fd = open(path, O_BITS, 0);
      serrno = errno;
      GID_RESET();
      if (fd != -1) {
         snprintf(apid, APID_SZ, "%d", pid);
         write(fd, apid, strlen(apid));
         close(fd);
         break;
      } else if (serrno != EEXIST) {
         serrno = -1;
         goto jleave;
      }
   }

   /* We link the path to the name */
   GID_MAYBESET(fname);
   cc = link(path, fname);
   serrno = errno;
   GID_RESET();
   if (cc == -1)
      goto jbad;

   /* Note that we stat our own exclusively created name, not the
    * destination, since the destination can be affected by others */
   if (stat(path, &st) == -1) {
      serrno = errno;
      goto jbad;
   }

   GID_MAYBESET(fname);
   unlink(path);
   GID_RESET();

   /* If the number of links was two (one for the unique file and one for
    * the lock), we've won the race */
   if (st.st_nlink != 2) {
      errno = EEXIST;
      serrno = -1;
      goto jleave;
   }
   serrno = 0;
jleave:
   NYD_LEAVE;
   return serrno;
jbad:
   unlink(path);
   errno = serrno;
   serrno = -1;
   goto jleave;
}

FL int
fcntl_lock(int fd, int ltype) /* TODO check callees for EINTR etc.!!! */
{
   struct flock flp;
   int rv;
   NYD_ENTER;

   flp.l_type = ltype;
   flp.l_start = 0;
   flp.l_whence = SEEK_SET;
   flp.l_len = 0;
   rv = fcntl(fd, F_SETLKW, &flp);
   NYD_LEAVE;
   return rv;
}

FL int
dot_lock(char const *fname, int fd, int pollival, FILE *fp, char const *msg)
{
   char path[PATH_MAX];
   sigset_t nset, oset;
   int i, olderrno, rv;
   NYD_ENTER;

   rv = 0;
   if (maildir_access(fname) != 0)
      goto jleave;
   rv = -1;

   sigemptyset(&nset);
   sigaddset(&nset, SIGHUP);
   sigaddset(&nset, SIGINT);
   sigaddset(&nset, SIGQUIT);
   sigaddset(&nset, SIGTERM);
   sigaddset(&nset, SIGTTIN);
   sigaddset(&nset, SIGTTOU);
   sigaddset(&nset, SIGTSTP);
   sigaddset(&nset, SIGCHLD);

   snprintf(path, sizeof(path), "%s.lock", fname);

   for (i = 0; i < DOTLOCK_RETRIES; ++i) {
      sigprocmask(SIG_BLOCK, &nset, &oset);
      rv = create_exclusive(path);
      olderrno = errno;
      sigprocmask(SIG_SETMASK, &oset, NULL);
      if (!rv)
         goto jleave;
      assert(rv == -1);

      fcntl_lock(fd, F_UNLCK);
      if (olderrno != EEXIST)
         goto jleave;

      if (fp != NULL && msg != NULL)
          fputs(msg, fp);

      if (pollival) {
         if (pollival == -1) {
            errno = EEXIST;
            goto jleave;
         }
         sleep(pollival);
      }
      fcntl_lock(fd, F_WRLCK);
   }
   fprintf(stderr, tr(71,
      "%s seems a stale lock? Need to be removed by hand?\n"), path);
jleave:
   NYD_LEAVE;
   return rv;
}

FL void
dot_unlock(char const *fname)
{
   char path[PATH_MAX];
   NYD_ENTER;

   if (maildir_access(fname) != 0)
      goto jleave;

   snprintf(path, sizeof(path), "%s.lock", fname);
   GID_MAYBESET(path);
   unlink(path);
   GID_RESET();
jleave:
   NYD_LEAVE;
}

/* vim:set fenc=utf-8:s-it-mode */
