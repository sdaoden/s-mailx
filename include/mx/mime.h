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
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MIME_H
#define mx_MIME_H

#include <su/code.h>

#include <stdio.h> /* TODO RID! */

#define mx_HEADER
#include <su/code-in.h>

struct header;
struct quoteflt;
struct str;

enum mx_mime_display_flags{
   mx_MIME_DISPLAY_NONE,
   mx_MIME_DISPLAY_ICONV = 1u<<1, /* iconv() buffer */
   mx_MIME_DISPLAY_ISPRINT = 1u<<0, /* makeprint() result */
   mx_MIME_DISPLAY_DEL_CNTRL = 1u<<2, /* Delete control characters */

   /* NOTE: _TD_EOF and _TD_BUFCOPY may be ORd with enum conversion and
    * enum sendaction, and may thus NOT clash with their bit range! */
   mx__MIME_DISPLAY_EOF = 1u<<14, /* EOF seen, last round! TODO GROSS hack */
   mx__MIME_DISPLAY_BUF_CONST = 1u<<15 /* Buffer may be const, copy it */
};

/* *sendcharsets* .. *charset-8bit* iterator; *a_charset_to_try_first* may be
 * used to prepend a charset to this list (e.g., for *reply-in-same-charset*).
 * The returned boolean indicates charset_iter_is_valid().
 * Without HAVE_ICONV, this "iterates" over *ttycharset* only */
EXPORT boole mx_mime_charset_iter_reset(char const *a_charset_to_try_first);
EXPORT boole mx_mime_charset_iter_next(void);
EXPORT boole mx_mime_charset_iter_is_valid(void);
EXPORT char const *mx_mime_charset_iter(void);

/* And this is (xxx temporary?) which returns the iterator if that is valid and
 * otherwise either *charset-8bit* or *ttycharset*, dep. on mx_HAVE_ICONV */
EXPORT char const *mx_mime_charset_iter_or_fallback(void);

EXPORT void mx_mime_charset_iter_recurse(char *outer_storage[2]);/* TODO drop*/
EXPORT void mx_mime_charset_iter_restore(char *outer_storage[2]);/* TODO drop*/

/* Check whether header body needs MIME conversion */
/* TODO mime_header_needs_mime should always be available - gross flow! */
#ifdef mx_HAVE_ICONV
EXPORT boole mx_mime_header_needs_mime(char const *body);
#endif

/* Convert header fields from RFC 5322 format */
EXPORT void mx_mime_display_from_header(struct str const *in, struct str *out,
      BITENUM_IS(u32,mx_mime_display_flags) flags);

/* Interpret MIME strings in parts of an address field */
EXPORT char *mx_mime_fromaddr(char const *name);

/* fwrite(3) performing the given MIME conversion TODO mess! filter!! */
EXPORT sz mx_mime_write(char const *ptr, uz size, FILE *f,
      enum conversion convert, BITENUM_IS(u32,mx_mime_display_flags) dflags,
      struct quoteflt *qf, struct str *outrest, struct str *inrest);
EXPORT sz mx_xmime_write(char const *ptr, uz size, /* TODO drop */
      FILE *f, enum conversion convert,
      BITENUM_IS(u32,mx_mime_display_flags) dflags,
      struct str *outrest, struct str *inrest);

#include <su/code-ou.h>
#endif /* mx_MIME_H */
/* s-it-mode */
