/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ User commands which save, copy, write (parts of) messages.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE cmd_write

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Save/copy the indicated messages at the end of the passed file name.
 * If mark is true, mark the message "saved" */
static int a_cwrite_save1(void *vp, struct n_ignore const *itp,
            int convert, bool_t domark, bool_t domove);

static int
a_cwrite_save1(void *vp, struct n_ignore const *itp,
   int convert, bool_t domark, bool_t domove)
{
   ui64_t mstats[1], tstats[2];
   enum n_fopen_state fs;
   struct message *mp;
   char *file, *cp, *cq;
   FILE *obuf;
   char const *shell, *disp;
   bool_t success;
   int last, *msgvec, *ip;
   struct n_cmd_arg *cap;
   struct n_cmd_arg_ctx *cacp;
   NYD2_ENTER;

   cacp = vp;
   cap = cacp->cac_arg;
   last = 0;
   msgvec = cap->ca_arg.ca_msglist;
   success = FAL0;
   shell = NULL;
   file = NULL;

   if(!(cap->ca_ent_flags[0] & n_CMD_ARG_DESC_MSGLIST_AND_TARGET)){
      struct name *np;

      if((cp = n_header_senderfield_of(message + *msgvec - 1)) == NULL ||
            (np = lextract(cp, GTO | GSKIN)) == NULL){
         n_err(_("Cannot determine message sender to %s.\n"),
            cacp->cac_desc->cad_name);
         goto jleave;
      }
      cp = np->n_name;

      for (cq = cp; *cq != '\0' && *cq != '@'; cq++)
         ;
      *cq = '\0';
      if (ok_blook(outfolder)) {
         size_t sz = strlen(cp) +1;
         file = n_autorec_alloc(sz + 1);
         file[0] = '+';
         memcpy(file + 1, cp, sz);
      } else
         file = cp;
   }else{
      cap = cap->ca_next;
      if((file = cap->ca_arg.ca_str.s)[0] == '\0')
         file = fexpand("&", FEXP_NVAR);

      while(spacechar(*file))
         ++file;
      if (*file == '|') {
         ++file;
         shell = ok_vlook(SHELL);

         /* Pipe target is special TODO hacked in later, normalize flow! */
         if((obuf = Popen(file, "w", shell, NULL, 1)) == NULL){
            int esave;

            n_perr(file, esave = n_err_no);
            n_err_no = esave;
            goto jleave;
         }
         disp = A_("[Piped]");
         goto jsend;
      }
   }

   if ((file = fexpand(file, FEXP_FULL)) == NULL)
      goto jleave;

   /* TODO all this should be URL and Mailbox-"VFS" based, and then finally
    * TODO end up as Mailbox()->append().  Unless SEND_TOFILE, of course.
    * TODO However, URL parse because that file:// prefix check is a HACK! */
   if(convert == SEND_TOFILE && !is_prefix("file://", file))
      file = savecat("file://", file);
   if((obuf = n_fopen_any(file, "a+", &fs)) == NULL){
      n_perr(file, 0);
      goto jleave;
   }

#if defined HAVE_POP3 && defined HAVE_IMAP
   if(mb.mb_type == MB_POP3 && (fs & n_PROTO_MASK) == n_PROTO_IMAP){
      Fclose(obuf);
      n_err(_("Direct copy from POP3 to IMAP not supported before v15\n"));
      goto jleave;
   }
#endif

   disp = (fs & n_FOPEN_STATE_EXISTS) ? A_("[Appended]") : A_("[New file]");

   if((fs & (n_PROTO_MASK | n_FOPEN_STATE_EXISTS)) ==
         (n_PROTO_FILE | n_FOPEN_STATE_EXISTS)){
      int xerr;

      /* TODO RETURN check, but be aware of protocols: v15: Mailbox->lock()!
       * TODO BETTER yet: should be returned in lock state already! */
      n_file_lock(fileno(obuf), FLT_WRITE, 0,0, UIZ_MAX);

      if((xerr = n_folder_mbox_prepare_append(obuf, NULL)) != n_ERR_NONE){
         n_perr(file, xerr);
         goto jleave;
      }
   }

jsend:
   success = TRU1;
   tstats[0] = tstats[1] = 0;
#ifdef HAVE_IMAP
   imap_created_mailbox = 0;
#endif

   n_autorec_relax_create();
   for (ip = msgvec; *ip != 0; ++ip) {
      mp = &message[*ip - 1];
#ifdef HAVE_IMAP
      if((fs & n_PROTO_MASK) == n_PROTO_IMAP &&
            !n_ignore_is_any(n_IGNORE_SAVE) && imap_thisaccount(file)){
         if(imap_copy(mp, PTR2SIZE(mp - message + 1), file) == STOP){
            success = FAL0;
            goto jferr;
         }
         mstats[0] = mp->m_xsize;
      }else
#endif
      if (sendmp(mp, obuf, itp, NULL, convert, mstats) < 0) {
         success = FAL0;
         goto jferr;
      }
      n_autorec_relax_unroll();

      touch(mp);
      if (domark)
         mp->m_flag |= MSAVED;
      if (domove) {
         mp->m_flag |= MDELETED | MSAVED;
         last = *ip;
      }

      tstats[0] += mstats[0];
      tstats[1] += mp->m_lines;/* TODO won't work, need target! v15!! */
   }
   n_autorec_relax_gut();

   fflush(obuf);

   /* TODO Should be a VFS, then n_MBOX knows what to do upon .close()! */
   if((fs & n_PROTO_MASK) == n_PROTO_FILE && convert == SEND_MBOX)
      n_folder_mbox_prepare_append(obuf, NULL);

   if (ferror(obuf)) {
jferr:
      n_perr(file, 0);
      if (!success)
         srelax_rele();
      success = FAL0;
   }
   if (shell != NULL) {
      if (!Pclose(obuf, TRU1))
         success = FAL0;
   } else if (Fclose(obuf) != 0)
      success = FAL0;

   if (success) {
#ifdef HAVE_IMAP
      if((fs & n_PROTO_MASK) == n_PROTO_IMAP){
         if(disconnected(file))
            disp = A_("[Queued]");
         else if(imap_created_mailbox)
            disp = A_("[New file]");
         else
            disp = A_("[Appended]");
      }
#endif
      fprintf(n_stdout, "%s %s %" /*PRIu64 "/%"*/ PRIu64 " bytes\n",
         n_shexp_quote_cp(file, FAL0), disp,
         /*tstats[1], TODO v15: lines written */ tstats[0]);
   } else if (domark) {
      for (ip = msgvec; *ip != 0; ++ip) {
         mp = message + *ip - 1;
         mp->m_flag &= ~MSAVED;
      }
   } else if (domove) {
      for (ip = msgvec; *ip != 0; ++ip) {
         mp = message + *ip - 1;
         mp->m_flag &= ~(MSAVED | MDELETED);
      }
   }

   if (domove && last && success) {
      setdot(message + last - 1);
      last = first(0, MDELETED);
      setdot(message + (last != 0 ? last - 1 : 0));
   }
jleave:
   NYD2_LEAVE;
   return (success == FAL0);
}

FL int
c_save(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_MBOX, TRU1, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Save(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_MBOX, TRU1, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_copy(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_MBOX, FAL0, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Copy(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_MBOX, FAL0, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_move(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_MBOX, FAL0, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Move(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_MBOX, FAL0, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_decrypt(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_DECRYPT, FAL0, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Decrypt(void *vp){
   int rv;
   NYD_ENTER;

   rv = a_cwrite_save1(vp, n_IGNORE_SAVE, SEND_DECRYPT, FAL0, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_write(void *vp){
   int rv;
   struct n_cmd_arg *cap;
   struct n_cmd_arg_ctx *cacp;
   NYD_ENTER;

   if((cap = (cacp = vp)->cac_arg->ca_next)->ca_arg.ca_str.s[0] == '\0')
      cap->ca_arg.ca_str.s = savestrbuf(n_path_devnull,
            cap->ca_arg.ca_str.l = strlen(n_path_devnull));

   rv = a_cwrite_save1(vp, n_IGNORE_ALL, SEND_TOFILE, FAL0, FAL0);
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
