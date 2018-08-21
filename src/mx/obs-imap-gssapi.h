/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of IMAP GSS-API authentication according to RFC 1731.
 *@ TODO GSS-API should also be joined into "a VFS".
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 * SPDX-License-Identifier: BSD-4-Clause
 */
/*
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

#ifdef HAVE_GSSAPI
#ifndef GSSAPI_REG_INCLUDE
# include <gssapi/gssapi.h>
# ifdef GSSAPI_OLD_STYLE
#  include <gssapi/gssapi_generic.h>
#  define GSS_C_NT_HOSTBASED_SERVICE   gss_nt_service_name
#  define NAIL_DEFINED_GCC_C_NT_HOSTBASED_SERVICE
# endif
#else
# include <gssapi.h>
#endif

static void _imap_gssapi_error1(const char *s, OM_uint32 code, int typ);
static void _imap_gssapi_error(const char *s, OM_uint32 maj_stat,
               OM_uint32 min_stat);
static char * _imap_gssapi_last_at_before_slash(char const *sp);

static void
_imap_gssapi_error1(const char *s, OM_uint32 code, int typ)
{
   OM_uint32 maj_stat, min_stat;
   gss_buffer_desc msg = GSS_C_EMPTY_BUFFER;
   OM_uint32 msg_ctx = 0;
   NYD_IN;

   do {
      maj_stat = gss_display_status(&min_stat, code, typ, GSS_C_NO_OID,
            &msg_ctx, &msg);
      if (maj_stat == GSS_S_COMPLETE) {
         fprintf(stderr, "GSS error: %s / %.*s\n",
            s, (int)msg.length, (char*)msg.value);
         gss_release_buffer(&min_stat, &msg);
      } else {
         fprintf(stderr, "GSS error: %s / unknown\n", s);
         break;
      }
   } while (msg_ctx);
   NYD_OU;
}

static void
_imap_gssapi_error(const char *s, OM_uint32 maj_stat, OM_uint32 min_stat)
{
   NYD_IN;
   _imap_gssapi_error1(s, maj_stat, GSS_C_GSS_CODE);
   _imap_gssapi_error1(s, min_stat, GSS_C_MECH_CODE);
   NYD_OU;
}

static char *
_imap_gssapi_last_at_before_slash(char const *sp)
{
   char const *cp;
   char c;
   NYD_IN;

   for (cp = sp; (c = *cp) != '\0'; ++cp)
      if (c == '/')
         break;
   while (cp > sp && *--cp != '@')
      ;
   if (*cp != '@')
      cp = NULL;
   NYD_OU;
   return n_UNCONST(cp);
}

static enum okay
_imap_gssapi(struct mailbox *mp, struct ccred *ccred)
{
   char o[LINESIZE];
   struct str in, out;
   gss_buffer_desc send_tok, recv_tok;
   gss_name_t target_name;
   gss_ctx_id_t gss_context;
   OM_uint32 maj_stat, min_stat, ret_flags;
   int conf_state;
   FILE *queuefp = NULL;
   char *server, *cp;
   enum{
      a_F_NONE,
      a_F_RECV_TOK = 1u<<0,
      a_F_SEND_TOK = 1u<<1,
      a_F_TARGET_NAME = 1u<<2,
      a_F_GSS_CONTEXT = 1u<<3
   } f;
   enum okay ok;
   NYD_X;

   ok = STOP;
   f = a_F_NONE;

   {  size_t i = strlen(mp->mb_imap_account) +1;
      server = n_autorec_alloc(i);
      memcpy(server, mp->mb_imap_account, i);
   }
   if (!strncmp(server, "imap://", 7))
      server += 7;
   else if (!strncmp(server, "imaps://", 8))
      server += 8;
   if ((cp = _imap_gssapi_last_at_before_slash(server)) != NULL)
      server = &cp[1];
   for (cp = server; *cp; cp++)
      *cp = lowerconv(*cp);
   send_tok.value = n_autorec_alloc(
         (send_tok.length = strlen(server) -1 + 5) +1);
   snprintf(send_tok.value, send_tok.length, "imap@%s", server);
   maj_stat = gss_import_name(&min_stat, &send_tok, GSS_C_NT_HOSTBASED_SERVICE,
         &target_name);
   f |= a_F_TARGET_NAME;
   if (maj_stat != GSS_S_COMPLETE) {
      _imap_gssapi_error(savestrbuf(send_tok.value, send_tok.length),
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
      _imap_gssapi_error("initializing GSS context", maj_stat, min_stat);
      goto jleave;
   }

   snprintf(o, sizeof o, "%s AUTHENTICATE GSSAPI\r\n", tag(1));
   IMAP_OUT(o, 0, goto jleave);

   /*
    * No response data expected.
    */
   imap_answer(mp, 1);
   if (response_type != RESPONSE_CONT)
      goto jleave;
   while (maj_stat == GSS_S_CONTINUE_NEEDED) {
      /* Pass token obtained from first gss_init_sec_context() call. */
      if(b64_encode_buf(&out, send_tok.value, send_tok.length,
            B64_SALLOC | B64_CRLF) == NULL)
         goto jleave;
      gss_release_buffer(&min_stat, &send_tok);
      f &= ~a_F_SEND_TOK;
      IMAP_OUT(out.s, 0, goto jleave);
      imap_answer(mp, 1);
      if (response_type != RESPONSE_CONT)
         goto jleave;
      out.s = NULL;
      in.s = responded_text;
      in.l = strlen(responded_text);
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
         _imap_gssapi_error("initializing context", maj_stat, min_stat);
         goto jleave;
      }
   }

   gss_release_name(&min_stat, &target_name);
   f &= ~a_F_TARGET_NAME;

   /* Pass token obtained from second gss_init_sec_context() call */
   if(b64_encode_buf(&out, send_tok.value, send_tok.length,
         B64_SALLOC | B64_CRLF) == NULL)
      goto jleave;
   IMAP_OUT(out.s, 0, goto jleave);

   gss_release_buffer(&min_stat, &send_tok);
   f &= ~a_F_SEND_TOK;

   /*
    * First octet: bit-mask with protection mechanisms.
    * Second to fourth octet: maximum message size in network byte order.
    *
    * This code currently does not care about the values.
    */
   imap_answer(mp, 1);
   if (response_type != RESPONSE_CONT)
      goto jleave;
   out.s = NULL;
   in.s = responded_text;
   in.l = strlen(responded_text);
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
      _imap_gssapi_error("unwrapping data", maj_stat, min_stat);
      goto jleave;
   }

   /* First octet: bit-mask with protection mechanisms (1 = no protection
    *    mechanism).
    * Second to fourth octet: maximum message size in network byte order.
    * Fifth and following octets: user name string.
    */
   o[0] = 1;
   o[1] = 0;
   o[2] = o[3] = (char)0377;
   snprintf(&o[4], sizeof o - 4, "%s", ccred->cc_user.s);
   send_tok.value = o;
   send_tok.length = strlen(&o[4]) -1 + 4;
   maj_stat = gss_wrap(&min_stat, gss_context, 0, GSS_C_QOP_DEFAULT, &send_tok,
         &conf_state, &recv_tok);
   f |= a_F_RECV_TOK;
   if (maj_stat != GSS_S_COMPLETE) {
      _imap_gssapi_error("wrapping data", maj_stat, min_stat);
      goto jleave;
   }

   if(b64_encode_buf(&out, recv_tok.value, recv_tok.length,
         B64_SALLOC | B64_CRLF) == NULL)
      goto jleave;
   IMAP_OUT(out.s, MB_COMD, goto jleave);

   while (mp->mb_active & MB_COMD)
      ok = imap_answer(mp, 1);
jleave:
   if(f & a_F_RECV_TOK)
      gss_release_buffer(&min_stat, &recv_tok);
   if(f & a_F_SEND_TOK)
      gss_release_buffer(&min_stat, &send_tok);
   if(f & a_F_TARGET_NAME)
      gss_release_name(&min_stat, &target_name);
   if(f & a_F_GSS_CONTEXT)
      gss_delete_sec_context(&min_stat, &gss_context, GSS_C_NO_BUFFER);
   return ok;
}

# ifdef NAIL_DEFINED_GCC_C_NT_HOSTBASED_SERVICE
#  undef GSS_C_NT_HOSTBASED_SERVICE
# endif
#endif /* HAVE_GSSAPI */

/* s-it-mode */
