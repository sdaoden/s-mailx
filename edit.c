/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Perform message editing functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE edit

#ifndef HAVE_AMALGAMATION
# include "nail.h"
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
   NYD_ENTER;

   wb = ok_blook(writebackedited);

   /* Deal with each message to be edited... */
   for (i = 0; msgvec[i] != 0 && i < msgCount; ++i) {
      sighandler_type sigint;

      if(i > 0){
         char prompt[64];

         snprintf(prompt, sizeof prompt, _("Edit message %d"), msgvec[i]);
         if(!getapproval(prompt, FAL0))
            continue;
      }

      mp = message + msgvec[i] - 1;
      setdot(mp);
      pstate |= PS_DID_PRINT_DOT;
      touch(mp);

      sigint = safe_signal(SIGINT, SIG_IGN);

      --mp->m_size; /* Strip final NL.. TODO MAILVFS->MESSAGE->length() */
      fp = run_editor(fp, -1/*mp->m_size TODO */, viored,
            ((mb.mb_perm & MB_EDIT) == 0 || !wb), NULL, mp,
            (wb ? SEND_MBOX : SEND_TODISP_ALL), sigint);
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
   NYD_LEAVE;
   return 0;
}

FL int
c_editor(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = edit1(msgvec, 'e');
   NYD_LEAVE;
   return rv;
}

FL int
c_visual(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = edit1(msgvec, 'v');
   NYD_LEAVE;
   return rv;
}

FL FILE *
run_editor(FILE *fp, off_t size, int viored, int readonly, struct header *hp,
   struct message *mp, enum sendaction action, sighandler_type oldint)
{
   struct stat statb;
   sigset_t cset;
   FILE *nf = NULL;
   int t;
   time_t modtime;
   off_t modsize;
   char *tempEdit;
   NYD_ENTER;

   if ((nf = Ftmp(&tempEdit, "runed", OF_WRONLY | OF_REGISTER)) == NULL) {
      n_perr(_("temporary mail edit file"), 0);
      goto jleave;
   }

   if (hp != NULL) {
      assert(mp == NULL);
      t = GTO | GSUBJECT | GCC | GBCC | GNL | GCOMMA;
      if ((hp->h_from != NULL || myaddrs(hp) != NULL) ||
            (hp->h_sender != NULL || ok_vlook(sender) != NULL) ||
            (hp->h_replyto != NULL || ok_vlook(replyto) != NULL) ||
            hp->h_list_post != NULL || (hp->h_flags & HF_LIST_REPLY))
         t |= GIDENT;
      puthead(TRUM1, hp, nf, t, SEND_TODISP, CONV_NONE, NULL, NULL);
   }

   if (mp != NULL) {
      if (sendmp(mp, nf, NULL, NULL, action, NULL) < 0) {
         n_err(_("Failed to prepare editable message\n"));
         goto jleave;
      }
   } else {
      if (size >= 0)
         while (--size >= 0 && (t = getc(fp)) != EOF)
            putc(t, nf);
      else
         while ((t = getc(fp)) != EOF)
            putc(t, nf);
   }

   fflush(nf);
   if ((t = ferror(nf)) == 0) {
      if (fstat(fileno(nf), &statb) == -1)
         modtime = 0, modsize = 0;
      else
         modtime = statb.st_mtime, modsize = statb.st_size;

      if (readonly)
         t = (fchmod(fileno(nf), S_IRUSR) != 0);
   }

   if (Fclose(nf) < 0 || t != 0) {
      n_perr(tempEdit, 0);
      t = 1;
   }
   nf = NULL;
   if (t != 0)
      goto jleave;

   sigemptyset(&cset);
   if (run_command((viored == 'e' ? ok_vlook(EDITOR) : ok_vlook(VISUAL)),
         (oldint != SIG_IGN ? &cset : NULL),
         COMMAND_FD_PASS, COMMAND_FD_PASS, tempEdit, NULL, NULL, NULL) < 0)
      goto jleave;

   /* If in read only mode or file unchanged, just remove the editor temporary
    * and return.  Otherwise switch to new file */
   if (readonly)
      goto jleave;
   if (stat(tempEdit, &statb) == -1) {
      n_perr(tempEdit, 0);
      goto jleave;
   }

   if ((modtime != statb.st_mtime || modsize != statb.st_size) &&
         (nf = Fopen(tempEdit, "a+")) == NULL)
      n_perr(tempEdit, 0);
jleave:
   if (tempEdit != NULL) { /* TODO i'd rather do more signal handling */
      unlink(tempEdit);    /* TODO in here */
      Ftmp_free(&tempEdit);
   }
   NYD_LEAVE;
   return nf;
}

/* s-it-mode */
