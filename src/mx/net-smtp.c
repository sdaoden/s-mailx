/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of net-smtp.h.
 *@ TODO - use initial responses to save a round-trip (RFC 4954)
 *@ TODO - more (verbose) understanding+rection upon STATUS CODES
 *@ TODO - this is so dumb :(; except on macos we can shutdown.
 *@ TODO   do not care no more after 221? seen some short hangs.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define mx_SOURCE_NET_SMTP

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_SMTP
#include <sys/socket.h>

#include <su/cs.h>
#include <su/mem.h>

#include "mx/compat.h"
#include "mx/cred-auth.h"
#include "mx/cred-md5.h"
#include "mx/file-streams.h"
#include "mx/mime-enc.h"
#include "mx/names.h"
#include "mx/sigs.h"
#include "mx/net-socket.h"

#ifdef mx_HAVE_GSSAPI
# include "mx/net-gssapi.h" /* $(MX_SRCDIR) */
#endif

#include "mx/net-smtp.h"
#include "su/code-in.h"

enum a_netsmtp_flags{
   a_NETSMTP_CAP_NONE,

   a_NETSMTP_CAP_EHLO = 1u<<0, /* RFC 1869 */
   a_NETSMTP_CAP_PIPELINING = 1u<<1, /* RFC 2920 */
   a_NETSMTP_CAP_STARTTLS = 1u<<2, /* RFC 3207 */
   a_NETSMTP_CAP_ALL = a_NETSMTP_CAP_EHLO |
         a_NETSMTP_CAP_PIPELINING | a_NETSMTP_CAP_STARTTLS,

   a_NETSMTP_CAP_FORCE_TLS = 1u<<13,
   a_NETSMTP_CAP_PROP_ALL = a_NETSMTP_CAP_FORCE_TLS,

   a_NETSMTP_CAP_MASK = a_NETSMTP_CAP_ALL | a_NETSMTP_CAP_PROP_ALL,
   a_NETSMTP_CAP_READ_IS_HOT = 1u<<15
};

struct a_netsmtp_ctx{
   u32 nsc_config;
   u32 nsc_server_config;
   struct str nsc_dat;
   struct str nsc_buf;
};

static sigjmp_buf a_netsmtp_jmp;

static void a_netsmtp_onsig(int signo);

/* */
static u32 a_netsmtp_parse_config(struct mx_send_ctx *scp);

/* Get the SMTP server's answer, expecting val */
static int a_netsmtp_read(struct mx_socket *sop, struct a_netsmtp_ctx *nscp,
      int val, boole ign_eof, boole want_dat);

/* Talk to a SMTP server */
static boole a_netsmtp_talk(struct mx_socket *sop, struct mx_send_ctx *scp);

#ifdef mx_HAVE_GSSAPI
# include <mx/net-gssapi.h>
#endif

/* Indirect SMTP I/O */
#define a_SMTP_OUT(X) \
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
   if(!(n_poption & n_PO_D))\
      mx_socket_write(sop, __cx__);\
}while(0)

#define a_SMTP_ANSWER(X, IGNEOF, WANTDAT) \
do if(!(n_poption & n_PO_D)){\
   int y;\
   \
   if((y = a_netsmtp_read(sop, nscp, X, IGNEOF, WANTDAT)) != (X) &&\
         (!(IGNEOF) || y != -1))\
      goto jleave;\
}while(0)

static void
a_netsmtp_onsig(int signo){
   UNUSED(signo);
   siglongjmp(a_netsmtp_jmp, 1);
}

static u32
a_netsmtp_parse_config(struct mx_send_ctx *scp){
   struct capdis{
      u16 cd_on;
      u16 cd_off;
      char const cd_user_name[12];
   } const cda[] = {
      {a_NETSMTP_CAP_ALL, a_NETSMTP_CAP_MASK, "all\0"}, /* All */
      {a_NETSMTP_CAP_EHLO, a_NETSMTP_CAP_ALL, "ehlo\0"},
      {a_NETSMTP_CAP_PIPELINING, a_NETSMTP_CAP_PIPELINING, "pipelining\0"},
      {a_NETSMTP_CAP_STARTTLS, a_NETSMTP_CAP_STARTTLS, "starttls\0"},

      {a_NETSMTP_CAP_PROP_ALL, a_NETSMTP_CAP_PROP_ALL, "prop-all\0"},
      {a_NETSMTP_CAP_FORCE_TLS, a_NETSMTP_CAP_FORCE_TLS, "force-tls\0"}
   };
   char *buf;
   char const *ccp;
   u32 rv, i;
   NYD_IN;

   rv = a_NETSMTP_CAP_ALL;

   if((ccp = xok_vlook(smtp_config, scp->sc_urlp, OXM_ALL)) == NIL)
      goto jleave;

   for(buf = savestr(ccp); (ccp = su_cs_sep_c(&buf, ',', TRU1)) != NIL;){
      boole minus;

      if((minus = (*ccp == '-')) || *ccp == '-')
         ++ccp;
      for(i = 0;;)
         if(!su_cs_cmp_case(ccp, cda[i].cd_user_name)){
            if(minus)
               rv &= ~cda[i].cd_off;
            else
               rv |= cda[i].cd_on;
            break;
         }else if(++i == NELEM(cda)){
            n_err(_("*smtp-config*: unknown directive: %s\n"),
               n_shexp_quote_cp(ccp, FAL0));
            break;
         }
   }

jleave:
   NYD_OU;
   return rv;
}

static int
a_netsmtp_read(struct mx_socket *sop, struct a_netsmtp_ctx *nscp, int val,
      boole ign_eof, boole want_dat){
   char *cp;
   int rv, len;
   NYD_IN;

   do{
      if((len = mx_socket_getline(&nscp->nsc_buf.s, &nscp->nsc_buf.l, NIL, sop)
            ) < 6){
         if(len >= 0 && !ign_eof)
            n_err(_("Unexpected EOF on SMTP connection\n"));
         rv = -1;
         goto jleave;
      }

      for(; len > 0; --len){
         cp = &nscp->nsc_buf.s[len - 1];
         if(*cp != NETNL[0] && *cp != NETNL[1])
            break;
         *cp = '\0';
      }
      (nscp->nsc_dat.s = nscp->nsc_buf.s)[nscp->nsc_dat.l = S(uz,len)] = '\0';

      if(n_poption & n_PO_VV)
         n_err(">>> SERVER: %s\n", nscp->nsc_dat.s);

      switch(nscp->nsc_dat.s[0]){
      case '1': rv = 1; break;
      case '2': rv = 2; break;
      case '3': rv = 3; break;
      case '4': rv = 4; break;
      default: rv = 5; break;
      }
      if(UNLIKELY(val != rv)){
         n_err(_("SMTP: unexpected status from server: %s\n"),
            nscp->nsc_dat.s);
         goto jleave;
      }else if(UNLIKELY(nscp->nsc_server_config & a_NETSMTP_CAP_READ_IS_HOT)){
         if(!su_cs_cmp(&nscp->nsc_dat.s[4], "PIPELINING"))
            nscp->nsc_server_config |= a_NETSMTP_CAP_PIPELINING;
         else if(!su_cs_cmp(&nscp->nsc_dat.s[4], "STARTTLS"))
            nscp->nsc_server_config |= a_NETSMTP_CAP_STARTTLS;
      }
   }while(nscp->nsc_dat.s[3] == '-');

   if(rv == val && want_dat){
      for(cp = nscp->nsc_dat.s; len > 0 && su_cs_is_digit(*cp); --len, ++cp)
         ;
      for(; len > 0 && su_cs_is_blank(*cp); --len, ++cp)
         ;
      if(len < 2){
         rv = -2;
         goto jleave;
      }
      nscp->nsc_dat.s = cp;
      nscp->nsc_dat.l = S(uz,len);
   }

jleave:
   NYD_OU;
   return rv;
}

static boole
a_netsmtp_talk(struct mx_socket *sop, struct mx_send_ctx *scp){ /* TODO split*/
   enum{
      a_ERROR = 1u<<0,
      a_IS_OAUTHBEARER = 1u<<1,
      a_IN_HEAD = 1u<<2,
      a_IN_BCC = 1u<<3
   };

   char o[LINESIZE]; /* TODO n_string++ */
   struct a_netsmtp_ctx nsc_b, *nscp = &nsc_b;
   struct str b64;
   struct mx_name *np;
   u8 f;
   uz resp2_cnt, blen, cnt;
   char const *hostname;
   NYD_IN;

   hostname = n_nodename(TRU1);
   /* With the pipelining extension we may be able to save (an) authentication
    * roundtrip(s), too, so start counting expected 2xx responses beforehand */
   resp2_cnt = 0;

   su_mem_set(nscp, 0, sizeof(*nscp));
   nscp->nsc_config = a_netsmtp_parse_config(scp);

   f = a_ERROR | a_IN_HEAD;

   /* Read greeting */
   a_SMTP_ANSWER(2, FAL0, FAL0);

   /* The initial EHLO */
   if(nscp->nsc_config & a_NETSMTP_CAP_EHLO){
      int sr;

      snprintf(o, sizeof o, NETLINE("EHLO %s"), hostname);
      a_SMTP_OUT(o);
      if(!(n_poption & n_PO_D)){
         nscp->nsc_server_config = a_NETSMTP_CAP_READ_IS_HOT |
               a_NETSMTP_CAP_EHLO;
         sr = a_netsmtp_read(sop, nscp, 2, FAL0, FAL0);
         nscp->nsc_server_config ^= a_NETSMTP_CAP_READ_IS_HOT;
      }else{
         sr = 2;
         nscp->nsc_server_config = a_NETSMTP_CAP_MASK;
      }

      /* Keep only user desires */
      nscp->nsc_server_config &= nscp->nsc_config;

      if(sr != 2){
         if(sr == -1)
            goto jleave;
         nscp->nsc_server_config = nscp->nsc_config = a_NETSMTP_CAP_NONE;
         /* TODO RFC 1869 gives detailed error codes etc. but of course
          * TODO this is all we (can) do for now, anyway */
         switch(scp->sc_credp->cc_authtype){
         case mx_CRED_AUTHTYPE_NONE:
            goto jhelo;
         default:
            n_err(_("SMTP server incapable of EHLO, "
               "the chosen authentication is not possible\n"));
            goto jleave;
         }
      }
   }else if(scp->sc_credp->cc_authtype == mx_CRED_AUTHTYPE_NONE){
jhelo:
      nscp->nsc_config &= ~a_NETSMTP_CAP_ALL;
      nscp->nsc_server_config &= ~a_NETSMTP_CAP_ALL;
      snprintf(o, sizeof o, NETLINE("HELO %s"), hostname);
      a_SMTP_OUT(o);
      a_SMTP_ANSWER(2, FAL0, FAL0);
      /* Skip TLS + Authentication */
      goto jsend;
   }else{
      n_err(_("The chosen authentication is not possible with "
         "the given *smtp-config*\n"));
      goto jleave;
   }

   /* In a session state; if unencrypted, try to upgrade */
#ifdef mx_HAVE_TLS
   if(!sop->s_use_tls){
      if((nscp->nsc_config & a_NETSMTP_CAP_STARTTLS) &&
            ((nscp->nsc_config & a_NETSMTP_CAP_FORCE_TLS) ||
             scp->sc_credp->cc_needs_tls ||
             (nscp->nsc_server_config & a_NETSMTP_CAP_STARTTLS) ||
             xok_blook(smtp_use_starttls, scp->sc_urlp, OXM_ALL))){
         a_SMTP_OUT(NETLINE("STARTTLS"));
         a_SMTP_ANSWER(2, FAL0, FAL0);

         if(!(n_poption & n_PO_D) && !n_tls_open(scp->sc_urlp, sop))
            goto jleave;

         /* Capabilities might have changed after STARTTLS */
         snprintf(o, sizeof o, NETLINE("EHLO %s"), hostname);
         a_SMTP_OUT(o);
         nscp->nsc_server_config &= ~a_NETSMTP_CAP_ALL;
         nscp->nsc_server_config |= a_NETSMTP_CAP_READ_IS_HOT;
         a_SMTP_ANSWER(2, FAL0, FAL0);
         nscp->nsc_server_config ^= a_NETSMTP_CAP_READ_IS_HOT;
         nscp->nsc_server_config &= nscp->nsc_config;
      }else if(scp->sc_credp->cc_needs_tls){
         n_err(_("SMTP authentication %s needs TLS "
               "(see manual, *smtp-use-starttls*)\n"),
            scp->sc_credp->cc_auth);
         goto jleave;
      }
   }
#else
   if((nscp->nsc_config & a_NETSMTP_CAP_FORCE_TLS) ||
         scp->sc_credp->cc_needs_tls ||
         xok_blook(smtp_use_starttls, scp->sc_urlp, OXM_ALL)){
      n_err(_("No TLS support compiled in\n"));
      goto jleave;
   }
#endif /* mx_HAVE_TLS */

   switch(scp->sc_credp->cc_authtype){
   case mx_CRED_AUTHTYPE_NONE:
      break;
   case mx_CRED_AUTHTYPE_OAUTHBEARER:
      f |= a_IS_OAUTHBEARER;
      /* FALLTHRU */
   case mx_CRED_AUTHTYPE_PLAIN:
   default: /* (this does not happen) */
      /* Calculate required storage */
      cnt = scp->sc_credp->cc_user.l;
#define a_MAX \
   (2 + sizeof("AUTH XOAUTH2 " "user=\001auth=Bearer \001\001" NETNL))

      if(scp->sc_credp->cc_pass.l >= UZ_MAX - a_MAX ||
            cnt >= UZ_MAX - a_MAX - scp->sc_credp->cc_pass.l){
jerr_cred:
         n_err(_("Credentials overflow buffer sizes\n"));
         goto jleave;
      }
      cnt += scp->sc_credp->cc_pass.l;

      cnt += a_MAX;
      if((cnt = mx_b64_enc_calc_size(cnt)) == UZ_MAX)
         goto jerr_cred;
      if(cnt >= sizeof(o))
         goto jerr_cred;
#undef a_MAX

      /* Then create login query */
      if(f & a_IS_OAUTHBEARER){
         int i;

         i = snprintf(o, sizeof o, "user=%s\001auth=Bearer %s\001\001",
            scp->sc_credp->cc_user.s, scp->sc_credp->cc_pass.s);
         if(mx_b64_enc_buf(&b64, o, i, mx_B64_AUTO_ALLOC) == NIL)
            goto jleave;
         snprintf(o, sizeof o, NETLINE("AUTH XOAUTH2 %s"), b64.s);
         b64.s = o;
      }else{
         int i;

         a_SMTP_OUT(NETLINE("AUTH PLAIN"));
         a_SMTP_ANSWER(3, FAL0, FAL0);

         i = snprintf(o, sizeof o, "%c%s%c%s",
            '\0', scp->sc_credp->cc_user.s, '\0', scp->sc_credp->cc_pass.s);
         if(mx_b64_enc_buf(&b64, o, i, mx_B64_AUTO_ALLOC | mx_B64_CRLF
               ) == NIL)
            goto jleave;
      }
      a_SMTP_OUT(b64.s);
      ++resp2_cnt;
      if(!(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING))
         a_SMTP_ANSWER(2, FAL0, FAL0);
      /* TODO OAUTHBEARER ERROR: send empty message to gain actual error
       * message (when status was 334) */
      break;

   case mx_CRED_AUTHTYPE_EXTERNAL:
#define a_MAX (sizeof("AUTH EXTERNAL " NETNL))
      cnt = mx_b64_enc_calc_size(scp->sc_credp->cc_user.l);
      if(/*cnt == UZ_MAX ||*/ cnt >= sizeof(o) - a_MAX)
         goto jerr_cred;
#undef a_MAX

      su_mem_copy(o, "AUTH EXTERNAL ", sizeof("AUTH EXTERNAL ") -1);
      b64.s = &o[sizeof("AUTH EXTERNAL ") -1];
      mx_b64_enc_buf(&b64, scp->sc_credp->cc_user.s,
         scp->sc_credp->cc_user.l, mx_B64_BUF | mx_B64_CRLF);
      a_SMTP_OUT(o);
      ++resp2_cnt;
      if(!(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING))
         a_SMTP_ANSWER(2, FAL0, FAL0);
      break;

   case mx_CRED_AUTHTYPE_EXTERNANON:
      a_SMTP_OUT(NETLINE("AUTH EXTERNAL ="));
      ++resp2_cnt;
      if(!(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING))
         a_SMTP_ANSWER(2, FAL0, FAL0);
      break;

   case mx_CRED_AUTHTYPE_LOGIN:
      if(mx_b64_enc_calc_size(scp->sc_credp->cc_user.l) == UZ_MAX ||
            mx_b64_enc_calc_size(scp->sc_credp->cc_pass.l) == UZ_MAX)
         goto jerr_cred;

      a_SMTP_OUT(NETLINE("AUTH LOGIN"));
      a_SMTP_ANSWER(3, FAL0, FAL0);

      if(mx_b64_enc_buf(&b64, scp->sc_credp->cc_user.s,
            scp->sc_credp->cc_user.l, mx_B64_AUTO_ALLOC | mx_B64_CRLF) == NIL)
         goto jleave;
      a_SMTP_OUT(b64.s);
      a_SMTP_ANSWER(3, FAL0, FAL0);

      if(mx_b64_enc_buf(&b64, scp->sc_credp->cc_pass.s,
            scp->sc_credp->cc_pass.l, mx_B64_AUTO_ALLOC | mx_B64_CRLF) == NIL)
         goto jleave;
      a_SMTP_OUT(b64.s);
      ++resp2_cnt;
      if(!(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING))
         a_SMTP_ANSWER(2, FAL0, FAL0);
      break;

#ifdef mx_HAVE_MD5
   case mx_CRED_AUTHTYPE_CRAM_MD5:{
      char *cp;

      a_SMTP_OUT(NETLINE("AUTH CRAM-MD5"));
      a_SMTP_ANSWER(3, FAL0, TRU1);

      if((cp = mx_md5_cram_string(&scp->sc_credp->cc_user,
            &scp->sc_credp->cc_pass, nscp->nsc_dat.s)) == NIL)
         goto jerr_cred;
      a_SMTP_OUT(cp);
      ++resp2_cnt;
      if(!(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING))
         a_SMTP_ANSWER(2, FAL0, FAL0);
      }break;
#endif

#ifdef mx_HAVE_GSSAPI
   case mx_CRED_AUTHTYPE_GSSAPI:
      if(n_poption & n_PO_D)
         n_err(_(">>> We would perform GSS-API authentication now\n"));
      else if(!su_CONCAT(su_FILE,_gss)(sop, scp->sc_urlp, scp->sc_credp, nscp))
         goto jleave;
      break;
#endif
   }

jsend:
   snprintf(o, sizeof o, NETLINE("MAIL FROM:<%s>"), scp->sc_urlp->url_u_h.s);
   a_SMTP_OUT(o);
   ++resp2_cnt;
   if(!(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING))
      a_SMTP_ANSWER(2, FAL0, FAL0);

   for(np = scp->sc_to; np != NIL; np = np->n_flink){
      if(!(np->n_type & GDEL)){ /* TODO should not happen!?! */
         if(np->n_flags & mx_NAME_ADDRSPEC_WITHOUT_DOMAIN)
            snprintf(o, sizeof o, NETLINE("RCPT TO:<%s@%s>"),
               np->n_name, hostname);
         else
            snprintf(o, sizeof o, NETLINE("RCPT TO:<%s>"), np->n_name);
         a_SMTP_OUT(o);
         ++resp2_cnt;
         if(!(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING))
            a_SMTP_ANSWER(2, FAL0, FAL0);
      }
   }

   a_SMTP_OUT(NETLINE("DATA"));
   if(nscp->nsc_server_config & a_NETSMTP_CAP_PIPELINING)
      while(resp2_cnt-- > 0)
         a_SMTP_ANSWER(2, FAL0, FAL0);
   a_SMTP_ANSWER(3, FAL0, FAL0);

   fflush_rewind(scp->sc_input);
   cnt = fsize(scp->sc_input);
   while(fgetline(&nscp->nsc_buf.s, &nscp->nsc_buf.l, &cnt, &blen,
         scp->sc_input, TRU1) != NIL){
      if(f & a_IN_HEAD){
         if(*nscp->nsc_buf.s == '\n')
            f &= ~(a_IN_HEAD | a_IN_BCC);
         else if((f & a_IN_BCC) && su_cs_is_blank(*nscp->nsc_buf.s))
            continue;
         /* We know what we have generated first, so do not look for whitespace
          * before the ':' */
         else if(!su_cs_cmp_case_n(nscp->nsc_buf.s, "bcc:", 4)){
            f |= a_IN_BCC;
            continue;
         }else
            f &= ~a_IN_BCC;
      }

      if(*nscp->nsc_buf.s == '.' && !(n_poption & n_PO_D))
         mx_socket_write1(sop, ".", 1, 1); /* TODO I/O rewrite.. */
      nscp->nsc_buf.s[blen - 1] = NETNL[0];
      nscp->nsc_buf.s[blen] = NETNL[1];
      nscp->nsc_buf.s[blen + 1] = '\0';
      a_SMTP_OUT(nscp->nsc_buf.s);
   }
   if(ferror(scp->sc_input))
      goto jleave;
   a_SMTP_OUT(NETLINE("."));
   a_SMTP_ANSWER(2, FAL0, FAL0);

   a_SMTP_OUT(NETLINE("QUIT"));
   a_SMTP_ANSWER(2, TRU1, FAL0);

   f &= ~a_ERROR;
jleave:
   if(nscp->nsc_buf.s != NIL)
      n_free(nscp->nsc_buf.s);
   NYD_OU;
   return ((f & a_ERROR) == 0);
}

#ifdef mx_HAVE_GSSAPI
# include <mx/net-gssapi.h>
#endif

#undef a_SMTP_OUT
#undef a_SMTP_ANSWER

boole
mx_smtp_mta(struct mx_send_ctx *scp){
   struct mx_socket so;
   n_sighdl_t volatile saveterm, savepipe;
   boole volatile rv;
   NYD_IN;

   rv = FAL0;

   saveterm = safe_signal(SIGTERM, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if(sigsetjmp(a_netsmtp_jmp, 1))
      goto jleave;
   if(saveterm != SIG_IGN)
      safe_signal(SIGTERM, &a_netsmtp_onsig);
   safe_signal(SIGPIPE, &a_netsmtp_onsig);

   if(n_poption & n_PO_D)
      su_mem_set(&so, 0, sizeof so);
   else if(!mx_socket_open(&so, scp->sc_urlp))
      goto j_leave;

   so.s_desc = "SMTP";
   rv = a_netsmtp_talk(&so, scp);

jleave:
   if(!(n_poption & n_PO_D))
      mx_socket_close(&so);
j_leave:
   safe_signal(SIGPIPE, savepipe);
   safe_signal(SIGTERM, saveterm);
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_SMTP */
#undef mx_SOURCE_NET_SMTP
#undef su_FILE
/* s-it-mode */
