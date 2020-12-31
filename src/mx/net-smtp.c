/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of net-smtp.h.
 *@ TODO - more (verbose) understanding+rection upon STATUS CODES
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
#include "mx/cred-oauthbearer.h"
#include "mx/file-streams.h"
#include "mx/mime-enc.h"
#include "mx/names.h"
#include "mx/sigs.h"
#include "mx/net-socket.h"

#ifdef mx_HAVE_GSSAPI
# include "mx/cred-gssapi.h" /* $(MX_SRCDIR) */
#endif

#include "mx/net-smtp.h"
#include "su/code-in.h"

CTA(mx_CRED_AUTHTYPE_LASTBIT <= 15, "Flag bits excess storage type");
#define a_X(X) (mx_CRED_AUTHTYPE_LASTBIT + 1 + (X))
enum a_netsmtp_flags{
   /* *smtp-config* parser verifies some conditions.
    * The rest of this file assumes for example that if _AUTH is not set no
    * authentication types are set */
   a_NETSMTP_EXT_NONE,
   a_NETSMTP_EXT_EHLO = 1u<<a_X(1), /* RFC 1869 */
   a_NETSMTP_EXT_PIPELINING = 1u<<a_X(2), /* RFC 2920 */
   a_NETSMTP_EXT_STARTTLS = 1u<<a_X(3), /* RFC 3207 */
   a_NETSMTP_EXT_AUTH = 1u<<a_X(4), /* RFC 4954 */
   a_NETSMTP_EXT_ALL = a_NETSMTP_EXT_EHLO |
         a_NETSMTP_EXT_PIPELINING | a_NETSMTP_EXT_STARTTLS |
         a_NETSMTP_EXT_AUTH,

   a_NETSMTP_FORCE_TLS = 1u<<a_X(5),
   a_NETSMTP_FORCE_TLS_IFF = a_NETSMTP_EXT_NONE
#ifdef mx_HAVE_TLS
         | a_NETSMTP_FORCE_TLS
#endif
         ,

   /* cred_auth_type_verify_bits() says all possible mechs are set */
   /*a_NETSMTP_AUTH_ALLMECHS = 1u<<a_X(6),*/

   a_NETSMTP_ALL_MASK = a_NETSMTP_EXT_ALL | a_NETSMTP_FORCE_TLS_IFF |
         mx_CRED_PROTO_AUTHTYPES_SMTP,
   a_NETSMTP_ALL_AVAILABLE_MASK = a_NETSMTP_EXT_ALL | a_NETSMTP_FORCE_TLS_IFF |
         mx_CRED_PROTO_AUTHTYPES_AVAILABLE_SMTP,
   a_NETSMTP_ALL_AVAILABLE_AUTO_MASK = a_NETSMTP_EXT_ALL |
         a_NETSMTP_FORCE_TLS_IFF |
         (mx_CRED_PROTO_AUTHTYPES_AVAILABLE_SMTP &
            mx_CRED_AUTHTYPE_MECH_AUTO_MASK),

   /* Temporary or calculated */
   a_NETSMTP_EXT_READ_IS_HOT = 1u<<a_X(15)
};
#undef a_X
CTAV(a_NETSMTP_EXT_NONE == 0);
CTA(a_NETSMTP_EXT_READ_IS_HOT <= S32_MAX, "Flag bits excess storage type");

struct a_netsmtp_ctx{
   u32 nsc_config;
   u32 nsc_server_config;
   struct str nsc_dat;
   struct str nsc_buf;
};

static sigjmp_buf a_netsmtp_jmp;

static void a_netsmtp_onsig(int signo);

/* Get the SMTP server's answer, expecting val */
static int a_netsmtp_read(struct mx_socket *sop, struct a_netsmtp_ctx *nscp,
      int val, boole ign_eof, boole want_dat);
static void a_netsmtp_parse_caps(struct a_netsmtp_ctx *nscp, char const *cp);

/* Talk to a SMTP server */
static boole a_netsmtp_talk(struct mx_socket *sop, struct mx_send_ctx *scp,
      struct a_netsmtp_ctx *nscp);

#ifdef mx_HAVE_GSSAPI
# include <mx/cred-gssapi.h>
#endif

/* Indirect SMTP I/O; manual claims log happens for 2x*verbose*! */
#define a_SMTP_OUT(X) \
do{\
   char const *__cx__ = (X);\
   \
   if(n_poption & n_PO_D_VV){\
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
   int __x__, __y__;\
   \
   __x__ = (X);\
   if((__y__ = a_netsmtp_read(sop, nscp, __x__, IGNEOF, WANTDAT)) != __x__ &&\
         (!(IGNEOF) || __y__ != -1))\
      goto jleave;\
}while(0)

static void
a_netsmtp_onsig(int signo){
   UNUSED(signo);
   siglongjmp(a_netsmtp_jmp, 1);
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
      }else if(UNLIKELY(nscp->nsc_server_config & a_NETSMTP_EXT_READ_IS_HOT))
         a_netsmtp_parse_caps(nscp, &nscp->nsc_dat.s[4]);
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

static void
a_netsmtp_parse_caps(struct a_netsmtp_ctx *nscp, char const *cp){
   NYD_IN;
   if(!su_cs_cmp(cp, "PIPELINING"))
      nscp->nsc_server_config |= a_NETSMTP_EXT_PIPELINING;
   else if(!su_cs_cmp(cp, "STARTTLS"))
      nscp->nsc_server_config |= a_NETSMTP_EXT_STARTTLS;
   else if(su_cs_starts_with_case(cp, "AUTH ")){
      struct mx_cred_authtype_info const *caip;
      char *buf;

      for(buf = savestr(&cp[sizeof("AUTH ") -1]);
            (cp = su_cs_sep_c(&buf, ' ', TRU1)) != NIL;){
         if((caip = mx_cred_auth_type_find_name(cp, FAL0)) != NIL){
            nscp->nsc_server_config |= caip->cai_type;
            if(caip->cai_type == mx_CRED_AUTHTYPE_EXTERNAL)
               nscp->nsc_server_config |= mx_CRED_AUTHTYPE_EXTERNANON;
         }
         else if(n_poption & n_PO_D_V)
            n_err("SMTP: SERVER: authentication: skipping %s\n", cp);
      }
      nscp->nsc_server_config |= a_NETSMTP_EXT_AUTH;
   }
   NYD_OU;
}

static boole
a_netsmtp_talk(struct mx_socket *sop, struct mx_send_ctx *scp, /* TODO split*/
      struct a_netsmtp_ctx *nscp){
   enum{
      a_ERROR = 1u<<0,
      a_IS_OAUTHBEARER = 1u<<1,
      a_IN_HEAD = 1u<<2,
      a_IN_BCC = 1u<<3
   };

   char o[LINESIZE]; /* TODO n_string++ */
   struct str b64;
   struct mx_name *np;
   u8 f;
   uz resp2_cnt, blen, i, j;
   char const *hostname;
   NYD_IN;

   hostname = n_nodename(TRU1); /* TODO in parent process (at least init) */
   /* With the pipelining extension we may be able to save (an) authentication
    * roundtrip(s), too, so start counting expected 2xx responses beforehand */
   resp2_cnt = 0;

   f = a_ERROR | a_IN_HEAD;

   /* Read greeting */
   a_SMTP_ANSWER(2, FAL0, FAL0);

   /* The initial EHLO */
   if(nscp->nsc_config & a_NETSMTP_EXT_EHLO){
      int sr;

      if(n_poption & n_PO_D_V)
         n_err(_("*smtp-config*: using ehlo extension\n"));

      snprintf(o, sizeof o, NETLINE("EHLO %s"), hostname);
      a_SMTP_OUT(o);
      if(!(n_poption & n_PO_D)){
         nscp->nsc_server_config = a_NETSMTP_EXT_READ_IS_HOT |
               a_NETSMTP_EXT_EHLO;
         sr = a_netsmtp_read(sop, nscp, 2, FAL0, FAL0);
         nscp->nsc_server_config ^= a_NETSMTP_EXT_READ_IS_HOT;
      }else{
         sr = 2;
         nscp->nsc_server_config = a_NETSMTP_ALL_AVAILABLE_MASK;
      }

      /* In case of error we may be able to try to use normal HELO */
      if(sr != 2){
         /* TODO RFC 1869 gives detailed error codes etc. but of course
          * TODO this is all we (can) do for now, anyway */
         u32 at;

         if(sr == -1)
            goto jleave;
         n_err(_("*smtp-config*: cannot use \"ehlo\" extension!\n"));

         at = nscp->nsc_config;
         nscp->nsc_server_config = nscp->nsc_config = a_NETSMTP_EXT_NONE;

         if(!(at & (a_NETSMTP_EXT_AUTH
#ifdef mx_HAVE_TLS
               | (sop->s_use_tls ? a_NETSMTP_EXT_NONE
                  : a_NETSMTP_EXT_STARTTLS | a_NETSMTP_FORCE_TLS)
#endif
         )))
            goto jhelo;
         goto je_ehlo;
      }

      /* Keep only user desires */
      nscp->nsc_server_config &= nscp->nsc_config;
      if(nscp->nsc_config & a_NETSMTP_FORCE_TLS)
         nscp->nsc_server_config |= a_NETSMTP_EXT_STARTTLS;
   }else if(!(nscp->nsc_config & a_NETSMTP_EXT_AUTH)){
jhelo:
      nscp->nsc_config &= ~a_NETSMTP_ALL_MASK;
      nscp->nsc_server_config &= ~a_NETSMTP_ALL_MASK;
      snprintf(o, sizeof o, NETLINE("HELO %s"), hostname);
      a_SMTP_OUT(o);
      a_SMTP_ANSWER(2, FAL0, FAL0);
      /* Skip TLS + Authentication */
      goto jsend;
   }else{
je_ehlo:
      n_err(_("*smtp-config* settings do not work out.\n"
         "  Communication with server can be viewed by setting "
            "*verbose* 2 times\n"));
      goto jleave;
   }

/* In a session state; if unencrypted, try to upgrade */
#ifdef mx_HAVE_TLS
   if(!sop->s_use_tls && (nscp->nsc_server_config & a_NETSMTP_EXT_STARTTLS)){
      if(n_poption & n_PO_D_V)
         n_err(_("*smtp-config*: using starttls extension\n"));
      a_SMTP_OUT(NETLINE("STARTTLS"));
      a_SMTP_ANSWER(2, FAL0, FAL0);

      if(!(n_poption & n_PO_D) && !n_tls_open(scp->sc_urlp, sop))
         goto jleave;

      /* Capabilities might have changed after STARTTLS */
      snprintf(o, sizeof o, NETLINE("EHLO %s"), hostname);
      a_SMTP_OUT(o);
      nscp->nsc_server_config &= ~a_NETSMTP_ALL_MASK;
      nscp->nsc_server_config |= a_NETSMTP_EXT_READ_IS_HOT;
      a_SMTP_ANSWER(2, FAL0, FAL0);
      nscp->nsc_server_config ^= a_NETSMTP_EXT_READ_IS_HOT;
      /* Keep only user desires */
      nscp->nsc_server_config &= nscp->nsc_config;
   }

   if(sop->s_use_tls)
      nscp->nsc_config |= a_NETSMTP_FORCE_TLS;
   else
#endif /* mx_HAVE_TLS */
      nscp->nsc_config &= ~a_NETSMTP_FORCE_TLS;

   if(n_poption & n_PO_D_V){
      if(nscp->nsc_server_config & a_NETSMTP_EXT_PIPELINING)
         n_err(_("*smtp-config*: using pipelining extension\n"));
   }

   if(!(nscp->nsc_server_config & a_NETSMTP_EXT_AUTH))
      goto jsend;
   if(n_poption & n_PO_D_V)
      n_err(_("*smtp-config*: using auth extension\n"));

/*
TODO
check smtputf8 and check against line length maximum!!
*/

   /* Let us decide upon the authentication mechanism.
    * All authentication types end with a 2 response: share this */
   switch(mx_cred_auth_type_select(CPROTO_SMTP, nscp->nsc_server_config,
         ((nscp->nsc_config & a_NETSMTP_FORCE_TLS) != 0))){
   default:
      ASSERT(0);
      /* FALLTHRU */
   case U32_MAX:
      /* Was already logged */
      goto jleave;
   case mx_CRED_AUTHTYPE_NONE:
      break;
   case mx_CRED_AUTHTYPE_PLAIN:
      /* Calculate required storage */
#define a_MAX (2 + sizeof("AUTH PLAIN " NETNL))

      i = scp->sc_credp->cc_user.l;
      if(scp->sc_credp->cc_pass.l >= (j = UZ_MAX - a_MAX) ||
            i >= (j -= scp->sc_credp->cc_pass.l)){
jerr_cred:
         n_err(_("Credentials overflow buffer size, or I/O error\n"));
         goto jleave;
      }
      i += scp->sc_credp->cc_pass.l;
      i += a_MAX;
      if((i = mx_b64_enc_calc_size(i)) == UZ_MAX)
         goto jerr_cred;
      if(i >= sizeof(o))
         goto jerr_cred;

#undef a_MAX

      /* C99 */{
         int s;

         if((s = snprintf(o, sizeof o, "%c%s%c%s",
               '\0', scp->sc_credp->cc_user.s,
               '\0', scp->sc_credp->cc_pass.s)) < 0)
            goto jerr_cred;

         if(mx_b64_enc_buf(&b64, o, s, mx_B64_AUTO_ALLOC) == NIL)
            goto jerr_cred;

         if(snprintf(o, sizeof o, NETLINE("AUTH PLAIN %s"), b64.s) < 0)
            goto jerr_cred;
      }
      a_SMTP_OUT(o);
      break;

   case mx_CRED_AUTHTYPE_OAUTHBEARER:
      f |= a_IS_OAUTHBEARER;
      /* FALLTHRU */
   case mx_CRED_AUTHTYPE_XOAUTH2:{
      char const *mech, oa[] = "AUTH OAUTHBEARER ", xoa[] = "AUTH XOAUTH2 ";

      if(f & a_IS_OAUTHBEARER)
         mech = oa, i = sizeof(oa) -1;
      else
         mech = xoa, i = sizeof(xoa) -1;

      if(!mx_oauthbearer_create_icr(&b64, i,
            scp->sc_urlp, scp->sc_credp, ((f & a_IS_OAUTHBEARER) == 0)))
         goto jerr_cred;

      su_mem_copy(&b64.s[0], mech, i);
      if((i = (b64.l < sizeof(o))))
         su_mem_copy(o, b64.s, b64.l +1);

      n_lofi_free(b64.s);

      a_SMTP_OUT(o);
      }break;

   case mx_CRED_AUTHTYPE_EXTERNAL:
#define a_MAX (sizeof("AUTH EXTERNAL " NETNL))
      i = mx_b64_enc_calc_size(scp->sc_credp->cc_user.l);
      if(/*i == UZ_MAX ||*/ i >= sizeof(o) - a_MAX)
         goto jerr_cred;
#undef a_MAX

      su_mem_copy(o, "AUTH EXTERNAL ", sizeof("AUTH EXTERNAL ") -1);
      b64.s = &o[sizeof("AUTH EXTERNAL ") -1];
      mx_b64_enc_buf(&b64, scp->sc_credp->cc_user.s,
         scp->sc_credp->cc_user.l, mx_B64_BUF | mx_B64_CRLF);
      a_SMTP_OUT(o);
      break;

   case mx_CRED_AUTHTYPE_EXTERNANON:
      a_SMTP_OUT(NETLINE("AUTH EXTERNAL ="));
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

   /* Complete authentication; since we assume everything is fine, just
    * pipeline the final 2 "success" answer of the server already */
   ++resp2_cnt;
   if(!(nscp->nsc_server_config & a_NETSMTP_EXT_PIPELINING))
      a_SMTP_ANSWER(2, FAL0, FAL0);
   /* TODO OAUTHBEARER/XOAUTH2 ERROR: send empty message to gain actual error
    * message (when status was 334) */

jsend:
   snprintf(o, sizeof o, NETLINE("MAIL FROM:<%s>"), scp->sc_urlp->url_u_h.s);
   a_SMTP_OUT(o);
   ++resp2_cnt;
   if(!(nscp->nsc_server_config & a_NETSMTP_EXT_PIPELINING))
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
         if(!(nscp->nsc_server_config & a_NETSMTP_EXT_PIPELINING))
            a_SMTP_ANSWER(2, FAL0, FAL0);
      }
   }

   a_SMTP_OUT(NETLINE("DATA"));
   if(nscp->nsc_server_config & a_NETSMTP_EXT_PIPELINING){
      for(; resp2_cnt != 0; --resp2_cnt)
         a_SMTP_ANSWER(2, FAL0, FAL0);
   }else
      resp2_cnt = 0;
   a_SMTP_ANSWER(3, FAL0, FAL0);

   fflush_rewind(scp->sc_input);
   i = S(uz,fsize(scp->sc_input)); /* xxx off_t (->s64!) -> uz!! */
   while(fgetline(&nscp->nsc_buf.s, &nscp->nsc_buf.l, &i, &blen,
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
   if(!(nscp->nsc_server_config & a_NETSMTP_EXT_PIPELINING))
      a_SMTP_ANSWER(2, FAL0, FAL0);
   else
      ++resp2_cnt;

   a_SMTP_OUT(NETLINE("QUIT"));
   ++resp2_cnt;
   while(resp2_cnt-- != 0)
      a_SMTP_ANSWER(2, FAL0, FAL0);

   f &= ~a_ERROR;
jleave:
   if(nscp->nsc_buf.s != NIL)
      n_free(nscp->nsc_buf.s);
   NYD_OU;
   return ((f & a_ERROR) == 0);
}

#ifdef mx_HAVE_GSSAPI
# include <mx/cred-gssapi.h>
#endif

#undef a_SMTP_OUT
#undef a_SMTP_ANSWER

boole
mx_smtp_parse_config(struct mx_cred_ctx *credp, struct mx_url *urlp){
   static struct capdis{
      u32 cd_on;
      u32 cd_off;
      char const cd_user_name[16];
   } const cda[] = {
      {a_NETSMTP_EXT_ALL, a_NETSMTP_ALL_MASK, "all"},
      {a_NETSMTP_EXT_EHLO, a_NETSMTP_EXT_ALL, "ehlo"},
      {a_NETSMTP_EXT_PIPELINING | a_NETSMTP_EXT_EHLO,
         a_NETSMTP_EXT_PIPELINING, "pipelining\0"},
      /* User desire to use STARTTLS for us always means: force it!
       * As of today (June 2020) all servers are expected to be TLS aware */
      {a_NETSMTP_EXT_STARTTLS | a_NETSMTP_FORCE_TLS_IFF | a_NETSMTP_EXT_EHLO,
         a_NETSMTP_EXT_STARTTLS | a_NETSMTP_FORCE_TLS, "starttls"},
      {(a_NETSMTP_EXT_AUTH | (mx_CRED_PROTO_AUTHTYPES_SMTP &
            mx_CRED_PROTO_AUTHTYPES_AVAILABLE_SMTP &
            mx_CRED_AUTHTYPE_MECH_AUTO_MASK) | a_NETSMTP_EXT_EHLO),
         (a_NETSMTP_EXT_AUTH | mx_CRED_PROTO_AUTHTYPES_SMTP), "auth"},
      /* The rest comes in via mx_cred_proto_authtypes */
   };
   struct mx_cred_authtype_verify_ctx cavc;
   char *buf;
   char const *config, *ccp;
   u32 flags, i;
   boole rv;
   NYD_IN;

   if(xok_blook(smtp_use_starttls, urlp, OXM_ALL)) /* v15-compat */
      n_OBSOLETE("*smtp-use-starttls* became a default "
         "(please adjust *smtp-config* as necessary)");

   if(xok_vlook(smtp_auth, urlp, OXM_ALL)) /* v15-compat */
      n_OBSOLETE("*smtp-auth* is gone "
         "(please adjust *smtp-config* as necessary)");

   rv = TRU1;
   flags = a_NETSMTP_ALL_AVAILABLE_AUTO_MASK;

   if((config = xok_vlook(smtp_config, urlp, OXM_ALL)) == NIL)
      goto jleave;

   for(buf = savestr(config); (ccp = su_cs_sep_c(&buf, ',', TRU1)) != NIL;){
      boole minus;

      if((minus = (*ccp == '-')) || *ccp == '+')
         ++ccp;
      for(i = 0;;){
         if(!su_cs_cmp_case(ccp, cda[i].cd_user_name)){
            if(cda[i].cd_off == 0)
               goto jeavail;
            if(minus)
               flags &= ~cda[i].cd_off;
            else
               flags |= cda[i].cd_on;
            break;
         }

         if(++i == NELEM(cda)){
            /* It could also be an authentication type, check that first */
            struct mx_cred_authtype_info const *caip;

            if((caip = mx_cred_auth_type_find_name(ccp, TRU1)) != NIL){
               if(caip == R(struct mx_cred_authtype_info*,-1)){
                  if(minus)
                     flags &= ~mx_CRED_PROTO_AUTHTYPES_SMTP;
                  else
                     flags |= a_NETSMTP_EXT_AUTH | a_NETSMTP_EXT_EHLO |
                           mx_CRED_PROTO_AUTHTYPES_AVAILABLE_SMTP;
               }else{
                  u32 b;

                  b = caip->cai_type;
                  if(minus)
                     flags &= ~b;
                  else
                     flags |= b | a_NETSMTP_EXT_AUTH | a_NETSMTP_EXT_EHLO;
               }
            }else
jeavail:
               n_err(_("*smtp-config*: unsupported directive: %s: %s\n"),
                  n_shexp_quote_cp(ccp, FAL0), n_shexp_quote_cp(config, FAL0));
            break;
         }
      }
   }

   /* Authentication mechanisms */
   if(flags & a_NETSMTP_EXT_AUTH){
      cavc.cavc_proto = CPROTO_SMTP;
      cavc.cavc_mechplusbits = flags & mx_CRED_PROTO_AUTHTYPES_SMTP;
      flags &= ~(a_NETSMTP_EXT_AUTH | mx_CRED_PROTO_AUTHTYPES_SMTP);

      /* We do not take into account the TLS state for URL here!
       * "none" is valid for SMTP */
      if((rv = mx_cred_auth_type_verify_bits(&cavc,
            ((urlp->url_flags & mx_URL_TLS_REQUIRED) != 0 ||
             (flags & a_NETSMTP_EXT_STARTTLS) != 0)))){
         flags |= cavc.cavc_mechplusbits & mx_CRED_AUTHTYPE_MASK;
         if((flags & mx_CRED_PROTO_AUTHTYPES_SMTP) != mx_CRED_AUTHTYPE_NONE)
            flags |= a_NETSMTP_EXT_AUTH | a_NETSMTP_EXT_EHLO;
         /*if(rv == TRUM1)
          * flags |= a_NETSMTP_AUTH_ALLMECHS;*/
         if(flags & mx_CRED_AUTHTYPE_NEED_TLS)
            flags |= a_NETSMTP_EXT_STARTTLS | a_NETSMTP_EXT_EHLO |
                  a_NETSMTP_FORCE_TLS;
         rv = TRU1;
      }
   }

   ASSERT(!(flags & a_NETSMTP_EXT_ALL) || (flags & a_NETSMTP_EXT_EHLO));

   if(!a_NETSMTP_FORCE_TLS_IFF && rv && (flags & a_NETSMTP_FORCE_TLS)){
      n_err(_("*smtp-config*: starttls: no TLS support compiled in\n"));
      rv = FAL0;
   }

jleave:
   credp->cc_config = flags;
   NYD_OU;
   return rv;
}

boole
mx_smtp_mta(struct mx_send_ctx *scp){
   struct a_netsmtp_ctx nsc;
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

   su_mem_set(&nsc, 0, sizeof nsc);
   nsc.nsc_config = scp->sc_credp->cc_config;

   rv = a_netsmtp_talk(&so, scp, &nsc);

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
