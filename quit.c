/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Termination processing.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <utime.h>

enum quitflags {
   QUITFLAG_HOLD      = 001,
   QUITFLAG_KEEPSAVE  = 002,
   QUITFLAG_APPEND    = 004,
   QUITFLAG_EMPTYBOX  = 010
};

struct quitnames {
   enum quitflags flag;
   enum okeys     okey;
};

static struct quitnames const _quitnames[] = {
   {QUITFLAG_HOLD, ok_b_hold},
   {QUITFLAG_KEEPSAVE, ok_b_keepsave},
   {QUITFLAG_APPEND, ok_b_append},
   {QUITFLAG_EMPTYBOX, ok_b_emptybox}
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
static void edstop(void);

static void
_alter(char const *name)
{
   struct stat sb;
   struct utimbuf utb;
   NYD_ENTER;

   if (!stat(name, &sb)) {
      utb.actime = time(NULL) + 1;
      utb.modtime = sb.st_mtime;
      utime(name, &utb);
   }
   NYD_LEAVE;
}

static int
writeback(FILE *res, FILE *obuf)
{
   struct message *mp;
   int rv = -1, p, c;
   NYD_ENTER;

   if (fseek(obuf, 0L, SEEK_SET) == -1)
      goto jleave;

#ifndef APPEND
   if (res != NULL)
      while ((c = getc(res)) != EOF)
         putc(c, obuf);
#endif
   srelax_hold();
   for (p = 0, mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
      if ((mp->m_flag & MPRESERVE) || !(mp->m_flag & MTOUCH)) {
         ++p;
         if (sendmp(mp, obuf, NULL, NULL, SEND_MBOX, NULL) < 0) {
            srelax_rele();
            goto jerror;
         }
         srelax();
      }
   srelax_rele();
#ifdef APPEND
   if (res != NULL)
      while ((c = getc(res)) != EOF)
         putc(c, obuf);
#endif
   ftrunc(obuf);

   if (ferror(obuf)) {
jerror:
      perror(mailname);
      fseek(obuf, 0L, SEEK_SET);
      goto jleave;
   }
   if (fseek(obuf, 0L, SEEK_SET) == -1)
      goto jleave;

   _alter(mailname);
   if (p == 1)
      printf(_("Held 1 message in %s\n"), displayname);
   else
      printf(_("Held %d messages in %s\n"), p, displayname);
   rv = 0;
jleave:
   if (res != NULL)
      Fclose(res);
   NYD_LEAVE;
   return rv;
}

static void
edstop(void) /* TODO oh my god - and REMOVE that CRAPPY reset(0) jump!! */
{
   int gotcha, c;
   struct message *mp;
   FILE *obuf = NULL, *ibuf = NULL;
   struct stat statb;
   bool_t doreset;
   NYD_ENTER;

   hold_sigs();
   doreset = FAL0;

   if (mb.mb_perm == 0)
      goto jleave;

   for (mp = message, gotcha = 0; PTRCMP(mp, <, message + msgCount); ++mp) {
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

   doreset = TRU1;

   if (!stat(mailname, &statb) && statb.st_size > mailsize) {
      if ((obuf = Ftmp(NULL, "edstop", OF_RDWR | OF_UNLINK | OF_REGISTER,
            0600)) == NULL) {
         perror(_("tmpfile"));
         goto jleave;
      }
      if ((ibuf = Zopen(mailname, "r", &mb.mb_compressed)) == NULL) {
         perror(mailname);
         Fclose(obuf);
         goto jleave;
      }
      fseek(ibuf, (long)mailsize, SEEK_SET);
      while ((c = getc(ibuf)) != EOF)
         putc(c, obuf);
      Fclose(ibuf);
      ibuf = obuf;
      fflush_rewind(obuf);
   }

   printf(_("\"%s\" "), displayname);
   fflush(stdout);
   if ((obuf = Zopen(mailname, "r+", &mb.mb_compressed)) == NULL) {
      perror(mailname);
      goto jleave;
   }
   ftrunc(obuf);

   srelax_hold();
   c = 0;
   for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MDELETED)
         continue;
      ++c;
      if (sendmp(mp, obuf, NULL, NULL, SEND_MBOX, NULL) < 0) {
         perror(mailname);
         srelax_rele();
         goto jleave;
      }
      srelax();
   }
   srelax_rele();

   gotcha = (c == 0 && ibuf == NULL);
   if (ibuf != NULL) {
      while ((c = getc(ibuf)) != EOF)
         putc(c, obuf);
   }
   fflush(obuf);
   if (ferror(obuf)) {
      perror(mailname);
      goto jleave;
   }
   Fclose(obuf);

   doreset = FAL0;

   if (gotcha && !ok_blook(emptybox)) {
      rm(mailname);
      printf((ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         ? _("removed\n") : _("removed.\n"));
   } else
      printf((ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         ? _("complete\n") : _("updated.\n"));
   fflush(stdout);
jleave:
   if (ibuf != NULL)
      Fclose(ibuf);
   rele_sigs();
   NYD_LEAVE;
   if (doreset)
      reset(0);
}

FL int
c_quit(void *v)
{
   int rv;
   NYD_ENTER;
   UNUSED(v);

   /* If we are sourcing, then return 1 so evaluate() can handle it.
    * Otherwise return -1 to abort command loop */
   rv = sourcing ? 1 : -1;
   NYD_LEAVE;
   return rv;
}

FL void
quit(void)
{
   int p, modify, anystat, c;
   FILE *fbuf = NULL, *rbuf, *abuf;
   struct message *mp;
   char *tempResid;
   struct stat minfo;
   NYD_ENTER;

   temporary_localopts_folder_hook_unroll();

   /* If we are read only, we can't do anything, so just return quickly. IMAP
    * can set some flags (e.g. "\\Seen") so imap_quit must be called anyway */
   if (mb.mb_perm == 0 && mb.mb_type != MB_IMAP)
      goto jleave;

   /* TODO lex.c:setfile() has just called hold_sigs(); before it called
    * TODO us, but this causes uninterruptible hangs due to blocked sigs
    * TODO anywhere except for MB_FILE (all others install their own
    * TODO handlers, as it seems, properly); marked YYY */
   switch (mb.mb_type) {
   case MB_FILE:
      break;
   case MB_MAILDIR:
      rele_sigs(); /* YYY */
      maildir_quit();
      hold_sigs(); /* YYY */
      goto jleave;
#ifdef HAVE_POP3
   case MB_POP3:
      rele_sigs(); /* YYY */
      pop3_quit();
      hold_sigs(); /* YYY */
      goto jleave;
#endif
#ifdef HAVE_IMAP
   case MB_IMAP:
   case MB_CACHE:
      rele_sigs(); /* YYY */
      imap_quit();
      hold_sigs(); /* YYY */
      goto jleave;
#endif
   case MB_VOID:
   default:
      goto jleave;
   }

   /* If editing (not reading system mail box), then do the work in edstop() */
   if (edit) {
      edstop();
      goto jleave;
   }

   /* See if there any messages to save in mbox.  If no, we
    * can save copying mbox to /tmp and back.
    *
    * Check also to see if any files need to be preserved.
    * Delete all untouched messages to keep them out of mbox.
    * If all the messages are to be preserved, just exit with
    * a message */
   fbuf = Zopen(mailname, "r+", &mb.mb_compressed);
   if (fbuf == NULL) {
      if (errno == ENOENT)
         goto jleave;
jnewmail:
      printf(_("Thou hast new mail.\n"));
      goto jleave;
   }

   if (fcntl_lock(fileno(fbuf), FLOCK_WRITE) == -1 ||
         dot_lock(mailname, fileno(fbuf), 1, stdout, ".") == -1) {
      perror(_("Unable to lock mailbox"));
      Fclose(fbuf);
      fbuf = NULL;
      goto jleave;
   }

   rbuf = NULL;
   if (!fstat(fileno(fbuf), &minfo) && minfo.st_size > mailsize) {
      printf(_("New mail has arrived.\n"));
      rbuf = Ftmp(&tempResid, "quit", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600);
      if (rbuf == NULL || fbuf == NULL)
         goto jnewmail;
#ifdef APPEND
      fseek(fbuf, (long)mailsize, SEEK_SET);
      while ((c = getc(fbuf)) != EOF)
         putc(c, rbuf);
#else
      p = minfo.st_size - mailsize;
      while (p-- > 0) {
         c = getc(fbuf);
         if (c == EOF)
            goto jnewmail;
         putc(c, rbuf);
      }
#endif
      fflush_rewind(rbuf);
   }

   anystat = holdbits();
   modify = 0;
   for (c = 0, p = 0, mp = message; PTRCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MBOX)
         c++;
      if (mp->m_flag & MPRESERVE)
         p++;
      if (mp->m_flag & MODIFY)
         modify++;
   }
   if (p == msgCount && !modify && !anystat) {
      if (p == 1)
         printf(_("Held 1 message in %s\n"), displayname);
      else if (p > 1)
         printf(_("Held %d messages in %s\n"), p, displayname);
      goto jleave;
   }
   if (c == 0) {
      if (p != 0) {
         writeback(rbuf, fbuf);
         goto jleave;
      }
      goto jcream;
   }

   if (makembox() == STOP)
      goto jleave;

   /* Now we are ready to copy back preserved files to the system mailbox, if
    * any were requested */
   if (p != 0) {
      writeback(rbuf, fbuf);
      goto jleave;
   }

   /* Finally, remove his file.  If new mail has arrived, copy it back */
jcream:
   if (rbuf != NULL) {
      abuf = fbuf;
      fseek(abuf, 0L, SEEK_SET);
      while ((c = getc(rbuf)) != EOF)
         putc(c, abuf);
      Fclose(rbuf);
      ftrunc(abuf);
      _alter(mailname);
      goto jleave;
   }
   demail();
jleave:
   if (fbuf != NULL) {
      Fclose(fbuf);
      dot_unlock(mailname);
   }
   NYD_LEAVE;
}

FL int
holdbits(void)
{
   struct message *mp;
   int anystat, autohold, holdbit, nohold;
   NYD_ENTER;

   anystat = 0;
   autohold = ok_blook(hold);
   holdbit = autohold ? MPRESERVE : MBOX;
   nohold = MBOX | MSAVED | MDELETED | MPRESERVE;
   if (ok_blook(keepsave))
      nohold &= ~MSAVED;
   for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp) {
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
   NYD_LEAVE;
   return anystat;
}

FL enum okay
makembox(void) /* TODO oh my god */
{
   struct message *mp;
   char *mbox, *tempQuit;
   int mcount, c;
   FILE *ibuf = NULL, *obuf, *abuf;
   enum protocol prot;
   enum okay rv = STOP;
   NYD_ENTER;

   mbox = _mboxname;
   mcount = 0;
   if (!ok_blook(append)) {
      if ((obuf = Ftmp(&tempQuit, "makembox",
            OF_WRONLY | OF_HOLDSIGS | OF_REGISTER, 0600)) == NULL) {
         perror(_("temporary mail quit file"));
         goto jleave;
      }
      if ((ibuf = Fopen(tempQuit, "r")) == NULL)
         perror(tempQuit);
      Ftmp_release(&tempQuit);
      if (ibuf == NULL) {
         Fclose(obuf);
         goto jleave;
      }

      if ((abuf = Zopen(mbox, "r", NULL)) != NULL) {
         while ((c = getc(abuf)) != EOF)
            putc(c, obuf);
         Fclose(abuf);
      }
      if (ferror(obuf)) {
         perror(_("temporary mail quit file"));
         Fclose(ibuf);
         Fclose(obuf);
         goto jleave;
      }
      Fclose(obuf);

      if ((c = open(mbox, O_CREAT | O_TRUNC | O_WRONLY, 0600)) != -1)
         close(c);
      if ((obuf = Zopen(mbox, "r+", NULL)) == NULL) {
         perror(mbox);
         Fclose(ibuf);
         goto jleave;
      }
   } else {
      if ((obuf = Zopen(mbox, "a", NULL)) == NULL) {
         perror(mbox);
         goto jleave;
      }
      fchmod(fileno(obuf), 0600);
   }

   srelax_hold();
   prot = which_protocol(mbox);
   for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MBOX) {
         ++mcount;
         if (prot == PROTO_IMAP &&
               saveignore[0].i_count == 0 && saveignore[1].i_count == 0
#ifdef HAVE_IMAP /* TODO revisit */
               && imap_thisaccount(mbox)
#endif
         ) {
#ifdef HAVE_IMAP
            if (imap_copy(mp, PTR2SIZE(mp - message + 1), mbox) == STOP)
#endif
               goto jerr;
         } else if (sendmp(mp, obuf, saveignore, NULL, SEND_MBOX, NULL) < 0) {
            perror(mbox);
jerr:
            if (ibuf != NULL)
               Fclose(ibuf);
            Fclose(obuf);
            srelax_rele();
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
      rewind(ibuf);
      c = getc(ibuf);
      while (c != EOF) {
         putc(c, obuf);
         if (ferror(obuf))
            break;
         c = getc(ibuf);
      }
      Fclose(ibuf);
      fflush(obuf);
   }
   ftrunc(obuf);
   if (ferror(obuf)) {
      perror(mbox);
      Fclose(obuf);
      goto jleave;
   }
   if (Fclose(obuf) != 0) {
      if (prot != PROTO_IMAP)
         perror(mbox);
      goto jleave;
   }
   if (mcount == 1)
      printf(_("Saved 1 message in mbox\n"));
   else
      printf(_("Saved %d messages in mbox\n"), mcount);
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL void
save_mbox_for_possible_quitstuff(void) /* TODO try to get rid of that */
{
   char const *cp;
   NYD_ENTER;

   if ((cp = expand("&")) == NULL)
      cp = "";
   n_strlcpy(_mboxname, cp, sizeof _mboxname);
   NYD_LEAVE;
}

FL int
savequitflags(void)
{
   enum quitflags qf = 0;
   size_t i;
   NYD_ENTER;

   for (i = 0; i < NELEM(_quitnames); ++i)
      if (_var_oklook(_quitnames[i].okey) != NULL)
         qf |= _quitnames[i].flag;
   NYD_LEAVE;
   return qf;
}

FL void
restorequitflags(int qf)
{
   size_t i;
   NYD_ENTER;

   for (i = 0;  i < NELEM(_quitnames); ++i) {
      char *x = _var_oklook(_quitnames[i].okey);
      if (qf & _quitnames[i].flag) {
         if (x == NULL)
            _var_okset(_quitnames[i].okey, TRU1);
      } else if (x != NULL)
         _var_okclear(_quitnames[i].okey);
   }
   NYD_LEAVE;
}

/* s-it-mode */
