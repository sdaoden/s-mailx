/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of net-smtp.h.
 *@ TODO - use initial responses to save a round-trip (RFC 4954)
 *@ TODO - more (verbose) understanding+rection upon STATUS CODES
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause
 */
/*
 * Copyright (c) 2000
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
#define su_FILE net_smtp
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_SMTP
#include <sys/socket.h>

#include <su/cs.h>
#include <su/mem.h>

#include "mx/file-streams.h"
#include "mx/names.h"
#include "mx/sigs.h"
#include "mx/net-socket.h"

#ifdef mx_HAVE_GSSAPI
# include "mx/net-smtp-gss.h" /* $(MX_SRCDIR) */
#endif

#include "mx/net-smtp.h"
#include "su/code-in.h"

struct a_netsmtp_line{
   struct str sl_dat;
   struct str sl_buf;
};

static sigjmp_buf a_netsmtp_jmp;

static void a_netsmtp_onterm(int signo);

/* Get the SMTP server's answer, expecting val */
static int a_netsmtp_read(struct mx_socket *sp, struct a_netsmtp_line *slp,
      int val, boole ign_eof, boole want_dat);

/* Talk to a SMTP server */
static boole a_netsmtp_talk(struct mx_socket *sp, struct sendbundle *sbp);

#ifdef mx_HAVE_GSSAPI
# include <mx/net-smtp-gss.h>
#endif

/* Indirect SMTP I/O */
#define a_OUT(X) \
do{\
   char const *__cx__ = (X);\
   \
   if(n_poption & n_PO_D_VV){\
      /* TODO for now n_err() cannot normalize newlines in %s expansions */\
      char *__x__, *__y__;\
      uz __z__;\
      \
      __y__ = UNCONST(char*,__cx__);\
      __z__ = su_cs_len(__y__);\
      __x__ = n_lofi_alloc(__z__);\
      \
      su_mem_copy(__x__, __y__, __z__);\
      __y__ = &__x__[__z__];\
      \
      while(__y__ > __x__ && (__y__[-1] == '\n' || __y__[-1] == '\r'))\
         --__y__;\
      *__y__ = '\0';\
      n_err(">>> %s\n", __x__);\
      \
      n_lofi_free(__x__);\
   }\
   \
   if(!(n_poption & n_PO_DEBUG))\
      mx_socket_write(sop, __cx__);\
}while(0)

#define a_ANSWER(X, IGNEOF, WANTDAT) \
do if(!(n_poption & n_PO_DEBUG)) {\
   int y;\
   \
   if((y = a_netsmtp_read(sop, slp, X, IGNEOF, WANTDAT)) != (X) &&\
         (!(IGNEOF) || y != -1))\
      goto jleave;\
}while(0)

static void
a_netsmtp_onterm(int signo){
   UNUSED(signo);
   siglongjmp(a_netsmtp_jmp, 1);
}

static int
a_netsmtp_read(struct mx_socket *sop, struct a_netsmtp_line *slp, int val,
      boole ign_eof, boole want_dat){
   int rv, len;
   char *cp;
   NYD_IN;

   do{
      if((len = mx_socket_getline(&slp->sl_buf.s, &slp->sl_buf.l, NIL, sop)
            ) < 6){
         if(len >= 0 && !ign_eof)
            n_err(_("Unexpected EOF on SMTP connection\n"));
         rv = -1;
         goto jleave;
      }
      if(n_poption & n_PO_VERBVERB)
         n_err(">>> %s", slp->sl_buf.s);

      switch(slp->sl_buf.s[0]){
      case '1': rv = 1; break;
      case '2': rv = 2; break;
      case '3': rv = 3; break;
      case '4': rv = 4; break;
      default: rv = 5; break;
      }
      if(val != rv)
         n_err(_("smtp-server: %s"), slp->sl_buf.s);
   }while(slp->sl_buf.s[3] == '-');

   if(want_dat){
      for(cp = slp->sl_buf.s; su_cs_is_digit(*cp); --len, ++cp)
         ;
      for(; su_cs_is_blank(*cp); --len, ++cp)
         ;
      slp->sl_dat.s = cp;
      ASSERT(len >= 2);
      len -= 2;
      cp[slp->sl_dat.l = S(uz,len)] = '\0';
   }

jleave:
   NYD_OU;
   return rv;
}

static boole
a_netsmtp_talk(struct mx_socket *sop, struct sendbundle *sbp){
   char o[LINESIZE]; /* TODO n_string++ */
   char const *hostname;
   struct a_netsmtp_line _sl, *slp = &_sl;
   struct str b64;
   struct mx_name *np;
   uz blen, cnt;
   boole inhdr, inbcc, rv;
   NYD_IN;

   inhdr = TRU1;
   inbcc = FAL0;
   rv = FAL0;
   hostname = n_nodename(TRU1);
   su_mem_set(slp, 0, sizeof(*slp));

   /* Read greeting */
   a_ANSWER(2, FAL0, FAL0);

#ifdef mx_HAVE_TLS
   if(!sop->s_use_tls &&
         xok_blook(smtp_use_starttls, sbp->sb_url, OXM_ALL)){
      snprintf(o, sizeof o, NETLINE("EHLO %s"), hostname);
      a_OUT(o);
      a_ANSWER(2, FAL0, FAL0);

      a_OUT(NETLINE("STARTTLS"));
      a_ANSWER(2, FAL0, FAL0);

      if(!(n_poption & n_PO_DEBUG) && !n_tls_open(sbp->sb_url, sop))
         goto jleave;
   }
#else
   if(xok_blook(smtp_use_starttls, sbp->sb_url, OXM_ALL)){
      n_err(_("No TLS support compiled in\n"));
      goto jleave;
   }
#endif

   /* Shorthand: no authentication, plain HELO? */
   if(sbp->sb_ccred.cc_authtype == AUTHTYPE_NONE){
      snprintf(o, sizeof o, NETLINE("HELO %s"), hostname);
      a_OUT(o);
      a_ANSWER(2, FAL0, FAL0);
      goto jsend;
   }

   /* We'll have to deal with authentication */
   snprintf(o, sizeof o, NETLINE("EHLO %s"), hostname);
   a_OUT(o);
   a_ANSWER(2, FAL0, FAL0);

   switch(sbp->sb_ccred.cc_authtype){
   default:
      /* FALLTHRU (doesn't happen) */
   case AUTHTYPE_PLAIN:
      cnt = sbp->sb_ccred.cc_user.l;
      if(sbp->sb_ccred.cc_pass.l >= UZ_MAX - 2 ||
            cnt >= UZ_MAX - 2 - sbp->sb_ccred.cc_pass.l){
jerr_cred:
         n_err(_("Credentials overflow buffer sizes\n"));
         goto jleave;
      }
      cnt += sbp->sb_ccred.cc_pass.l;

      if(cnt >= sizeof(o) - 2)
         goto jerr_cred;
      cnt += 2;
      if(b64_encode_calc_size(cnt) == UZ_MAX)
         goto jerr_cred;

      a_OUT(NETLINE("AUTH PLAIN"));
      a_ANSWER(3, FAL0, FAL0);

      snprintf(o, sizeof o, "%c%s%c%s",
         '\0', sbp->sb_ccred.cc_user.s, '\0', sbp->sb_ccred.cc_pass.s);
      if(b64_encode_buf(&b64, o, cnt, B64_SALLOC | B64_CRLF) == NIL)
         goto jleave;
      a_OUT(b64.s);
      a_ANSWER(2, FAL0, FAL0);
      break;
   case AUTHTYPE_LOGIN:
      if(b64_encode_calc_size(sbp->sb_ccred.cc_user.l) == UZ_MAX ||
            b64_encode_calc_size(sbp->sb_ccred.cc_pass.l) == UZ_MAX)
         goto jerr_cred;

      a_OUT(NETLINE("AUTH LOGIN"));
      a_ANSWER(3, FAL0, FAL0);

      if(b64_encode_buf(&b64, sbp->sb_ccred.cc_user.s, sbp->sb_ccred.cc_user.l,
            B64_SALLOC | B64_CRLF) == NIL)
         goto jleave;
      a_OUT(b64.s);
      a_ANSWER(3, FAL0, FAL0);

      if(b64_encode_buf(&b64, sbp->sb_ccred.cc_pass.s, sbp->sb_ccred.cc_pass.l,
            B64_SALLOC | B64_CRLF) == NIL)
         goto jleave;
      a_OUT(b64.s);
      a_ANSWER(2, FAL0, FAL0);
      break;
#ifdef mx_HAVE_MD5
   case AUTHTYPE_CRAM_MD5:{
      char *cp;

      a_OUT(NETLINE("AUTH CRAM-MD5"));
      a_ANSWER(3, FAL0, TRU1);

      if((cp = cram_md5_string(&sbp->sb_ccred.cc_user, &sbp->sb_ccred.cc_pass,
            slp->sl_dat.s)) == NIL)
         goto jerr_cred;
      a_OUT(cp);
      a_ANSWER(2, FAL0, FAL0);
      }break;
#endif
#ifdef mx_HAVE_GSSAPI
   case AUTHTYPE_GSSAPI:
      if(n_poption & n_PO_DEBUG)
         n_err(_(">>> We would perform GSS-API authentication now\n"));
      else if(!a_netsmtp_gss(sop, sbp, slp))
         goto jleave;
      break;
#endif
   }

jsend:
   snprintf(o, sizeof o, NETLINE("MAIL FROM:<%s>"), sbp->sb_url->url_u_h.s);
   a_OUT(o);
   a_ANSWER(2, FAL0, FAL0);

   for(np = sbp->sb_to; np != NIL; np = np->n_flink){
      if(!(np->n_type & GDEL)){ /* TODO should not happen!?! */
         if(np->n_flags & mx_NAME_ADDRSPEC_WITHOUT_DOMAIN)
            snprintf(o, sizeof o, NETLINE("RCPT TO:<%s@%s>"),
               np->n_name, hostname);
         else
            snprintf(o, sizeof o, NETLINE("RCPT TO:<%s>"), np->n_name);
         a_OUT(o);
         a_ANSWER(2, FAL0, FAL0);
      }
   }

   a_OUT(NETLINE("DATA"));
   a_ANSWER(3, FAL0, FAL0);

   fflush_rewind(sbp->sb_input);
   cnt = fsize(sbp->sb_input);
   while(fgetline(&slp->sl_buf.s, &slp->sl_buf.l, &cnt, &blen, sbp->sb_input,
         1) != NIL){
      if(inhdr){
         if(*slp->sl_buf.s == '\n')
            inhdr = inbcc = FAL0;
         else if(inbcc && su_cs_is_blank(*slp->sl_buf.s))
            continue;
         /* We know what we have generated first, so do not look for whitespace
          * before the ':' */
         else if(!su_cs_cmp_case_n(slp->sl_buf.s, "bcc:", 4)){
            inbcc = TRU1;
            continue;
         }else
            inbcc = FAL0;
      }

      if(*slp->sl_buf.s == '.' && !(n_poption & n_PO_DEBUG))
         mx_socket_write1(sop, ".", 1, 1); /* TODO I/O rewrite.. */
      slp->sl_buf.s[blen - 1] = NETNL[0];
      slp->sl_buf.s[blen] = NETNL[1];
      slp->sl_buf.s[blen + 1] = '\0';
      a_OUT(slp->sl_buf.s);
   }
   a_OUT(NETLINE("."));
   a_ANSWER(2, FAL0, FAL0);

   a_OUT(NETLINE("QUIT"));
   a_ANSWER(2, TRU1, FAL0);

   rv = TRU1;
jleave:
   if(slp->sl_buf.s != NIL)
      n_free(slp->sl_buf.s);
   NYD_OU;
   return rv;
}

#ifdef mx_HAVE_GSSAPI
# include <mx/net-smtp-gss.h>
#endif

#undef a_OUT
#undef a_ANSWER

boole
mx_smtp_mta(struct sendbundle *sbp){
   struct mx_socket so;
   n_sighdl_t volatile saveterm;
   boole volatile rv;
   NYD_IN;

   rv = FAL0;

   saveterm = safe_signal(SIGTERM, SIG_IGN);
   if(sigsetjmp(a_netsmtp_jmp, 1))
      goto jleave;
   if(saveterm != SIG_IGN)
      safe_signal(SIGTERM, &a_netsmtp_onterm);

   if(n_poption & n_PO_DEBUG)
      su_mem_set(&so, 0, sizeof so);
   else if(!mx_socket_open(&so, sbp->sb_url))
      goto jleave;

   so.s_desc = "SMTP";
   rv = a_netsmtp_talk(&so, sbp);

   if(!(n_poption & n_PO_DEBUG))
      mx_socket_close(&so);
jleave:
   safe_signal(SIGTERM, saveterm);
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_SMTP */
/* s-it-mode */
