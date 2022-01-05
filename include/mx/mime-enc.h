/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Content-Transfer-Encodings as defined in RFC 2045 (and RFC 2047;
 *@ for _header() versions: including "encoded word" as of RFC 2049):
 *@ - Quoted-Printable, section 6.7
 *@ - Base64, section 6.8
 * TODO For now this is pretty mixed up regarding this external interface
 * TODO (and due to that the code is, too).
 * TODO In v15.0 CTE will be filter based, and explicit conversion will
 * TODO gain clear error codes
 *
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MIME_ENC_H
#define mx_MIME_ENC_H

#include <su/code.h>

#define mx_HEADER
#include <su/code-in.h>

struct str;

/* Max. compliant QP linesize */
#define mx_QP_LINESIZE (4 * 19)

/* Max. compliant Base64 linesize */
#define mx_B64_LINESIZE (4 * 19)
#define mx_B64_ENC_INPUT_PER_LINE ((mx_B64_LINESIZE / 4) * 3)

enum mx_mime_enc{
   mx_MIME_ENC_NONE, /* Not MIME */
   mx_MIME_ENC_BIN, /* Borked, but seen in wild: binary encoding */
   mx_MIME_ENC_8B, /* 8-bit */
   mx_MIME_ENC_7B, /* 7-bit */
   mx_MIME_ENC_QP, /* quoted-printable */
   mx_MIME_ENC_B64 /* base64 */
};

/* xxx QP came later, maybe rewrite all to use mime_enc_flags directly? */
enum mx__mime_enc_flags{
   mx_MIME_ENC_F_NONE,
   mx_MIME_ENC_F_AUTO_ALLOC = 1u<<0, /* Use AUTO_ALLOC() not normal heap */
   /* ..result .s,.l point to user buffer of *_LINESIZE+[+[+]] bytes instead */
   mx_MIME_ENC_F_BUF = 1u<<1,
   mx_MIME_ENC_F_CRLF = 1u<<2, /* (encode) Append "\r\n" to lines */
   mx_MIME_ENC_F_LF = 1u<<3, /* (encode) Append "\n" to lines */
   /* (encode) If one of _CRLF/_LF is set, honour *_LINESIZE+[+[+]] and
    * inject the desired line-ending whenever a linewrap is desired */
   mx_MIME_ENC_F_MULTILINE = 1u<<4,
   /* (encode) Quote with header rules, do not generate soft NL breaks?
    * For mustquote(), specifies whether special RFC 2047 header rules
    * should be used instead */
   mx_MIME_ENC_F_ISHEAD = 1u<<5,
   /* (encode) Ditto; for mustquote() this furtherly fine-tunes behaviour in
    * that characters which would not be reported as "must-quote" when
    * detecting whether quoting is necessary at all will be reported as
    * "must-quote" if they have to be encoded in an encoded word */
   mx_MIME_ENC_F_ISENCWORD = 1u<<6,
   mx__MIME_ENC_F_LAST = 6
};

enum mx_qp_flags{
   mx_QP_NONE = mx_MIME_ENC_F_NONE,
   mx_QP_AUTO_ALLOC = mx_MIME_ENC_F_AUTO_ALLOC,
   mx_QP_BUF = mx_MIME_ENC_F_BUF,
   mx_QP_ISHEAD = mx_MIME_ENC_F_ISHEAD,
   mx_QP_ISENCWORD = mx_MIME_ENC_F_ISENCWORD
};

enum mx_b64_flags{
   mx_B64_NONE = mx_MIME_ENC_F_NONE,
   mx_B64_AUTO_ALLOC = mx_MIME_ENC_F_AUTO_ALLOC,
   mx_B64_BUF = mx_MIME_ENC_F_BUF,
   mx_B64_CRLF = mx_MIME_ENC_F_CRLF,
   mx_B64_LF = mx_MIME_ENC_F_LF,
   mx_B64_MULTILINE = mx_MIME_ENC_F_MULTILINE,
   /* Not used, but for clarity only */
   mx_B64_ISHEAD = mx_MIME_ENC_F_ISHEAD,
   mx_B64_ISENCWORD = mx_MIME_ENC_F_ISENCWORD,
   /* Special version of Base64, "Base64URL", according to RFC 4648.
    * Only supported for encoding! */
   mx_B64_RFC4648URL = 1u<<(mx__MIME_ENC_F_LAST+1),
   /* Don't use any ("=") padding;
    * may NOT be used with any of _CRLF, _LF or _MULTILINE */
   mx_B64_NOPAD = 1u<<(mx__MIME_ENC_F_LAST+2)
};

/* Default MIME Content-Transfer-Encoding: as via *mime-encoding*.
 * Cannot be MIME_ENC_BIN nor MIME_ENC_7B (i.e., only B64, QP, 8B) */
EXPORT enum mx_mime_enc mx_mime_enc_target(void);

/* Map from a Content-Transfer-Encoding: header body (which may be NIL) */
EXPORT enum mx_mime_enc mx_mime_enc_from_name(char const *hbody);

/* XXX Try to get rid of that */
EXPORT char const *mx_mime_enc_name_from_conversion(
      enum conversion const convert);

/* How many characters of (the complete body) ln need to be quoted */
EXPORT uz mx_mime_enc_mustquote(char const *ln, uz lnlen, boole ishead);

/* QP */

/* How much space is necessary to encode len bytes in QP, worst case.
 * Includes room for terminator, UZ_MAX on overflow */
EXPORT uz mx_qp_enc_calc_size(uz len);

/* If flags includes QP_ISHEAD these assume "word" input and use special
 * quoting rules in addition; soft line breaks are not generated.
 * Otherwise complete input lines are assumed and soft line breaks are
 * generated as necessary.  Return NIL on error (overflow) */
EXPORT struct str *mx_qp_enc(struct str *out, struct str const *in,
      BITENUM_IS(u32,mx_qp_flags) flags);

/* The buffers of out and *rest* will be managed via n_realloc().
 * If inrest_or_nil is needed but NIL an error occurs, otherwise tolerant.
 * Return FAL0 on error; caller is responsible to free buffers */
EXPORT boole mx_qp_dec_header(struct str *out, struct str const *in);
EXPORT boole mx_qp_dec_part(struct str *out, struct str const *in,
      struct str *outrest, struct str *inrest_or_nil);

/* B64 */

/* How much space is necessary to encode len bytes in Base64, worst case.
 * Includes room for (CR/LF/CRLF and) terminator, UZ_MAX on overflow */
EXPORT uz mx_b64_enc_calc_size(uz len);

/* Note these simply convert all the input (if possible), including the
 * insertion of NL sequences if B64_CRLF or B64_LF is set (and multiple thereof
 * if B64_MULTILINE is set).
 * Thus, in the B64_BUF case, better call b64_enc_calc_size() first.
 * Return NIL on error (overflow; cannot happen for B64_BUF) */
EXPORT struct str *mx_b64_enc(struct str *out, struct str const *in,
      BITENUM_IS(u32,mx_b64_flags) flags);
EXPORT struct str *mx_b64_enc_buf(struct str *out, void const *vp,
      uz vp_len, BITENUM_IS(u32,mx_b64_flags) flags);

/* The _{header,part}() variants are failure tolerant, the latter requires
 * outrest to be set; due to the odd 4:3 relation inrest_or_nil should be
 * given, _then_, it is an error if it is needed but not set.
 * TODO pre v15 callers should ensure that no endless loop is entered because
 * TODO the inrest cannot be converted and ends up as inrest over and over:
 * TODO give NIL to stop such loops.
 * The buffers of out and possibly *rest* will be managed via n_realloc().
 * Returns FAL0 on error; caller is responsible to free buffers.
 * XXX FAL0 is effectively not returned for _part*() variants,
 * XXX (instead replacement characters are produced for invalid data.
 * XXX _Unless_ operation could EOVERFLOW.)
 * XXX I.e. this is bad and is tolerant for text and otherwise not */
EXPORT boole mx_b64_dec(struct str *out, struct str const *in);
EXPORT boole mx_b64_dec_header(struct str *out, struct str const *in);
EXPORT boole mx_b64_dec_part(struct str *out, struct str const *in,
      struct str *outrest, struct str *inrest_or_nil);

#include <su/code-ou.h>
#endif /* mx_MIME_ENC_H */
/* s-it-mode */
