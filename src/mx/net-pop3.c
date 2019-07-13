/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of net-pop3.h.
 *@ TODO UIDL (as struct message.m_uid, *headline* %U), etc...
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause
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
#undef su_FILE
#define su_FILE net_pop3
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_POP3
#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/file-streams.h"
#include "mx/net-socket.h"
#include "mx/sigs.h"

#include "mx/net-pop3.h"
/* TODO fake */
#include "su/code-in.h"

#define POP3_ANSWER(RV,ACTIONSTOP) \
do if (((RV) = pop3_answer(mp)) == STOP) {\
   ACTIONSTOP;\
} while (0)

#define POP3_OUT(RV,X,Y,ACTIONSTOP) \
do {\
   if (((RV) = pop3_finish(mp)) == STOP) {\
      ACTIONSTOP;\
   }\
   if (n_poption & n_PO_VERBVERB)\
      n_err(">>> %s", X);\
   mp->mb_active |= Y;\
   if (((RV) = mx_socket_write(mp->mb_sock, X)) == STOP) {\
      ACTIONSTOP;\
   }\
} while (0)

static char             *_pop3_buf;
static uz           _pop3_bufsize;
static sigjmp_buf       _pop3_jmp;
static n_sighdl_t  _pop3_savealrm;
static s32           _pop3_keepalive;
static int volatile     _pop3_lock;

/* Perform entire login handshake */
static enum okay  _pop3_login(struct mailbox *mp, struct mx_socket_conn *scp);

/* APOP: get greeting credential or NULL */
#ifdef mx_HAVE_MD5
static char *     _pop3_lookup_apop_timestamp(char const *bp);
#endif

/* Several authentication methods */
#ifdef mx_HAVE_MD5
static enum okay  _pop3_auth_apop(struct mailbox *mp,
                     struct mx_socket_conn const *scp, char const *ts);
#endif
static enum okay  _pop3_auth_plain(struct mailbox *mp,
                     struct mx_socket_conn const *scp);

static void       pop3_timer_off(void);
static enum okay  pop3_answer(struct mailbox *mp);
static enum okay  pop3_finish(struct mailbox *mp);
static void       pop3catch(int s);
static void       _pop3_maincatch(int s);
static enum okay  pop3_noop1(struct mailbox *mp);
static void       pop3alarm(int s);
static enum okay  pop3_stat(struct mailbox *mp, off_t *size, int *cnt);
static enum okay  pop3_list(struct mailbox *mp, int n, uz *size);
static void       pop3_setptr(struct mailbox *mp,
                     struct mx_socket_conn const *scp);
static enum okay  pop3_get(struct mailbox *mp, struct message *m,
                     enum needspec need);
static enum okay  pop3_exit(struct mailbox *mp);
static enum okay  pop3_delete(struct mailbox *mp, int n);
static enum okay  pop3_update(struct mailbox *mp);

static enum okay
_pop3_login(struct mailbox *mp, struct mx_socket_conn *scp)
{
#ifdef mx_HAVE_MD5
   char *ts;
#endif
   enum okey_xlook_mode oxm;
   enum okay rv;
   NYD_IN;

   oxm = (ok_vlook(v15_compat) != su_NIL) ? OXM_ALL : OXM_PLAIN | OXM_U_H_P;

   /* Get the greeting, check whether APOP is advertised */
   POP3_ANSWER(rv, goto jleave);
#ifdef mx_HAVE_MD5
   ts = _pop3_lookup_apop_timestamp(_pop3_buf);
#endif

   /* If not yet secured, can we upgrade to TLS? */
#ifdef mx_HAVE_TLS
   if (!(scp->sc_url.url_flags & n_URL_TLS_REQUIRED) &&
         xok_blook(pop3_use_starttls, &scp->sc_url, oxm)) {
      POP3_OUT(rv, "STLS" NETNL, MB_COMD, goto jleave);
      POP3_ANSWER(rv, goto jleave);
      if(!n_tls_open(&scp->sc_url, scp->sc_sock)){
         rv = STOP;
         goto jleave;
      }
   }
#else
   if (xok_blook(pop3_use_starttls, &scp->sc_url, oxm)) {
      n_err(_("No TLS support compiled in\n"));
      rv = STOP;
      goto jleave;
   }
#endif

   /* Use the APOP single roundtrip? */
#ifdef mx_HAVE_MD5
   if (ts != NULL && !xok_blook(pop3_no_apop, &scp->sc_url, oxm)) {
      if ((rv = _pop3_auth_apop(mp, scp, ts)) != OKAY) {
         char const *ccp;

# ifdef mx_HAVE_TLS
         if (scp->sc_sock->s_use_tls)
            ccp = _("over an encrypted connection");
         else
# endif
            ccp = _("(unsafe clear text!)");
         n_err(_("POP3 APOP authentication failed!\n"
            "  Server indicated support..  Set *pop3-no-apop*\n"
            "  for plain text authentication %s\n"), ccp);
      }
      goto jleave;
   }
#endif

   rv = _pop3_auth_plain(mp, scp);
jleave:
   NYD_OU;
   return rv;
}

#ifdef mx_HAVE_MD5
static char *
_pop3_lookup_apop_timestamp(char const *bp)
{
   /* RFC 1939:
    * A POP3 server which implements the APOP command will include
    * a timestamp in its banner greeting.  The syntax of the timestamp
    * corresponds to the "msg-id" in [RFC822]
    * RFC 822:
    * msg-id   = "<" addr-spec ">"
    * addr-spec   = local-part "@" domain */
   char const *cp, *ep;
   uz tl;
   char *rp = NULL;
   boole hadat = FAL0;
   NYD_IN;

   if ((cp = su_cs_find_c(bp, '<')) == NULL)
      goto jleave;

   /* xxx What about malformed APOP timestamp (<@>) here? */
   for (ep = cp; *ep != '\0'; ++ep) {
      if (su_cs_is_space(*ep))
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

   tl = P2UZ(++ep - cp);
   rp = n_autorec_alloc(tl +1);
   su_mem_copy(rp, cp, tl);
   rp[tl] = '\0';
jleave:
   NYD_OU;
   return rp;
}
#endif

#ifdef mx_HAVE_MD5
static enum okay
_pop3_auth_apop(struct mailbox *mp, struct mx_socket_conn const *scp,
   char const *ts)
{
   unsigned char digest[16];
   char hex[MD5TOHEX_SIZE], *cp;
   md5_ctx ctx;
   uz i;
   enum okay rv = STOP;
   NYD_IN;

   md5_init(&ctx);
   md5_update(&ctx, (uc*)n_UNCONST(ts), su_cs_len(ts));
   md5_update(&ctx, (uc*)scp->sc_cred.cc_pass.s, scp->sc_cred.cc_pass.l);
   md5_final(digest, &ctx);
   md5tohex(hex, digest);

   i = scp->sc_cred.cc_user.l;
   cp = n_lofi_alloc(5 + i + 1 + MD5TOHEX_SIZE + sizeof(NETNL)-1 +1);

   su_mem_copy(cp, "APOP ", 5);
   su_mem_copy(cp + 5, scp->sc_cred.cc_user.s, i);
   i += 5;
   cp[i++] = ' ';
   su_mem_copy(cp + i, hex, MD5TOHEX_SIZE);
   i += MD5TOHEX_SIZE;
   su_mem_copy(cp + i, NETNL, sizeof(NETNL));
   POP3_OUT(rv, cp, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);

   rv = OKAY;
jleave:
   n_lofi_free(cp);
   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_MD5 */

static enum okay
_pop3_auth_plain(struct mailbox *mp, struct mx_socket_conn const *scp)
{
   char *cp;
   enum okay rv = STOP;
   NYD_IN;

   /* The USER/PASS plain text version */
   cp = n_lofi_alloc(MAX(scp->sc_cred.cc_user.l, scp->sc_cred.cc_pass.l) +
         5 + sizeof(NETNL)-1 +1);

   su_mem_copy(cp, "USER ", 5);
   su_mem_copy(cp + 5, scp->sc_cred.cc_user.s, scp->sc_cred.cc_user.l);
   su_mem_copy(cp + 5 + scp->sc_cred.cc_user.l, NETNL, sizeof(NETNL));
   POP3_OUT(rv, cp, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);

   su_mem_copy(cp, "PASS ", 5);
   su_mem_copy(cp + 5, scp->sc_cred.cc_pass.s, scp->sc_cred.cc_pass.l);
   su_mem_copy(cp + 5 + scp->sc_cred.cc_pass.l, NETNL, sizeof(NETNL));
   POP3_OUT(rv, cp, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);

   rv = OKAY;
jleave:
   n_lofi_free(cp);
   NYD_OU;
   return rv;
}

static void
pop3_timer_off(void)
{
   NYD_IN;
   if (_pop3_keepalive > 0) {
      alarm(0);
      safe_signal(SIGALRM, _pop3_savealrm);
   }
   NYD_OU;
}

static enum okay
pop3_answer(struct mailbox *mp)
{
   int i;
   uz blen;
   enum okay rv = STOP;
   NYD_IN;

jretry:
   if ((i = mx_socket_getline(&_pop3_buf, &_pop3_bufsize, &blen, mp->mb_sock)
         ) > 0) {
      if ((mp->mb_active & (MB_COMD | MB_MULT)) == MB_MULT)
         goto jmultiline;
      if (n_poption & n_PO_VERBVERB)
         n_err(_pop3_buf);
      switch (*_pop3_buf) {
      case '+':
         rv = OKAY;
         mp->mb_active &= ~MB_COMD;
         break;
      case '-':
         rv = STOP;
         mp->mb_active = MB_NONE;
         while (blen > 0 &&
               (_pop3_buf[blen - 1] == NETNL[0] ||
                _pop3_buf[blen - 1] == NETNL[1]))
            _pop3_buf[--blen] = '\0';
         n_err(_("POP3 error: %s\n"), _pop3_buf);
         break;
      default:
         /* If the answer starts neither with '+' nor with '-', it must be part
          * of a multiline response.  Get lines until a single dot appears */
jmultiline:
         while (_pop3_buf[0] != '.' || _pop3_buf[1] != NETNL[0] ||
               _pop3_buf[2] != NETNL[1] || _pop3_buf[3] != '\0') {
            i = mx_socket_getline(&_pop3_buf, &_pop3_bufsize, NULL,
                  mp->mb_sock);
            if (i <= 0)
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
   NYD_OU;
   return rv;
}

static enum okay
pop3_finish(struct mailbox *mp)
{
   NYD_IN;
   while (mp->mb_sock->s_fd > 0 && mp->mb_active != MB_NONE)
      pop3_answer(mp);
   NYD_OU;
   return OKAY;
}

static void
pop3catch(int s)
{
   NYD; /* Signal handler */
   switch (s) {
   case SIGINT:
      /*n_err_sighdl(_("Interrupt during POP3 operation\n"));*/
      interrupts = 2; /* Force "Interrupt" message shall we onintr(0) */
      siglongjmp(_pop3_jmp, 1);
   case SIGPIPE:
      n_err_sighdl(_("Received SIGPIPE during POP3 operation\n"));
      break;
   }
}

static void
_pop3_maincatch(int s)
{
   NYD; /* Signal handler */
   UNUSED(s);
   if (interrupts == 0)
      n_err_sighdl(_("\n(Interrupt -- one more to abort operation)\n"));
   else {
      interrupts = 1;
      siglongjmp(_pop3_jmp, 1);
   }
}

static enum okay
pop3_noop1(struct mailbox *mp)
{
   enum okay rv;
   NYD_IN;

   POP3_OUT(rv, "NOOP" NETNL, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_OU;
   return rv;
}

static void
pop3alarm(int s)
{
   n_sighdl_t volatile saveint, savepipe;
   NYD; /* Signal handler */
   UNUSED(s);

   if (_pop3_lock++ == 0) {
      mx_sigs_all_holdx();
      if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &_pop3_maincatch);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if (sigsetjmp(_pop3_jmp, 1)) {
         interrupts = 0;
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         goto jbrk;
      }
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, pop3catch);
      mx_sigs_all_rele();
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
   char const *cp;
   enum okay rv;
   NYD_IN;

   POP3_OUT(rv, "STAT" NETNL, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);

   for (cp = _pop3_buf; *cp != '\0' && !su_cs_is_space(*cp); ++cp)
      ;
   while (*cp != '\0' && su_cs_is_space(*cp))
      ++cp;

   rv = STOP;
   if (*cp != '\0') {
      uz i;

      if(su_idec_uz_cp(&i, cp, 10, &cp) & su_IDEC_STATE_EMASK)
         goto jerr;
      if(i > INT_MAX)
         goto jerr;
      *cnt = (int)i;

      while(*cp != '\0' && !su_cs_is_space(*cp))
         ++cp;
      while(*cp != '\0' && su_cs_is_space(*cp))
         ++cp;

      if(*cp == '\0')
         goto jerr;
      if(su_idec_uz_cp(&i, cp, 10, NULL) & su_IDEC_STATE_EMASK)
         goto jerr;
      *size = (off_t)i;
      rv = OKAY;
   }

   if (rv == STOP)
jerr:
      n_err(_("Invalid POP3 STAT response: %s\n"), _pop3_buf);
jleave:
   NYD_OU;
   return rv;
}

static enum okay
pop3_list(struct mailbox *mp, int n, uz *size)
{
   char o[LINESIZE], *cp;
   enum okay rv;
   NYD_IN;

   snprintf(o, sizeof o, "LIST %u" NETNL, n);
   POP3_OUT(rv, o, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);

   for (cp = _pop3_buf; *cp != '\0' && !su_cs_is_space(*cp); ++cp)
      ;
   while (*cp != '\0' && su_cs_is_space(*cp))
      ++cp;
   while (*cp != '\0' && !su_cs_is_space(*cp))
      ++cp;
   while (*cp != '\0' && su_cs_is_space(*cp))
      ++cp;
   if (*cp != '\0')
      su_idec_uz_cp(size, cp, 10, NULL);
jleave:
   NYD_OU;
   return rv;
}

static void
pop3_setptr(struct mailbox *mp, struct mx_socket_conn const *scp)
{
   uz i;
   enum needspec ns;
   NYD_IN;

   message = n_calloc(msgCount + 1, sizeof *message);
   message[msgCount].m_size = 0;
   message[msgCount].m_lines = 0;
   dot = message; /* (Just do it: avoid crash -- shall i now do ointr(0).. */

   for (i = 0; UCMP(z, i, <, msgCount); ++i) {
      struct message *m = message + i;
      m->m_flag = MUSED | MNEW | MNOFROM | MNEWEST;
      m->m_block = 0;
      m->m_offset = 0;
      m->m_size = m->m_xsize = 0;
   }

   for (i = 0; UCMP(z, i, <, msgCount); ++i)
      if (!pop3_list(mp, i + 1, &message[i].m_xsize))
         goto jleave;

   /* Force the load of all messages right now */
   ns = xok_blook(pop3_bulk_load, &scp->sc_url, OXM_ALL)
         ? NEED_BODY : NEED_HEADER;
   for (i = 0; UCMP(z, i, <, msgCount); ++i)
      if (!pop3_get(mp, message + i, ns))
         goto jleave;

   srelax_hold();
   for (i = 0; UCMP(z, i, <, msgCount); ++i) {
      struct message *m = message + i;
      char const *cp;

      if ((cp = hfield1("status", m)) != NULL)
         while (*cp != '\0') {
            if (*cp == 'R')
               m->m_flag |= MREAD;
            else if (*cp == 'O')
               m->m_flag &= ~MNEW;
            ++cp;
         }

      substdate(m);
      srelax();
   }
   srelax_rele();

   setdot(message);
jleave:
   NYD_OU;
}

static enum okay
pop3_get(struct mailbox *mp, struct message *m, enum needspec volatile need)
{
   char o[LINESIZE], *line, *lp;
   n_sighdl_t volatile saveint, savepipe;
   uz linesize, linelen, size;
   int number, lines;
   int volatile emptyline;
   off_t offset;
   enum okay volatile rv;
   NYD_IN;

   mx_fs_linepool_aquire(&line, &linesize);
   saveint = savepipe = SIG_IGN;
   number = (int)P2UZ(m - message + 1);
   emptyline = 0;
   rv = STOP;

   if (mp->mb_sock == NIL || mp->mb_sock->s_fd < 0) {
      n_err(_("POP3 connection already closed\n"));
      ++_pop3_lock;
      goto jleave;
   }

   if (_pop3_lock++ == 0) {
      mx_sigs_all_holdx();
      if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &_pop3_maincatch);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if (sigsetjmp(_pop3_jmp, 1))
         goto jleave;
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, pop3catch);
      mx_sigs_all_rele();
   }

   fseek(mp->mb_otf, 0L, SEEK_END);
   offset = ftell(mp->mb_otf);
jretry:
   switch (need) {
   case NEED_HEADER:
      snprintf(o, sizeof o, "TOP %u 0" NETNL, number);
      break;
   case NEED_BODY:
      snprintf(o, sizeof o, "RETR %u" NETNL, number);
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
   while (mx_socket_getline(&line, &linesize, &linelen, mp->mb_sock) > 0) {
      if (line[0] == '.' && line[1] == NETNL[0] && line[2] == NETNL[1] &&
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
      if (emptyline && is_head(lp, linelen, FAL0)) {
         putc('>', mp->mb_otf);
         ++size;
      }
      lines++;
      if (lp[linelen-1] == NETNL[1] &&
            (linelen == 1 || lp[linelen-2] == NETNL[0])) {
         emptyline = linelen <= 2;
         if (linelen > 2)
            fwrite(lp, 1, linelen - 2, mp->mb_otf);
         putc('\n', mp->mb_otf);
         size += linelen - 1;
      } else {
         emptyline = 0;
         fwrite(lp, 1, linelen, mp->mb_otf);
         size += linelen;
      }
   }
   if (!emptyline) {
      /* TODO This is very ugly; but some POP3 daemons don't end a
       * TODO message with NETNL NETNL, and we need \n\n for mbox format.
       * TODO That is to say we do it wrong here in order to get it right
       * TODO when send.c stuff or with MBOX handling, even though THIS
       * TODO line is solely a property of the MBOX database format! */
      putc('\n', mp->mb_otf);
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
      m->m_content_info |= CI_HAVE_HEADER;
      break;
   case NEED_BODY:
      m->m_content_info |= CI_HAVE_HEADER | CI_HAVE_BODY;
      m->m_xlines = m->m_lines;
      m->m_xsize = m->m_size;
      break;
   case NEED_UNSPEC:
      break;
   }

   rv = OKAY;
jleave:
   mx_fs_linepool_release(line, linesize);
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, saveint);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, savepipe);
   --_pop3_lock;
   NYD_OU;
   if (interrupts)
      n_raise(SIGINT);
   return rv;
}

static enum okay
pop3_exit(struct mailbox *mp)
{
   enum okay rv;
   NYD_IN;

   POP3_OUT(rv, "QUIT" NETNL, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_OU;
   return rv;
}

static enum okay
pop3_delete(struct mailbox *mp, int n)
{
   char o[LINESIZE];
   enum okay rv;
   NYD_IN;

   snprintf(o, sizeof o, "DELE %u" NETNL, n);
   POP3_OUT(rv, o, MB_COMD, goto jleave);
   POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_OU;
   return rv;
}

static enum okay
pop3_update(struct mailbox *mp)
{
   struct message *m;
   int dodel, c, gotcha, held;
   NYD_IN;

   if (!(n_pstate & n_PS_EDIT)) {
      holdbits();
      c = 0;
      for (m = message; PCMP(m, <, message + msgCount); ++m)
         if (m->m_flag & MBOX)
            ++c;
      if (c > 0)
         makembox();
   }

   gotcha = held = 0;
   for (m = message; PCMP(m, <, message + msgCount); ++m) {
      if (n_pstate & n_PS_EDIT)
         dodel = m->m_flag & MDELETED;
      else
         dodel = !((m->m_flag & MPRESERVE) || !(m->m_flag & MTOUCH));
      if (dodel) {
         pop3_delete(mp, P2UZ(m - message + 1));
         ++gotcha;
      } else
         ++held;
   }

   /* C99 */{
      char const *dnq;

      dnq = n_shexp_quote_cp(displayname, FAL0);

      if (gotcha && (n_pstate & n_PS_EDIT)) {
         fprintf(n_stdout, _("%s "), dnq);
         fprintf(n_stdout, (ok_blook(bsdcompat) || ok_blook(bsdmsgs))
            ? _("complete\n") : _("updated\n"));
      } else if (held && !(n_pstate & n_PS_EDIT)) {
         if (held == 1)
            fprintf(n_stdout, _("Held 1 message in %s\n"), dnq);
         else
            fprintf(n_stdout, _("Held %d messages in %s\n"), held, dnq);
      }
   }
   fflush(n_stdout);
   NYD_OU;
   return OKAY;
}

enum okay
mx_pop3_noop(void)
{
   n_sighdl_t volatile saveint, savepipe;
   enum okay volatile rv = STOP;
   NYD_IN;

   _pop3_lock = 1;
   mx_sigs_all_holdx();
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_pop3_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(_pop3_jmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, pop3catch);
      mx_sigs_all_rele();
      rv = pop3_noop1(&mb);
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   _pop3_lock = 0;
   NYD_OU;
   return rv;
}

int
mx_pop3_setfile(char const *who, char const *server, enum fedit_mode fm)
{
   struct mx_socket_conn sc;
   n_sighdl_t saveint, savepipe;
   char const *cp;
   int volatile rv;
   NYD_IN;

   rv = 1;
   if (fm & FEDIT_NEWMAIL)
      goto jleave;
   rv = -1;

   if (!url_parse(&sc.sc_url, CPROTO_POP3, server))
      goto jleave;
   if (ok_vlook(v15_compat) == su_NIL &&
         (!(sc.sc_url.url_flags & n_URL_HAD_USER) ||
            sc.sc_url.url_pass.s != NULL)) {
      n_err(_("New-style URL used without *v15-compat* being set\n"));
      goto jleave;
   }

   if (!((ok_vlook(v15_compat) != su_NIL)
         ? ccred_lookup(&sc.sc_cred, &sc.sc_url)
         : ccred_lookup_old(&sc.sc_cred, CPROTO_POP3,
            ((sc.sc_url.url_flags & n_URL_HAD_USER)
             ? sc.sc_url.url_eu_h_p.s
             : sc.sc_url.url_u_h_p.s))))
      goto jleave;

   if (!quit(FAL0))
      goto jleave;

   sc.sc_sock = su_TALLOC(struct mx_socket, 1);
   if(!mx_socket_open(sc.sc_sock, &sc.sc_url)){
      su_FREE(sc.sc_sock);
      goto jleave;
   }

   rv = 1;

   if (fm & FEDIT_SYSBOX)
      n_pstate &= ~n_PS_EDIT;
   else
      n_pstate |= n_PS_EDIT;

   if(mb.mb_sock != NIL){
      if(mb.mb_sock->s_fd >= 0)
         mx_socket_close(mb.mb_sock);
      su_FREE(mb.mb_sock);
      mb.mb_sock = NIL;
   }

   if(mb.mb_itf != NIL){
      fclose(mb.mb_itf);
      mb.mb_itf = NIL;
   }
   if(mb.mb_otf != NIL){
      fclose(mb.mb_otf);
      mb.mb_otf = NIL;
   }

   initbox(sc.sc_url.url_p_u_h_p);
   mb.mb_type = MB_VOID;
   _pop3_lock = 1;

   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(_pop3_jmp, 1)) {
      mx_socket_close(sc.sc_sock);
      su_FREE(sc.sc_sock);
      n_err(_("POP3 connection closed\n"));
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      _pop3_lock = 0;
      rv = -1;
      if (interrupts > 0)
         n_raise(SIGINT);
      goto jleave;
   }
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, pop3catch);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, pop3catch);

   if ((cp = xok_vlook(pop3_keepalive, &sc.sc_url, OXM_ALL)) != NULL) {
      su_idec_s32_cp(&_pop3_keepalive, cp, 10, NULL);
      if (_pop3_keepalive > 0) {
         _pop3_savealrm = safe_signal(SIGALRM, pop3alarm);
         alarm(_pop3_keepalive);
      }
   }

   sc.sc_sock->s_desc = (sc.sc_url.url_flags & n_URL_TLS_REQUIRED)
         ? "POP3S" : "POP3";
   sc.sc_sock->s_onclose = pop3_timer_off;
   mb.mb_sock = sc.sc_sock;

   if (_pop3_login(&mb, &sc) != OKAY ||
         pop3_stat(&mb, &mailsize, &msgCount) != OKAY) {
      mb.mb_sock = NIL;
      mx_socket_close(sc.sc_sock);
      su_FREE(sc.sc_sock);
      pop3_timer_off();
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      _pop3_lock = 0;
      goto jleave;
   }

   setmsize(msgCount);
   mb.mb_type = MB_POP3;
   mb.mb_perm = ((n_poption & n_PO_R_FLAG) || (fm & FEDIT_RDONLY))
         ? 0 : MB_DELE;
   pop3_setptr(&mb, &sc);

   /*if (!(fm & FEDIT_NEWMAIL)) */{
      n_pstate &= ~n_PS_SAW_COMMAND;
      n_pstate |= n_PS_SETFILE_OPENED;
   }

   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   _pop3_lock = 0;

   if ((n_poption & (n_PO_EXISTONLY | n_PO_HEADERLIST)) == n_PO_EXISTONLY) {
      rv = (msgCount == 0);
      goto jleave;
   }

   if (!(n_pstate & n_PS_EDIT) && msgCount == 0) {
      if (!ok_blook(emptystart))
         n_err(_("No mail for %s at %s\n"), who, sc.sc_url.url_p_eu_h_p);
      goto jleave;
   }

   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

enum okay
mx_pop3_header(struct message *m)
{
   enum okay rv;
   NYD_IN;

   /* TODO no URL here, no OXM possible; (however it is used in setfile()..) */
   rv = pop3_get(&mb, m, (ok_blook(pop3_bulk_load) ? NEED_BODY : NEED_HEADER));
   NYD_OU;
   return rv;
}

enum okay
mx_pop3_body(struct message *m)
{
   enum okay rv;
   NYD_IN;

   rv = pop3_get(&mb, m, NEED_BODY);
   NYD_OU;
   return rv;
}

boole
mx_pop3_quit(boole hold_sigs_on)
{
   n_sighdl_t volatile saveint, savepipe;
   boole rv;
   NYD_IN;

   if(hold_sigs_on)
      rele_sigs();

   rv = FAL0;

   if (mb.mb_sock == NIL || mb.mb_sock->s_fd < 0) {
      n_err(_("POP3 connection already closed\n"));
      rv = TRU1;
      goto jleave;
   }

   _pop3_lock = 1;
   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(_pop3_jmp, 1)) {
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      _pop3_lock = 0;
      interrupts = 0;
      goto jleave;
   }
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, pop3catch);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, pop3catch);
   pop3_update(&mb);
   pop3_exit(&mb);
   mx_socket_close(mb.mb_sock);
   su_FREE(mb.mb_sock);
   mb.mb_sock = NIL;
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   _pop3_lock = 0;

   rv = TRU1;
jleave:
   if(hold_sigs_on)
      hold_sigs();
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_POP3 */
/* s-it-mode */
