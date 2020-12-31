/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ iconv(3) interface.
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
#ifndef mx_ICONV_H
#define mx_ICONV_H

#include <mx/nail.h>

#ifdef mx_HAVE_ICONV
# include <iconv.h>
#endif

#define mx_HEADER
#include <su/code-in.h>

#ifdef mx_HAVE_ICONV
enum n_iconv_flags{
   n_ICONV_NONE,
   n_ICONV_IGN_ILSEQ = 1u<<0, /* Ignore input EILSEQ (replacement char) */
   n_ICONV_IGN_NOREVERSE = 1u<<1, /* .. non-reversible conversions in output */
   n_ICONV_UNIREPL = 1u<<2, /* Use Unicode replacement 0xFFFD=EF BF BD */
   n_ICONV_DEFAULT = n_ICONV_IGN_ILSEQ | n_ICONV_IGN_NOREVERSE,
   n_ICONV_UNIDEFAULT = n_ICONV_DEFAULT | n_ICONV_UNIREPL
};

EXPORT_DATA s32 n_iconv_err_no; /* TODO HACK: part of CTX to not get lost */
EXPORT_DATA iconv_t iconvd;
#endif /* mx_HAVE_ICONV */

/* Returns a newly n_autorec_alloc()ated thing if there were adjustments.
 * Return value is always smaller or of equal size.
 * NIL will be returned if cset is an invalid character set name */
EXPORT char *n_iconv_normalize_name(char const *cset);

/* Is it ASCII indeed? */
EXPORT boole n_iconv_name_is_ascii(char const *cset);

#ifdef mx_HAVE_ICONV
EXPORT iconv_t n_iconv_open(char const *tocode, char const *fromcode);
/* If *cd* == *iconvd*, assigns -1 to the latter */
EXPORT void n_iconv_close(iconv_t cd);

/* Reset encoding state */
EXPORT void n_iconv_reset(iconv_t cd);

/* iconv(3), but return su_err_no() or 0 upon success.
 * The err_no may be ERR_NOENT unless n_ICONV_IGN_NOREVERSE is set in icf.
 * iconv_str() auto-grows on ERR_2BIG errors; in and in_rest_or_nil may be
 * the same object.
 * Note: ERR_INVAL (incomplete sequence at end of input) is NOT handled, so the
 * replacement character must be added manually if that happens at EOF!
 * TODO These must be contexts.  For now we duplicate su_err_no() into
 * TODO n_iconv_err_no in order to be able to access it when stuff happens
 * TODO "in between"! */
EXPORT int n_iconv_buf(iconv_t cd, enum n_iconv_flags icf,
      char const **inb, uz *inbleft, char **outb, uz *outbleft);
EXPORT int n_iconv_str(iconv_t icp, enum n_iconv_flags icf,
      struct str *out, struct str const *in, struct str *in_rest_or_nil);

/* If tocode==NIL, uses *ttycharset*.  If fromcode==NIL, uses UTF-8.
 * Returns a autorec_alloc()ed buffer or NIL */
EXPORT char *n_iconv_onetime_cp(enum n_iconv_flags icf, char const *tocode,
      char const *fromcode, char const *input);
#endif /* mx_HAVE_ICONV */

#include <su/code-ou.h>
#endif /* mx_ICONV_H */
/* s-it-mode */
