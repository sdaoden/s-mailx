/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ User commands which save, copy, write (parts of) messages.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
static int     save1(char *str, int domark, char const *cmd,
                  struct n_ignore const *itp, int convert, int sender_record,
                  int domove);

/* Snarf the file from the end of the command line and return a pointer to it.
 * If there is no file attached, return the mbox file.  Put a null in front of
 * the file name so that the message list processing won't see it, unless the
 * file name is the only thing on the line, in which case, return 0 in the
 * reference flag variable */
static char *  snarf(char *linebuf, bool_t *flag, bool_t usembox);

static int
save1(char *str, int domark, char const *cmd, struct n_ignore const *itp,
   int convert, int sender_record, int domove)
{
   ui64_t mstats[1], tstats[2];
   struct stat st;
   int last = 0, *msgvec, *ip;
   struct message *mp;
   char *file = NULL, *cp, *cq;
   char const *disp = n_empty, *shell = NULL;
   FILE *obuf;
   bool_t success = FAL0, isflag;
   NYD_ENTER;

   msgvec = salloc((msgCount + 2) * sizeof *msgvec);
   if (sender_record) {
      for (cp = str; *cp != '\0' && spacechar(*cp); ++cp)
         ;
      isflag = (*cp != '\0');
   } else {
      if ((file = snarf(str, &isflag, convert != SEND_TOFILE)) == NULL)
         goto jleave;
      while(spacechar(*file))
         ++file;
      if (*file == '|') {
         ++file;
         shell = ok_vlook(SHELL);
      }
   }

   if (!isflag) {
      *msgvec = first(0, MMNORM);
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;
   if (*msgvec == 0) {
      if (pstate & (PS_HOOK_MASK | PS_ROBOT)) {
         success = TRU1;
         goto jleave;
      }
      printf(_("No messages to %s.\n"), cmd);
      goto jleave;
   }

   if (sender_record) {
      if ((cp = nameof(message + *msgvec - 1, 0)) == NULL) {
         printf(_("Cannot determine message sender to %s.\n"), cmd);
         goto jleave;
      }

      for (cq = cp; *cq != '\0' && *cq != '@'; cq++)
         ;
      *cq = '\0';
      if (ok_blook(outfolder)) {
         size_t sz = strlen(cp) +1;
         file = salloc(sz + 1);
         file[0] = '+';
         memcpy(file + 1, cp, sz);
      } else
         file = cp;
   }

   /* Pipe target is special TODO hacked in later, normalize flow! */
   if (shell != NULL) {
      if ((obuf = Popen(file, "w", shell, NULL, 1)) == NULL) {
         int esave = errno;

         n_perr(file, esave);
         errno = esave;
         goto jleave;
      }
      isflag = FAL0;
      disp = _("[Piped]");
      goto jsend;
   }

   if ((file = expand(file)) == NULL)
      goto jleave;

   obuf = ((convert == SEND_TOFILE) ? Fopen(file, "a+") : Zopen(file, "a+"));
   if (obuf == NULL) {
      obuf = ((convert == SEND_TOFILE) ? Fopen(file, "wx") : Zopen(file, "wx"));
      if (obuf == NULL) {
         n_perr(file, 0);
         goto jleave;
      }
      isflag = TRU1;
      disp = _("[New file]");
   } else {
      isflag = FAL0;
      disp = _("[Appended]");
   }

   /* TODO RETURN check, but be aware of protocols: v15: Mailbox->lock()! */
   n_file_lock(fileno(obuf), FLT_WRITE, 0,0, UIZ_MAX);

   if (!isflag && !fstat(fileno(obuf), &st) && S_ISREG(st.st_mode) &&
         fseek(obuf, -2L, SEEK_END) == 0) {
      char buf[2];
      int prependnl = 0;

      switch (fread(buf, sizeof *buf, 2, obuf)) {
      case 2:
         if (buf[1] != '\n') {
            prependnl = 1;
            break;
         }
         /* FALLTHRU */
      case 1:
         if (buf[0] != '\n')
            prependnl = 1;
         break;
      default:
         if (ferror(obuf)) {
            n_perr(file, 0);
            goto jleave;
         }
         prependnl = 0;
      }

      fflush(obuf);
      if (prependnl) {
         putc('\n', obuf);
         fflush(obuf);
      }
   }

jsend:
   success = TRU1;
   tstats[0] = tstats[1] = 0;

   srelax_hold();
   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip) {
      mp = message + *ip - 1;
      if (sendmp(mp, obuf, itp, NULL, convert, mstats) < 0) {
         success = FAL0;
         goto jferr;
      }
      srelax();

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
   srelax_rele();

   fflush(obuf);
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
      printf("%s %s %" /*PRIu64 "/%"*/ PRIu64 " bytes\n",
         n_shexp_quote_cp(file, FAL0), disp,
         /*tstats[1], TODO v15: lines written */ tstats[0]);
   } else if (domark) {
      for (ip = msgvec; *ip != 0 &&
            UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount); ++ip) {
         mp = message + *ip - 1;
         mp->m_flag &= ~MSAVED;
      }
   } else if (domove) {
      for (ip = msgvec; *ip != 0 &&
            UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount); ++ip) {
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
   NYD_LEAVE;
   return (success == FAL0);
}

static char *
snarf(char *linebuf, bool_t *flag, bool_t usembox)
{
   char *cp;
   NYD_ENTER;

   if ((cp = laststring(linebuf, flag, TRU1)) == NULL) {
      if (usembox) {
         *flag = FAL0;
         cp = expand("&");
      } else
         n_err(_("No file specified\n"));
   }
   NYD_LEAVE;
   return cp;
}

FL int
c_save(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 1, "save", n_IGNORE_SAVE, SEND_MBOX, 0, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Save(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 1, "save", n_IGNORE_SAVE, SEND_MBOX, 1, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_copy(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "copy", n_IGNORE_SAVE, SEND_MBOX, 0, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Copy(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "copy", n_IGNORE_SAVE, SEND_MBOX, 1, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_move(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "move", n_IGNORE_SAVE, SEND_MBOX, 0, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Move(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "move", n_IGNORE_SAVE, SEND_MBOX, 1, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_decrypt(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "decrypt", n_IGNORE_SAVE, SEND_DECRYPT, 0, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Decrypt(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "decrypt", n_IGNORE_SAVE, SEND_DECRYPT, 1, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_write(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   if (str == NULL || *str == '\0')
      str = savestr("/dev/null");
   rv = save1(str, 0, "write", n_IGNORE_ALL, SEND_TOFILE, 0, 0);
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
