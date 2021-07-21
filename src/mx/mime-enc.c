/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mime-enc.h.
 *@ QP quoting and _b64_dec(), b64_enc() inspired from NetBSDs mailx(1):
 *@   $NetBSD: mime_codecs.c,v 1.9 2009/04/10 13:08:25 christos Exp $
 *@ TODO We have no notion of a "current message context" and thus badly log.
 *@ TODO This is not final yet, v15 will bring "filters".
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
#define su_FILE mime_enc
#define mx_SOURCE
#define mx_SOURCE_MIME_ENC

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/mime-enc.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_me_qact{
   a_ME_N = 0,
   a_ME_Q = 1, /* Must quote */
   a_ME_SP = 2, /* sp */
   a_ME_XF = 3, /* Special character 'F' - maybe quoted */
   a_ME_XD = 4, /* Special character '.' - maybe quoted */
   a_ME_UU = 5, /* In header, _ must be quoted in encoded word */
   a_ME_US = '_', /* In header, ' ' must be quoted as _ in encoded word */
   a_ME_QM = '?', /* In header, special character ? not always quoted */
   a_ME_EQ = '=', /* In header, '=' must be quoted in encoded word */
   a_ME_HT ='\t', /* Body HT=SP.  Head HT=HT, BUT quote in encoded word */
   a_ME_NL = 0, /* Don't quote '\n' (NL) */
   a_ME_CR = a_ME_Q /* Always quote a '\r' (CR) */
};

/* Lookup tables to decide whether a character must be encoded or not.
 * Email header differences according to RFC 2047, section 4.2:
 * - also quote SP (as the underscore _), TAB, ?, _, CR, LF
 * - don't care about the special ^F[rom] and ^.$ */
static u8 const a_me_qp_body[] = {
    a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,
    a_ME_Q, a_ME_SP, a_ME_NL,  a_ME_Q,  a_ME_Q, a_ME_CR,  a_ME_Q,  a_ME_Q,
    a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,
    a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,
   a_ME_SP,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N, a_ME_XD,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_Q,  a_ME_N,  a_ME_N,

    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N, a_ME_XF,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_Q,
}, a_me_qp_head[] = {
    a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,
    a_ME_Q, a_ME_HT,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,
    a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,
    a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,  a_ME_Q,
   a_ME_US,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N, a_ME_EQ,  a_ME_N, a_ME_QM,

    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N, a_ME_UU,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,
    a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_N,  a_ME_Q,
};

/* The decoding table is only accessed via a_ME_B64_DECUI8() */
static char const a_me_b64_enctbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz" "0123456789" "+/";
static signed char const a_me_b64__dectbl[] = {
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
   52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-2,-1,-1,
   -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
   15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
   -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
   41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};
#define a_ME_B64_EQU S(u32,-2)
#define a_ME_B64_BAD S(u32,-1)
#define a_ME_B64_DECUI8(C) \
   (S(u8,C) >= sizeof(a_me_b64__dectbl)\
    ? a_ME_B64_BAD : S(u32,a_me_b64__dectbl[(u8)(C)]))

/* (Ugly to place an enum here) */
static char const a_me_ctes[] = "7bit\0" "8bit\0" \
      "base64\0" "quoted-printable\0" "binary\0" \
      /* abbrevs */ "8b\0" "b64\0" "qp\0";
enum a_me_ctes_off{
   a_ME_CTES_7B_OFF = 0, a_ME_CTES_7B_LEN = 4,
   a_ME_CTES_8B_OFF = 5, a_ME_CTES_8B_LEN = 4,
   a_ME_CTES_B64_OFF = 10, a_ME_CTES_B64_LEN = 6,
   a_ME_CTES_QP_OFF = 17,  a_ME_CTES_QP_LEN = 16,
   a_ME_CTES_BIN_OFF = 34, a_ME_CTES_BIN_LEN = 6,

   a_ME_CTES_S8B_OFF = 41, a_ME_CTES_S8B_LEN = 2,
   a_ME_CTES_SB64_OFF = 44, a_ME_CTES_SB64_LEN = 3,
   a_ME_CTES_SQP_OFF = 48, a_ME_CTES_SQP_LEN = 2
};

/* Check whether *s must be quoted according to flags, else body rules;
 * sol indicates whether we are at the first character of a line/field */
SINLINE enum a_me_qact a_me_mustquote(char const *s, char const *e, boole sol,
      BITENUM_IS(u32,mx_mime_enc_flags) flags);

/* Trim WS and make work point to the decodable range of in.
 * Return the amount of bytes a b64_dec operation on that buffer requires,
 * or UZ_MAX on overflow error */
static uz a_me_b64_dec_prepare(struct str *work, struct str const *in);

/* Perform b64_dec on in(put) to sufficiently spaced out(put).
 * Return number of useful bytes in out or -1 on error.
 * Note: may enter endless loop if in->l < 4 and 0 return is not handled! */
static sz a_me_b64_dec(struct str *out, struct str *in);

SINLINE enum a_me_qact
a_me_mustquote(char const *s, char const *e, boole sol,
      BITENUM_IS(u32,mx_mime_enc_flags) flags){
   u8 const *qtab;
   enum a_me_qact a, r;
   NYD2_IN;

   qtab = (flags & (mx_MIME_ENC_F_ISHEAD | mx_MIME_ENC_F_ISENCWORD))
         ? a_me_qp_head : a_me_qp_body;

   if(S(u8,*s) > 0x7F){
      r = a_ME_Q;
      goto jleave;
   }

   a = qtab[S(u8,*s)];

   if((r = a) == a_ME_N || a == a_ME_Q)
      goto jleave;

   r = a_ME_Q;

   /* Special header fields */
   if(flags & (mx_MIME_ENC_F_ISHEAD | mx_MIME_ENC_F_ISENCWORD)){
      /* Special massage for encoded words */
      if(flags & mx_MIME_ENC_F_ISENCWORD){
         switch(a){
         case a_ME_HT:
         case a_ME_US:
         case a_ME_EQ:
            r = a;
            /* FALLTHRU */
         case a_ME_UU:
            goto jleave;
         default:
            break;
         }
      }

      /* Treat '?' only special if part of '=?' .. '?=' (still too much quoting
       * since it's '=?CHARSET?CTE?stuff?=', and especially the trailing ?=
       * should be hard to match */
      if(a == a_ME_QM && ((!sol && s[-1] == '=') || (s < e && s[1] == '=')))
         goto jleave;
      goto jnquote;
   }

   /* Body-only */

   if(a == a_ME_SP){
      /* WS only if trailing white space */
      if(&s[1] == e || s[1] == '\n')
         goto jleave;
      goto jnquote;
   }

   /* Rest are special begin-of-line cases */
   if(!sol)
      goto jnquote;

   /* ^From */
   if(a == a_ME_XF){
      if(&s[4] < e && s[1] == 'r' && s[2] == 'o' && s[3] == 'm' && s[4] == ' ')
         goto jleave;
      goto jnquote;
   }
   /* ^.$ */
   if(a == a_ME_XD && (&s[1] == e || s[1] == '\n'))
      goto jleave;

jnquote:
   r = 0;

jleave:
   NYD2_OU;
   return r;
}

static uz
a_me_b64_dec_prepare(struct str *work, struct str const *in){
   uz cp_len;
   NYD2_IN;

   *work = *in;
   cp_len = n_str_trim(work, n_STR_TRIM_BOTH)->l;

   if(cp_len > 16){
      /* su_ERR_OVERFLOW */
      if(UZ_MAX / 3 <= cp_len){
         cp_len = UZ_MAX;
         goto jleave;
      }
      cp_len = ((cp_len * 3) >> 2) + (cp_len >> 3);
   }
   cp_len += (2 * 3) +1;

jleave:
   NYD2_OU;
   return cp_len;
}

static sz
a_me_b64_dec(struct str *out, struct str *in){
   u8 *p, pb;
   u8 const *q, *end;
   sz rv;
   NYD2_IN;

   rv = -1;
   p = S(u8*,&out->s[out->l]);
   q = S(u8 const*,in->s);

   for(end = &q[in->l]; P2UZ(end - q) >= 4; q += 4){
      u32 a, b, c, d;

      a = a_ME_B64_DECUI8(q[0]);
      b = a_ME_B64_DECUI8(q[1]);
      c = a_ME_B64_DECUI8(q[2]);
      d = a_ME_B64_DECUI8(q[3]);

      if(UNLIKELY(a >= a_ME_B64_EQU || b >= a_ME_B64_EQU ||
            c == a_ME_B64_BAD || d == a_ME_B64_BAD))
         goto jleave;

      pb = ((a << 2) | ((b & 0x30) >> 4));
      if(pb != (u8)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
         *p++ = pb;

      if(c == a_ME_B64_EQU){ /* got '=' */
         q += 4;
         if(UNLIKELY(d != a_ME_B64_EQU))
            goto jleave;
         break;
      }

      pb = (((b & 0x0F) << 4) | ((c & 0x3C) >> 2));
      if(pb != (u8)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
         *p++ = pb;

      if(d == a_ME_B64_EQU) /* got '=' */
         break;
      pb = (((c & 0x03) << 6) | d);
      if(pb != (u8)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
         *p++ = pb;
   }
   rv ^= rv;

jleave:{
      uz i;

      i = P2UZ(S(char*,p) - out->s);
      out->l = i;
      if(rv == 0)
         rv = S(sz,i);
   }
   in->l -= P2UZ(q - S(u8*,in->s));
   in->s = UNCONST(char*,q);

   NYD2_OU;
   return rv;
}

enum mx_mime_enc
mx_mime_enc_target(void){
   char const *cp, *v15;
   enum mx_mime_enc rv;
   NYD2_IN;

   if((v15 = ok_vlook(encoding)) != NIL)
      n_OBSOLETE(_("please use *mime-encoding* instead of *encoding*"));

   if((cp = ok_vlook(mime_encoding)) == NIL && (cp = v15) == NIL)
      rv = mx_MIME_DEFAULT_ENCODING;
   else if(!su_cs_cmp_case(cp, &a_me_ctes[a_ME_CTES_S8B_OFF]) ||
         !su_cs_cmp_case(cp, &a_me_ctes[a_ME_CTES_8B_OFF]))
      rv = mx_MIME_ENC_8B;
   else if(!su_cs_cmp_case(cp, &a_me_ctes[a_ME_CTES_SB64_OFF]) ||
         !su_cs_cmp_case(cp, &a_me_ctes[a_ME_CTES_B64_OFF]))
      rv = mx_MIME_ENC_B64;
   else if(!su_cs_cmp_case(cp, &a_me_ctes[a_ME_CTES_SQP_OFF]) ||
         !su_cs_cmp_case(cp, &a_me_ctes[a_ME_CTES_QP_OFF]))
      rv = mx_MIME_ENC_QP;
   else{
      n_err(_("Warning: invalid *mime-encoding*, using Base64: %s\n"), cp);
      rv = mx_MIME_ENC_B64;
   }

   NYD2_OU;
   return rv;
}

enum mx_mime_enc
mx_mime_enc_from_name(char const *hbody){
   enum mx_mime_enc rv;
   NYD2_IN;

   if(hbody == NIL)
      rv = mx_MIME_ENC_7B;
   else{
      struct{
         u8 off;
         u8 len;
         u8 enc;
         u8 __dummy;
      } const *cte, cte_base[] = {
         {a_ME_CTES_7B_OFF, a_ME_CTES_7B_LEN, mx_MIME_ENC_7B, 0},
         {a_ME_CTES_8B_OFF, a_ME_CTES_8B_LEN, mx_MIME_ENC_8B, 0},
         {a_ME_CTES_B64_OFF, a_ME_CTES_B64_LEN, mx_MIME_ENC_B64, 0},
         {a_ME_CTES_QP_OFF, a_ME_CTES_QP_LEN, mx_MIME_ENC_QP, 0},
         {a_ME_CTES_BIN_OFF, a_ME_CTES_BIN_LEN, mx_MIME_ENC_BIN, 0},
         {0, 0, mx_MIME_ENC_NONE, 0}
      };
      union {char const *s; uz l;} u;

      if(*hbody == '"')
         for(u.s = ++hbody; *u.s != '\0' && *u.s != '"'; ++u.s)
            ;
      else
         for(u.s = hbody; *u.s != '\0' && !su_cs_is_white(*u.s); ++u.s)
            ;
      u.l = P2UZ(u.s - hbody);

      for(cte = cte_base;;)
         if(cte->len == u.l && !su_cs_cmp_case(&a_me_ctes[cte->off], hbody)){
            rv = cte->enc;
            break;
         }else if((++cte)->enc == mx_MIME_ENC_NONE){
            rv = mx_MIME_ENC_NONE;
            break;
         }
   }

   NYD2_OU;
   return rv;
}

char const *
mx_mime_enc_name_from_conversion(enum conversion const convert){
   char const *rv;
   NYD2_IN;

   switch(convert){
   case CONV_7BIT: rv = &a_me_ctes[a_ME_CTES_7B_OFF]; break;
   case CONV_8BIT: rv = &a_me_ctes[a_ME_CTES_8B_OFF]; break;
   case CONV_TOQP: rv = &a_me_ctes[a_ME_CTES_QP_OFF]; break;
   case CONV_TOB64: rv = &a_me_ctes[a_ME_CTES_B64_OFF]; break;
   case CONV_NONE: rv = &a_me_ctes[a_ME_CTES_BIN_OFF]; break;
   default: rv = su_empty; break;
   }

   NYD2_OU;
   return rv;
}

uz
mx_mime_enc_mustquote(char const *ln, uz lnlen, boole ishead){
   uz rv;
   boole sol;
   BITENUM_IS(u32,mx_mime_enc_flags) flags;
   NYD2_IN;

   flags = ishead ? mx_MIME_ENC_F_ISHEAD : mx_MIME_ENC_F_NONE;

   for(rv = 0, sol = TRU1; lnlen > 0; sol = FAL0, ++ln, --lnlen)
      switch(a_me_mustquote(ln, ln + lnlen, sol, flags)){
      case a_ME_US:
      case a_ME_EQ:
      case a_ME_HT:
         ASSERT(0);
         /* FALLTHRU */
      case 0:
         continue;
      default:
         ++rv;
      }

   NYD2_OU;
   return rv;
}

uz
mx_qp_enc_calc_size(uz len){
   uz bytes, lines;
   NYD2_IN;

   /* The worst case sequence is 'CRLF' -> '=0D=0A=\n\0'.
    * However, we must be aware that (a) the output may span multiple lines
    * and (b) the input does not end with a newline itself (nonetheless):
    *    LC_ALL=C awk 'BEGIN{
    *       for (i = 1; i < 100000; ++i) printf "\xC3\xBC"
    *    }' |
    *    s-nail -:/ -dSsendcharsets=utf8 -s testsub no@where */

   /* Several su_ERR_OVERFLOW */
   if(len >= UZ_MAX / 3){
      len = UZ_MAX;
      goto jleave;
   }
   bytes = len * 3;
   lines = bytes / mx_QP_LINESIZE;
   len += lines;

   if(len >= UZ_MAX / 3){
      len = UZ_MAX;
      goto jleave;
   }
   /* Trailing hard NL may be missing, so there may be two lines.
    * Thus add soft + hard NL per line and a trailing NUL */
   bytes = len * 3;
   lines = (bytes / mx_QP_LINESIZE) + 1;
   lines <<= 1;
   ++bytes;
   /*if(UZ_MAX - bytes >= lines){
      len = UZ_MAX;
      goto jleave;
   }*/
   bytes += lines;
   len = bytes;

jleave:
   NYD2_OU;
   return len;
}

struct str *
mx_qp_enc(struct str *out, struct str const *in,
      BITENUM_IS(u32,mx_qp_flags) flags){
   uz lnlen;
   char *qp;
   char const *is, *ie;
   boole sol, seenx;
   NYD_IN;

   sol = (flags & mx_QP_ISHEAD ? FAL0 : TRU1);

   if(!(flags & mx_QP_BUF)){
      if((lnlen = mx_qp_enc_calc_size(in->l)) == UZ_MAX){
         out = NIL;
         goto jerr;
      }
      out->s = (flags & mx_QP_AUTO_ALLOC) ? su_AUTO_ALLOC(lnlen)
            : su_REALLOC(out->s, lnlen);
   }

   qp = out->s;
   is = in->s;
   ie = is + in->l;

   if(flags & mx_QP_ISHEAD){
      BITENUM_IS(u32,mx_mime_enc_flags) ef;

      ef = mx_MIME_ENC_F_ISHEAD |
            (flags & mx_QP_ISENCWORD ? mx_MIME_ENC_F_ISENCWORD : 0);

      for(seenx = FAL0, sol = TRU1; is < ie; sol = FAL0, ++qp){
         char c;
         enum a_me_qact mq;

         mq = a_me_mustquote(is, ie, sol, ef);
         c = *is++;

         if(mq == a_ME_N){
            /* We convert into a single *encoded-word*, that'll end up in
             * =?C?Q??=; quote '?' from when we're inside there on */
            if(seenx && c == '?')
               goto jheadq;
            *qp = c;
         }else if(mq == a_ME_US)
            *qp = a_ME_US;
         else{
            seenx = TRU1;
jheadq:
            *qp++ = '=';
            qp = n_c_to_hex_base16(qp, c) + 1;
         }
      }
      goto jleave;
   }

   /* The body needs to take care for soft line breaks etc. */
   for(lnlen = 0, seenx = FAL0; is < ie; sol = FAL0){
      char c;
      enum a_me_qact mq;

      mq = a_me_mustquote(is, ie, sol, mx_MIME_ENC_F_NONE);
      c = *is++;

      if(mq == a_ME_N && (c != '\n' || !seenx)){
         *qp++ = c;
         if(++lnlen < mx_QP_LINESIZE - 1)
            continue;
         /* Don't write a soft line break when we're in the last possible
          * column and either an LF has been written or only an LF follows, as
          * that'll end the line anyway */
         /* XXX but - ensure is+1>=ie, then??
          * xxx and/or - what about resetting lnlen; that contra
          * xxx dicts input==1 input line ASSERTion, though */
         if(c == '\n' || is == ie || is[0] == '\n' || is[1] == '\n')
            continue;
jsoftnl:
         qp[0] = '=';
         qp[1] = '\n';
         qp += 2;
         lnlen = 0;
         continue;
      }

      if(lnlen > mx_QP_LINESIZE - 3 - 1){
         qp[0] = '=';
         qp[1] = '\n';
         qp += 2;
         lnlen = 0;
      }
      *qp++ = '=';
      qp = n_c_to_hex_base16(qp, c);
      qp += 2;
      lnlen += 3;
      if(c != '\n' || !seenx)
         seenx = (c == '\r');
      else{
         seenx = FAL0;
         goto jsoftnl;
      }
   }

   /* Enforce soft line break if we haven't seen LF */
   if(in->l > 0 && *--is != '\n'){
      qp[0] = '=';
      qp[1] = '\n';
      qp += 2;
   }

jleave:
   out->l = P2UZ(qp - out->s);
   out->s[out->l] = '\0';

jerr:
   NYD_OU;
   return out;
}

boole
mx_qp_dec_header(struct str *out, struct str const *in){
   struct n_string s;
   char const *is, *ie;
   NYD_IN;

   /* su_ERR_OVERFLOW */
   if(UZ_MAX -1 - out->l <= in->l ||
         S32_MAX <= out->l + in->l){ /* XXX wrong, we may replace */
      out->l = 0;
      out = NIL;
      goto jleave;
   }

   n_string_creat(&s);
   n_string_reserve(n_string_take_ownership(&s, out->s,
         (out->l == 0 ? 0 : out->l +1), out->l),
      in->l + (in->l >> 2));

   for(is = in->s, ie = &is[in->l - 1]; is <= ie;){
      s32 c;

      c = *is++;
      if(c == '='){
         if(is >= ie){
            goto jpushc; /* TODO According to RFC 2045, 6.7,
            * ++is; TODO we should warn the user, but have no context
            * goto jehead; TODO to do so; can't over and over */
         }else if((c = n_c_from_hex_base16(is)) >= 0){
            is += 2;
            goto jpushc;
         }else{
            /* Invalid according to RFC 2045, section 6.7 */
            /* TODO Follow RFC 2045, 6.7 advise and simply put through */
            c = '=';
            goto jpushc;
/* TODO jehead:
 * TODO      if(n_psonce & n_PSO_UNICODE)
 *              n_string_push_buf(&s, su_utf_8_replacer,
 *                 sizeof(su_utf_8_replacer) -1);
 * TODO       else{
 * TODO          c = '?';
 * TODO          goto jpushc;
 * TODO       }*/
         }
      }else{
         if(c == '_' /* a_ME_US */)
            c = ' ';
jpushc:
         n_string_push_c(&s, S(char,c));
      }
   }

   out->s = n_string_cp(&s);
   out->l = s.s_len;
   n_string_gut(n_string_drop_ownership(&s));

jleave:
   NYD_OU;
   return (out != NIL);
}

boole
mx_qp_dec_part(struct str *out, struct str const *in, struct str *outrest,
      struct str *inrest_or_nil){
   struct n_string s_b, *s;
   char const *is, *ie;
   NYD_IN;

   if(outrest->l != 0){
      is = out->s;
      *out = *outrest;
      outrest->s = UNCONST(char*,is);
      outrest->l = 0;
   }

   /* su_ERR_OVERFLOW */
   if(UZ_MAX -1 - out->l <= in->l ||
         S32_MAX <= out->l + in->l) /* XXX wrong, we may replace */
      goto jerr;

   s = n_string_creat(&s_b);
   s = n_string_take_ownership(s, out->s,
         (out->l == 0 ? 0 : out->l +1), out->l);
   s = n_string_reserve(s, in->l + (in->l >> 2));

   for(is = in->s, ie = &is[in->l - 1]; is <= ie;){
      s32 c;

      if((c = *is++) != '='){
jpushc:
         n_string_push_c(s, (char)c);
         continue;
      }

      /* RFC 2045, 6.7:
       *   Therefore, when decoding a Quoted-Printable body, any
       *   trailing white space on a line must be deleted, as it will
       *   necessarily have been added by intermediate transport
       *   agents */
      for(; is <= ie && su_cs_is_blank(*is); ++is)
         ;
      if(is >= ie){
         /* Soft line break? */
         if(*is == '\n')
            goto jsoftnl;
        goto jpushc; /* TODO According to RFC 2045, 6.7,
         * ++is; TODO we should warn the user, but have no context
         * goto jebody; TODO to do so; can't over and over */
      }

      /* Not a soft line break? */
      if(*is != '\n'){
         if((c = n_c_from_hex_base16(is)) >= 0){
            is += 2;
            goto jpushc;
         }
         /* Invalid according to RFC 2045, section 6.7 */
         /* TODO Follow RFC 2045, 6.7 advise and simply put through */
         c = '=';
         goto jpushc;
/* TODO jebody:
 * TODO   if(n_psonce & n_PSO_UNICODE)
 *           n_string_push_buf(&s, su_utf_8_replacer,
 *              sizeof(su_utf_8_replacer) -1);
 * TODO    else{
 * TODO       c = '?';
 * TODO       goto jpushc;
 * TODO    }*/
      }

      /* CRLF line endings are encoded as QP, followed by a soft line break, so
       * check for this special case, and simply forget we have seen one, so as
       * not to end up with the entire DOS file in a contiguous buffer */
jsoftnl:
      if(s->s_len > 0 && s->s_dat[s->s_len - 1] == '\n'){
#if 0       /* TODO qp_dec_part() we do not normalize CRLF
          * TODO to LF because for that we would need
          * TODO to know if we are about to write to
          * TODO the display or do save the file!
          * TODO 'hope the MIME/send layer rewrite will
          * TODO offer the possibility to DTRT */
         if(s->s_len > 1 && s->s_dat[s->s_len - 2] == '\r')
            n_string_push_c(n_string_trunc(s, s->s_len - 2), '\n');
#endif
         break;
      }

      /* C99 */{
         char *cp;
         uz l;

         if((l = P2UZ(ie - is)) > 0){
            if(inrest_or_nil == NIL)
               goto jerr;
            n_str_assign_buf(inrest_or_nil, is, l);
         }
         cp = outrest->s;
         outrest->s = n_string_cp(s);
         outrest->l = s->s_len;
         n_string_drop_ownership(s);
         if(cp != NIL)
            su_FREE(cp);
      }
      break;
   }

   out->s = n_string_cp(s);
   out->l = s->s_len;
   n_string_gut(n_string_drop_ownership(s));

jleave:
   NYD_OU;
   return (out != NIL);

jerr:
   out->l = 0;
   out = NIL;
   goto jleave;
}

uz
mx_b64_enc_calc_size(uz len){
   NYD2_IN;

   if(len >= UZ_MAX / 4)
      len = UZ_MAX;
   else{
      len = (len * 4) / 3;
      len += (((len / mx_B64_ENC_INPUT_PER_LINE) + 1) * 3);
      len += 2 + 1; /* CRLF, \0 */
   }

   NYD2_OU;
   return len;
}

struct str *
mx_b64_enc(struct str *out, struct str const *in,
      BITENUM_IS(u32,mx_b64_flags) flags){
   u8 const *p;
   uz i, lnlen;
   char *b64;
   NYD_IN;

   ASSERT(!(flags & mx_B64_NOPAD) ||
      !(flags & (mx_B64_CRLF | mx_B64_LF | mx_B64_MULTILINE)));

   p = S(u8 const*,in->s);

   if(!(flags & mx_B64_BUF)){
      if((i = mx_b64_enc_calc_size(in->l)) == UZ_MAX){
         out = NIL;
         goto jleave;
      }
      out->s = (flags & mx_B64_AUTO_ALLOC) ? su_AUTO_ALLOC(i)
            : su_REALLOC(out->s, i);
   }
   b64 = out->s;

   if(!(flags & (mx_B64_CRLF | mx_B64_LF)))
      flags &= ~mx_B64_MULTILINE;

   for(lnlen = 0, i = in->l; S(sz,i) > 0; p += 3, i -= 3){
      u32 a, b, c;

      a = p[0];
      b64[0] = a_me_b64_enctbl[a >> 2];

      switch(i){
      case 1:
         b64[1] = a_me_b64_enctbl[((a & 0x3) << 4)];
         b64[2] =
         b64[3] = '=';
         break;
      case 2:
         b = p[1];
         b64[1] = a_me_b64_enctbl[((a & 0x03) << 4) | ((b & 0xF0u) >> 4)];
         b64[2] = a_me_b64_enctbl[((b & 0x0F) << 2)];
         b64[3] = '=';
         break;
      default:
         b = p[1];
         c = p[2];
         b64[1] = a_me_b64_enctbl[((a & 0x03) << 4) | ((b & 0xF0u) >> 4)];
         b64[2] = a_me_b64_enctbl[((b & 0x0F) << 2) | ((c & 0xC0u) >> 6)];
         b64[3] = a_me_b64_enctbl[c & 0x3F];
         break;
      }

      b64 += 4;
      if(!(flags & mx_B64_MULTILINE))
         continue;
      lnlen += 4;
      if(lnlen < mx_B64_LINESIZE)
         continue;

      lnlen = 0;
      if(flags & mx_B64_CRLF)
         *b64++ = '\r';
      if(flags & (mx_B64_CRLF | mx_B64_LF))
         *b64++ = '\n';
   }

   if((flags & (mx_B64_CRLF | mx_B64_LF)) &&
         (!(flags & mx_B64_MULTILINE) || lnlen != 0)){
      if(flags & mx_B64_CRLF)
         *b64++ = '\r';
      if(flags & (mx_B64_CRLF | mx_B64_LF))
         *b64++ = '\n';
   }else if(flags & mx_B64_NOPAD)
      while(b64 != out->s && b64[-1] == '=')
         --b64;

   out->l = P2UZ(b64 - out->s);
   out->s[out->l] = '\0';

   /* Base64 includes + and /, replace them with _ and -.
    * This is base64url according to RFC 4648, then.  Since we only support
    * that for encoding and it is only used for boundary strings, this is
    * yet a primitive implementation; xxx use tables; support decoding */
   if(flags & mx_B64_RFC4648URL){
      char c;

      for(b64 = out->s; (c = *b64) != '\0'; ++b64)
         if(c == '+')
            *b64 = '-';
         else if(c == '/')
               *b64 = '_';
   }

jleave:
   NYD_OU;
   return out;
}

struct str *
mx_b64_enc_buf(struct str *out, void const *vp, uz vp_len,
      BITENUM_IS(u32,mx_b64_flags) flags){
   struct str in;
   NYD_IN;

   in.s = UNCONST(char*, vp);
   in.l = vp_len;
   out = mx_b64_enc(out, &in, flags);

   NYD_OU;
   return out;
}

boole
mx_b64_dec(struct str *out, struct str const *in){
   struct str work;
   uz len;
   NYD_IN;

   out->l = 0;

   if((len = a_me_b64_dec_prepare(&work, in)) == UZ_MAX)
      goto jerr;

   /* Ignore an empty input, as may happen for an empty final line */
   if(work.l == 0)
      out->s = su_REALLOC(out->s, 1);
   else if(work.l >= 4 && !(work.l & 3)){
      out->s = su_REALLOC(out->s, len +1);
      if(S(sz,len = a_me_b64_dec(out, &work)) < 0)
         goto jerr;
   }else
      goto jerr;

   out->s[out->l] = '\0';

jleave:
   NYD_OU;
   return (out != NIL);
jerr:
   out = NIL;
   goto jleave;
}

boole
mx_b64_dec_header(struct str *out, struct str const *in){
   struct str outr, inr;
   NYD_IN;

   if(!mx_b64_dec(out, in)){
      su_mem_set(&outr, 0, sizeof outr);
      su_mem_set(&inr, 0, sizeof inr);

      if(!mx_b64_dec_part(out, in, &outr, &inr) || outr.l > 0 || inr.l > 0)
         out = NIL;

      if(inr.s != NIL)
         su_FREE(inr.s);
      if(outr.s != NIL)
         su_FREE(outr.s);
   }

   NYD_OU;
   return (out != NIL);
}

boole
mx_b64_dec_part(struct str *out, struct str const *in, struct str *outrest,
      struct str *inrest_or_nil){
   struct str work, save;
   struct n_string s, workbuf;
   u32 a, b, c, b64l;
   char ca, cb, cc, cx;
   boole stripcr;
   uz len;
   NYD_IN;

   n_string_creat(&s);
   if((len = out->l) > 0 && out->s[len] == '\0')
      (void)n_string_take_ownership(&s, out->s, len +1, len);
   else{
      if(len > 0)
         n_string_push_buf(&s, out->s, len);
      if(out->s != NIL)
         su_FREE(out->s);
   }

   out->s = NIL, out->l = 0;

   n_string_creat(&workbuf);

   if((len = a_me_b64_dec_prepare(&work, in)) == UZ_MAX)
      goto jerr;

   if(outrest->l > 0){
      n_string_push_buf(&s, outrest->s, outrest->l);
      outrest->l = 0;
   }

   /* su_ERR_OVERFLOW */
   if(UZ_MAX - len <= s.s_len ||
         S32_MAX <= len + s.s_len) /* XXX wrong, we may replace */
      goto jerr;

   if(work.l == 0)
      goto jok;

   /* This text decoder is extremely expensive, especially given that in all
    * but _invalid_ cases it is not even needed!  So try once to do the normal
    * decoding, if that fails, go the hard way */
   save = work;
   out->s = n_string_resize(&s, len + (out->l = b64l = s.s_len))->s_dat;

   if(work.l >= 4 && a_me_b64_dec(out, &work) >= 0){
      n_string_trunc(&s, out->l);
      if(work.l == 0)
         goto jok;
   }

   n_string_trunc(&s, b64l);
   work = save;
   out->s = NIL, out->l = 0;

   /* TODO b64_dec_part() does not yet STOP if it sees padding, whereas
    * TODO OpenSSL and mutt simply bail on such stuff */
   stripcr = ((n_pstate & n_PS_BASE64_STRIP_CR) != 0);
   UNINIT(ca, 0); UNINIT(a, 0);
   UNINIT(cb, 0); UNINIT(b, 0);
   UNINIT(cc, 0); UNINIT(c, 0);
   for(b64l = 0;;){
      u32 x;

      x = a_ME_B64_DECUI8(S(u8,cx = *work.s));
      switch(b64l){
      case 0:
         if(x >= a_ME_B64_EQU)
            goto jrepl;
         ca = cx;
         a = x;
         ++b64l;
         break;
      case 1:
         if(x >= a_ME_B64_EQU)
            goto jrepl;
         cb = cx;
         b = x;
         ++b64l;
         break;
      case 2:
         if(x == a_ME_B64_BAD)
            goto jrepl;
         cc = cx;
         c = x;
         ++b64l;
         break;
      case 3:
         if(x == a_ME_B64_BAD){
jrepl:
            /* TODO This would be wrong since iconv(3) may be applied first! */
            n_err(_("Invalid base64 encoding ignored\n"));
#if 0
            if(n_psonce & n_PSO_UNICODE)
               n_string_push_buf(&s, su_utf_8_replacer,
                  sizeof(su_utf_8_replacer) -1);
            else
               n_string_push_c(&s, '?');
#endif
            ;
         }else if(c == a_ME_B64_EQU && x != a_ME_B64_EQU){
            /* This is not only invalid but bogus.  Skip it over! */
            /* TODO This would be wrong since iconv(3) may be applied first! */
            n_err(_("Illegal base64 encoding ignored\n"));
#if 0
            n_string_push_buf(&s, su_UTF_8_REPLACER su_UTF_8_REPLACEMENT
               su_UTF_8_REPLACER su_UTF_8_REPLACEMENT,
               (sizeof(su_UTF_8_REPLACER) -1) * 4);
#endif
            b64l = 0;
         }else{
            u8 pb;

            pb = ((a << 2) | ((b & 0x30) >> 4));
            if(pb != S(u8,'\r') || !stripcr)
               n_string_push_c(&s, (char)pb);
            pb = (((b & 0x0F) << 4) | ((c & 0x3C) >> 2));
            if(pb != S(u8,'\r') || !stripcr)
               n_string_push_c(&s, (char)pb);
            if(x != a_ME_B64_EQU){
               pb = (((c & 0x03) << 6) | x);
               if(pb != S(u8,'\r') || !stripcr)
                  n_string_push_c(&s, (char)pb);
            }
            ++b64l;
         }
         break;
      }

      ++work.s;
      if(--work.l == 0){
         if(b64l > 0 && b64l != 4){
            if(inrest_or_nil == NIL)
               goto jerr;
            inrest_or_nil->s = su_REALLOC(inrest_or_nil->s, b64l +1);
            inrest_or_nil->s[0] = ca;
            if(b64l > 1)
               inrest_or_nil->s[1] = cb;
            if(b64l > 2)
               inrest_or_nil->s[2] = cc;
            inrest_or_nil->s[inrest_or_nil->l = b64l] = '\0';
         }
         goto jok;
      }
      if(b64l == 4)
         b64l = 0;
   }

jok:
   out->s = n_string_cp(&s);
   out->l = s.s_len;
   n_string_drop_ownership(&s);

jleave:
   n_string_gut(&workbuf);
   n_string_gut(&s);
   NYD_OU;
   return (out != NIL);

jerr:
   out = NIL;
   goto jleave;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MIME_ENC
/* s-it-mode */
