/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Termination processing. TODO MBOX -> VFS; error handling: catastrophe!
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
#define su_FILE quit
#define mx_SOURCE
#define mx_SOURCE_QUIT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <utime.h>

#include "mx/compat.h"
#include "mx/dig-msg.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/ignore.h"
#include "mx/net-pop3.h"
#include "mx/sigs.h"
#include "mx/tty.h"

#include <su/cs.h>

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum quitflags {
   QUITFLAG_HOLD      = 1<<0,
   QUITFLAG_KEEP      = 1<<1,
   QUITFLAG_KEEPSAVE  = 1<<2,
   QUITFLAG_APPEND    = 1<<3
};

struct quitnames {
   enum quitflags flag;
   enum okeys     okey;
};

static struct quitnames const _quitnames[] = {
   {QUITFLAG_HOLD, ok_b_hold},
   {QUITFLAG_KEEP, ok_b_keep},
   {QUITFLAG_KEEPSAVE, ok_b_keepsave},
   {QUITFLAG_APPEND, ok_b_append}
};

static char _mboxname[PATH_MAX];  /* Name of mbox */

/* Touch the indicated file */
static void _alter(char const *name);

/* Preserve all the appropriate messages back in the system mailbox, and print
 * a nice message indicated how many were saved.  On any error, just return -1.
 * Else return 0.  Incorporate the any new mail that we found */
static int  writeback(FILE *res, FILE *obuf);

/* Terminate an editing session by attempting to write out the user's file from
 * the temporary.  Save any new stuff appended to the file */
static boole edstop(void);

static void
_alter(char const *name) /* TODO error handling */
{
#ifdef mx_HAVE_UTIMENSAT
   struct timespec tsa[2];
#else
   struct stat sb;
   struct utimbuf utb;
#endif
   struct n_timespec const *tsp;
   NYD_IN;

   tsp = n_time_now(TRU1); /* TODO -> eventloop */

#ifdef mx_HAVE_UTIMENSAT
   tsa[0].tv_sec = tsp->ts_sec + 1;
   tsa[0].tv_nsec = tsp->ts_nsec;
   tsa[1].tv_nsec = UTIME_OMIT;
   utimensat(AT_FDCWD, name, tsa, 0);
#else
   if (!stat(name, &sb)) {
      utb.actime = tsp->ts_sec;
      utb.modtime = sb.st_mtime;
      utime(name, &utb);
   }
#endif
   NYD_OU;
}

static int
writeback(FILE *res, FILE *obuf) /* TODO errors */
{
   struct message *mp;
   int rv = -1, p, c;
   NYD_IN;

   if (fseek(obuf, 0L, SEEK_SET) == -1)
      goto jleave;

   srelax_hold();
   for (p = 0, mp = message; PCMP(mp, <, message + msgCount); ++mp)
      if ((mp->m_flag & MPRESERVE) || !(mp->m_flag & MTOUCH)) {
         ++p;
         if (sendmp(mp, obuf, NULL, NULL, SEND_MBOX, NULL) < 0) {
            n_perr(mailname, 0);
            srelax_rele();
            goto jerror;
         }
         srelax();
      }
   srelax_rele();

   if(res != NULL){
      boole lastnl;

      for(lastnl = FAL0; (c = getc(res)) != EOF && putc(c, obuf) != EOF;)
         lastnl = (c == '\n') ? (lastnl ? TRU2 : TRU1) : FAL0;
      if(lastnl != TRU2)
         putc('\n', obuf);
   }
   ftrunc(obuf);

   if (ferror(obuf)) {
      n_perr(mailname, 0);
jerror:
      fseek(obuf, 0L, SEEK_SET);
      goto jleave;
   }
   if (fseek(obuf, 0L, SEEK_SET) == -1)
      goto jleave;

   _alter(mailname);
   if (p == 1)
      fprintf(n_stdout, _("Held 1 message in %s\n"), displayname);
   else
      fprintf(n_stdout, _("Held %d messages in %s\n"), p, displayname);
   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

static boole
edstop(void) /* TODO oh my god */
{
   int gotcha, c;
   struct message *mp;
   FILE *obuf = NULL, *ibuf = NULL;
   struct stat statb;
   enum mx_fs_open_state fs;
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
   if(!stat(mailname, &statb) && statb.st_size > mailsize){
      if((obuf = mx_fs_tmp_open(NIL, "edstop", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
               mx_FS_O_REGISTER), NIL)) == NIL){
         n_perr(_("tmpfile"), 0);
         goto jleave;
      }
      if((ibuf = mx_fs_open_any(mailname, "r", NIL)) == NIL){
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

   if((obuf = mx_fs_open_any(mailname, "r+", &fs)) == NIL ||
         !mx_file_lock(fileno(obuf), (mx_FILE_LOCK_MODE_TSHARE |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
      int e;

      e = su_err_no();
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
      if(sendmp(mp, obuf, NIL, NIL, SEND_MBOX, NIL) < 0){
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
      n_folder_mbox_prepare_append(obuf, NULL);
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
      int fd;

      if((fd = open(mailname, (O_WRONLY | O_CREAT | n_O_NOXY_BITS | O_TRUNC),
            0600)) != -1)
         close(fd);
#endif

      if(ok_blook(posix) && !ok_blook(keep) && n_path_rm(mailname))
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
   int p, modify, anystat, c;
   FILE *fbuf, *lckfp, *rbuf, *abuf;
   struct message *mp;
   struct stat minfo;
   boole rv;
   NYD_IN;

   if(!hold_sigs_on)
      hold_sigs();

   rv = FAL0;
   fbuf = lckfp = rbuf = NIL;
   if(mb.mb_digmsg != NIL)
      mx_dig_msg_on_mailbox_close(&mb);
   temporary_folder_hook_unroll();

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
   fbuf = mx_fs_open_any(mailname, "r+", NIL);
   if(fbuf == NIL){
      if(su_err_no() != su_ERR_NOENT)
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
   if (!fstat(fileno(fbuf), &minfo) && minfo.st_size > mailsize) {
      boole lastnl;

      fprintf(n_stdout, _("New mail has arrived.\n"));
      rbuf = mx_fs_tmp_open(NIL, "quit", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
               mx_FS_O_REGISTER), NIL);
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

   anystat = holdbits();
   modify = 0;
   for (c = 0, p = 0, mp = message; PCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MBOX)
         c++;
      if (mp->m_flag & MPRESERVE)
         p++;
      if (mp->m_flag & MODIFY)
         modify++;
   }
   if (p == msgCount && !modify && !anystat) {
      rv = TRU1;
      if (p == 1)
         fprintf(n_stdout, _("Held 1 message in %s\n"), displayname);
      else if (p > 1)
         fprintf(n_stdout, _("Held %d messages in %s\n"), p, displayname);
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

   if (makembox() == STOP) {
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
      _alter(mailname);
      rv = TRU1;
   } else {
#ifdef mx_HAVE_FTRUNCATE
      ftruncate(fileno(fbuf), 0);
#else
      int fd;

      if((fd = open(mailname, (O_WRONLY | O_CREAT | n_O_NOXY_BITS | O_TRUNC),
            0600)) != -1)
         close(fd);
#endif
      if(!ok_blook(keep))
         n_path_rm(mailname);
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

FL int
holdbits(void)
{
   struct message *mp;
   int anystat, autohold, holdbit, nohold;
   NYD_IN;

   anystat = 0;
   autohold = ok_blook(hold);
   holdbit = autohold ? MPRESERVE : MBOX;
   nohold = MBOX | MSAVED | MDELETED | MPRESERVE;
   if (ok_blook(keepsave))
      nohold &= ~MSAVED;
   for (mp = message; PCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MNEW) {
         mp->m_flag &= ~MNEW;
         mp->m_flag |= MSTATUS;
      }
      if (mp->m_flag & (MSTATUS | MFLAG | MUNFLAG | MANSWER | MUNANSWER |
            MDRAFT | MUNDRAFT))
         ++anystat;
      if (!(mp->m_flag & MTOUCH))
         mp->m_flag |= MPRESERVE;
      if (!(mp->m_flag & nohold))
         mp->m_flag |= holdbit;
   }
   NYD_OU;
   return anystat;
}

FL enum okay
makembox(void) /* TODO oh my god (also error reporting) */
{
   struct message *mp;
   char *mbox;
   int mcount, c;
   FILE *ibuf = NULL, *obuf, *abuf;
   enum mx_fs_open_state fs;
   enum okay rv = STOP;
   NYD_IN;

   mbox = _mboxname;
   mcount = 0;
   if(ok_blook(append)){
      if((obuf = mx_fs_open_any(mbox, "a+", &fs)) == NIL){
         n_perr(mbox, 0);
         goto jleave;
      }
      if((fs & n_PROTO_MASK) == n_PROTO_FILE)
         n_folder_mbox_prepare_append(obuf, NULL);
   }else{
      struct mx_fs_tmp_ctx *fstcp;

      if((obuf = mx_fs_tmp_open(NIL, "makembox", (mx_FS_O_WRONLY |
               mx_FS_O_HOLDSIGS | mx_FS_O_REGISTER), &fstcp)) == NIL){
         n_perr(_("creation of temporary mail quit file"), 0);
         goto jleave;
      }
      if((ibuf = mx_fs_open(fstcp->fstc_filename, "r")) == NIL)
         n_perr(fstcp->fstc_filename, 0);
      mx_fs_tmp_release(fstcp);
      if(ibuf == NIL){
         mx_fs_close(obuf);
         goto jleave;
      }

      if((abuf = mx_fs_open_any(mbox, "r", &fs)) != NIL){
         boole lastnl;

         for (lastnl = FAL0; (c = getc(abuf)) != EOF && putc(c, obuf) != EOF;)
            lastnl = (c == '\n') ? (lastnl ? TRU2 : TRU1) : FAL0;
         if(lastnl != TRU2 && (fs & n_PROTO_MASK) == n_PROTO_FILE)
            putc('\n', obuf);

         mx_fs_close(abuf);
      }
      if(ferror(obuf)){
         n_perr(_("temporary mail quit file"), 0);
         mx_fs_close(ibuf);
         mx_fs_close(obuf);
         goto jleave;
      }
      mx_fs_close(obuf);

      if((c = open(mbox, (O_WRONLY | O_CREAT | mx_O_NOXY_BITS | O_TRUNC), 0666)
            ) != -1)
         close(c);
      if((obuf = mx_fs_open_any(mbox, "r+", &fs)) == NIL){
         n_perr(mbox, 0);
         mx_fs_close(ibuf);
         goto jleave;
      }
   }

   srelax_hold();
   for (mp = message; PCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MBOX) {
         ++mcount;
#ifdef mx_HAVE_IMAP
         if((fs & n_PROTO_MASK) == n_PROTO_IMAP &&
               !mx_ignore_is_any(mx_IGNORE_SAVE) && imap_thisaccount(mbox)){
            if(imap_copy(mp, P2UZ(mp - message + 1), mbox) == STOP)
               goto jcopyerr;
         }else
#endif
         if(sendmp(mp, obuf, mx_IGNORE_SAVE, NIL, SEND_MBOX, NIL) < 0){
#ifdef mx_HAVE_IMAP
jcopyerr:
#endif
            n_perr(mbox, 0);
            srelax_rele();
            if(ibuf != NIL)
               mx_fs_close(ibuf);
            mx_fs_close(obuf);
            goto jleave;
         }
         mp->m_flag |= MBOXED;
         srelax();
      }
   }
   srelax_rele();

   /* Copy the user's old mbox contents back to the end of the stuff we just
    * saved.  If we are appending, this is unnecessary */
   if (!ok_blook(append)) {
      boole lastnl;

      rewind(ibuf);
      for(lastnl = FAL0; (c = getc(ibuf)) != EOF && putc(c, obuf) != EOF;)
         lastnl = (c == '\n') ? (lastnl ? TRU2 : TRU1) : FAL0;
      if(lastnl != TRU2 && (fs & n_PROTO_MASK) == n_PROTO_FILE)
         putc('\n', obuf);
      mx_fs_close(ibuf);
      fflush(obuf);
   }
   ftrunc(obuf);
   if(ferror(obuf)){
      n_perr(mbox, 0);
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
   if (mcount == 1)
      fprintf(n_stdout, _("Saved 1 message in mbox\n"));
   else
      fprintf(n_stdout, _("Saved %d messages in mbox\n"), mcount);
   rv = OKAY;
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
