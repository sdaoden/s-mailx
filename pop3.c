/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ POP3 (RFCs 1939, 2595) client.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2002
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
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

EMPTY_FILE(pop3)
#ifdef HAVE_POP3

#define POP3_ANSWER(RV,ACTIONSTOP) \
do if (((RV) = pop3_answer(mp)) == STOP) {\
   ACTIONSTOP;\
} while (0)

#define POP3_OUT(RV,X,Y,ACTIONSTOP) \
do {\
   if (((RV) = pop3_finish(mp)) == STOP) {\
      ACTIONSTOP;\
   }\
   if (options & OPT_VERBOSE)\
      fprintf(stderr, ">>> %s", X);\
   mp->mb_active |= Y;\
   if (((RV) = swrite(&mp->mb_sock, X)) == STOP) {\
      ACTIONSTOP;\
   }\
} while (0)

static char             *_pop3_buf;
static size_t           _pop3_bufsize;
static sigjmp_buf       _pop3_jmp;
static sighandler_type  _pop3_savealrm;
static int              _pop3_keepalive;
static int volatile     _pop3_lock;

/* Perform entire login handshake */
static enum okay  _pop3_login(struct mailbox *mp, char *xuser, char *pass,
                     char const *uhp, char const *xserver);

/* APOP: get greeting credential or NULL */
#ifdef HAVE_MD5
static char *     _pop3_lookup_apop_timestamp(char const *bp);
#endif

static bool_t     _pop3_use_starttls(char const *uhp);

/* APOP: shall we use it (for uhp)? */
static bool_t     _pop3_no_apop(char const *uhp);

/* Several authentication methods */
#ifdef HAVE_MD5
static enum okay  _pop3_auth_apop(struct mailbox *mp, char *xuser, char *pass,
                     char const *ts);
#endif
static enum okay  _pop3_auth_plain(struct mailbox *mp, char *xuser, char *pass);

static void       pop3_timer_off(void);
static enum okay  pop3_answer(struct mailbox *mp);
static enum okay  pop3_finish(struct mailbox *mp);
static void       pop3catch(int s);
static void       _pop3_maincatch(int s);
static enum okay  pop3_noop1(struct mailbox *mp);
static void       pop3alarm(int s);
static enum okay  pop3_stat(struct mailbox *mp, off_t *size, int *cnt);
static enum okay  pop3_list(struct mailbox *mp, int n, size_t *size);
static void       pop3_init(struct mailbox *mp, int n);
static void       pop3_dates(struct mailbox *mp);
static void       pop3_setptr(struct mailbox *mp);
static enum okay  pop3_get(struct mailbox *mp, struct message *m,
                     enum needspec need);
static enum okay  pop3_exit(struct mailbox *mp);
static enum okay  pop3_delete(struct mailbox *mp, int n);
static enum okay  pop3_update(struct mailbox *mp);

static enum okay
_pop3_login(struct mailbox *mp, char *xuser, char *pass, char const *uhp,
   char const *xserver)
{
#ifdef HAVE_MD5
   char *ts;
#endif
   char *cp;
   enum okay rv;
   NYD_ENTER;

   /* Get the greeting, check wether APOP is advertised */
   POP3_ANSWER(rv, goto jleave);
#ifdef HAVE_MD5
   ts = _pop3_lookup_apop_timestamp(_pop3_buf);
#endif

   if ((cp = strchr(xserver, ':')) != NULL) { /* TODO GENERIC URI PARSE! */
      size_t l = PTR2SIZE(cp - xserver);
      char *x = salloc(l +1);
      memcpy(x, xserver, l);
      x[l] = '\0';
      xserver = x;
   }

   /* If not yet secured, can we upgrade to TLS? */
#ifdef HAVE_SSL
   if (mp->mb_sock.s_use_ssl == 0 && _pop3_use_starttls(uhp)) {
      POP3_OUT(rv, "STLS\r\n", MB_COMD, goto jleave);
      POP3_ANSWER(rv, goto jleave);
      if ((rv = ssl_open(xserver, &mp->mb_sock, uhp)) != OKAY)
         goto jleave;
   }
#else
   if (_pop3_use_starttls(uhp)) {
      fprintf(stderr, "No SSL support compiled in.\n");
      rv = STOP;
      goto jleave;
   }
#endif

   /* Use the APOP single roundtrip? */
   if (!_pop3_no_apop(uhp)) {
#ifdef HAVE_MD5
      if (ts != NULL) {
         if ((rv = _pop3_auth_apop(mp, xuser, pass, ts)) != OKAY)
            fprintf(stderr, tr(276,
               "POP3 `APOP' authentication failed, "
               "maybe try setting *pop3-no-apop*\n"));
         goto jleave;
      } else
#endif
      if (options & OPT_VERBOSE)
         fprintf(stderr, tr(204, "No POP3 `APOP' support "
            "available, sending password in clear text\n"));
   }
   rv = _pop3_auth_plain(mp, xuser, pass);
jleave:
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_MD5
static char *
_pop3_lookup_apop_timestamp(char const *bp)
{
   /* RFC 1939:
    * A POP3 server which implements the APOP command will include
    * a timestamp in its banner greeting.  The syntax of the timestamp
    * corresponds to the `msg-id' in [RFC822]
    * RFC 822:
    * msg-id   = "<" addr-spec ">"
    * addr-spec   = local-part "@" domain */
   char const *cp, *ep;
   size_t tl;
   char *rp = NULL;
   bool_t hadat = FAL0;
   NYD_ENTER;

   if ((cp = strchr(bp, '<')) == NULL)
      goto jleave;

   /* xxx What about malformed APOP timestamp (<@>) here? */
   for (ep = cp; *ep != '\0'; ++ep) {
      if (spacechar(*ep))
         goto jleave;
      else if (*ep == '@')
         hadat = TRU1;
      else if (*ep == '>') {
         if (!hadat)
            goto jleave;
         break;
      }
   }
   if (*ep != '>')
      goto jleave;

   tl = PTR2SIZE(++ep - cp);
   rp = salloc(tl +1);
   memcpy(rp, cp, tl);
   rp[tl] = '\0';
jleave:
   NYD_LEAVE;
   return rp;
}
#endif

static bool_t
_pop3_use_starttls(char const *uhp)
{
   bool_t rv;
   NYD_ENTER;

   if (!(rv = ok_blook(pop3_use_starttls)))
      rv = vok_blook(savecat("pop3-use-starttls-", uhp));
   NYD_LEAVE;
   return rv;
}

static bool_t
_pop3_no_apop(char const *uhp)
{
   bool_t rv;
   NYD_ENTER;

   if (!(rv = ok_blook(pop3_no_apop)))
      rv = vok_blook(savecat("pop3-no-apop-", uhp));
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_MD5
static enum okay
_pop3_auth_apop(struct mailbox *mp, char *xuser, char *pass, char const *ts)
{
   unsigned char digest[16];
   char hex[MD5TOHEX_SIZE];
   md5_ctx  ctx;
   size_t tl, i;
   char *user, *cp;
   enum okay rv = STOP;
   NYD_ENTER;

   for (tl = strlen(ts);;) {
      user = xuser;
      if (!getcredentials(&user, &pass))
         break;

      md5_init(&ctx);
      md5_update(&ctx, (unsigned char*)UNCONST(ts), tl);
      md5_update(&ctx, (unsigned char*)pass, strlen(pass));
      md5_final(digest, &ctx);
      md5tohex(hex, digest);

      i = strlen(user);
      cp = ac_alloc(5 + i + 1 + MD5TOHEX_SIZE + 2 +1);

      memcpy(cp, "APOP ", 5);
      memcpy(cp + 5, user, i);
      i += 5;
      cp[i++] = ' ';
      memcpy(cp + i, hex, MD5TOHEX_SIZE);
      i += MD5TOHEX_SIZE;
      memcpy(cp + i, "\r\n\0", 3);
      POP3_OUT(rv, cp, MB_COMD, goto jcont);
      POP3_ANSWER(rv, goto jcont);
      rv = OKAY;
jcont:
      ac_free(cp);
      if (rv == OKAY)
         break;
      pass = NULL;
   }
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_MD5 */

static enum okay
_pop3_auth_plain(struct mailbox *mp, char *xuser, char *pass)
{
   char *user, *cp;
   size_t ul, pl;
   enum okay rv = STOP;
   NYD_ENTER;

   /* The USER/PASS plain text version */
   for (;;) {
      user = xuser;
      if (!getcredentials(&user, &pass))
         break;

      ul = strlen(user);
      pl = strlen(pass);
      cp = ac_alloc(MAX(ul, pl) + 5 + 2 +1);

      memcpy(cp, "USER ", 5);
      memcpy(cp + 5, user, ul);
      memcpy(cp + 5 + ul, "\r\n\0", 3);
      POP3_OUT(rv, cp, MB_COMD, goto jcont);
      POP3_ANSWER(rv, goto jcont);

      memcpy(cp, "PASS ", 5);
      memcpy(cp + 5, pass, pl);
      memcpy(cp + 5 + pl, "\r\n\0", 3);
      POP3_OUT(rv, cp, MB_COMD, goto jcont);
      POP3_ANSWER(rv, goto jcont);
      rv = OKAY;
jcont:
      ac_free(cp);
      if (rv == OKAY)
         break;
      pass = NULL;
   }
   NYD_LEAVE;
   return rv;
}

static void
pop3_timer_off(void)
{
   NYD_ENTER;
   if (_pop3_keepalive > 0) {
      alarm(0);
      safe_signal(SIGALRM, _pop3_savealrm);
   }
   NYD_LEAVE;
}

static enum okay
pop3_answer(struct mailbox *mp)
{
   int sz;
   enum okay rv = STOP;
   NYD_ENTER;

jretry:
   if ((sz = sgetline(&_pop3_buf, &_pop3_bufsize, NULL, &mp->mb_sock)) > 0) {
      if ((mp->mb_active & (MB_COMD | MB_MULT)) == MB_MULT)
         goto jmultiline;
      if (options & OPT_VERBOSE)
         fputs(_pop3_buf, stderr);
      switch (*_pop3_buf) {
      case '+':
         rv = OKAY;
         mp->mb_active &= ~MB_COMD;
         break;
      case '-':
         rv = STOP;
         mp->mb_active = MB_NONE;
         fprintf(stderr, tr(218, "POP3 error: %s"), _pop3_buf);
         break;
      default:
         /* If the answer starts neither with '+' nor with '-', it must be part
          * of a multiline response.  Get lines until a single dot appears */
jmultiline:
         while (_pop3_buf[0] != '.' || _pop3_buf[1] != '\r' ||
               _pop3_buf[2] != '\n' || _pop3_buf[3] != '\0') {
            sz = sgetline(&_pop3_buf, &_pop3_bufsize, NULL, &mp->mb_sock);
            if (sz <= 0)
               goto jeof;
         }
         mp->mb_active &= ~MB_MULT;
         if (mp->mb_active != MB_NONE)
            goto jretry;
      }
   } else {
jeof:
      rv = STOP;
      mp->mb_active = MB_NONE;
   }
   NYD_LEAVE;
   return rv;
}

static enum okay
pop3_finish(struct mailbox *mp)
{
   NYD_ENTER;
   while (mp->mb_sock.s_fd > 0 && mp->mb_active != MB_NONE)
      pop3_answer(mp);
   NYD_LEAVE;
   return OKAY;
}

static void
pop3catch(int s)
{
   NYD_X; /* Signal handler */
   switch (s) {
   case SIGINT:
      fprintf(stderr, tr(102, "Interrupt\n"));
      siglongjmp(_pop3_jmp, 1);
      break;
   case SIGPIPE:
      fprintf(stderr, "Received SIGPIPE during POP3 operation\n");
      break;
   }
}

static void
_pop3_maincatch(int s)
{
   NYD_X; /* Signal handler */
   UNUSED(s);

   if (interrupts++ == 0)
      fprintf(stderr, tr(102, "Interrupt\n"));
   else
      onintr(0);
}

static enum okay
pop3_noop1(struct mailbox *mp)
{
   enum okay rv;
   NYD_ENTER;

   POP3_OUT(rv, "NOOP\r\n", MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_LEAVE;
   return rv;
}

static void
pop3alarm(int s)
{
   sighandler_type volatile saveint, savepipe;
   NYD_X; /* Signal handler */
   UNUSED(s);

   if (_pop3_lock++ == 0) {
      if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &_pop3_maincatch);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if (sigsetjmp(_pop3_jmp, 1)) {
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         goto jbrk;
      }
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, pop3catch);
      if (pop3_noop1(&mb) != OKAY) {
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         goto jleave;
      }
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
   }
jbrk:
   alarm(_pop3_keepalive);
jleave:
   --_pop3_lock;
}

static enum okay
pop3_stat(struct mailbox *mp, off_t *size, int *cnt)
{
   char *cp;
   enum okay rv;
   NYD_ENTER;

   POP3_OUT(rv, "STAT\r\n", MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);

   for (cp = _pop3_buf; *cp != '\0' && !spacechar(*cp); ++cp)
      ;
   while (*cp != '\0' && spacechar(*cp))
      ++cp;

   if (*cp != '\0') {
      *cnt = (int)strtol(cp, NULL, 10);
      while (*cp != '\0' && !spacechar(*cp))
         ++cp;
      while (*cp != '\0' && spacechar(*cp))
         ++cp;
      if (*cp != '\0')
         *size = (int)strtol(cp, NULL, 10);
      else
         rv = STOP;
   } else
      rv = STOP;

   if (rv == STOP)
      fprintf(stderr, tr(260, "invalid POP3 STAT response: %s\n"), _pop3_buf);
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
pop3_list(struct mailbox *mp, int n, size_t *size)
{
   char o[LINESIZE], *cp;
   enum okay rv;
   NYD_ENTER;

   snprintf(o, sizeof o, "LIST %u\r\n", n);
   POP3_OUT(rv, o, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);

   for (cp = _pop3_buf; *cp != '\0' && !spacechar(*cp); ++cp)
      ;
   while (*cp != '\0' && spacechar(*cp))
      ++cp;
   while (*cp != '\0' && !spacechar(*cp))
      ++cp;
   while (*cp != '\0' && spacechar(*cp))
      ++cp;
   if (*cp != '\0')
      *size = (size_t)strtol(cp, NULL, 10);
   else
      *size = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static void
pop3_init(struct mailbox *mp, int n)
{
   struct message *m;
   char *cp;
   NYD_ENTER;

   m =  message + n;
   m->m_flag = MUSED | MNEW | MNOFROM | MNEWEST;
   m->m_block = 0;
   m->m_offset = 0;
   pop3_list(mp, PTR2SIZE(m - message + 1), &m->m_xsize);

   if ((cp = hfield1("status", m)) != NULL) {
      while (*cp != '\0') {
         if (*cp == 'R')
            m->m_flag |= MREAD;
         else if (*cp == 'O')
            m->m_flag &= ~MNEW;
         ++cp;
      }
   }
   NYD_LEAVE;
}

static void
pop3_dates(struct mailbox *mp)
{
   size_t i;
   NYD_ENTER;
   UNUSED(mp);

   for (i = 0; UICMP(z, i, <, msgCount); ++i)
      substdate(message + i);
   NYD_LEAVE;
}

static void
pop3_setptr(struct mailbox *mp)
{
   size_t i;
   NYD_ENTER;

   message = scalloc(msgCount + 1, sizeof *message);
   for (i = 0; UICMP(z, i, <, msgCount); ++i)
      pop3_init(mp, i);

   setdot(message);
   message[msgCount].m_size = 0;
   message[msgCount].m_lines = 0;
   pop3_dates(mp);
   NYD_LEAVE;
}

static enum okay
pop3_get(struct mailbox *mp, struct message *m, enum needspec volatile need)
{
   char o[LINESIZE], *line, *lp;
   sighandler_type volatile saveint, savepipe;
   size_t linesize, linelen, size;
   int number, emptyline, lines;
   off_t offset;
   enum okay rv;
   NYD_ENTER;

   line = NULL; /* TODO line pool */
   saveint = savepipe = SIG_IGN;
   linesize = 0;
   number = (int)PTR2SIZE(m - message + 1);
   emptyline = 0;
   rv = STOP;

   if (mp->mb_sock.s_fd < 0) {
      fprintf(stderr, tr(219, "POP3 connection already closed.\n"));
      ++_pop3_lock;
      goto jleave;
   }

   if (_pop3_lock++ == 0) {
      if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &_pop3_maincatch);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if (sigsetjmp(_pop3_jmp, 1)) {
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         --_pop3_lock;
         goto jleave;
      }
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, pop3catch);
   }

   fseek(mp->mb_otf, 0L, SEEK_END);
   offset = ftell(mp->mb_otf);
jretry:
   switch (need) {
   case NEED_HEADER:
      snprintf(o, sizeof o, "TOP %u 0\r\n", number);
      break;
   case NEED_BODY:
      snprintf(o, sizeof o, "RETR %u\r\n", number);
      break;
   case NEED_UNSPEC:
      abort(); /* XXX */
   }
   POP3_OUT(rv, o, MB_COMD | MB_MULT, goto jleave);

   if (pop3_answer(mp) == STOP) {
      if (need == NEED_HEADER) {
         /* The TOP POP3 command is optional, so retry with entire message */
         need = NEED_BODY;
         goto jretry;
      }
      goto jleave;
   }

   size = 0;
   lines = 0;
   while (sgetline(&line, &linesize, &linelen, &mp->mb_sock) > 0) {
      if (line[0] == '.' && line[1] == '\r' && line[2] == '\n' &&
            line[3] == '\0') {
         mp->mb_active &= ~MB_MULT;
         break;
      }
      if (line[0] == '.') {
         lp = line + 1;
         --linelen;
      } else
         lp = line;
      /* TODO >>
       * Need to mask 'From ' lines. This cannot be done properly
       * since some servers pass them as 'From ' and others as
       * '>From '. Although one could identify the first kind of
       * server in principle, it is not possible to identify the
       * second as '>From ' may also come from a server of the
       * first type as actual data. So do what is absolutely
       * necessary only - mask 'From '.
       *
       * If the line is the first line of the message header, it
       * is likely a real 'From ' line. In this case, it is just
       * ignored since it violates all standards.
       * TODO i have *never* seen the latter?!?!?
       * TODO <<
       */
      /* Since we simply copy over data without doing any transfer
       * encoding reclassification/adjustment we *have* to perform
       * RFC 4155 compliant From_ quoting here */
      if (is_head(lp, linelen)) {
         if (lines == 0)
            continue;
         fputc('>', mp->mb_otf);
         ++size;
      }
      lines++;
      if (lp[linelen-1] == '\n' && (linelen == 1 ||
               lp[linelen-2] == '\r')) {
         emptyline = linelen <= 2;
         if (linelen > 2)
            fwrite(lp, 1, linelen - 2, mp->mb_otf);
         fputc('\n', mp->mb_otf);
         size += linelen - 1;
      } else {
         emptyline = 0;
         fwrite(lp, 1, linelen, mp->mb_otf);
         size += linelen;
      }
   }
   if (!emptyline) {
      /* This is very ugly; but some POP3 daemons don't end a
       * message with \r\n\r\n, and we need \n\n for mbox format */
      fputc('\n', mp->mb_otf);
      ++lines;
      ++size;
   }
   m->m_size = size;
   m->m_lines = lines;
   m->m_block = mailx_blockof(offset);
   m->m_offset = mailx_offsetof(offset);
   fflush(mp->mb_otf);

   switch (need) {
   case NEED_HEADER:
      m->m_have |= HAVE_HEADER;
      break;
   case NEED_BODY:
      m->m_have |= HAVE_HEADER | HAVE_BODY;
      m->m_xlines = m->m_lines;
      m->m_xsize = m->m_size;
      break;
   case NEED_UNSPEC:
      break;
   }

   rv = OKAY;
jleave:
   if (line != NULL)
      free(line);
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, saveint);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, savepipe);
   --_pop3_lock;
   NYD_LEAVE;
   if (interrupts)
      onintr(0);
   return rv;
}

static enum okay
pop3_exit(struct mailbox *mp)
{
   enum okay rv;
   NYD_ENTER;

   POP3_OUT(rv, "QUIT\r\n", MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
pop3_delete(struct mailbox *mp, int n)
{
   char o[LINESIZE];
   enum okay rv;
   NYD_ENTER;

   snprintf(o, sizeof o, "DELE %u\r\n", n);
   POP3_OUT(rv, o, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
pop3_update(struct mailbox *mp)
{
   struct message *m;
   int dodel, c, gotcha, held;
   NYD_ENTER;

   if (!edit) {
      holdbits();
      c = 0;
      for (m = message; PTRCMP(m, <, message + msgCount); ++m)
         if (m->m_flag & MBOX)
            ++c;
      if (c > 0)
         makembox();
   }

   gotcha = held = 0;
   for (m = message; PTRCMP(m, <, message + msgCount); ++m) {
      if (edit)
         dodel = m->m_flag & MDELETED;
      else
         dodel = !((m->m_flag & MPRESERVE) || !(m->m_flag & MTOUCH));
      if (dodel) {
         pop3_delete(mp, PTR2SIZE(m - message + 1));
         ++gotcha;
      } else
         ++held;
   }
   if (gotcha && edit) {
      printf(tr(168, "\"%s\" "), displayname);
      printf((ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         ? tr(170, "complete\n") : tr(212, "updated.\n"));
   } else if (held && !edit) {
      if (held == 1)
         printf(tr(155, "Held 1 message in %s\n"), displayname);
      else
         printf(tr(156, "Held %d messages in %s\n"), held, displayname);
   }
   fflush(stdout);
   NYD_LEAVE;
   return OKAY;
}

FL enum okay
pop3_noop(void)
{
   sighandler_type volatile saveint, savepipe;
   enum okay rv = STOP;
   NYD_ENTER;

   _pop3_lock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_pop3_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(_pop3_jmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, pop3catch);
      rv = pop3_noop1(&mb);
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   _pop3_lock = 0;
   NYD_LEAVE;
   return rv;
}

FL int
pop3_setfile(char const *server, int nmail, int isedit)
{
   struct sock so;
   sighandler_type saveint, savepipe;
   char * volatile user, * volatile pass;
   char const *cp, *uhp, * volatile sp = server;
   int use_ssl = 0, rv = 1;
   NYD_ENTER;

   if (nmail)
      goto jleave;

   if (!strncmp(sp, "pop3://", 7)) {
      sp = sp + 7;
      use_ssl = 0;
#ifdef HAVE_SSL
   } else if (!strncmp(sp, "pop3s://", 8)) {
      sp = sp + 8;
      use_ssl = 1;
#endif
   }
   uhp = sp;
   pass = lookup_password_for_token(uhp);

   if ((cp = last_at_before_slash(sp)) != NULL) {
      user = salloc(cp - sp +1);
      memcpy(user, sp, cp - sp);
      user[cp - sp] = '\0';
      sp = cp + 1;
      user = urlxdec(user);
   } else
      user = NULL;

   if (sopen(sp, &so, use_ssl, uhp, (use_ssl ? "pop3s" : "pop3")) != OKAY) {
      rv = -1;
      goto jleave;
   }
   quit();
   edit = (isedit != 0);
   if (mb.mb_sock.s_fd >= 0)
      sclose(&mb.mb_sock);
   if (mb.mb_itf) {
      fclose(mb.mb_itf);
      mb.mb_itf = NULL;
   }
   if (mb.mb_otf) {
      fclose(mb.mb_otf);
      mb.mb_otf = NULL;
   }
   initbox(server);
   mb.mb_type = MB_VOID;
   _pop3_lock = 1;
   mb.mb_sock = so;

   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(_pop3_jmp, 1)) {
      sclose(&mb.mb_sock);
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      _pop3_lock = 0;
      goto jleave;
   }
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, pop3catch);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, pop3catch);

   if ((cp = ok_vlook(pop3_keepalive)) != NULL) {
      if ((_pop3_keepalive = (int)strtol(cp, NULL, 10)) > 0) {
         _pop3_savealrm = safe_signal(SIGALRM, pop3alarm);
         alarm(_pop3_keepalive);
      }
   }

   mb.mb_sock.s_desc = "POP3";
   mb.mb_sock.s_onclose = pop3_timer_off;
   if (_pop3_login(&mb, user, pass, uhp, sp) != OKAY ||
         pop3_stat(&mb, &mailsize, &msgCount) != OKAY) {
      sclose(&mb.mb_sock);
      pop3_timer_off();
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      _pop3_lock = 0;
      goto jleave;
   }
   mb.mb_type = MB_POP3;
   mb.mb_perm = (options & OPT_R_FLAG) ? 0 : MB_DELE;
   pop3_setptr(&mb);
   setmsize(msgCount);
   sawcom = FAL0;

   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   _pop3_lock = 0;
   if (!edit && msgCount == 0) {
      if (mb.mb_type == MB_POP3 && !ok_blook(emptystart))
         fprintf(stderr, tr(258, "No mail at %s\n"), server);
      goto jleave;
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
pop3_header(struct message *m)
{
   enum okay rv;
   NYD_ENTER;

   rv = pop3_get(&mb, m, (ok_blook(pop3_bulk_load) ? NEED_BODY : NEED_HEADER));
   NYD_LEAVE;
   return rv;
}

FL enum okay
pop3_body(struct message *m)
{
   enum okay rv;
   NYD_ENTER;

   rv = pop3_get(&mb, m, NEED_BODY);
   NYD_LEAVE;
   return rv;
}

FL void
pop3_quit(void)
{
   sighandler_type volatile saveint, savepipe;
   NYD_ENTER;

   if (mb.mb_sock.s_fd < 0) {
      fprintf(stderr, tr(219, "POP3 connection already closed.\n"));
      goto jleave;
   }

   _pop3_lock = 1;
   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(_pop3_jmp, 1)) {
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, saveint);
      _pop3_lock = 0;
      goto jleave;
   }
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, pop3catch);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, pop3catch);
   pop3_update(&mb);
   pop3_exit(&mb);
   sclose(&mb.mb_sock);
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   _pop3_lock = 0;
jleave:
   NYD_LEAVE;
}
#endif /* HAVE_POP3 */

/* vim:set fenc=utf-8:s-it-mode */
