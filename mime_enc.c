/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Content-Transfer-Encodings as defined in RFC 2045 (and RFC 2047):
 *@ - Quoted-Printable, section 6.7
 *@ - Base64, section 6.8
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/* QP quoting idea, _b64_decode(), b64_encode() taken from NetBSDs mailx(1): */
/* $NetBSD: mime_codecs.c,v 1.9 2009/04/10 13:08:25 christos Exp $ */
/*
 * Copyright (c) 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Anon Ymous.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#undef n_FILE
#define n_FILE mime_enc

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

enum _qact {
    N =   0,   /* Do not quote */
    Q =   1,   /* Must quote */
   SP =   2,   /* sp */
   XF =   3,   /* Special character 'F' - maybe quoted */
   XD =   4,   /* Special character '.' - maybe quoted */
   UU =   5,   /* In header, _ must be quoted in encoded word */
   US = '_',   /* In header, ' ' must be quoted as _ in encoded word */
   QM = '?',   /* In header, special character ? not always quoted */
   EQ = '=',   /* In header, '=' must be quoted in encoded word */
   HT ='\t',   /* In body HT=SP, in head HT=HT, but quote in encoded word */
   NL =   N,   /* Don't quote '\n' (NL) */
   CR =   Q    /* Always quote a '\r' (CR) */
};

/* Lookup tables to decide wether a character must be encoded or not.
 * Email header differences according to RFC 2047, section 4.2:
 * - also quote SP (as the underscore _), TAB, ?, _, CR, LF
 * - don't care about the special ^F[rom] and ^.$ */
static ui8_t const         _qtab_body[] = {
    Q, Q, Q, Q,  Q, Q, Q, Q,  Q,SP,NL, Q,  Q,CR, Q, Q,
    Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,
   SP, N, N, N,  N, N, N, N,  N, N, N, N,  N, N,XD, N,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, Q, N, N,

    N, N, N, N,  N, N,XF, N,  N, N, N, N,  N, N, N, N,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, Q,
},
                           _qtab_head[] = {
    Q, Q, Q, Q,  Q, Q, Q, Q,  Q,HT, Q, Q,  Q, Q, Q, Q,
    Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,
   US, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N,EQ, N,QM,

    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N,UU,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
    N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, Q,
};

/* For decoding be robust and allow lowercase letters, too */
static char const          _qp_itoa16[] = "0123456789ABCDEF";
static ui8_t const         _qp_atoi16[] = {
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x30-0x37 */
   0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x38-0x3F */
   0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, /* 0x40-0x47 */
   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x48-0x4f */
   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x50-0x57 */
   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x58-0x5f */
   0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF  /* 0x60-0x67 */
};

/* The decoding table is only accessed via _B64_DECUI8() */
static char const          _b64_enctbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static signed char const   _b64__dectbl[] = {
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
   -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
   52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-2,-1,-1,
   -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
   15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
   -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
   41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};
#define _B64_EQU           (ui32_t)-2
#define _B64_BAD           (ui32_t)-1
#define _B64_DECUI8(C)     \
   ((C) >= sizeof(_b64__dectbl) ? _B64_BAD : (ui32_t)_b64__dectbl[(ui8_t)(C)])

/* ASCII case-insensitive check wether Content-Transfer-Encoding: header body
 * hbody defined this encoding type */
static bool_t        _is_ct_enc(char const *hbody, char const *encoding);

/* Check wether *s must be quoted according to flags, else body rules;
 * sol indicates wether we are at the first character of a line/field */
SINLINE enum _qact   _mustquote(char const *s, char const *e, bool_t sol,
                        enum mime_enc_flags flags);

/* Convert c to/from a hexadecimal character string */
SINLINE char *       _qp_ctohex(char *store, char c);
SINLINE si32_t       _qp_cfromhex(char const *hex);

/* Trim WS and make work point to the decodable range of in*
 * Return the amount of bytes a b64_decode operation on that buffer requires */
static size_t        _b64_decode_prepare(struct str *work,
                        struct str const *in);

/* Perform b64_decode on sufficiently spaced & multiple-of-4 base in(put).
 * Return number of useful bytes in out or -1 on error */
static ssize_t       _b64_decode(struct str *out, struct str *in);

static bool_t
_is_ct_enc(char const *hbody, char const *encoding)
{
   bool_t quoted, rv;
   int c;
   NYD2_ENTER;

   if (*hbody == '"')
      quoted = TRU1, ++hbody;
   else
      quoted = FAL0;
   rv = FAL0;

   while (*hbody != '\0' && *encoding != '\0')
      if ((c = *hbody++, lowerconv(c) != *encoding++))
         goto jleave;
   rv = TRU1;

   if (quoted && *hbody == '"')
      goto jleave;
   if (*hbody == '\0' || whitechar(*hbody))
      goto jleave;
   rv = FAL0;
jleave:
   NYD2_LEAVE;
   return rv;
}

SINLINE enum _qact
_mustquote(char const *s, char const *e, bool_t sol, enum mime_enc_flags flags)
{
   ui8_t const *qtab;
   enum _qact a, r;
   NYD2_ENTER;

   qtab = (flags & (MIMEEF_ISHEAD | MIMEEF_ISENCWORD))
         ? _qtab_head : _qtab_body;
   a = ((ui8_t)*s > 0x7F) ? Q : qtab[(ui8_t)*s];

   if ((r = a) == N || (r = a) == Q)
      goto jleave;
   r = Q;

   /* Special header fields */
   if (flags & (MIMEEF_ISHEAD | MIMEEF_ISENCWORD)) {
      /* Special massage for encoded words */
      if (flags & MIMEEF_ISENCWORD) {
         switch (a) {
         case HT:
         case US:
         case EQ:
            r = a;
            /* FALLTHRU */
         case UU:
            goto jleave;
         default:
            break;
         }
      }

      /* Treat '?' only special if part of '=?' .. '?=' (still too much quoting
       * since it's '=?CHARSET?CTE?stuff?=', and especially the trailing ?=
       * should be hard too match */
      if (a == QM && ((!sol && s[-1] == '=') || (s < e && s[1] == '=')))
         goto jleave;
      goto jnquote;
   }

   /* Body-only */

   if (a == SP) {
      /* WS only if trailing white space */
      if (PTRCMP(s + 1, ==, e) || s[1] == '\n')
         goto jleave;
      goto jnquote;
   }

   /* Rest are special begin-of-line cases */
   if (!sol)
      goto jnquote;

   /* ^From */
   if (a == XF) {
      if (PTRCMP(s + 4, <, e) && s[1] == 'r' && s[2] == 'o' && s[3] == 'm')
         goto jleave;
      goto jnquote;
   }
   /* ^.$ */
   if (a == XD && (PTRCMP(s + 1, ==, e) || s[1] == '\n'))
      goto jleave;
jnquote:
   r = N;
jleave:
   NYD2_LEAVE;
   return r;
}

SINLINE char *
_qp_ctohex(char *store, char c)
{
   NYD2_ENTER;
   store[2] = '\0';
   store[1] = _qp_itoa16[(ui8_t)c & 0x0F];
   c = ((ui8_t)c >> 4) & 0x0F;
   store[0] = _qp_itoa16[(ui8_t)c];
   NYD2_LEAVE;
   return store;
}

SINLINE si32_t
_qp_cfromhex(char const *hex)
{
   ui8_t i1, i2;
   si32_t rv;
   NYD2_ENTER;

   if ((i1 = (ui8_t)hex[0] - '0') >= NELEM(_qp_atoi16) ||
         (i2 = (ui8_t)hex[1] - '0') >= NELEM(_qp_atoi16))
      goto jerr;
   i1 = _qp_atoi16[i1];
   i2 = _qp_atoi16[i2];
   if ((i1 | i2) & 0xF0u)
      goto jerr;
   rv = i1;
   rv <<= 4;
   rv += i2;
jleave:
   NYD2_LEAVE;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

static size_t
_b64_decode_prepare(struct str *work, struct str const *in)
{
   char *cp;
   size_t cp_len;
   NYD2_ENTER;

   cp = in->s;
   cp_len = in->l;

   while (cp_len > 0 && spacechar(*cp))
      ++cp, --cp_len;
   work->s = cp;

   for (cp += cp_len; cp_len > 0; --cp_len) {
      char c = *--cp;
      if (!spacechar(c))
         break;
   }
   work->l = cp_len;

   if (cp_len > 16)
      cp_len = ((cp_len * 3) >> 2) + (cp_len >> 3);
   NYD2_LEAVE;
   return cp_len;
}

static ssize_t
_b64_decode(struct str *out, struct str *in)
{
   ssize_t rv = -1;
   ui8_t *p;
   ui8_t const *q, *end;
   NYD2_ENTER;

   p = (ui8_t*)out->s + out->l;
   q = (ui8_t const*)in->s;

   for (end = q + in->l; PTRCMP(q + 4, <=, end);) {
      ui32_t a = _B64_DECUI8(q[0]), b = _B64_DECUI8(q[1]),
         c = _B64_DECUI8(q[2]), d = _B64_DECUI8(q[3]);
      q += 4;

      if (a >= _B64_EQU || b >= _B64_EQU || c == _B64_BAD || d == _B64_BAD)
         goto jleave;

      *p++ = ((a << 2) | ((b & 0x30) >> 4));
      if (c == _B64_EQU)  { /* got '=' */
         if (d != _B64_EQU)
            goto jleave;
         break;
      }
      *p++ = (((b & 0x0F) << 4) | ((c & 0x3C) >> 2));
      if (d == _B64_EQU) /* got '=' */
         break;
      *p++ = (((c & 0x03) << 6) | d);
   }
   rv ^= rv;

jleave: {
      size_t i = PTR2SIZE((char*)p - out->s);
      out->l = i;
      if (rv == 0)
         rv = (ssize_t)i;
   }
   in->l -= PTR2SIZE((char*)UNCONST(q) - in->s);
   in->s = UNCONST(q);
   NYD2_LEAVE;
   return rv;
}

FL char *
mime_char_to_hexseq(char store[3], char c)
{
   char *rv;
   NYD2_ENTER;

   rv = _qp_ctohex(store, c);
   NYD2_LEAVE;
   return rv;
}

FL si32_t
mime_hexseq_to_char(char const *hex)
{
   si32_t rv;
   NYD2_ENTER;

   rv = _qp_cfromhex(hex);
   NYD2_LEAVE;
   return rv;
}

FL enum mime_enc
mime_enc_target(void)
{
   char const *cp;
   enum mime_enc rv;
   NYD2_ENTER;

   if ((cp = ok_vlook(encoding)) == NULL)
      rv = MIME_DEFAULT_ENCODING;
   else if (!asccasecmp(cp, "quoted-printable"))
      rv = MIMEE_QP;
   else if (!asccasecmp(cp, "8bit"))
      rv = MIMEE_8B;
   else if (!asccasecmp(cp, "base64"))
      rv = MIMEE_B64;
   else {
      n_err(_("Warning: invalid *encoding*, using Base64: \"%s\"\n"), cp);
      rv = MIMEE_B64;
   }
   NYD2_LEAVE;
   return rv;
}

FL enum mime_enc
mime_enc_from_ctehead(char const *hbody)
{
   enum mime_enc rv;
   NYD2_ENTER;

   if (hbody == NULL || _is_ct_enc(hbody, "7bit"))
      rv = MIMEE_7B;
   else if (_is_ct_enc(hbody, "8bit"))
      rv = MIMEE_8B;
   else if (_is_ct_enc(hbody, "base64"))
      rv = MIMEE_B64;
   else if (_is_ct_enc(hbody, "binary"))
      rv = MIMEE_BIN;
   else if (_is_ct_enc(hbody, "quoted-printable"))
      rv = MIMEE_QP;
   else
      rv = MIMEE_NONE;
   NYD2_LEAVE;
   return rv;
}

FL char const *
mime_enc_from_conversion(enum conversion const convert) /* TODO booom */
{
   char const *rv;
   NYD_ENTER;

   switch (convert) {
   case CONV_7BIT:   rv = "7bit"; break;
   case CONV_8BIT:   rv = "8bit"; break;
   case CONV_TOQP:   rv = "quoted-printable"; break;
   case CONV_TOB64:  rv = "base64"; break;
   default:          rv = ""; break;
   }
   NYD_LEAVE;
   return rv;
}

FL size_t
mime_enc_mustquote(char const *ln, size_t lnlen, enum mime_enc_flags flags)
{
   size_t rv;
   bool_t sol;
   NYD_ENTER;

   for (rv = 0, sol = TRU1; lnlen > 0; sol = FAL0, ++ln, --lnlen)
      switch (_mustquote(ln, ln + lnlen, sol, flags)) {
      case US:
      case EQ:
      case HT:
         assert(flags & MIMEEF_ISENCWORD);
         /* FALLTHRU */
      case N:
         continue;
      default:
         ++rv;
      }
   NYD_LEAVE;
   return rv;
}

FL size_t
qp_encode_calc_size(size_t len)
{
   size_t bytes, lines;
   NYD_ENTER;

   /* The worst case sequence is 'CRLF' -> '=0D=0A=\n\0'.
    * However, we must be aware that (a) the output may span multiple lines
    * and (b) the input does not end with a newline itself (nonetheless):
    *    LC_ALL=C awk 'BEGIN{
    *       for (i = 1; i < 100000; ++i) printf "\xC3\xBC"
    *    }' |
    *    MAILRC=/dev/null LC_ALL=en_US.UTF-8 s-nail -nvvd \
    *       -Ssendcharsets=utf8 -s testsub ./LETTER */
   bytes = len * 3;
   lines = bytes / QP_LINESIZE;
   len += lines;

   bytes = len * 3;
   /* Trailing hard NL may be missing, so there may be two lines.
    * Thus add soft + hard NL per line and a trailing NUL */
   lines = (bytes / QP_LINESIZE) + 1;
   lines <<= 1;
   bytes += lines;
   len = ++bytes;

   NYD_LEAVE;
   return len;
}

#ifdef notyet
FL struct str *
qp_encode_cp(struct str *out, char const *cp, enum qpflags flags)
{
   struct str in;
   NYD_ENTER;

   in.s = UNCONST(cp);
   in.l = strlen(cp);
   out = qp_encode(out, &in, flags);
   NYD_LEAVE;
   return out;
}

FL struct str *
qp_encode_buf(struct str *out, void const *vp, size_t vp_len,
   enum qpflags flags)
{
   struct str in;
   NYD_ENTER;

   in.s = UNCONST(vp);
   in.l = vp_len;
   out = qp_encode(out, &in, flags);
   NYD_LEAVE;
   return out;
}
#endif /* notyet */

FL struct str *
qp_encode(struct str *out, struct str const *in, enum qpflags flags)
{
   bool_t sol = (flags & QP_ISHEAD ? FAL0 : TRU1), seenx;
   ssize_t lnlen;
   char *qp;
   char const *is, *ie;
   NYD_ENTER;

   if (!(flags & QP_BUF)) {
      lnlen = qp_encode_calc_size(in->l);
      out->s = (flags & QP_SALLOC) ? salloc(lnlen) : srealloc(out->s, lnlen);
   }
   qp = out->s;
   is = in->s;
   ie = is + in->l;

   /* QP_ISHEAD? */
   if (!sol) {
      enum mime_enc_flags ef = MIMEEF_ISHEAD |
            (flags & QP_ISENCWORD ? MIMEEF_ISENCWORD : 0);

      for (seenx = FAL0, sol = TRU1; is < ie; sol = FAL0, ++qp) {
         enum _qact mq = _mustquote(is, ie, sol, ef);
         char c = *is++;

         if (mq == N) {
            /* We convert into a single *encoded-word*, that'll end up in
             * =?C?Q??=; quote '?' from when we're inside there on */
            if (seenx && c == '?')
               goto jheadq;
            *qp = c;
         } else if (mq == US)
            *qp = US;
         else {
            seenx = TRU1;
jheadq:
            *qp++ = '=';
            qp = _qp_ctohex(qp, c) + 1;
         }
      }
      goto jleave;
   }

   /* The body needs to take care for soft line breaks etc. */
   for (lnlen = 0, seenx = FAL0; is < ie; sol = FAL0) {
      enum _qact mq = _mustquote(is, ie, sol, MIMEEF_NONE);
      char c = *is++;

      if (mq == N && (c != '\n' || !seenx)) {
         *qp++ = c;
         if (++lnlen < QP_LINESIZE - 1)
            continue;
         /* Don't write a soft line break when we're in the last possible
          * column and either an LF has been written or only an LF follows, as
          * that'll end the line anyway */
         /* XXX but - ensure is+1>=ie, then??
          * xxx and/or - what about resetting lnlen; that contra
          * xxx dicts input==1 input line assertion, though */
         if (c == '\n' || is == ie || *is == '\n')
            continue;
jsoftnl:
         qp[0] = '=';
         qp[1] = '\n';
         qp += 2;
         lnlen = 0;
         continue;
      }

      if (lnlen > QP_LINESIZE - 3 - 1) {
         qp[0] = '=';
         qp[1] = '\n';
         qp += 2;
         lnlen = 0;
      }
      *qp++ = '=';
      qp = _qp_ctohex(qp, c);
      qp += 2;
      lnlen += 3;
      if (c != '\n' || !seenx)
         seenx = (c == '\r');
      else {
         seenx = FAL0;
         goto jsoftnl;
      }
   }

   /* Enforce soft line break if we haven't seen LF */
   if (in->l > 0 && *--is != '\n') {
      qp[0] = '=';
      qp[1] = '\n';
      qp += 2;
   }
jleave:
   out->l = PTR2SIZE(qp - out->s);
   out->s[out->l] = '\0';
   NYD_LEAVE;
   return out;
}

FL int
qp_decode(struct str *out, struct str const *in, struct str *rest)
{
   int rv = STOP;
   char *os, *oc;
   char const *is, *ie;
   NYD_ENTER;

   if (rest != NULL && rest->l != 0) {
      os = out->s;
      *out = *rest;
      rest->s = os;
      rest->l = 0;
   }

   oc = os =
   out->s = srealloc(out->s, out->l + in->l + 3);
   oc += out->l;
   is = in->s;
   ie = is + in->l;

   /* Decoding encoded-word (RFC 2049) in a header field? */
   if (rest == NULL) {
      while (is < ie) {
         si32_t c = *is++;
         if (c == '=') {
            if (PTRCMP(is + 1, >=, ie)) {
               ++is;
               goto jehead;
            }
            c = _qp_cfromhex(is);
            is += 2;
            if (c >= 0)
               *oc++ = (char)c;
            else {
               /* Invalid according to RFC 2045, section 6.7. Almost follow */
jehead:
               /* TODO 0xFFFD
               *oc[0] = '['; oc[1] = '?'; oc[2] = ']';
               *oc += 3; 0xFFFD TODO
               */ *oc++ = '?';
            }
         } else
            *oc++ = (c == '_' /* US */) ? ' ' : (char)c;
      }
      goto jleave; /* XXX QP decode, header: errors not reported */
   }

   /* Decoding a complete message/mimepart body line */
   while (is < ie) {
      si32_t c = *is++;
      if (c != '=') {
         *oc++ = (char)c;
         continue;
      }

      /* RFC 2045, 6.7:
       *   Therefore, when decoding a Quoted-Printable body, any
       *   trailing white space on a line must be deleted, as it will
       *   necessarily have been added by intermediate transport
       *   agents */
      for (; is < ie && blankchar(*is); ++is)
         ;
      if (PTRCMP(is + 1, >=, ie)) {
         /* Soft line break? */
         if (*is == '\n')
            goto jsoftnl;
         ++is;
         goto jebody;
      }

      /* Not a soft line break? */
      if (*is != '\n') {
         c = _qp_cfromhex(is);
         is += 2;
         if (c >= 0)
            *oc++ = (char)c;
         else {
            /* Invalid according to RFC 2045, section 6.7.
             * Almost follow it and include the = and the follow char */
jebody:
            /* TODO 0xFFFD
            *oc[0] = '['; oc[1] = '?'; oc[2] = ']';
            *oc += 3; 0xFFFD TODO
            */ *oc++ = '?';
         }
         continue;
      }

      /* CRLF line endings are encoded as QP, followed by a soft line break, so
       * check for this special case, and simply forget we have seen one, so as
       * not to end up with the entire DOS file in a contiguous buffer */
jsoftnl:
      if (oc > os && oc[-1] == '\n') {
#if 0       /* TODO qp_decode() we do not normalize CRLF
          * TODO to LF because for that we would need
          * TODO to know if we are about to write to
          * TODO the display or do save the file!
          * TODO 'hope the MIME/send layer rewrite will
          * TODO offer the possibility to DTRT */
         if (oc - 1 > os && oc[-2] == '\r') {
            --oc;
            oc[-1] = '\n';
         }
#endif
         break;
      }
      out->l = PTR2SIZE(oc - os);
      rest->s = srealloc(rest->s, rest->l + out->l);
      memcpy(rest->s + rest->l, out->s, out->l);
      rest->l += out->l;
      oc = os;
      break;
   }
   /* XXX RFC: QP decode should check no trailing WS on line */
jleave:
   out->l = PTR2SIZE(oc - os);
   rv = OKAY;
   NYD_LEAVE;
   return rv;
}

FL size_t
b64_encode_calc_size(size_t len)
{
   NYD_ENTER;
   len = (len * 4) / 3;
   len += (((len / B64_ENCODE_INPUT_PER_LINE) + 1) * 3);
   len += 2 + 1; /* CRLF, \0 */
   NYD_LEAVE;
   return len;
}

FL struct str *
b64_encode(struct str *out, struct str const *in, enum b64flags flags)
{
   ui8_t const *p;
   ssize_t i, lnlen;
   char *b64;
   NYD_ENTER;

   p = (ui8_t const*)in->s;

   if (!(flags & B64_BUF)) {
      i = b64_encode_calc_size(in->l);
      out->s = (flags & B64_SALLOC) ? salloc(i) : srealloc(out->s, i);
   }
   b64 = out->s;

   if (!(flags & (B64_CRLF | B64_LF)))
      flags &= ~B64_MULTILINE;

   for (lnlen = 0, i = (ssize_t)in->l; i > 0; p += 3, i -= 3) {
      ui32_t a = p[0], b, c;

      b64[0] = _b64_enctbl[a >> 2];
      switch (i) {
      case 1:
         b64[1] = _b64_enctbl[((a & 0x3) << 4)];
         b64[2] =
         b64[3] = '=';
         break;
      case 2:
         b = p[1];
         b64[1] = _b64_enctbl[((a & 0x03) << 4) | ((b & 0xF0u) >> 4)];
         b64[2] = _b64_enctbl[((b & 0x0F) << 2)];
         b64[3] = '=';
         break;
      default:
         b = p[1];
         c = p[2];
         b64[1] = _b64_enctbl[((a & 0x03) << 4) | ((b & 0xF0u) >> 4)];
         b64[2] = _b64_enctbl[((b & 0x0F) << 2) | ((c & 0xC0u) >> 6)];
         b64[3] = _b64_enctbl[c & 0x3F];
         break;
      }

      b64 += 4;
      if (!(flags & B64_MULTILINE))
         continue;
      lnlen += 4;
      if (lnlen < B64_LINESIZE)
         continue;

      lnlen = 0;
      if (flags & B64_CRLF)
         *b64++ = '\r';
      if (flags & (B64_CRLF | B64_LF))
         *b64++ = '\n';
   }

   if ((flags & (B64_CRLF | B64_LF)) &&
         (!(flags & B64_MULTILINE) || lnlen != 0)) {
      if (flags & B64_CRLF)
         *b64++ = '\r';
      if (flags & (B64_CRLF | B64_LF))
         *b64++ = '\n';
   }
   out->l = PTR2SIZE(b64 - out->s);
   out->s[out->l] = '\0';

   /* Base64 includes + and /, replace them with _ and -.
    * This is base64url according to RFC 4648, then.  Since we only support
    * that for encoding and it is only used for boundary strings, this is
    * yet a primitive implementation; xxx use tables; support decoding */
   if (flags & B64_RFC4648URL) {
      char c;

      for (b64 = out->s; (c = *b64) != '\0'; ++b64)
         if (c == '+')
            *b64 = '-';
         else if (c == '/')
            *b64 = '_';
   }
   NYD_LEAVE;
   return out;
}

FL struct str *
b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
   enum b64flags flags)
{
   struct str in;
   NYD_ENTER;

   in.s = UNCONST(vp);
   in.l = vp_len;
   out = b64_encode(out, &in, flags);
   NYD_LEAVE;
   return out;
}

#ifdef HAVE_SMTP
FL struct str *
b64_encode_cp(struct str *out, char const *cp, enum b64flags flags)
{
   struct str in;
   NYD_ENTER;

   in.s = UNCONST(cp);
   in.l = strlen(cp);
   out = b64_encode(out, &in, flags);
   NYD_LEAVE;
   return out;
}
#endif

FL int
b64_decode(struct str *out, struct str const *in, struct str *rest)
{
   struct str work;
   char *x;
   size_t len;
   int rv; /* XXX -> bool_t */
   NYD_ENTER;

   len = _b64_decode_prepare(&work, in);
   out->l = 0;

   /* TODO B64_T is different since we must not fail for errors; in v15.0 this
    * TODO will be filter based and B64_T will have a different one than B64,
    * TODO for now special treat this all-horror */
   if (rest != NULL) {
      /* With B64_T there may be leftover decoded data for iconv(3), even if
       * that means it's incomplete multibyte character we have to copy over */
      /* TODO strictly speaking this should not be handled in here,
       * TODO since its leftover decoded data from an iconv(3);
       * TODO In v15.0 this path will be filter based, each filter having its
       * TODO own buffer for such purpose; for now we are BUSTED since for
       * TODO Base64 rest is owned by iconv(3) */
      if (rest->l > 0) {
         x = out->s;
         *out = *rest;
         rest->s = x; /* Just for ownership reasons (all TODO in here..) */
         rest->l = 0;
         len += out->l;
      }

      out->s = srealloc(out->s, len +1);

      for (;;) {
         if (_b64_decode(out, &work) >= 0) {
            if (work.l == 0)
               break;
         }
         x = out->s + out->l;

         /* Partial/False last sequence.  TODO not solvable for non-EOF;
          * TODO yes, invalid, but seen in the wild and should be handled,
          * TODO but for that we had to have our v15.0 filter which doesn't
          * TODO work line based but content buffer based */
         if ((len = work.l) <= 4) {
            switch (len) {
            case 4:  /* FALLTHRU */
            case 3:  x[2] = '?'; /* FALLTHRU */
            case 2:  x[1] = '?'; /* FALLTHRU */
            default: x[0] = '?'; break;
            }
            out->l += len;
            break;
         }

         /* TODO Bad content: this problem is not solvable!  I've seen
          * TODO messages which broke lines in the middle of a Base64
          * TODO tuple, followed by an invalid character ("!"), the follow
          * TODO line starting with whitespace and the remaining sequence.
          * TODO OpenSSL bailed, mutt(1) got it right (silently..).
          * TODO Since "rest" is not usable by us, we cannot continue
          * TODO sequences.  We will be able to do so with the v15.0 filter
          * TODO approach, if we */
         /* Bad content: skip over a single sequence */
         for (;;) {
            *x++ = '?';
            ++out->l;
            if (--work.l == 0)
               break;
            else {
               ui8_t bc = (ui8_t)*++work.s;
               ui32_t state = _B64_DECUI8(bc);

               if (state != _B64_EQU && state != _B64_BAD)
                  break;
            }
         }
      }
      rv = OKAY;
      goto jleave;
   }

   /* Ignore an empty input, as may happen for an empty final line */
   if (work.l == 0) {
      out->s = srealloc(out->s, 1);
      rv = OKAY;
   } else if (work.l >= 4 && !(work.l & 3)) {
      out->s = srealloc(out->s, len +1);
      if ((ssize_t)(len = _b64_decode(out, &work)) < 0)
         goto jerr;
      rv = OKAY;
   } else
      goto jerr;

jleave:
   out->s[out->l] = '\0';
   NYD_LEAVE;
   return rv;

jerr: {
   char const *err = _("[Invalid Base64 encoding]\n");
   out->l = len = strlen(err);
   out->s = srealloc(out->s, len +1);
   memcpy(out->s, err, len);
   rv = STOP;
   goto jleave;
   }
}

/* s-it-mode */
