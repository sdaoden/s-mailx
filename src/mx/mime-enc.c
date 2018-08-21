/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Content-Transfer-Encodings as defined in RFC 2045 (and RFC 2047;
 *@ for _header() versions: including "encoded word" as of RFC 2049):
 *@ - Quoted-Printable, section 6.7
 *@ - Base64, section 6.8
 *@ QP quoting and _b64_decode(), b64_encode() inspired from NetBSDs mailx(1):
 *@   $NetBSD: mime_codecs.c,v 1.9 2009/04/10 13:08:25 christos Exp $
 *@ TODO We have no notion of a "current message context" and thus badly log.
 *@ TODO This is not final yet, v15 will bring "filters".
 *
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE mime_enc

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

enum a_me_qact{
   a_ME_N = 0,
   a_ME_Q = 1,       /* Must quote */
   a_ME_SP = 2,      /* sp */
   a_ME_XF = 3,      /* Special character 'F' - maybe quoted */
   a_ME_XD = 4,      /* Special character '.' - maybe quoted */
   a_ME_UU = 5,      /* In header, _ must be quoted in encoded word */
   a_ME_US = '_',    /* In header, ' ' must be quoted as _ in encoded word */
   a_ME_QM = '?',    /* In header, special character ? not always quoted */
   a_ME_EQ = '=',    /* In header, '=' must be quoted in encoded word */
   a_ME_HT ='\t',    /* Body HT=SP.  Head HT=HT, BUT quote in encoded word */
   a_ME_NL = 0,      /* Don't quote '\n' (NL) */
   a_ME_CR = a_ME_Q  /* Always quote a '\r' (CR) */
};

/* Lookup tables to decide whether a character must be encoded or not.
 * Email header differences according to RFC 2047, section 4.2:
 * - also quote SP (as the underscore _), TAB, ?, _, CR, LF
 * - don't care about the special ^F[rom] and ^.$ */
static ui8_t const a_me_qp_body[] = {
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
#define a_ME_B64_EQU (ui32_t)-2
#define a_ME_B64_BAD (ui32_t)-1
#define a_ME_B64_DECUI8(C) \
   ((ui8_t)(C) >= sizeof(a_me_b64__dectbl)\
    ? a_ME_B64_BAD : (ui32_t)a_me_b64__dectbl[(ui8_t)(C)])

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
n_INLINE enum a_me_qact a_me_mustquote(char const *s, char const *e,
                           bool_t sol, enum mime_enc_flags flags);

/* Trim WS and make work point to the decodable range of in.
 * Return the amount of bytes a b64_decode operation on that buffer requires,
 * or UIZ_MAX on overflow error */
static size_t a_me_b64_decode_prepare(struct str *work, struct str const *in);

/* Perform b64_decode on in(put) to sufficiently spaced out(put).
 * Return number of useful bytes in out or -1 on error.
 * Note: may enter endless loop if in->l < 4 and 0 return is not handled! */
static ssize_t a_me_b64_decode(struct str *out, struct str *in);

n_INLINE enum a_me_qact
a_me_mustquote(char const *s, char const *e, bool_t sol,
      enum mime_enc_flags flags){
   ui8_t const *qtab;
   enum a_me_qact a, r;
   NYD2_IN;

   qtab = (flags & (MIMEEF_ISHEAD | MIMEEF_ISENCWORD))
         ? a_me_qp_head : a_me_qp_body;

   if((ui8_t)*s > 0x7F){
      r = a_ME_Q;
      goto jleave;
   }

   a = qtab[(ui8_t)*s];

   if((r = a) == a_ME_N || a == a_ME_Q)
      goto jleave;

   r = a_ME_Q;

   /* Special header fields */
   if(flags & (MIMEEF_ISHEAD | MIMEEF_ISENCWORD)){
      /* Special massage for encoded words */
      if(flags & MIMEEF_ISENCWORD){
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

static size_t
a_me_b64_decode_prepare(struct str *work, struct str const *in){
   size_t cp_len;
   NYD2_IN;

   *work = *in;
   cp_len = n_str_trim(work, n_STR_TRIM_BOTH)->l;

   if(cp_len > 16){
      /* n_ERR_OVERFLOW */
      if(UIZ_MAX / 3 <= cp_len){
         cp_len = UIZ_MAX;
         goto jleave;
      }
      cp_len = ((cp_len * 3) >> 2) + (cp_len >> 3);
   }
   cp_len += (2 * 3) +1;
jleave:
   NYD2_OU;
   return cp_len;
}

static ssize_t
a_me_b64_decode(struct str *out, struct str *in){
   ui8_t *p, pb;
   ui8_t const *q, *end;
   ssize_t rv;
   NYD2_IN;

   rv = -1;
   p = (ui8_t*)&out->s[out->l];
   q = (ui8_t const*)in->s;

   for(end = &q[in->l]; PTR2SIZE(end - q) >= 4; q += 4){
      ui32_t a, b, c, d;

      a = a_ME_B64_DECUI8(q[0]);
      b = a_ME_B64_DECUI8(q[1]);
      c = a_ME_B64_DECUI8(q[2]);
      d = a_ME_B64_DECUI8(q[3]);

      if(n_UNLIKELY(a >= a_ME_B64_EQU || b >= a_ME_B64_EQU ||
            c == a_ME_B64_BAD || d == a_ME_B64_BAD))
         goto jleave;

      pb = ((a << 2) | ((b & 0x30) >> 4));
      if(pb != (ui8_t)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
         *p++ = pb;

      if(c == a_ME_B64_EQU){ /* got '=' */
         q += 4;
         if(n_UNLIKELY(d != a_ME_B64_EQU))
            goto jleave;
         break;
      }

      pb = (((b & 0x0F) << 4) | ((c & 0x3C) >> 2));
      if(pb != (ui8_t)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
         *p++ = pb;

      if(d == a_ME_B64_EQU) /* got '=' */
         break;
      pb = (((c & 0x03) << 6) | d);
      if(pb != (ui8_t)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
         *p++ = pb;
   }
   rv ^= rv;

jleave:{
      size_t i;

      i = PTR2SIZE((char*)p - out->s);
      out->l = i;
      if(rv == 0)
         rv = (ssize_t)i;
   }
   in->l -= PTR2SIZE(q - (ui8_t*)in->s);
   in->s = n_UNCONST(q);
   NYD2_OU;
   return rv;
}

FL enum mime_enc
mime_enc_target(void){
   char const *cp, *v15;
   enum mime_enc rv;
   NYD2_IN;

   if((v15 = ok_vlook(encoding)) != NULL)
      n_OBSOLETE(_("please use *mime-encoding* instead of *encoding*"));

   if((cp = ok_vlook(mime_encoding)) == NULL && (cp = v15) == NULL)
      rv = MIME_DEFAULT_ENCODING;
   else if(!asccasecmp(cp, &a_me_ctes[a_ME_CTES_S8B_OFF]) ||
         !asccasecmp(cp, &a_me_ctes[a_ME_CTES_8B_OFF]))
      rv = MIMEE_8B;
   else if(!asccasecmp(cp, &a_me_ctes[a_ME_CTES_SB64_OFF]) ||
         !asccasecmp(cp, &a_me_ctes[a_ME_CTES_B64_OFF]))
      rv = MIMEE_B64;
   else if(!asccasecmp(cp, &a_me_ctes[a_ME_CTES_SQP_OFF]) ||
         !asccasecmp(cp, &a_me_ctes[a_ME_CTES_QP_OFF]))
      rv = MIMEE_QP;
   else{
      n_err(_("Warning: invalid *mime-encoding*, using Base64: %s\n"), cp);
      rv = MIMEE_B64;
   }
   NYD2_OU;
   return rv;
}

FL enum mime_enc
mime_enc_from_ctehead(char const *hbody){
   enum mime_enc rv;
   NYD2_IN;

   if(hbody == NULL)
      rv = MIMEE_7B;
   else{
      struct{
         ui8_t off;
         ui8_t len;
         ui8_t enc;
         ui8_t __dummy;
      } const *cte, cte_base[] = {
         {a_ME_CTES_7B_OFF, a_ME_CTES_7B_LEN, MIMEE_7B, 0},
         {a_ME_CTES_8B_OFF, a_ME_CTES_8B_LEN, MIMEE_8B, 0},
         {a_ME_CTES_B64_OFF, a_ME_CTES_B64_LEN, MIMEE_B64, 0},
         {a_ME_CTES_QP_OFF, a_ME_CTES_QP_LEN, MIMEE_QP, 0},
         {a_ME_CTES_BIN_OFF, a_ME_CTES_BIN_LEN, MIMEE_BIN, 0},
         {0, 0, MIMEE_NONE, 0}
      };
      union {char const *s; size_t l;} u;

      if(*hbody == '"')
         for(u.s = ++hbody; *u.s != '\0' && *u.s != '"'; ++u.s)
            ;
      else
         for(u.s = hbody; *u.s != '\0' && !whitechar(*u.s); ++u.s)
            ;
      u.l = PTR2SIZE(u.s - hbody);

      for(cte = cte_base;;)
         if(cte->len == u.l && !asccasecmp(&a_me_ctes[cte->off], hbody)){
            rv = cte->enc;
            break;
         }else if((++cte)->enc == MIMEE_NONE){
            rv = MIMEE_NONE;
            break;
         }
   }
   NYD2_OU;
   return rv;
}

FL char const *
mime_enc_from_conversion(enum conversion const convert){
   char const *rv;
   NYD2_IN;

   switch(convert){
   case CONV_7BIT: rv = &a_me_ctes[a_ME_CTES_7B_OFF]; break;
   case CONV_8BIT: rv = &a_me_ctes[a_ME_CTES_8B_OFF]; break;
   case CONV_TOQP: rv = &a_me_ctes[a_ME_CTES_QP_OFF]; break;
   case CONV_TOB64: rv = &a_me_ctes[a_ME_CTES_B64_OFF]; break;
   case CONV_NONE: rv = &a_me_ctes[a_ME_CTES_BIN_OFF]; break;
   default: rv = n_empty; break;
   }
   NYD2_OU;
   return rv;
}

FL size_t
mime_enc_mustquote(char const *ln, size_t lnlen, enum mime_enc_flags flags){
   size_t rv;
   bool_t sol;
   NYD2_IN;

   for(rv = 0, sol = TRU1; lnlen > 0; sol = FAL0, ++ln, --lnlen)
      switch(a_me_mustquote(ln, ln + lnlen, sol, flags)){
      case a_ME_US:
      case a_ME_EQ:
      case a_ME_HT:
         assert(flags & MIMEEF_ISENCWORD);
         /* FALLTHRU */
      case 0:
         continue;
      default:
         ++rv;
      }
   NYD2_OU;
   return rv;
}

FL size_t
qp_encode_calc_size(size_t len){
   size_t bytes, lines;
   NYD2_IN;

   /* The worst case sequence is 'CRLF' -> '=0D=0A=\n\0'.
    * However, we must be aware that (a) the output may span multiple lines
    * and (b) the input does not end with a newline itself (nonetheless):
    *    LC_ALL=C awk 'BEGIN{
    *       for (i = 1; i < 100000; ++i) printf "\xC3\xBC"
    *    }' |
    *    s-nail -:/ -dSsendcharsets=utf8 -s testsub no@where */

   /* Several n_ERR_OVERFLOW */
   if(len >= UIZ_MAX / 3){
      len = UIZ_MAX;
      goto jleave;
   }
   bytes = len * 3;
   lines = bytes / QP_LINESIZE;
   len += lines;

   if(len >= UIZ_MAX / 3){
      len = UIZ_MAX;
      goto jleave;
   }
   /* Trailing hard NL may be missing, so there may be two lines.
    * Thus add soft + hard NL per line and a trailing NUL */
   bytes = len * 3;
   lines = (bytes / QP_LINESIZE) + 1;
   lines <<= 1;
   ++bytes;
   /*if(UIZ_MAX - bytes >= lines){
      len = UIZ_MAX;
      goto jleave;
   }*/
   bytes += lines;
   len = bytes;
jleave:
   NYD2_OU;
   return len;
}

#ifdef notyet
FL struct str *
qp_encode_cp(struct str *out, char const *cp, enum qpflags flags){
   struct str in;
   NYD_IN;

   in.s = n_UNCONST(cp);
   in.l = strlen(cp);
   out = qp_encode(out, &in, flags);
   NYD_OU;
   return out;
}

FL struct str *
qp_encode_buf(struct str *out, void const *vp, size_t vp_len,
      enum qpflags flags){
   struct str in;
   NYD_IN;

   in.s = n_UNCONST(vp);
   in.l = vp_len;
   out = qp_encode(out, &in, flags);
   NYD_OU;
   return out;
}
#endif /* notyet */

FL struct str *
qp_encode(struct str *out, struct str const *in, enum qpflags flags){
   size_t lnlen;
   char *qp;
   char const *is, *ie;
   bool_t sol, seenx;
   NYD_IN;

   sol = (flags & QP_ISHEAD ? FAL0 : TRU1);

   if(!(flags & QP_BUF)){
      if((lnlen = qp_encode_calc_size(in->l)) == UIZ_MAX){
         out = NULL;
         goto jerr;
      }
      out->s = (flags & QP_SALLOC) ? n_autorec_alloc(lnlen)
            : n_realloc(out->s, lnlen);
   }
   qp = out->s;
   is = in->s;
   ie = is + in->l;

   if(flags & QP_ISHEAD){
      enum mime_enc_flags ef;

      ef = MIMEEF_ISHEAD | (flags & QP_ISENCWORD ? MIMEEF_ISENCWORD : 0);

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

      mq = a_me_mustquote(is, ie, sol, MIMEEF_NONE);
      c = *is++;

      if(mq == a_ME_N && (c != '\n' || !seenx)){
         *qp++ = c;
         if(++lnlen < QP_LINESIZE - 1)
            continue;
         /* Don't write a soft line break when we're in the last possible
          * column and either an LF has been written or only an LF follows, as
          * that'll end the line anyway */
         /* XXX but - ensure is+1>=ie, then??
          * xxx and/or - what about resetting lnlen; that contra
          * xxx dicts input==1 input line assertion, though */
         if(c == '\n' || is == ie || is[0] == '\n' || is[1] == '\n')
            continue;
jsoftnl:
         qp[0] = '=';
         qp[1] = '\n';
         qp += 2;
         lnlen = 0;
         continue;
      }

      if(lnlen > QP_LINESIZE - 3 - 1){
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
   out->l = PTR2SIZE(qp - out->s);
   out->s[out->l] = '\0';
jerr:
   NYD_OU;
   return out;
}

FL bool_t
qp_decode_header(struct str *out, struct str const *in){
   struct n_string s;
   char const *is, *ie;
   NYD_IN;

   /* n_ERR_OVERFLOW */
   if(UIZ_MAX -1 - out->l <= in->l ||
         SI32_MAX <= out->l + in->l){ /* XXX wrong, we may replace */
      out->l = 0;
      out = NULL;
      goto jleave;
   }

   n_string_creat(&s);
   n_string_reserve(n_string_take_ownership(&s, out->s,
         (out->l == 0 ? 0 : out->l +1), out->l),
      in->l + (in->l >> 2));

   for(is = in->s, ie = &is[in->l - 1]; is <= ie;){
      si32_t c;

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
 *              n_string_push_buf(&s, n_unirepl, sizeof(n_unirepl) -1);
 * TODO       else{
 * TODO          c = '?';
 * TODO          goto jpushc;
 * TODO       }*/
         }
      }else{
jpushc:
         if(c == '_' /* a_ME_US */)
            c = ' ';
         n_string_push_c(&s, (char)c);
      }
   }

   out->s = n_string_cp(&s);
   out->l = s.s_len;
   n_string_gut(n_string_drop_ownership(&s));
jleave:
   NYD_OU;
   return (out != NULL);
}

FL bool_t
qp_decode_part(struct str *out, struct str const *in, struct str *outrest,
      struct str *inrest_or_null){
   struct n_string s, *sp;
   char const *is, *ie;
   NYD_IN;

   if(outrest->l != 0){
      is = out->s;
      *out = *outrest;
      outrest->s = n_UNCONST(is);
      outrest->l = 0;
   }

   /* n_ERR_OVERFLOW */
   if(UIZ_MAX -1 - out->l <= in->l ||
         SI32_MAX <= out->l + in->l) /* XXX wrong, we may replace */
      goto jerr;

   sp = n_string_creat(&s);
   sp = n_string_take_ownership(sp, out->s,
         (out->l == 0 ? 0 : out->l +1), out->l);
   sp = n_string_reserve(sp, in->l + (in->l >> 2));

   for(is = in->s, ie = &is[in->l - 1]; is <= ie;){
      si32_t c;

      if((c = *is++) != '='){
jpushc:
         n_string_push_c(sp, (char)c);
         continue;
      }

      /* RFC 2045, 6.7:
       *   Therefore, when decoding a Quoted-Printable body, any
       *   trailing white space on a line must be deleted, as it will
       *   necessarily have been added by intermediate transport
       *   agents */
      for(; is <= ie && blankchar(*is); ++is)
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
 *           n_string_push_buf(&s, n_unirepl, sizeof(n_unirepl) -1);
 * TODO    else{
 * TODO       c = '?';
 * TODO       goto jpushc;
 * TODO    }*/
      }

      /* CRLF line endings are encoded as QP, followed by a soft line break, so
       * check for this special case, and simply forget we have seen one, so as
       * not to end up with the entire DOS file in a contiguous buffer */
jsoftnl:
      if(sp->s_len > 0 && sp->s_dat[sp->s_len - 1] == '\n'){
#if 0       /* TODO qp_decode_part() we do not normalize CRLF
          * TODO to LF because for that we would need
          * TODO to know if we are about to write to
          * TODO the display or do save the file!
          * TODO 'hope the MIME/send layer rewrite will
          * TODO offer the possibility to DTRT */
         if(sp->s_len > 1 && sp->s_dat[sp->s_len - 2] == '\r')
            n_string_push_c(n_string_trunc(sp, sp->s_len - 2), '\n');
#endif
         break;
      }

      /* C99 */{
         char *cp;
         size_t l;

         if((l = PTR2SIZE(ie - is)) > 0){
            if(inrest_or_null == NULL)
               goto jerr;
            n_str_assign_buf(inrest_or_null, is, l);
         }
         cp = outrest->s;
         outrest->s = n_string_cp(sp);
         outrest->l = s.s_len;
         n_string_drop_ownership(sp);
         if(cp != NULL)
            n_free(cp);
      }
      break;
   }

   out->s = n_string_cp(sp);
   out->l = sp->s_len;
   n_string_gut(n_string_drop_ownership(sp));
jleave:
   NYD_OU;
   return (out != NULL);
jerr:
   out->l = 0;
   out = NULL;
   goto jleave;
}

FL size_t
b64_encode_calc_size(size_t len){
   NYD2_IN;
   if(len >= UIZ_MAX / 4)
      len = UIZ_MAX;
   else{
      len = (len * 4) / 3;
      len += (((len / B64_ENCODE_INPUT_PER_LINE) + 1) * 3);
      len += 2 + 1; /* CRLF, \0 */
   }
   NYD2_OU;
   return len;
}

FL struct str *
b64_encode(struct str *out, struct str const *in, enum b64flags flags){
   ui8_t const *p;
   size_t i, lnlen;
   char *b64;
   NYD_IN;

   assert(!(flags & B64_NOPAD) ||
      !(flags & (B64_CRLF | B64_LF | B64_MULTILINE)));

   p = (ui8_t const*)in->s;

   if(!(flags & B64_BUF)){
      if((i = b64_encode_calc_size(in->l)) == UIZ_MAX){
         out = NULL;
         goto jleave;
      }
      out->s = (flags & B64_SALLOC) ? n_autorec_alloc(i)
            : n_realloc(out->s, i);
   }
   b64 = out->s;

   if(!(flags & (B64_CRLF | B64_LF)))
      flags &= ~B64_MULTILINE;

   for(lnlen = 0, i = in->l; (ssize_t)i > 0; p += 3, i -= 3){
      ui32_t a, b, c;

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
      if(!(flags & B64_MULTILINE))
         continue;
      lnlen += 4;
      if(lnlen < B64_LINESIZE)
         continue;

      lnlen = 0;
      if(flags & B64_CRLF)
         *b64++ = '\r';
      if(flags & (B64_CRLF | B64_LF))
         *b64++ = '\n';
   }

   if((flags & (B64_CRLF | B64_LF)) &&
         (!(flags & B64_MULTILINE) || lnlen != 0)){
      if(flags & B64_CRLF)
         *b64++ = '\r';
      if(flags & (B64_CRLF | B64_LF))
         *b64++ = '\n';
   }else if(flags & B64_NOPAD)
      while(b64 != out->s && b64[-1] == '=')
         --b64;

   out->l = PTR2SIZE(b64 - out->s);
   out->s[out->l] = '\0';

   /* Base64 includes + and /, replace them with _ and -.
    * This is base64url according to RFC 4648, then.  Since we only support
    * that for encoding and it is only used for boundary strings, this is
    * yet a primitive implementation; xxx use tables; support decoding */
   if(flags & B64_RFC4648URL){
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

FL struct str *
b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
      enum b64flags flags){
   struct str in;
   NYD_IN;

   in.s = n_UNCONST(vp);
   in.l = vp_len;
   out = b64_encode(out, &in, flags);
   NYD_OU;
   return out;
}

#ifdef notyet
FL struct str *
b64_encode_cp(struct str *out, char const *cp, enum b64flags flags){
   struct str in;
   NYD_IN;

   in.s = n_UNCONST(cp);
   in.l = strlen(cp);
   out = b64_encode(out, &in, flags);
   NYD_OU;
   return out;
}
#endif /* notyet */

FL bool_t
b64_decode(struct str *out, struct str const *in){
   struct str work;
   size_t len;
   NYD_IN;

   out->l = 0;

   if((len = a_me_b64_decode_prepare(&work, in)) == UIZ_MAX)
      goto jerr;

   /* Ignore an empty input, as may happen for an empty final line */
   if(work.l == 0)
      out->s = n_realloc(out->s, 1);
   else if(work.l >= 4 && !(work.l & 3)){
      out->s = n_realloc(out->s, len +1);
      if((ssize_t)(len = a_me_b64_decode(out, &work)) < 0)
         goto jerr;
   }else
      goto jerr;
   out->s[out->l] = '\0';
jleave:
   NYD_OU;
   return (out != NULL);
jerr:
   out = NULL;
   goto jleave;
}

FL bool_t
b64_decode_header(struct str *out, struct str const *in){
   struct str outr, inr;
   NYD_IN;

   if(!b64_decode(out, in)){
      memset(&outr, 0, sizeof outr);
      memset(&inr, 0, sizeof inr);

      if(!b64_decode_part(out, in, &outr, &inr) || outr.l > 0 || inr.l > 0)
         out = NULL;

      if(inr.s != NULL)
         n_free(inr.s);
      if(outr.s != NULL)
         n_free(outr.s);
   }
   NYD_OU;
   return (out != NULL);
}

FL bool_t
b64_decode_part(struct str *out, struct str const *in, struct str *outrest,
      struct str *inrest_or_null){
   struct str work, save;
   ui32_t a, b, c, b64l;
   char ca, cb, cc, cx;
   struct n_string s, workbuf;
   size_t len;
   NYD_IN;

   n_string_creat(&s);
   if((len = out->l) > 0 && out->s[len] == '\0')
      (void)n_string_take_ownership(&s, out->s, len +1, len);
   else{
      if(len > 0)
         n_string_push_buf(&s, out->s, len);
      if(out->s != NULL)
         n_free(out->s);
   }
   out->s = NULL, out->l = 0;
   n_string_creat(&workbuf);

   if((len = a_me_b64_decode_prepare(&work, in)) == UIZ_MAX)
      goto jerr;

   if(outrest->l > 0){
      n_string_push_buf(&s, outrest->s, outrest->l);
      outrest->l = 0;
   }

   /* n_ERR_OVERFLOW */
   if(UIZ_MAX - len <= s.s_len ||
         SI32_MAX <= len + s.s_len) /* XXX wrong, we may replace */
      goto jerr;

   if(work.l == 0)
      goto jok;

   /* This text decoder is extremely expensive, especially given that in all
    * but _invalid_ cases it is not even needed!  So try once to do the normal
    * decoding, if that fails, go the hard way */
   save = work;
   out->s = n_string_resize(&s, len + (out->l = b64l = s.s_len))->s_dat;

   if(work.l >= 4 && a_me_b64_decode(out, &work) >= 0){
      n_string_trunc(&s, out->l);
      if(work.l == 0)
         goto jok;
   }

   n_string_trunc(&s, b64l);
   work = save;
   out->s = NULL, out->l = 0;

   /* TODO b64_decode_part() does not yet STOP if it sees padding, whereas
    * TODO OpenSSL and mutt simply bail on such stuff */
   n_UNINIT(ca, 0);
   n_UNINIT(cb, 0);
   n_UNINIT(cc, 0);
   for(b64l = 0;;){
      ui32_t x;

      x = a_ME_B64_DECUI8((ui8_t)(cx = *work.s));
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
#if 0
            if(n_psonce & n_PSO_UNICODE)
               n_string_push_buf(&s, n_unirepl, sizeof(n_unirepl) -1);
            else
               n_string_push_c(&s, '?');
#endif
            ;
         }else if(c == a_ME_B64_EQU && x != a_ME_B64_EQU){
            /* This is not only invalid but bogus.  Skip it over! */
            /* TODO This would be wrong since iconv(3) may be applied first! */
#if 0
            n_string_push_buf(&s, n_UNIREPL n_UNIREPL n_UNIREPL n_UNIREPL,
               (sizeof(n_UNIREPL) -1) * 4);
#endif
            b64l = 0;
         }else{
            ui8_t pb;

            pb = ((a << 2) | ((b & 0x30) >> 4));
            if(pb != (ui8_t)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
               n_string_push_c(&s, (char)pb);
            pb = (((b & 0x0F) << 4) | ((c & 0x3C) >> 2));
            if(pb != (ui8_t)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
               n_string_push_c(&s, (char)pb);
            if(x != a_ME_B64_EQU){
               pb = (((c & 0x03) << 6) | x);
               if(pb != (ui8_t)'\r' || !(n_pstate & n_PS_BASE64_STRIP_CR))
                  n_string_push_c(&s, (char)pb);
            }
            ++b64l;
         }
         break;
      }

      ++work.s;
      if(--work.l == 0){
         if(b64l > 0 && b64l != 4){
            if(inrest_or_null == NULL)
               goto jerr;
            inrest_or_null->s = n_realloc(inrest_or_null->s, b64l +1);
            inrest_or_null->s[0] = ca;
            if(b64l > 1)
               inrest_or_null->s[1] = cb;
            if(b64l > 2)
               inrest_or_null->s[2] = cc;
            inrest_or_null->s[inrest_or_null->l = b64l] = '\0';
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
   return (out != NULL);
jerr:
   out = NULL;
   goto jleave;
}

/* s-it-mode */
