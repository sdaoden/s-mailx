/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Perform message editing functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
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
#undef su_FILE
#define su_FILE edit

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/* Edit a message by writing the message into a funnily-named file (which
 * should not exist) and forking an editor on it */
static int edit1(int *msgvec, int viored);

static int
edit1(int *msgvec, int viored)
{
   int c, i;
   FILE *fp = NULL;
   struct message *mp;
   off_t size;
   bool_t wb, lastnl;
   NYD_IN;

   wb = ok_blook(writebackedited);

   /* Deal with each message to be edited... */
   for (i = 0; msgvec[i] != 0 && i < msgCount; ++i) {
      sighandler_type sigint;

      if(i > 0){
         char prompt[64];

         snprintf(prompt, sizeof prompt, _("Edit message %d"), msgvec[i]);
         if(!getapproval(prompt, TRU1))
            continue;
      }

      mp = message + msgvec[i] - 1;
      setdot(mp);
      n_pstate |= n_PS_DID_PRINT_DOT;
      touch(mp);

      sigint = safe_signal(SIGINT, SIG_IGN);

      --mp->m_size; /* Strip final NL.. TODO MAILVFS->MESSAGE->length() */
      fp = n_run_editor(fp, -1/*mp->m_size TODO */, viored,
            ((mb.mb_perm & MB_EDIT) == 0 || !wb), NULL, mp,
            (wb ? SEND_MBOX : SEND_TODISP_ALL), sigint, NULL);
      ++mp->m_size; /* And readd it TODO */

      if (fp != NULL) {
         fseek(mb.mb_otf, 0L, SEEK_END);
         size = ftell(mb.mb_otf);
         mp->m_block = mailx_blockof(size);
         mp->m_offset = mailx_offsetof(size);
         mp->m_lines = 0;
         mp->m_flag |= MODIFY;
         rewind(fp);
         lastnl = 0;
         size = 0;
         while ((c = getc(fp)) != EOF) {
            if ((lastnl = (c == '\n')))
               ++mp->m_lines;
            if (putc(c, mb.mb_otf) == EOF)
               break;
            ++size;
         }
         if (!lastnl && putc('\n', mb.mb_otf) != EOF)
            ++size;
         if (putc('\n', mb.mb_otf) != EOF)
            ++size;
         mp->m_size = (size_t)size;
         if (ferror(mb.mb_otf))
            n_perr(_("/tmp"), 0);
         Fclose(fp);
      }

      safe_signal(SIGINT, sigint);
   }
   NYD_OU;
   return 0;
}

FL int
c_editor(void *v)
{
   int *msgvec = v, rv;
   NYD_IN;

   rv = edit1(msgvec, 'e');
   NYD_OU;
   return rv;
}

FL int
c_visual(void *v)
{
   int *msgvec = v, rv;
   NYD_IN;

   rv = edit1(msgvec, 'v');
   NYD_OU;
   return rv;
}

FL FILE *
n_run_editor(FILE *fp, off_t size, int viored, bool_t readonly,
   struct header *hp, struct message *mp, enum sendaction action,
   sighandler_type oldint, char const *pipecmd)
{
   struct stat statb;
   sigset_t cset;
   int t, ws;
   time_t modtime;
   off_t modsize;
   char *tmp_name;
   FILE *nf, *nf_pipetmp, *nf_tmp;
   NYD_IN;

   nf = nf_pipetmp = NULL;
   tmp_name = NULL;
   modtime = 0, modsize = 0;

   if((nf_tmp = Ftmp(&tmp_name, "runed",
         ((viored == '|' ? OF_RDWR : OF_WRONLY) | OF_REGISTER |
          OF_REGISTER_UNLINK | OF_FN_AUTOREC))) == NULL){
jetempo:
      n_perr(_("temporary mail edit file"), 0);
      goto jleave;
   }

   if(hp != NULL){
      assert(mp == NULL);
      if(!n_header_put4compose(nf_tmp, hp))
         goto jleave;
   }

   if(mp != NULL){
      assert(hp == NULL);
      if(sendmp(mp, nf_tmp, NULL, NULL, action, NULL) < 0){
         n_err(_("Failed to prepare editable message\n"));
         goto jleave;
      }
   }else{
      if(size >= 0){
         while(--size >= 0 && (t = getc(fp)) != EOF)
            if(putc(t, nf_tmp) == EOF)
               break;
      }else{
         while((t = getc(fp)) != EOF)
            if(putc(t, nf_tmp) == EOF)
               break;
      }
   }

   fflush(nf_tmp);

   if((t = (fp != NULL && ferror(fp))) == 0 && (t = ferror(nf_tmp)) == 0){
      if(viored != '|'){
         if(!fstat(fileno(nf_tmp), &statb))
            modtime = statb.st_mtime, modsize = statb.st_size;

         if(readonly)
            t = (fchmod(fileno(nf_tmp), S_IRUSR) != 0);
      }
   }

   if(t != 0){
      n_perr(tmp_name, 0);
      goto jleave;
   }

   if(viored == '|'){
      assert(pipecmd != NULL);
      tmp_name = NULL;
      if((nf_pipetmp = Ftmp(&tmp_name, "runed", OF_WRONLY | OF_REGISTER |
            OF_REGISTER_UNLINK | OF_FN_AUTOREC)) == NULL)
         goto jetempo;
      really_rewind(nf = nf_tmp);
      nf_tmp = nf_pipetmp;
      nf_pipetmp = nf;
      nf = NULL;
      if(n_child_run(ok_vlook(SHELL), 0, fileno(nf_pipetmp), fileno(nf_tmp),
            "-c", pipecmd, NULL, NULL, &ws) < 0 || WEXITSTATUS(ws) != 0)
         goto jleave;
   }else{
      sigemptyset(&cset);
      if(n_child_run((viored == 'e' ? ok_vlook(EDITOR) : ok_vlook(VISUAL)),
            (oldint != SIG_IGN ? &cset : NULL),
            n_CHILD_FD_PASS, n_CHILD_FD_PASS, tmp_name, NULL, NULL, NULL,
            &ws) < 0 || WEXITSTATUS(ws) != 0)
         goto jleave;
   }

   /* If in read only mode or file unchanged, just remove the editor temporary
    * and return.  Otherwise switch to new file */
   if(viored != '|'){
      if(readonly)
         goto jleave;
      if(stat(tmp_name, &statb) == -1){
         n_perr(tmp_name, 0);
         goto jleave;
      }
      if(modtime == statb.st_mtime && modsize == statb.st_size)
         goto jleave;
   }

   if((nf = Fopen(tmp_name, "r+")) == NULL)
      n_perr(tmp_name, 0);

jleave:
   if(nf_pipetmp != NULL)
      Fclose(nf_pipetmp);
   if(nf_tmp != NULL && Fclose(nf_tmp) < 0){
      if(tmp_name != NULL)
         n_perr(tmp_name, 0);
      if(nf != NULL)
         Fclose(nf);
      nf = NULL;
   }
   NYD_OU;
   return nf;
}

/* s-it-mode */
