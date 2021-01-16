/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Perform message editing functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_edit
#define mx_SOURCE
#define mx_SOURCE_CMD_EDIT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include "mx/child.h"
#include "mx/file-streams.h"
#include "mx/sigs.h"
#include "mx/tty.h"

/* TODO fake */
#include "mx/cmd-edit.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Edit a message by writing the message into a funnily-named file (which
 * should not exist) and forking an editor on it */
static int a_edit1(int *msgvec, int viored);

static int
a_edit1(int *msgvec, int viored)
{
   int c, i;
   FILE *fp = NULL;
   struct message *mp;
   off_t size;
   boole wb, lastnl;
   NYD_IN;

   wb = ok_blook(writebackedited);

   /* Deal with each message to be edited... */
   for (i = 0; msgvec[i] != 0 && i < msgCount; ++i) {
      n_sighdl_t sigint;

      if(i > 0){
         char prompt[64];

         snprintf(prompt, sizeof prompt, _("Edit message %d"), msgvec[i]);
         if(!mx_tty_yesorno(prompt, TRU1))
            continue;
      }

      mp = message + msgvec[i] - 1;
      setdot(mp, TRU1);
      touch(mp);

      sigint = safe_signal(SIGINT, SIG_IGN);

      --mp->m_size; /* Strip final NL.. TODO MAILVFS->MESSAGE->length() */
      fp = n_run_editor(fp, -1/*mp->m_size TODO */, viored,
            ((mb.mb_perm & MB_EDIT) == 0 || !wb), NULL, mp,
            (wb ? SEND_MBOX : SEND_TODISP_ALL), sigint, NULL);
      ++mp->m_size; /* And re-add it TODO */

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
         mp->m_size = (uz)size;
         if (ferror(mb.mb_otf))
            n_perr(_("/tmp"), 0);
         mx_fs_close(fp);
      }

      safe_signal(SIGINT, sigint);
   }

   NYD_OU;
   return 0;
}

int
c_edit(void *vp){
   int rv;
   NYD_IN;

   rv = a_edit1(vp, 'e');

   NYD_OU;
   return rv;
}

int
c_visual(void *vp){
   int rv;
   NYD_IN;

   rv = a_edit1(vp, 'v');

   NYD_OU;
   return rv;
}

FILE *
n_run_editor(FILE *fp, off_t size, int viored, boole readonly,/* TODO condom */
      struct header *hp, struct message *mp, enum sendaction action,
      n_sighdl_t oldint, char const *pipecmd){
   struct stat statb;
   struct mx_child_ctx cc;
   sigset_t cset;
   int t;
   time_t modtime;
   off_t modsize;
   struct mx_fs_tmp_ctx *fstcp;
   FILE *nf, *nf_pipetmp, *nf_tmp;
   NYD_IN;

   nf = nf_pipetmp = NIL;
   modtime = 0, modsize = 0;

   if((nf_tmp = mx_fs_tmp_open("edbase", ((viored == '|' ? mx_FS_O_RDWR
               : mx_FS_O_WRONLY) | mx_FS_O_REGISTER | mx_FS_O_REGISTER_UNLINK),
               &fstcp)) == NIL){
jetempo:
      n_perr(_("creation of temporary mail edit file"), 0);
      goto jleave;
   }

   if(hp != NIL){
      ASSERT(mp == NIL);
      if(!n_header_put4compose(nf_tmp, hp))
         goto jleave;
   }

   if(mp != NIL){
      ASSERT(hp == NIL);
      if(sendmp(mp, nf_tmp, NIL, NIL, action, NIL) < 0){
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

   if((t = (fp != NIL && ferror(fp))) == 0 && (t = ferror(nf_tmp)) == 0){
      if(viored != '|'){
         if(!fstat(fileno(nf_tmp), &statb))
            modtime = statb.st_mtime, modsize = statb.st_size;

         if(readonly)
            t = (fchmod(fileno(nf_tmp), S_IRUSR) != 0);
      }
   }

   if(t != 0){
      n_perr(fstcp->fstc_filename, 0);
      goto jleave;
   }

   mx_child_ctx_setup(&cc);
   cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;

   if(viored == '|'){
      ASSERT(pipecmd != NIL);

      if((nf_pipetmp = mx_fs_tmp_open("edpipe", (mx_FS_O_WRONLY |
               mx_FS_O_REGISTER | mx_FS_O_REGISTER_UNLINK), &fstcp)) == NIL)
         goto jetempo;
      really_rewind(nf = nf_tmp);
      nf_tmp = nf_pipetmp;
      nf_pipetmp = nf;
      nf = NIL;

      cc.cc_fds[mx_CHILD_FD_IN] = fileno(nf_pipetmp);
      cc.cc_fds[mx_CHILD_FD_OUT] = fileno(nf_tmp);
      cc.cc_cmd = ok_vlook(SHELL);
      cc.cc_args[0] = "-c";
      cc.cc_args[1] = pipecmd;
   }else{
      cc.cc_cmd = (viored == 'e') ? ok_vlook(EDITOR) : ok_vlook(VISUAL);
      if(oldint != SIG_IGN){
         sigemptyset(&cset);
         cc.cc_mask = &cset;
      }
      cc.cc_args[0] = fstcp->fstc_filename;
   }

   if(!mx_child_run(&cc) || cc.cc_exit_status != 0)
      goto jleave;

   /* If in read only mode or file unchanged, just remove the editor temporary
    * and return.  Otherwise switch to new file */
   if(viored != '|'){
      if(readonly)
         goto jleave;
      if(stat(fstcp->fstc_filename, &statb) == -1){
         n_perr(fstcp->fstc_filename, 0);
         goto jleave;
      }
      if(modtime == statb.st_mtime && modsize == statb.st_size)
         goto jleave;
   }

   if((nf = mx_fs_open(fstcp->fstc_filename, "r+")) == NIL)
      n_perr(fstcp->fstc_filename, 0);

jleave:
   if(nf_pipetmp != NIL)
      mx_fs_close(nf_pipetmp);

   if(nf_tmp != NIL && !mx_fs_close(nf_tmp)){
      n_perr(_("closing of temporary mail edit file"), 0);
      if(nf != NIL)
         mx_fs_close(nf);
      nf = NIL;
   }
   NYD_OU;
   return nf;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_EDIT
/* s-it-mode */
