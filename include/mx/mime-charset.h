/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME character set iterator, and support.
 *
 * Copyright (c) 2012 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MIME_CHARSET_H
#define mx_MIME_CHARSET_H

#include <mx/config.h>

#ifdef mx_HAVE_ICONV
# include <stdio.h> /* TODO RID! */
#endif

#define mx_HEADER
#include <su/code-in.h>

#ifdef mx_HAVE_ICONV
struct mx_mime_type_classify_fp_ctx;
#endif

/* *sendcharsets* .. *charset-8bit* iterator; one character sets may be prepended to try first
 * (e.g., for *reply-in-same-charset*), one may be used as replacement for *ttycharset*; both
 * arguments must be iconv_norm_name()d!  (*sendcharsets* and all *charset-** normalized in assignment time.)
 * The returned boolean indicates charset_iter_is_valid().
 * Without HAVE_ICONV, this "iterates" over *ttycharset* only.
 * Due to code flow _iter_reset() may not always be called: to avoid that _iter_or_fallback() accesses invalidated
 * AUTO_ALLOC() memory; _iter_clear() should be called first (or instead) */
EXPORT void mx_mime_charset_iter_clear(void);
EXPORT boole mx_mime_charset_iter_reset(char const *cset_tryfirst_or_nil, char const *cset_isttycs_or_nil);
EXPORT boole mx_mime_charset_iter_next(void);
EXPORT boole mx_mime_charset_iter_is_valid(void);
EXPORT char const *mx_mime_charset_iter(void);

/* And this is (xxx temporary?) which returns the iterator if that is valid,
 * and otherwise either *charset-8bit* or *ttycharset*, dependent upon HAVE_ICONV */
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

#include <su/code-ou.h>
#endif /* mx_MIME_CHARSET_H */
/* s-itt-mode */
