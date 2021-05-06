/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ User commands which save, copy, write (parts of) messages.
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
#define su_FILE cmd_write
#define mx_SOURCE
#define mx_SOURCE_CMD_WRITE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>

#include "mx/cmd.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/ignore.h"
#include "mx/names.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Save/copy the indicated messages at the end of the passed file name.
 * If mark is true, mark the message "saved" */
static int a_cwrite_save1(void *vp, struct mx_ignore const *itp,
            int convert, boole domark, boole domove);

static int
a_cwrite_save1(void *vp, struct mx_ignore const *itp,
   int convert, boole domark, boole domove)
{
   u64 mstats[1], tstats[2];
   BITENUM_IS(u32,mx_fs_open_state) fs;
   struct message *mp;
   char *file, *cp, *cq;
   FILE *obuf;
   char const *shell, *disp;
   boole success;
   int last, *msgvec, *ip;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   NYD2_IN;

   n_pstate_err_no = su_ERR_NONE;

   cacp = vp;
   cap = cacp->cac_arg;
   last = 0;
   msgvec = cap->ca_arg.ca_msglist;
   success = FAL0;
   shell = NULL;
   file = NULL;

   if(!(cap->ca_ent_flags[0] & mx_CMD_ARG_DESC_MSGLIST_AND_TARGET)){
      struct mx_name *np;

      if((cp = n_header_senderfield_of(n_msgmark1)) == NIL ||
            (np = lextract(cp, GTO | GSKIN)) == NIL){
         n_err(_("Cannot determine message sender to %s.\n"),
            cacp->cac_desc->cad_name);
         goto jleave;
      }
      cp = np->n_name;

      for(cq = cp; *cq != '\0' && *cq != '@'; cq++)
         ;
      *cq = '\0';
      if(ok_blook(outfolder)){
         uz i;

         i = su_cs_len(cp) +1;
         file = su_AUTO_ALLOC(i + 1);
         file[0] = '+';
         su_mem_copy(file + 1, cp, i);
      }else
         file = cp;
   }else{
      cap = cap->ca_next;
      if((file = cap->ca_arg.ca_str.s)[0] == '\0')
         file = fexpand("&", FEXP_NVAR);

      while(su_cs_is_space(*file))
         ++file;
      if (*file == '|') {
         ++file;
         shell = ok_vlook(SHELL);

         /* Pipe target is special TODO hacked in later, normalize flow! */
         if((obuf = mx_fs_pipe_open(file, mx_FS_PIPE_WRITE_CHILD_PASS, shell,
                  NIL, -1)) == NIL){
            n_perr(file, n_pstate_err_no = su_err_no());
            goto jleave;
         }
         disp = A_("[Piped]");
         fs = mx_FS_OPEN_STATE_NONE;
         goto jsend;
      }
   }

   if ((file = fexpand(file, FEXP_FULL)) == NULL)
      goto jleave;

   /* TODO all this should be URL and Mailbox-"VFS" based, and then finally
    * TODO end up as Mailbox()->append().  Unless SEND_TOFILE, of course.
    * TODO However, URL parse because that file:// prefix check is a HACK! */
   if(convert == SEND_TOFILE && !su_cs_starts_with(file, "file://"))
      file = savecat("file://", file);
   if((obuf = mx_fs_open_any(file, (mx_FS_O_RDWR | mx_FS_O_APPEND |
            mx_FS_O_CREATE), &fs)) == NIL){
      n_perr(file, n_pstate_err_no = su_err_no());
      goto jleave;
   }
   ASSERT((fs & n_PROTO_MASK) == n_PROTO_IMAP ||
      (fs & n_PROTO_MASK) == n_PROTO_FILE ||
      (fs & n_PROTO_MASK) == n_PROTO_MAILDIR);

#if defined mx_HAVE_POP3 && defined mx_HAVE_IMAP
   if(mb.mb_type == MB_POP3 && (fs & n_PROTO_MASK) == n_PROTO_IMAP){
      mx_fs_close(obuf);
      n_err(_("Direct copy from POP3 to IMAP not supported before v15\n"));
      goto jleave;
   }
#endif

   disp = (fs & mx_FS_OPEN_STATE_EXISTS) ? A_("[Appended]") : A_("[New file]");

   if((fs & n_PROTO_MASK) != n_PROTO_IMAP){
      int xerr;

      /* v15: Mailbox->lock()!
       * TODO BETTER yet: should be returned in lock state already! */
      if(!mx_file_lock(fileno(obuf), (mx_FILE_LOCK_MODE_TEXCL |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
         xerr = su_err_no();
         goto jeappend;
      }

      if((fs & mx_FS_OPEN_STATE_EXISTS) &&
            (xerr = n_folder_mbox_prepare_append(obuf, NULL)) != su_ERR_NONE){
jeappend:
         n_perr(file, n_pstate_err_no = xerr);
         goto jleave;
      }
   }

jsend:
   success = TRU1;
   tstats[0] = tstats[1] = 0;
#ifdef mx_HAVE_IMAP
   imap_created_mailbox = 0;
#endif

   su_mem_bag_auto_relax_create(su_MEM_BAG_SELF);
   for (ip = msgvec; *ip != 0; ++ip) {
      mp = &message[*ip - 1];

      if(!(mp->m_flag & MVALID) && (ip != msgvec || ip[1] != 0)){
         n_err(_("Message %d is invalid, skipped (address by itself)\n"), *ip);
         continue;
      }

#ifdef mx_HAVE_IMAP
      if((fs & n_PROTO_MASK) == n_PROTO_IMAP &&
            !mx_ignore_is_any(mx_IGNORE_SAVE) && imap_thisaccount(file)){
         if(imap_copy(mp, P2UZ(mp - message + 1), file) == STOP){
            success = FAL0;
            goto jferr;
         }
         mstats[0] = mp->m_xsize;
      }else
#endif
           {
         if(sendmp(mp, obuf, itp, NIL, convert, mstats) < 0){
            success = FAL0;
            goto jferr;
         }

         /* TODO v15compat: solely Mailbox()->append() related, and today
          * TODO can mess with the content of a message (in that if a msg
          * TODO ends with two \n, that is ok'd as MBOX separator! */
         if(convert == SEND_MBOX)
            n_folder_mbox_prepare_append(obuf, NIL);
      }
      su_mem_bag_auto_relax_unroll(su_MEM_BAG_SELF);

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
   su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);

   fflush(obuf);

   if (ferror(obuf)) {
jferr:
      n_perr(file, n_pstate_err_no = su_err_no());
      if (!success)
         su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);
      success = FAL0;
   }
   if(shell != NIL){
      if(!mx_fs_pipe_close(obuf, TRU1))
         success = FAL0;
   }else if(!mx_fs_close(obuf))
      success = FAL0;

   if (success) {
#ifdef mx_HAVE_IMAP
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
      setdot(&message[last - 1], FAL0);
      last = first(0, MDELETED);
      setdot(&message[last != 0 ? last - 1 : 0], FAL0);
   }

jleave:
   NYD2_OU;
   return (success ? n_EXIT_OK : n_EXIT_ERR);
}

FL int
c_save(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_MBOX, TRU1, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_Save(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_MBOX, TRU1, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_copy(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_MBOX, FAL0, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_Copy(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_MBOX, FAL0, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_move(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_MBOX, FAL0, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_Move(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_MBOX, FAL0, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_decrypt(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_DECRYPT, FAL0, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_Decrypt(void *vp){
   int rv;
   NYD_IN;

   rv = a_cwrite_save1(vp, mx_IGNORE_SAVE, SEND_DECRYPT, FAL0, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_write(void *vp){
   int rv;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   if((cap = (cacp = vp)->cac_arg->ca_next)->ca_arg.ca_str.s[0] == '\0')
      cap->ca_arg.ca_str.s = savestrbuf(su_path_dev_null,
            cap->ca_arg.ca_str.l = sizeof(su_path_dev_null));

   rv = a_cwrite_save1(vp, mx_IGNORE_ALL, SEND_TOFILE, FAL0, FAL0);
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_WRITE
/* s-it-mode */
