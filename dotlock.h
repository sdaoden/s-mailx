/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Creation of an exclusive "dotlock" file.
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

#include <sys/utsname.h>

/* Create a unique file. O_EXCL does not really work over NFS so we follow
 * the following trick (inspired by  S.R. van den Berg):
 * - make a mostly unique filename and try to create it
 * - link the unique filename to our target
 * - get the link count of the target
 * - unlink the mostly unique filename
 * - if the link count was 2, then we are ok; else we've failed */
static int  _dotlock_create_excl(char const *fname);

static int
_dotlock_create_excl(char const *fname) /* TODO revisit! */
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
   for (ntries = 0; ++ntries <= DOTLOCK_TRIES;) {
      fd = open(path,
#ifdef O_SYNC
               (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL | O_SYNC),
#else
               (O_WRONLY | O_CREAT | O_TRUNC | O_EXCL),
#endif
            0);
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

/* s-it-mode */
