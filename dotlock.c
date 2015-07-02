/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Mailbox file locking.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
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
#undef n_FILE
#define n_FILE dotlock

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/utsname.h>

#define CREATE_RETRIES  5  /* XXX nail.h */
#define DOTLOCK_RETRIES 15 /* XXX nail.h */

#ifdef O_SYNC
# define O_BITS         (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL | O_SYNC)
#else
# define O_BITS         (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL)
#endif

/* Check if we can write a lock file at all */
static bool_t  _dot_dir_access(char const *fname);

/* Create a unique file. O_EXCL does not really work over NFS so we follow
 * the following trick (inspired by  S.R. van den Berg):
 * - make a mostly unique filename and try to create it
 * - link the unique filename to our target
 * - get the link count of the target
 * - unlink the mostly unique filename
 * - if the link count was 2, then we are ok; else we've failed */
static int     create_exclusive(char const *fname);

/* fcntl(2) plus error handling */
static bool_t  _dot_fcntl_lock(int fd, enum flock_type ft);

/* Print a message :) */
static void    _dot_lock_msg(char const *fname);

static bool_t
_dot_dir_access(char const *fname)
{
   size_t i;
   char *path, *p;
   bool_t rv;
   NYD_ENTER;

   i = strlen(fname);
   path = ac_alloc(i + 1 +1);

   memcpy(path, fname, i +1);
   p = strrchr(path, '/');
   if (p != NULL)
      *p = '\0';
   if (p == NULL || *path == '\0') {
      path[0] = '.';
      path[1] = '\0';
   }

   if ((rv = is_dir(path))) {
      for (;;)
         if (!access(path, R_OK | W_OK | X_OK))
            break;
         else if (errno != EINTR) {
            rv = FAL0;
            break;
         }
   }

   ac_free(path);
   NYD_LEAVE;
   return rv;
}

static int
create_exclusive(char const *fname) /* TODO revisit! */
{
   char path[PATH_MAX], *hostname;
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
      fd = open(path, O_BITS, 0);
      serrno = errno;
      if (fd != -1) {
         close(fd);
         break;
      } else if (serrno != EEXIST) {
         serrno = -1;
         goto jleave;
      }
   }

   /* We link the path to the name */
   cc = link(path, fname);
   serrno = errno;
   if (cc == -1)
      goto jbad;

   /* Note that we stat our own exclusively created name, not the
    * destination, since the destination can be affected by others */
   if (stat(path, &st) == -1) {
      serrno = errno;
      goto jbad;
   }

   unlink(path);

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

static bool_t
_dot_fcntl_lock(int fd, enum flock_type ft)
{
   struct flock flp;
   bool_t rv;
   NYD2_ENTER;

   switch (ft) {
   case FLOCK_READ:     rv = F_RDLCK;  break;
   case FLOCK_WRITE:    rv = F_WRLCK;  break;
   default:
   case FLOCK_UNLOCK:   rv = F_UNLCK;  break;
   }

   /* (For now we restart, but in the future we may not */
   flp.l_type = rv;
   flp.l_start = 0;
   flp.l_whence = SEEK_SET;
   flp.l_len = 0;
   while (!(rv = (fcntl(fd, F_SETLKW, &flp) != -1)) && errno == EINTR)
      ;
   NYD2_LEAVE;
   return rv;
}

static void
_dot_lock_msg(char const *fname)
{
   NYD2_ENTER;
   fprintf(stdout, _("Creating dot lock for \"%s\""), fname);
   NYD2_LEAVE;
}

FL bool_t
fcntl_lock(int fd, enum flock_type ft, size_t pollmsecs)
{
   size_t retries;
   bool_t rv;
   NYD_ENTER;

   for (rv = FAL0, retries = 0; retries < DOTLOCK_RETRIES; ++retries)
      if ((rv = _dot_fcntl_lock(fd, ft)) || pollmsecs == 0)
         break;
      else
         sleep(1); /* TODO pollmsecs -> use finer grain */
   NYD_LEAVE;
   return rv;
}

FL bool_t
dot_lock(char const *fname, int fd, size_t pollmsecs)
{
   char path[PATH_MAX];
   sigset_t nset, oset;
   int olderrno;
   size_t retries = 0;
   bool_t didmsg = FAL0, rv = FAL0;
   NYD_ENTER;

   if (options & OPT_D_VV) {
      _dot_lock_msg(fname);
      putchar('\n');
      didmsg = TRU1;
   }

   while (!_dot_fcntl_lock(fd, FLOCK_WRITE))
      switch (errno) {
      case EACCES:
      case EAGAIN:
      case ENOLCK:
         if (pollmsecs > 0 && ++retries < DOTLOCK_RETRIES) {
            if (!didmsg)
               _dot_lock_msg(fname);
            putchar('.');
            didmsg = -TRU1;
            sleep(1); /* TODO pollmsecs -> use finer grain */
            continue;
         }
         /* FALLTHRU */
      default:
         goto jleave;
      }

   /* If we can't deal with dot-lock files in there, go with the FLOCK lock and
    * don't fail otherwise */
   if (!_dot_dir_access(fname)) {
      if (options & OPT_D_V) /* TODO Really? dotlock's are crucial! Always!?! */
         n_err(_("Can't manage lock files in \"%s\", "
            "please check permissions\n"), fname);
      rv = TRU1;
      goto jleave;
   }

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

   while (retries++ < DOTLOCK_RETRIES) {
      sigprocmask(SIG_BLOCK, &nset, &oset);
      rv = (create_exclusive(path) == 0);
      olderrno = errno;
      sigprocmask(SIG_SETMASK, &oset, NULL);
      if (rv)
         goto jleave;

      while (!_dot_fcntl_lock(fd, FLOCK_UNLOCK))
         if (pollmsecs == 0 || retries++ >= DOTLOCK_RETRIES)
            goto jleave;
         else {
            if (!didmsg)
               _dot_lock_msg(fname);
            putchar('.');
            didmsg = -TRU1;
            sleep(1); /* TODO pollmsecs -> use finer grain */
         }

      if (olderrno != EEXIST)
         goto jleave;
      if (pollmsecs == 0) {
         errno = EEXIST;
         goto jleave;
      }

      while (!_dot_fcntl_lock(fd, FLOCK_WRITE))
         if (pollmsecs == 0 || retries++ >= DOTLOCK_RETRIES)
            goto jleave;
         else {
            if (!didmsg)
               _dot_lock_msg(fname);
            putchar('.');
            didmsg = -TRU1;
            sleep(1); /* TODO pollmsecs -> use finer grain */
         }
   }

   n_err(_("Is \"%s\" a stale lock?  Please remove file manually\n"), path);
jleave:
   if (didmsg < FAL0)
      putchar('\n');
   NYD_LEAVE;
   return rv;
}

FL void
dot_unlock(char const *fname)
{
   char path[PATH_MAX];
   NYD_ENTER;

   if (!_dot_dir_access(fname))
      goto jleave;

   snprintf(path, sizeof(path), "%s.lock", fname);
   unlink(path);
jleave:
   NYD_LEAVE;
}

/* s-it-mode */
