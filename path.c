/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Path and directory related operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
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
#define n_FILE path

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

FL bool_t
n_is_dir(char const *name, bool_t check_access){
   struct stat sbuf;
   bool_t rv;
   NYD2_IN;

   if((rv = (stat(name, &sbuf) == 0))){
      if((rv = (S_ISDIR(sbuf.st_mode) != 0)) && check_access){
         int mode;

         mode = R_OK | X_OK;
         if(check_access != TRUM1)
            mode |= W_OK;
         rv = (access(name, mode) == 0);
      }
   }
   NYD2_OU;
   return rv;
}

FL bool_t
n_path_mkdir(char const *name){
   struct stat st;
   bool_t rv;
   NYD_IN;

jredo:
   if(!mkdir(name, 0777))
      rv = TRU1;
   else{
      int e = n_err_no;

      /* Try it recursively */
      if(e == n_ERR_NOENT){
         char const *vp;

         if((vp = strrchr(name, '/')) != NULL){ /* TODO magic dirsep */
            while(vp > name && vp[-1] == '/')
               --vp;
            vp = savestrbuf(name, PTR2SIZE(vp - name));

            if(n_path_mkdir(vp))
               goto jredo;
         }
      }

      rv = ((e == n_ERR_EXIST || e == n_ERR_NOSYS) && !stat(name, &st) &&
            S_ISDIR(st.st_mode));
   }
   NYD_OU;
   return rv;
}

FL bool_t
n_path_rm(char const *name){
   struct stat sb;
   bool_t rv;
   NYD2_IN;

   if(stat(name, &sb) != 0)
      rv = FAL0;
   else if(!S_ISREG(sb.st_mode))
      rv = TRUM1;
   else
      rv = (unlink(name) == 0);
   NYD2_OU;
   return rv;
}

#ifdef HAVE_FCHDIR
FL enum okay
cwget(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if ((cw->cw_fd = open(".", O_RDONLY)) == -1)
      goto jleave;
   if (fchdir(cw->cw_fd) == -1) {
      close(cw->cw_fd);
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_OU;
   return rv;
}

FL enum okay
cwret(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if (!fchdir(cw->cw_fd))
      rv = OKAY;
   NYD_OU;
   return rv;
}

FL void
cwrelse(struct cw *cw)
{
   NYD_IN;
   close(cw->cw_fd);
   NYD_OU;
}

#else /* !HAVE_FCHDIR */
FL enum okay
cwget(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if (getcwd(cw->cw_wd, sizeof cw->cw_wd) != NULL && !chdir(cw->cw_wd))
      rv = OKAY;
   NYD_OU;
   return rv;
}

FL enum okay
cwret(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if (!chdir(cw->cw_wd))
      rv = OKAY;
   NYD_OU;
   return rv;
}

FL void
cwrelse(struct cw *cw)
{
   NYD_IN;
   n_UNUSED(cw);
   NYD_OU;
}
#endif /* !HAVE_FCHDIR */

/* s-it-mode */
