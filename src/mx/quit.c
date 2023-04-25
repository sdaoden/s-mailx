/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Termination processing. TODO MBOX -> VFS; error handling: catastrophe!
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE quit
#define mx_SOURCE
#define mx_SOURCE_QUIT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/path.h>

#include "mx/compat.h"
#include "mx/dig-msg.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/ignore.h"
#include "mx/net-pop3.h"
#include "mx/sigs.h"
#include "mx/tty.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum quitflags{
   QUITFLAG_HOLD = 1<<0,
   QUITFLAG_KEEP = 1<<1,
   QUITFLAG_KEEPSAVE = 1<<2
};

struct quitnames {
   enum quitflags flag;
   enum okeys     okey;
};

static struct quitnames const _quitnames[] = {
   {QUITFLAG_HOLD, ok_b_hold},
   {QUITFLAG_KEEP, ok_b_keep},
   {QUITFLAG_KEEPSAVE, ok_b_keepsave}
};

static char _mboxname[PATH_MAX];  /* Name of mbox */

/* Adjust the message flags in each message.  Return whether any message got
 * a status change */
static boole a_quit_holdbits(void);

/* Preserve all the appropriate messages back in the system mailbox, and print
 * a nice message indicated how many were saved.  On any error, just return -1.
 * Else return 0.  Incorporate the any new mail that we found */
static int  writeback(FILE *res, FILE *obuf);

/* Terminate an editing session by attempting to write out the user's file from
 * the temporary.  Save any new stuff appended to the file */
static boole edstop(void);

static boole
a_quit_holdbits(void){
   struct message *mp;
   int holdbit, nohold, mf;
   boole rv, autohold;
   NYD2_IN;

   rv = FAL0;
   autohold = ok_blook(hold);
   holdbit = autohold ? MPRESERVE : MBOX;
   nohold = MBOX | MSAVED | MDELETED | MPRESERVE;
   if(ok_blook(keepsave))
      nohold ^= MSAVED;

   for(mp = message; PCMP(mp, <, &message[msgCount]); ++mp){
      mf = mp->m_flag;
      if(mf & MNEW){
         mf &= ~MNEW;
         mf |= MSTATUS;
      }
      if(!(mf & MTOUCH))
         mf |= MPRESERVE;
      if(!(mf & nohold))
         mf |= holdbit;
      mp->m_flag = mf;

      if(mf & (MSTATUS | MFLAG | MUNFLAG | MANSWER | MUNANSWER | MDRAFT | MUNDRAFT))
         rv = TRU1;
   }

   NYD2_OU;
   return rv;
}

static int
writeback(FILE *res, FILE *obuf){ /* TODO errors */
   struct message *mp;
   uz p;
   int rv, c;
   NYD_IN;

   rv = -1;

   if(fseek(obuf, 0L, SEEK_SET) == -1)
      goto jleave;

   su_mem_bag_auto_relax_create(su_MEM_BAG_SELF);
   for(p = 0, mp = message; PCMP(mp, <, &message[msgCount]); ++mp){
      if((mp->m_flag & MPRESERVE) || !(mp->m_flag & MTOUCH)){
         ++p;
         if(sendmp(mp, obuf, NIL, NIL, SEND_MBOX, NIL, NIL) < 0){
            n_perr(mailname, 0);
            su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);
            goto jerror;
         }
         su_mem_bag_auto_relax_unroll(su_MEM_BAG_SELF);
      }
   }
   su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);

   if(res != NIL){
      boole lastnl;

      for(lastnl = FAL0; (c = getc(res)) != EOF && putc(c, obuf) != EOF;)
         lastnl = (c == '\n') ? (lastnl ? TRU2 : TRU1) : FAL0;
      if(lastnl != TRU2)
         putc('\n', obuf);
   }

   ftrunc(obuf);
   if(ferror(obuf)){
      n_perr(mailname, su_err_by_errno());
jerror:
      fseek(obuf, 0L, SEEK_SET);
      goto jleave;
   }
   if(fseek(obuf, 0L, SEEK_SET) == -1)
      goto jleave;

   su_path_touch(mailname, NIL);

   if(p > 0)
      fprintf(n_stdout, _("Held %" PRIuZ " %s in %s\n"),
         p, (p == 1 ? _("message") : _("messages")), displayname);

   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

static boole
edstop(void) /* TODO oh my god */
{
   struct su_pathinfo pi;
   int gotcha, c;
   struct message *mp;
   FILE *obuf = NULL, *ibuf = NULL;
   BITENUM_IS(u32,mx_fs_open_state) fs;
   boole rv;
   NYD_IN;

   rv = TRU1;

   if (mb.mb_perm == 0)
      goto j_leave;

   for (mp = message, gotcha = 0; PCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MNEW) {
         mp->m_flag &= ~MNEW;
         mp->m_flag |= MSTATUS;
      }
      if (mp->m_flag & (MODIFY | MDELETED | MSTATUS | MFLAG | MUNFLAG |
            MANSWER | MUNANSWER | MDRAFT | MUNDRAFT))
         ++gotcha;
   }
   if (!gotcha)
      goto jleave;

   rv = FAL0;

   /* TODO This is too simple minded?  We should regenerate an index file
    * TODO to be able to truly tell whether *anything* has changed!
    * TODO (Or better: only come here.. then!  It is an *object method!* */
   /* TODO Ignoring stat error is easy, huh? */
   if(su_pathinfo_stat(&pi, mailname) && UCMP(64, pi.pi_size, >, mailsize)){
      if((obuf = mx_fs_tmp_open(NIL, "edstop", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
               NIL)) == NIL){
         n_perr(_("tmpfile"), 0);
         goto jleave;
      }
      if((ibuf = mx_fs_open_any(mailname, mx_FS_O_RDONLY, NIL)) == NIL){
jemailname:
         n_perr(mailname, 0);
         goto jleave;
      }

      if(!mx_file_lock(fileno(ibuf), (mx_FILE_LOCK_MODE_TSHARE |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG)))
         goto jemailname;
      if(fseek(ibuf, (long)mailsize, SEEK_SET) == -1)
         goto jemailname;
      while((c = getc(ibuf)) != EOF){ /* xxx bytewise??? */
         if(putc(c, obuf) == EOF)
            goto jemailname;
      }
      if(ferror(ibuf))
         goto jemailname;
      mx_fs_close(ibuf);
      ibuf = obuf;
      fflush_rewind(obuf);
      /*obuf = NIL;*/
   }

   fprintf(n_stdout, _("%s "), n_shexp_quote_cp(displayname, FAL0));
   fflush(n_stdout);

   if((obuf = mx_fs_open_any(mailname, mx_FS_O_RDWR, &fs)) == NIL ||
         !mx_file_lock(fileno(obuf), (mx_FILE_LOCK_MODE_TSHARE |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
      int e;

      e = su_err();
      n_perr(n_shexp_quote_cp(mailname, FAL0), e);
      goto jleave;
   }
   ftrunc(obuf);

   su_mem_bag_auto_relax_create(su_MEM_BAG_SELF);
   c = 0;
   for(mp = message; mp < &message[msgCount]; ++mp){
      if((mp->m_flag & MDELETED) || !(mp->m_flag & MVALID))
         continue;
      ++c;
      if(sendmp(mp, obuf, NIL, NIL, SEND_MBOX, NIL, NIL) < 0){
         su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);
         n_err(_("Failed to finalize %s\n"), n_shexp_quote_cp(mailname, FAL0));
         goto jleave;
      }
      su_mem_bag_auto_relax_unroll(su_MEM_BAG_SELF);
   }
   su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);

   gotcha = (c == 0 && ibuf == NULL);
   if (ibuf != NULL) {
      boole lastnl;

      for(lastnl = FAL0; (c = getc(ibuf)) != EOF && putc(c, obuf) != EOF;)
         lastnl = (c == '\n') ? (lastnl ? TRU2 : TRU1) : FAL0;
      if(lastnl != TRU2 && (fs & n_PROTO_MASK) == n_PROTO_FILE)
         putc('\n', obuf);
   }
   /* May nonetheless be a broken MBOX TODO really: VFS, object KNOWS!! */
   else if(!gotcha && (fs & n_PROTO_MASK) == n_PROTO_FILE)
      n_folder_mbox_prepare_append(obuf, TRU1, NIL);
   fflush(obuf);
   if (ferror(obuf)) {
      n_err(_("Failed to finalize %s\n"), n_shexp_quote_cp(mailname, FAL0));
      goto jleave;
   }

   if(gotcha){
      /* Non-system boxes are never removed except forced via POSIX mode */
#ifdef mx_HAVE_FTRUNCATE
      ftruncate(fileno(obuf), 0);
#else
      s32 fd;

      if((fd = mx_fs_open_fd(mailname, (mx_FS_O_WRONLY | mx_FS_O_CREATE |
               mx_FS_O_TRUNC | mx_FS_O_NOCLOEXEC), 0600)) != -1)
         close(fd);
#endif

      if(ok_blook(posix) && !ok_blook(keep) && su_path_rm(mailname))
         fputs(_("removed\n"), n_stdout);
      else
         fputs(_("truncated\n"), n_stdout);
   } else
      fputs((ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         ? _("complete\n") : _("updated.\n"), n_stdout);
   fflush(n_stdout);

   rv = TRU1;
jleave:
   if(obuf != NIL)
      mx_fs_close(obuf);
   if(ibuf != NIL)
      mx_fs_close(ibuf);
   if(!rv){
      /* TODO The codebase aborted by jumping to the main loop here.
       * TODO The OpenBSD mailx simply ignores this error.
       * TODO For now we follow the latter unless we are interactive,
       * TODO in which case we ask the user whether the error is to be
       * TODO ignored or not.  More of this around here in this file! */
      rv = mx_tty_yesorno(_("Continue, possibly losing changes"), TRU1);
   }
j_leave:
   NYD_OU;
   return rv;
}

FL boole
quit(boole hold_sigs_on)
{
   struct su_pathinfo pi;
   int p, modify, c;
   FILE *fbuf, *lckfp, *rbuf, *abuf;
   struct message *mp;
   boole rv, anystat;
   NYD_IN;

   if(!hold_sigs_on)
      hold_sigs();

   rv = FAL0;
   fbuf = lckfp = rbuf = NIL;

   if(mb.mb_digmsg != NIL)
      mx_dig_msg_on_mailbox_close(&mb);

   mx_temporary_on_mailbox_event(mx_ON_MAILBOX_EVENT_CLOSE);

   /* If we are read only, we can't do anything, so just return quickly */
   /* TODO yet we cannot return quickly if resources have to be released!
    * TODO somewhen it'll be mailbox->quit() anyway, for now do it by hand
    *if (mb.mb_perm == 0)
    *   goto jleave;*/
   p = (mb.mb_perm == 0);

   switch (mb.mb_type) {
   case MB_FILE:
      break;
#ifdef mx_HAVE_MAILDIR
   case MB_MAILDIR:
      rv = maildir_quit(TRU1);
      goto jleave;
#endif
#ifdef mx_HAVE_POP3
   case MB_POP3:
      rv = mx_pop3_quit(TRU1);
      goto jleave;
#endif
#ifdef mx_HAVE_IMAP
   case MB_IMAP:
   case MB_CACHE:
      rv = imap_quit(TRU1);
      goto jleave;
#endif
   case MB_VOID:
      rv = TRU1;
      /* FALLTHRU */
   default:
      goto jleave;
   }
   if (p) {
      rv = TRU1;
      goto jleave; /* TODO */
   }

   /* If editing (not reading system mail box), then do the work in edstop() */
   if (n_pstate & n_PS_EDIT) {
      rv = edstop();
      goto jleave;
   }

   /* See if there any messages to save in mbox.  If no, we
    * can save copying mbox to /tmp and back.
    *
    * Check also to see if any files need to be preserved.
    * Delete all untouched messages to keep them out of mbox.
    * If all the messages are to be preserved, just exit with
    * a message */
   fbuf = mx_fs_open_any(mailname, mx_FS_O_RDWR, NIL);
   if(fbuf == NIL){
      if(su_err() != su_ERR_NOENT)
         fprintf(n_stdout, _("Thou hast new mail.\n"));
      rv = TRU1;
      goto jleave;
   }

   if((lckfp = mx_file_dotlock(mailname, fileno(fbuf),
         (mx_FILE_LOCK_MODE_TEXCL | mx_FILE_LOCK_MODE_RETRY))) == NIL){
      n_perr(_("Unable to (dot) lock mailbox"), 0);
      mx_fs_close(fbuf);
      fbuf = NIL;
      rv = mx_tty_yesorno(_("Continue, possibly losing changes"), TRU1);
      goto jleave;
   }

   rbuf = NULL;
   if(su_pathinfo_fstat(&pi, fileno(fbuf)) && UCMP(64, pi.pi_size, >, mailsize)){
      boole lastnl;

      fprintf(n_stdout, _("New mail has arrived.\n"));
      rbuf = mx_fs_tmp_open(NIL, "quit", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL);
      if(rbuf == NIL){
jerbuf:
         n_perr(_("temporary MBOX creation"), 0);
         rv = mx_tty_yesorno(_("Continue, possibly losing changes"), TRU1);
         goto jleave;
      }
      if(fseek(fbuf, (long)mailsize, SEEK_SET) == -1)
         goto jerbuf;
      for(lastnl = FAL0; (c = getc(fbuf)) != EOF && putc(c, rbuf) != EOF;)
         lastnl = (c == '\n') ? (lastnl ? TRU2 : TRU1) : FAL0;
      if(lastnl != TRU2)
         putc('\n', rbuf);
      if(ferror(fbuf) || ferror(rbuf))
         goto jerbuf;
      fflush_rewind(rbuf);
   }

   anystat = a_quit_holdbits();
   modify = 0;
   for(c = 0, p = 0, mp = message; PCMP(mp, <, &message[msgCount]); ++mp){
      int mf = mp->m_flag;
      if(mf & MBOX)
         ++c;
      if(mf & MPRESERVE)
         ++p;
      if(mf & MODIFY)
         ++modify;
   }
   if(p == msgCount && !modify && !anystat){
      if(p > 0)
         fprintf(n_stdout, _("Held %d %s in %s\n"),
            p, (p == 1 ? _("message") : _("messages")), displayname);
      rv = TRU1;
      goto jleave;
   }

   if (c == 0) {
      if (p != 0) {
         if (writeback(rbuf, fbuf) >= 0)
            rv = TRU1;
         else
            rv = mx_tty_yesorno(_("Continue, possibly losing changes"), TRU1);
         goto jleave;
      }
      goto jcream;
   }

   if(!mx_quit_automove_mbox(FAL0)){
      rv = mx_tty_yesorno(_("Continue, possibly losing changes"), TRU1);
      goto jleave;
   }

   /* Now we are ready to copy back preserved files to the system mailbox, if
    * any were requested */
   if (p != 0) {
      if (writeback(rbuf, fbuf) < 0)
         rv = mx_tty_yesorno(_("Continue, possibly losing changes"), TRU1);
      goto jleave;
   }

   /* Finally, remove his file.  If new mail has arrived, copy it back */
jcream:
   if (rbuf != NULL) {
      abuf = fbuf;
      fseek(abuf, 0L, SEEK_SET);
      while ((c = getc(rbuf)) != EOF)
         putc(c, abuf);
      ftrunc(abuf);
      su_path_touch(mailname, NIL);
      rv = TRU1;
   } else {
#ifdef mx_HAVE_FTRUNCATE
      ftruncate(fileno(fbuf), 0);
#else
      s32 fd;

      if((fd = mx_fs_open_fd(mailname, (mx_FS_O_WRONLY | mx_FS_O_CREATE |
               mx_FS_O_TRUNC | mx_FS_O_NOCLOEXEC), 0600)) != -1)
         close(fd);
#endif
      if(!ok_blook(keep))
         su_path_rm(mailname);
      rv = TRU1;
   }

jleave:
   if(rbuf != NIL)
      mx_fs_close(rbuf);
   if(fbuf != NIL){
      mx_fs_close(fbuf);
      if(lckfp != NIL && lckfp != R(FILE*,-1))
         mx_fs_pipe_close(lckfp, FAL0);
   }

   if(!hold_sigs_on)
      rele_sigs();

   NYD_OU;
   return rv;
}

FL boole
mx_quit_automove_mbox(boole need_stat_verify){
   FILE *obuf;
   enum mx_fs_open_state fs;
   uz mcount;
   char *mbox;
   struct message *mp;
   boole rv;
   NYD_IN;

   if(need_stat_verify){
      rv = TRU1;

      if(mb.mb_perm == 0 || (n_pstate & n_PS_EDIT))
         goto jleave;

      a_quit_holdbits();

      for(mp = message;; ++mp){
         if(PCMP(mp, ==, &message[msgCount]))
            goto jleave;
         if(mp->m_flag & MBOX)
            break;
      }
   }

   rv = FAL0;
   mbox = _mboxname;
   mcount = 0;

   if((obuf = mx_fs_open_any(mbox, (mx_FS_O_RDWR | mx_FS_O_APPEND |
            mx_FS_O_CREATE | mx_FS_O_CREATE_0600 | mx_FS_O_EXACT_MESSAGE_STATE_REFLECTION), &fs)) == NIL){
      n_perr(mbox, 0);
      goto jleave;
   }

   if((fs & n_PROTO_MASK) == n_PROTO_FILE)
      n_folder_mbox_prepare_append(obuf, FAL0, NIL);

   su_mem_bag_auto_relax_create(su_MEM_BAG_SELF);
   for(mp = message; PCMP(mp, <, &message[msgCount]); ++mp){
      if(mp->m_flag & MBOX){
         ++mcount;
#ifdef mx_HAVE_IMAP
         if((fs & n_PROTO_MASK) == n_PROTO_IMAP &&
               !mx_ignore_is_any(mx_IGNORE_SAVE) && imap_thisaccount(mbox)){
            if(imap_copy(mp, P2UZ(mp - message + 1), mbox) == STOP)
               goto jcopyerr;
         }else
#endif
         if(sendmp(mp, obuf, mx_IGNORE_SAVE, NIL, SEND_MBOX, NIL, NIL) < 0){
#ifdef mx_HAVE_IMAP
jcopyerr:
#endif
            n_perr(mbox, 0);
            su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);
            mx_fs_close(obuf);
            goto jleave;
         }

         mp->m_flag |= MBOXED;
         su_mem_bag_auto_relax_unroll(su_MEM_BAG_SELF);
      }
   }
   su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);

   ftrunc(obuf); /* XXX clears error, order.. */
   if(ferror(obuf)){
      n_perr(mbox, su_err_by_errno());
      mx_fs_close(obuf);
      goto jleave;
   }

   if(!mx_fs_close(obuf)){
#ifdef mx_HAVE_IMAP
      if((fs & n_PROTO_MASK) != n_PROTO_IMAP)
#endif
         n_perr(mbox, 0);
      goto jleave;
   }

   if(su_state_has(su_STATE_REPRODUCIBLE))
      mbox = n_filename_to_repro(mbox);
   mbox = n_shexp_quote_cp(mbox, FAL0);
   fprintf(n_stdout, _("Saved %" PRIuZ " %s in MBOX=%s\n"),
      mcount, (mcount == 1 ? _("message") : _("messages")), mbox);

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
}

FL void
save_mbox_for_possible_quitstuff(void){ /* TODO try to get rid of that */
   char const *cp;
   NYD2_IN;

   if((cp = fexpand("&", FEXP_NVAR)) == NULL)
      cp = n_empty;
   su_cs_pcopy_n(_mboxname, cp, sizeof _mboxname);
   NYD2_OU;
}

FL int
savequitflags(void)
{
   enum quitflags qf = 0;
   uz i;
   NYD_IN;

   for (i = 0; i < NELEM(_quitnames); ++i)
      if (n_var_oklook(_quitnames[i].okey) != NULL)
         qf |= _quitnames[i].flag;
   NYD_OU;
   return qf;
}

FL void
restorequitflags(int qf)
{
   uz i;
   NYD_IN;

   for (i = 0;  i < NELEM(_quitnames); ++i) {
      char *x = n_var_oklook(_quitnames[i].okey);
      if (qf & _quitnames[i].flag) {
         if (x == NULL)
            n_var_okset(_quitnames[i].okey, TRU1);
      } else if (x != NULL)
         n_var_okclear(_quitnames[i].okey);
   }
   NYD_OU;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_QUIT
/* s-it-mode */
