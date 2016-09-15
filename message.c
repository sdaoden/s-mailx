/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message, message array, and related operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#define n_FILE message

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Slots in ::message */
static size_t           _message_space;

static enum okay  get_header(struct message *mp);

static enum okay
get_header(struct message *mp)
{
   enum okay rv;
   NYD_ENTER;
   UNUSED(mp);

   switch (mb.mb_type) {
   case MB_FILE:
   case MB_MAILDIR:
      rv = OKAY;
      break;
#ifdef HAVE_POP3
   case MB_POP3:
      rv = pop3_header(mp);
      break;
#endif
   case MB_VOID:
   default:
      rv = STOP;
      break;
   }
   NYD_LEAVE;
   return rv;
}

FL FILE *
setinput(struct mailbox *mp, struct message *m, enum needspec need)
{
   FILE *rv = NULL;
   enum okay ok = STOP;
   NYD_ENTER;

   switch (need) {
   case NEED_HEADER:
      ok = (m->m_have & HAVE_HEADER) ? OKAY : get_header(m);
      break;
   case NEED_BODY:
      ok = (m->m_have & HAVE_BODY) ? OKAY : get_body(m);
      break;
   case NEED_UNSPEC:
      ok = OKAY;
      break;
   }
   if (ok != OKAY)
      goto jleave;

   fflush(mp->mb_otf);
   if (fseek(mp->mb_itf, (long)mailx_positionof(m->m_block, m->m_offset),
         SEEK_SET) == -1) {
      n_perr(_("fseek"), 0);
      n_panic(_("temporary file seek"));
   }
   rv = mp->mb_itf;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
get_body(struct message *mp)
{
   enum okay rv;
   NYD_ENTER;
   UNUSED(mp);

   switch (mb.mb_type) {
   case MB_FILE:
   case MB_MAILDIR:
      rv = OKAY;
      break;
#ifdef HAVE_POP3
   case MB_POP3:
      rv = pop3_body(mp);
      break;
#endif
   case MB_VOID:
   default:
      rv = STOP;
      break;
   }
   NYD_LEAVE;
   return rv;
}

FL void
message_reset(void)
{
   NYD_ENTER;
   if (message != NULL) {
      free(message);
      message = NULL;
   }
   msgCount = 0;
   _message_space = 0;
   NYD_LEAVE;
}

FL void
message_append(struct message *mp)
{
   NYD_ENTER;
   if (UICMP(z, msgCount + 1, >=, _message_space)) {
      /* XXX remove _message_space magics (or use s_Vector) */
      _message_space = (_message_space >= 128 && _message_space <= 1000000)
            ? _message_space << 1 : _message_space + 64;
      message = srealloc(message, _message_space * sizeof *message);
   }
   if (msgCount > 0) {
      if (mp != NULL)
         message[msgCount - 1] = *mp;
      else
         memset(message + msgCount - 1, 0, sizeof *message);
   }
   NYD_LEAVE;
}

FL void
message_append_null(void)
{
   NYD_ENTER;
   if (msgCount == 0)
      message_append(NULL);
   setdot(message);
   message[msgCount].m_size = 0;
   message[msgCount].m_lines = 0;
   NYD_LEAVE;
}

FL bool_t
message_match(struct message *mp, struct search_expr const *sep,
   bool_t with_headers)
{
   char **line;
   size_t *linesize, cnt;
   FILE *fp;
   bool_t rv = FAL0;
   NYD_ENTER;

   if ((fp = Ftmp(NULL, "mpmatch", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
      goto j_leave;

   if (sendmp(mp, fp, NULL, NULL, SEND_TOSRCH, NULL) < 0)
      goto jleave;
   fflush_rewind(fp);

   cnt = fsize(fp);
   line = &termios_state.ts_linebuf; /* XXX line pool */
   linesize = &termios_state.ts_linesize; /* XXX line pool */

   if (!with_headers)
      while (fgetline(line, linesize, &cnt, NULL, fp, 0))
         if (**line == '\n')
            break;

   while (fgetline(line, linesize, &cnt, NULL, fp, 0)) {
#ifdef HAVE_REGEX
      if (sep->ss_sexpr == NULL) {
         if (regexec(&sep->ss_regex, *line, 0,NULL, 0) == REG_NOMATCH)
            continue;
      } else
#endif
      if (!substr(*line, sep->ss_sexpr))
         continue;
      rv = TRU1;
      break;
   }

jleave:
   Fclose(fp);
j_leave:
   NYD_LEAVE;
   return rv;
}

FL struct message *
setdot(struct message *mp)
{
   NYD_ENTER;
   if (dot != mp) {
      prevdot = dot;
      pstate &= ~PS_DID_PRINT_DOT;
   }
   dot = mp;
   uncollapse1(dot, 0);
   NYD_LEAVE;
   return dot;
}

FL void
touch(struct message *mp)
{
   NYD_ENTER;
   mp->m_flag |= MTOUCH;
   if (!(mp->m_flag & MREAD))
      mp->m_flag |= MREAD | MSTATUS;
   NYD_LEAVE;
}

/* s-it-mode */
