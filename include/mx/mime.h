/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME-to-display and MIME-from-data.
 * TODO This should be filter, object-dump-to-{wire,user} etc.
 *
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

/**/
struct mx_mime_type_classify_fp_ctx;

enum mx_mime_display_flags{
	mx_MIME_DISPLAY_NONE,
	mx_MIME_DISPLAY_ICONV = 1u<<1, /* iconv() buffer */
	mx_MIME_DISPLAY_ISPRINT = 1u<<0, /* makeprint() result */
	mx_MIME_DISPLAY_DEL_CNTRL = 1u<<2, /* Delete control characters */

	/* NOTE: _TD_EOF and _TD_BUFCOPY may be ORd with enum conversion and enum sendaction, and may thus NOT clash
	 * with their bit range! */
	mx__MIME_DISPLAY_EOF = 1u<<14, /* EOF seen, last round! TODO GROSS hack */
	mx__MIME_DISPLAY_BUF_CONST = 1u<<15 /* Buffer may be const, copy it */
};

/* *sendcharsets* .. *charset-8bit* iterator; two character sets may be prepended to try first
 * (e.g., for *reply-in-same-charset*).
 * The returned boolean indicates charset_iter_is_valid().
 * Without HAVE_ICONV, this "iterates" over *ttycharset* only */
EXPORT boole mx_mime_charset_iter_reset(char const *cset_try1_or_nil, char const *cset_try2_or_nil);
EXPORT boole mx_mime_charset_iter_next(void);
EXPORT boole mx_mime_charset_iter_is_valid(void);
EXPORT char const *mx_mime_charset_iter(void);

/* And this is (xxx temporary?) which returns the iterator if that is valid and otherwise either *charset-8bit* or
 * *ttycharset*, dep. on mx_HAVE_ICONV */
EXPORT char const *mx_mime_charset_iter_or_fallback(void);

#ifdef mx_HAVE_ICONV
/* Successively try to convert ifp via iconv_onetime_fp() via complete iter_reset(cset_to_try_first_or_nil) cycle.
 * Any iconv_onetime_cp() docu is valid for this one; a set *ofpp_or_nil is ftrunc_x_trunc() in between cycles.
 * mtcfcp must hold classification data for ifp, .mtcfc_do_iconv and .mtcfc_mpccp_or_nil are ASSERT()ed;
 * .mtcfc_input_charset, .mtcfc_charset and .mtcfc_charset_is_ascii are updated.
 * With *mime-force-sendout* this function succeeds even if iconv is impossible: result differences are that
 * *ofpp_or_nil is not set (ifp is result), .mtcfc_do_iconv=FAL0, .mtcfc_conversion=CONV_TOB64,
 * .mtcfc_content_type="application/octet-stream", as well as .mtcfc_charset=.mtcfc_input_charset=NIL,
 * .mtcfc_charset_is_ascii=FAL0, and ifp is rewind()ed.
 * INVAL is returned on iter excess XXX NOTSUP */
EXPORT s32 mx_mime_charset_iter_onetime_fp(FILE **ofpp_or_nil, FILE *ifp, struct mx_mime_type_classify_fp_ctx *mtcfcp,
		char const *cset_to_try_first_or_nil, char const **emsg_or_nil);
#endif

/* Convert header fields from RFC 5322 format; out-> must be su_FREE()d.
 * FAL0 on hard error, in which case out->s is NIL */
EXPORT boole mx_mime_display_from_header(struct str const *in, struct str *out,
		BITENUM(u32,mx_mime_display_flags) flags);

/* Interpret MIME strings in parts of an address field.
 * NIL on error or NIL input */
EXPORT char *mx_mime_fromaddr(char const *name);

/* fwrite(3) performing the given MIME conversion TODO mess! filter!! */
EXPORT sz mx_mime_write(char const *ptr, uz size, FILE *f, enum conversion convert,
		BITENUM(u32,mx_mime_display_flags) dflags, struct quoteflt *qf, struct str *outrest,
		struct str *inrest);
EXPORT sz mx_xmime_write /* TODO drop */(char const *ptr, uz size, FILE *f, enum conversion convert,
		BITENUM(u32,mx_mime_display_flags) dflags, struct str *outrest, struct str *inrest);

#include <su/code-ou.h>
#endif /* mx_MIME_H */
/* s-itt-mode */
