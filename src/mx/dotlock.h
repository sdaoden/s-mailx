/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Creation of an exclusive "dotlock" file.  This is (potentially) shared
 *@ in between n_dotlock() and the privilege-separated "dotlocker"..
 *@ (Which is why it doesn't use NYD or other utilities.)
 *@ The code assumes it has been chdir(2)d into the target directory and
 *@ that SIGPIPE is ignored (we react upon ERR_PIPE).
 *@ It furtherly assumes that it can create a file name that is at least one
 *@ byte longer than the dotlock file's name!
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-2-Clause
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

/* Jump in */
static enum n_dotlock_state a_dotlock_create(struct n_dotlock_info *dip);

/* Create a unique file. O_EXCL does not really work over NFS so we follow
 * the following trick (inspired by S.R. van den Berg):
 * - make a mostly unique filename and try to create it
 * - link the unique filename to our target
 * - get the link count of the target
 * - unlink the mostly unique filename
 * - if the link count was 2, then we are ok; else we've failed */
static enum n_dotlock_state a_dotlock__create_excl(struct n_dotlock_info *dip,
                              char const *lname);

static enum n_dotlock_state
a_dotlock_create(struct n_dotlock_info *dip){
   /* Use PATH_MAX not NAME_MAX to catch those "we proclaim the minimum value"
    * problems (SunOS), since the pathconf(3) value came too late! */
   char lname[PATH_MAX +1];
   sigset_t nset, oset;
   uz tries;
   sz w;
   enum n_dotlock_state rv, xrv;

   /* The callee ensured this does not end up as plain "di_lock_name".
    * However, when called via dotlock-ps, this may not be true in malicious
    * cases, so add another check, then */
   snprintf(lname, sizeof lname, "%s%s%s",
      dip->di_lock_name, dip->di_randstr, dip->di_hostname);
#ifdef mx_SOURCE_DOTLOCK_PS
   if(!strcmp(lname, dip->di_lock_name)){
      rv = n_DLS_FISHY | n_DLS_ABANDON;
      goto jleave;
   }
#endif

   sigfillset(&nset);

   for(tries = 0;; ++tries){
      sigprocmask(SIG_BLOCK, &nset, &oset);
      rv = a_dotlock__create_excl(dip, lname);
      sigprocmask(SIG_SETMASK, &oset, NULL);

      if(rv == n_DLS_NONE || (rv & n_DLS_ABANDON))
         break;
      if(dip->di_pollmsecs == 0 || tries >= DOTLOCK_TRIES){
         rv |= n_DLS_ABANDON;
         break;
      }

      xrv = n_DLS_PING;
      w = write(STDOUT_FILENO, &xrv, sizeof xrv);
      if(w == -1 && su_err_no() == su_ERR_PIPE){
         rv = n_DLS_DUNNO | n_DLS_ABANDON;
         break;
      }
      n_msleep(dip->di_pollmsecs, FAL0);
   }
jleave:
   return rv;
}

static enum n_dotlock_state
a_dotlock__create_excl(struct n_dotlock_info *dip, char const *lname){
   struct stat stb;
   int fd, e;
   uz tries;
   enum n_dotlock_state rv = n_DLS_NONE;

   /* We try to create the unique filename */
   for(tries = 0;; ++tries){
      fd = open(lname,
#ifdef O_SYNC
               (O_WRONLY | O_CREAT | O_EXCL | O_SYNC),
#else
               (O_WRONLY | O_CREAT | O_EXCL),
#endif
            S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
      if(fd != -1){
#ifdef mx_SOURCE_DOTLOCK_PS
         if(dip->di_stb != NULL &&
               fchown(fd, dip->di_stb->st_uid, dip->di_stb->st_gid)){
            int x = su_err_no();
            close(fd);
            su_err_set_no(x);
            goto jbados;
         }
#endif
         close(fd);
         break;
      }else if((e = su_err_no()) != su_ERR_EXIST){
         rv = (e == su_ERR_ROFS) ? n_DLS_ROFS | n_DLS_ABANDON : n_DLS_NOPERM;
         goto jleave;
      }else if(tries >= DOTLOCK_TRIES){
         rv = n_DLS_EXIST;
         goto jleave;
      }
   }

   /* We link the name to the fname */
   if(link(lname, dip->di_lock_name) == -1)
      goto jbados;

   /* Note that we stat our own exclusively created name, not the
    * destination, since the destination can be affected by others */
   if(stat(lname, &stb) == -1)
      goto jbados;

   unlink(lname);

   /* If the number of links was two (one for the unique file and one for
    * the lock), we've won the race */
   if(stb.st_nlink != 2)
      rv = n_DLS_EXIST;
jleave:
   return rv;
jbados:
   rv = (su_err_no() == su_ERR_EXIST)
         ? n_DLS_EXIST : n_DLS_NOPERM | n_DLS_ABANDON;
   unlink(lname);
   goto jleave;
}

/* s-it-mode */
