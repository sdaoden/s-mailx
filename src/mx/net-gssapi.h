/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of GSS-API authentication.
 *@ According to RFC 4954 (SMTP), RFC 5034 (POP3), RFC 4422/4959 (IMAP).
 *
 * Copyright (c) 2014 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
/*
 * Partially derived from sample code in:
 *
 * GSS-API Programming Guide
 * Part No: 806-3814-10
 * Sun Microsystems, Inc. 901 San Antonio Road, Palo Alto, CA 94303-4900 U.S.A.
 * (c) 2000 Sun Microsystems
 */
/*
 * Copyright 1994 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#ifdef mx_HAVE_GSSAPI
#ifndef a_NET_GSSAPI_H
# define a_NET_GSSAPI_H 1

#ifndef GSSAPI_REG_INCLUDE
# include <gssapi/gssapi.h>
# ifdef GSSAPI_OLD_STYLE
#  include <gssapi/gssapi_generic.h>
#  define GSS_C_NT_HOSTBASED_SERVICE gss_nt_service_name
#  define m_DEFINED_GCC_C_NT_HOSTBASED_SERVICE
# endif
#else
# include <gssapi.h>
#endif

#include <su/cs.h>

#include "mx/compat.h"

#elif a_NET_GSSAPI_H == 1
# undef a_NET_GSSAPI_H
# define a_NET_GSSAPI_H 2

/* */
static boole su_CONCAT(su_FILE,_gss)(struct mx_socket *sp, struct mx_url *urlp,
      struct mx_cred_ctx *credp,
# ifdef mx_SOURCE_NET_SMTP
      struct a_netsmtp_ctx *nscp
# elif defined mx_SOURCE_NET_POP3 || defined mx_SOURCE_NET_IMAP
      struct mailbox *mp
# endif
      );

/* */
static void su_CONCAT(su_FILE,_gss__error)(char const *s, OM_uint32 maj_stat,
      OM_uint32 min_stat);
static void su_CONCAT(su_FILE,_gss__error1)(char const *s, OM_uint32 code,
      int typ);

#elif a_NET_GSSAPI_H == 2

static boole
su_CONCAT(su_FILE,_gss)(struct mx_socket *sop, struct mx_url *urlp,
      struct mx_cred_ctx *credp,
# ifdef mx_SOURCE_NET_SMTP
      struct a_netsmtp_ctx *nscp
# elif defined mx_SOURCE_NET_POP3 || defined mx_SOURCE_NET_IMAP
      struct mailbox *mp
# endif
      ){
   enum{
      a_F_NONE,
      a_F_TARGET_NAME = 1u<<0,
      a_F_GSS_CONTEXT = 1u<<1,
      a_F_SEND_TOK = 1u<<2,
      a_F_RECV_TOK = 1u<<3,
      a_F_SETUP = 1u<<4,
      a_F_OUTBUF = 1u<<5
   };

# if defined mx_SOURCE_NET_POP3
   int poprv;
# elif defined mx_SOURCE_NET_IMAP
   FILE *queuefp = NIL;
# endif
   struct str in, out;
   gss_buffer_desc send_tok, recv_tok;
   gss_ctx_id_t gss_context;
   int conf_state;
   gss_name_t target_name;
   OM_uint32 maj_stat, min_stat, ret_flags;
   char *buf;
   u32 f;
   boole ok;
   NYD_IN;
   UNUSED(sop);

   ok = FAL0;
   f = a_F_NONE;
   buf = NIL;

   if(INT_MAX - 1 - 5 <= urlp->url_host.l ||
         INT_MAX - 1 - 4 <= credp->cc_user.l){
      n_err(_("Credentials overflow buffer sizes\n"));
      goto jleave;
   }

# ifdef mx_SOURCE_NET_SMTP
#  define a_N "smtp@"
#  define a_L 5
# elif defined mx_SOURCE_NET_POP3
#  define a_N "pop@"
#  define a_L 4
# elif defined mx_SOURCE_NET_IMAP
#  define a_N "imap@"
#  define a_L 5
# endif

   su_cs_pcopy(su_cs_pcopy(
      send_tok.value = buf = n_lofi_alloc(
         (send_tok.length = urlp->url_host.l + a_L) +1), a_N),
      urlp->url_host.s);

# undef a_N
# undef a_L

   maj_stat = gss_import_name(&min_stat, &send_tok,
         GSS_C_NT_HOSTBASED_SERVICE, &target_name);
   f |= a_F_TARGET_NAME;
   if(maj_stat != GSS_S_COMPLETE){
      su_CONCAT(su_FILE,_gss__error)(savestrbuf(send_tok.value,
         send_tok.length), maj_stat, min_stat);
      goto jleave;
   }

   /* */
# ifdef mx_SOURCE_NET_IMAP
   if(!(mp->mb_flags & MB_SASL_IR)){
      IMAP_OUT(savecat(tag(1), NETLINE(" AUTHENTICATE GSSAPI")),
         0, goto jleave);
      imap_answer(mp, 1);
      if(response_type != RESPONSE_CONT)
         goto jleave;
   }
# endif

   gss_context = GSS_C_NO_CONTEXT;
   for(;;){
      maj_stat = gss_init_sec_context(&min_stat,
            GSS_C_NO_CREDENTIAL,
            &gss_context,
            target_name,
            GSS_C_NO_OID,
            GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG,
            0,
            GSS_C_NO_CHANNEL_BINDINGS,
            ((f & a_F_SETUP) ? &recv_tok : GSS_C_NO_BUFFER),
            NIL,
            &send_tok,
            &ret_flags,
            NIL);
      f |= a_F_GSS_CONTEXT | a_F_SEND_TOK;
      if(maj_stat != GSS_S_COMPLETE && maj_stat != GSS_S_CONTINUE_NEEDED){
         su_CONCAT(su_FILE,_gss__error)("gss_init_sec_context",
            maj_stat, min_stat);
         goto jleave;
      }
      if(f & a_F_OUTBUF){
         f ^= a_F_OUTBUF;
         n_free(out.s);
      }

      if(mx_b64_enc_buf(&out, send_tok.value, send_tok.length,
            mx_B64_AUTO_ALLOC | mx_B64_CRLF) == NIL)
         goto jleave;
      gss_release_buffer(&min_stat, &send_tok);
      f &= ~a_F_SEND_TOK;
      if(!(f & a_F_SETUP)){
         f |= a_F_SETUP;
# if defined mx_SOURCE_NET_SMTP || defined mx_SOURCE_NET_POP3
         out.s = savecat("AUTH GSSAPI ", out.s);
# elif defined mx_SOURCE_NET_IMAP
         if(mp->mb_flags & MB_SASL_IR)
            out.s = savecat(tag(1), savecat(" AUTHENTICATE GSSAPI ", out.s));
# endif
      }

# ifdef mx_SOURCE_NET_SMTP
      a_SMTP_OUT(out.s);
      a_SMTP_ANSWER(3, FAL0, TRU1);
      in = nscp->nsc_dat;
# elif defined mx_SOURCE_NET_POP3
      a_POP3_OUT(poprv, out.s, MB_COMD, goto jleave);
      a_POP3_ANSWER(poprv, goto jleave);
      in.l = su_cs_len(in.s = UNCONST(char*,a_pop3_realdat));
# elif defined mx_SOURCE_NET_IMAP
      IMAP_OUT(out.s, 0, goto jleave);
      imap_answer(mp, 1);
      if(response_type != RESPONSE_CONT)
         goto jleave;
      in.l = su_cs_len(in.s = responded_text);
# endif

      out.s = NIL;
      f |= a_F_OUTBUF;
      if(!mx_b64_dec(&out, &in)){
         n_err(_("Invalid base64 encoding from GSSAPI server\n"));
         goto jleave;
      }
      recv_tok.value = out.s;
      recv_tok.length = out.l;

      if(maj_stat != GSS_S_CONTINUE_NEEDED)
         break;
   }

   maj_stat = gss_unwrap(&min_stat, gss_context, &recv_tok, &send_tok,
         &conf_state, NIL);
   f |= a_F_SEND_TOK;
   if(maj_stat != GSS_S_COMPLETE){
      su_CONCAT(su_FILE,_gss__error)("unwrapping data", maj_stat, min_stat);
      goto jleave;
   }

   gss_release_buffer(&min_stat, &send_tok);
   n_free(out.s);
   f &= ~(a_F_OUTBUF | a_F_SEND_TOK);

   /* First octet: bit-mask with protection mechanisms (1 = no protection
    *    mechanism).
    * Second to fourth octet: maximum message size in network byte order.
    * Fifth and following octets: user name string */
   n_lofi_free(buf);
   in.s = buf = n_lofi_alloc((send_tok.length = 4u + credp->cc_user.l) +1);
   su_mem_copy(&in.s[4], credp->cc_user.s, credp->cc_user.l +1);
   in.s[0] = 1;
   in.s[1] = 0;
   in.s[2] = in.s[3] = S(char,0xFF);
   send_tok.value = in.s;
   maj_stat = gss_wrap(&min_stat, gss_context, 0, GSS_C_QOP_DEFAULT, &send_tok,
         &conf_state, &recv_tok);
   f |= a_F_RECV_TOK;
   if(maj_stat != GSS_S_COMPLETE){
      su_CONCAT(su_FILE,_gss__error)("wrapping data", maj_stat, min_stat);
      goto jleave;
   }
   if(mx_b64_enc_buf(&out, recv_tok.value, recv_tok.length,
         mx_B64_AUTO_ALLOC | mx_B64_CRLF) == NIL)
      goto jleave;

# ifdef mx_SOURCE_NET_SMTP
   a_SMTP_OUT(out.s);
   a_SMTP_ANSWER(2, FAL0, FAL0);
   ok = TRU1;
# elif defined mx_SOURCE_NET_POP3
   a_POP3_OUT(poprv, out.s, MB_COMD, goto jleave);
   a_POP3_ANSWER(poprv, goto jleave);
   ok = TRU1;
# elif defined mx_SOURCE_NET_IMAP
   IMAP_OUT(out.s, 0, goto jleave);
   ok = TRU1;
   while(mp->mb_active & MB_COMD)
      ok = imap_answer(mp, 1);
# endif

jleave:
   if(f & a_F_RECV_TOK)
      gss_release_buffer(&min_stat, &recv_tok);
   if(f & a_F_SEND_TOK)
      gss_release_buffer(&min_stat, &send_tok);
   if(f & a_F_TARGET_NAME)
      gss_release_name(&min_stat, &target_name);
   if(f & a_F_GSS_CONTEXT)
      gss_delete_sec_context(&min_stat, &gss_context, GSS_C_NO_BUFFER);
   if((f & a_F_OUTBUF) && out.s != NIL)
      n_free(out.s);
   if(buf != NIL)
      n_lofi_free(buf);

   NYD_OU;
   return ok;
}

static void
su_CONCAT(su_FILE,_gss__error)(char const *s, OM_uint32 maj_stat,
      OM_uint32 min_stat){
   NYD2_IN;
   su_CONCAT(su_FILE,_gss__error1)(s, maj_stat, GSS_C_GSS_CODE);
   su_CONCAT(su_FILE,_gss__error1)(s, min_stat, GSS_C_MECH_CODE);
   NYD2_OU;
}

static void
su_CONCAT(su_FILE,_gss__error1)(char const *s, OM_uint32 code, int typ){
   gss_buffer_desc msg = GSS_C_EMPTY_BUFFER;
   OM_uint32 maj_stat, min_stat;
   OM_uint32 msg_ctx;
   NYD2_IN;

   msg_ctx = 0;
   do{
      maj_stat = gss_display_status(&min_stat, code, typ, GSS_C_NO_OID,
            &msg_ctx, &msg);
      if(maj_stat == GSS_S_COMPLETE){
         n_err(_("GSSAPI error: %s / %.*s\n"),
            s, S(int,msg.length), S(char*,msg.value));
         gss_release_buffer(&min_stat, &msg);
      }else{
         n_err(_("GSSAPI error: %s / unknown\n"), s);
         break;
      }
   }while(msg_ctx);

   NYD2_OU;
}

   /* Cleanup, and re-enable for amalgamation */
# ifdef m_DEFINED_GCC_C_NT_HOSTBASED_SERVICE
#  undef m_DEFINED_GCC_C_NT_HOSTBASED_SERVICE
#  undef GSS_C_NT_HOSTBASED_SERVICE
# endif

# undef a_NET_GSSAPI_H
#endif
#endif /* mx_HAVE_GSSAPI */
/* s-it-mode */
