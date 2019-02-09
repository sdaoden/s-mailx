/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of SMTP GSS-API authentication according to RFC 4954.
 *@ TODO GSS-API should also be joined into "a VFS".
 *
 * Copyright (c) 2014 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause
 */

/* Derived from `imap_gssapi.h', which is:
 *
 * Copyright (c) 2004 Gunnar Ritter.
 * All rights reserved.
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
/*
 * Partially derived from sample code in:
 *
 * GSS-API Programming Guide
 * Part No: 816-1331-11
 * Sun Microsystems, Inc. 4150 Network Circle Santa Clara, CA 95054 U.S.A.
 *
 * (c) 2002 Sun Microsystems
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
#ifndef GSSAPI_REG_INCLUDE
# include <gssapi/gssapi.h>
# ifdef GSSAPI_OLD_STYLE
#  include <gssapi/gssapi_generic.h>
#  define GSS_C_NT_HOSTBASED_SERVICE   gss_nt_service_name
#  define m_DEFINED_GCC_C_NT_HOSTBASED_SERVICE
# endif
#else
# include <gssapi.h>
#endif

static void _smtp_gssapi_error1(char const *s, OM_uint32 code, int typ);
static void _smtp_gssapi_error(char const *s, OM_uint32 maj_stat,
               OM_uint32 min_stat);

static void
_smtp_gssapi_error1(char const *s, OM_uint32 code, int typ)
{
   OM_uint32 maj_stat, min_stat;
   gss_buffer_desc msg = GSS_C_EMPTY_BUFFER;
   OM_uint32 msg_ctx = 0;
   NYD_IN;

   do {
      maj_stat = gss_display_status(&min_stat, code, typ, GSS_C_NO_OID,
            &msg_ctx, &msg);
      if (maj_stat == GSS_S_COMPLETE) {
         n_err(_("GSS error: %s / %.*s\n"),
            s, (int)msg.length, (char*)msg.value);
         gss_release_buffer(&min_stat, &msg);
      } else {
         n_err(_("GSS error: %s / unknown\n"), s);
         break;
      }
   } while (msg_ctx);
   NYD_OU;
}

static void
_smtp_gssapi_error(char const *s, OM_uint32 maj_stat, OM_uint32 min_stat)
{
   NYD_IN;
   _smtp_gssapi_error1(s, maj_stat, GSS_C_GSS_CODE);
   _smtp_gssapi_error1(s, min_stat, GSS_C_MECH_CODE);
   NYD_OU;
}

static bool_t
_smtp_gssapi(struct sock *sop, struct sendbundle *sbp, struct smtp_line *slp)
{
   struct str in, out;
   gss_buffer_desc send_tok, recv_tok;
   gss_name_t target_name;
   gss_ctx_id_t gss_context;
   OM_uint32 maj_stat, min_stat, ret_flags;
   int conf_state;
   enum{
      a_F_NONE,
      a_F_RECV_TOK = 1u<<0,
      a_F_SEND_TOK = 1u<<1,
      a_F_TARGET_NAME = 1u<<2,
      a_F_GSS_CONTEXT = 1u<<3
   } f;
   bool_t ok;
   NYD_IN;

   ok = FAL0;
   f = a_F_NONE;

   if(INT_MAX - 1 - 4 <= sbp->sb_ccred.cc_user.l)
      goto jleave;

   send_tok.value = n_autorec_alloc(
         (send_tok.length = sbp->sb_url.url_host.l + 5) +1);
   su_mem_copy(send_tok.value, "smtp@", 5);
   su_mem_copy((char*)send_tok.value + 5, sbp->sb_url.url_host.s,
      sbp->sb_url.url_host.l +1);
   maj_stat = gss_import_name(&min_stat, &send_tok, GSS_C_NT_HOSTBASED_SERVICE,
         &target_name);
   f |= a_F_TARGET_NAME;
   if (maj_stat != GSS_S_COMPLETE) {
      _smtp_gssapi_error(savestrbuf(send_tok.value, send_tok.length),
         maj_stat, min_stat);
      goto jleave;
   }

   gss_context = GSS_C_NO_CONTEXT;
   maj_stat = gss_init_sec_context(&min_stat,
         GSS_C_NO_CREDENTIAL,
         &gss_context,
         target_name,
         GSS_C_NO_OID,
         GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG,
         0,
         GSS_C_NO_CHANNEL_BINDINGS,
         GSS_C_NO_BUFFER,
         NULL,
         &send_tok,
         &ret_flags,
         NULL);
   f |= a_F_SEND_TOK | a_F_GSS_CONTEXT;
   if (maj_stat != GSS_S_COMPLETE && maj_stat != GSS_S_CONTINUE_NEEDED) {
      _smtp_gssapi_error("initializing GSS context", maj_stat, min_stat);
      goto jleave;
   }

   _OUT(NETLINE("AUTH GSSAPI"));
   _ANSWER(3, FAL0, FAL0);
   while (maj_stat == GSS_S_CONTINUE_NEEDED) {
      /* Pass token obtained from first gss_init_sec_context() call */
      if(b64_encode_buf(&out, send_tok.value, send_tok.length,
            B64_SALLOC | B64_CRLF) == NULL)
         goto jleave;
      gss_release_buffer(&min_stat, &send_tok);
      f &= ~a_F_SEND_TOK;
      _OUT(out.s);
      _ANSWER(3, FAL0, TRU1);

      out.s = NULL;
      in.s = slp->dat;
      in.l = slp->datlen;
      if(!b64_decode(&out, &in))
         goto jebase64;
      recv_tok.value = out.s;
      recv_tok.length = out.l;
      maj_stat = gss_init_sec_context(&min_stat,
            GSS_C_NO_CREDENTIAL,
            &gss_context,
            target_name,
            GSS_C_NO_OID,
            GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG,
            0,
            GSS_C_NO_CHANNEL_BINDINGS,
            &recv_tok,
            NULL,
            &send_tok,
            &ret_flags,
            NULL);
      n_free(out.s);
      f |= a_F_SEND_TOK;
      if (maj_stat != GSS_S_COMPLETE && maj_stat != GSS_S_CONTINUE_NEEDED) {
         _smtp_gssapi_error("initializing context", maj_stat, min_stat);
         goto jleave;
      }
   }

   gss_release_name(&min_stat, &target_name);
   f &= ~a_F_TARGET_NAME;

   /* Pass token obtained from second gss_init_sec_context() call */
   if(b64_encode_buf(&out, send_tok.value, send_tok.length,
         B64_SALLOC | B64_CRLF) == NULL)
      goto jleave;
   _OUT(out.s);

   gss_release_buffer(&min_stat, &send_tok);
   f &= ~a_F_SEND_TOK;

   _ANSWER(3, FAL0, TRU1);
   out.s = NULL;
   in.s = slp->dat;
   in.l = slp->datlen;
   if(!b64_decode(&out, &in)){
jebase64:
      if(out.s != NULL)
         n_free(out.s);
      n_err(_("Invalid base64 encoding from GSSAPI server\n"));
      goto jleave;
   }
   recv_tok.value = out.s;
   recv_tok.length = out.l;
   maj_stat = gss_unwrap(&min_stat, gss_context, &recv_tok, &send_tok,
         &conf_state, NULL);
   n_free(out.s);
   gss_release_buffer(&min_stat, &send_tok);
   /*f &= ~a_F_SEND_TOK;*/
   if (maj_stat != GSS_S_COMPLETE) {
      _smtp_gssapi_error("unwrapping data", maj_stat, min_stat);
      goto jleave;
   }

   /* First octet: bit-mask with protection mechanisms (1 = no protection
    *    mechanism).
    * Second to fourth octet: maximum message size in network byte order.
    * Fifth and following octets: user name string */
   in.s = n_autorec_alloc((send_tok.length = 4 + sbp->sb_ccred.cc_user.l) +1);
   su_mem_copy(in.s + 4, sbp->sb_ccred.cc_user.s, sbp->sb_ccred.cc_user.l +1);
   in.s[0] = 1;
   in.s[1] = 0;
   in.s[2] = in.s[3] = (char)0xFF;
   send_tok.value = in.s;
   maj_stat = gss_wrap(&min_stat, gss_context, 0, GSS_C_QOP_DEFAULT, &send_tok,
         &conf_state, &recv_tok);
   f |= a_F_RECV_TOK;
   if (maj_stat != GSS_S_COMPLETE) {
      _smtp_gssapi_error("wrapping data", maj_stat, min_stat);
      goto jleave;
   }

   if(b64_encode_buf(&out, recv_tok.value, recv_tok.length,
         B64_SALLOC | B64_CRLF) == NULL)
      goto jleave;
   _OUT(out.s);
   _ANSWER(2, FAL0, FAL0);

   ok = TRU1;
jleave:
   if(f & a_F_RECV_TOK)
      gss_release_buffer(&min_stat, &recv_tok);
   if(f & a_F_SEND_TOK)
      gss_release_buffer(&min_stat, &send_tok);
   if(f & a_F_TARGET_NAME)
      gss_release_name(&min_stat, &target_name);
   if(f & a_F_GSS_CONTEXT)
      gss_delete_sec_context(&min_stat, &gss_context, GSS_C_NO_BUFFER);
   NYD_OU;
   return ok;
}

# ifdef m_DEFINED_GCC_C_NT_HOSTBASED_SERVICE
#  undef GSS_C_NT_HOSTBASED_SERVICE
# endif
#endif /* mx_HAVE_GSSAPI */

/* s-it-mode */
