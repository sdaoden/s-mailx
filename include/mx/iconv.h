/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ iconv(3) interface.
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
#ifndef mx_ICONV_H
#define mx_ICONV_H

#include <mx/nail.h>

#ifdef mx_HAVE_ICONV
# include <iconv.h>
#endif

#define mx_HEADER
#include <su/code-in.h>

#define n_ICONV_ASCII_NAME "us-ascii"

#ifdef mx_HAVE_ICONV
enum n_iconv_flags{
	n_ICONV_NONE,
	n_ICONV_IGN_ILSEQ = 1u<<0, /* Ignore input EILSEQ (replacement char) */
	n_ICONV_IGN_NOREVERSE = 1u<<1, /* .. non-reversible conversions in output */
	/* With n_PSO_UNICODE use Unicode replacement 0xFFFD=EF BF BD TODO iconv_open tocode! */
	n_ICONV_UNIREPL = 1u<<2,
	n_ICONV_DEFAULT = n_ICONV_IGN_ILSEQ | n_ICONV_IGN_NOREVERSE,
	n_ICONV_UNIDEFAULT = n_ICONV_DEFAULT | n_ICONV_UNIREPL
};
#endif

enum n__iconv_mib{
	n__ICONV_MIB_UTF8 = 106,
	n__ICONV_MIB_US = 3,
	n__ICONV_SET__MAX
};

#ifdef mx_HAVE_ICONV
EXPORT_DATA s32 n_iconv_err; /* TODO HACK: part of CTX to not get lost */
EXPORT_DATA iconv_t iconvd;
#endif

EXPORT boole n__iconv_name_is(char const *cset, enum n__iconv_mib mib);

/* May return newly AUTO_ALLOC()ated thing or a constant if there were adjustments.
 * NIL will be returned if cset is an invalid character set name.
 * //CONFIG strings are always stripped, and character set names are made lowercase.
 * With mime_name_norm cset is fully normalized and looked up in character set name DB */
EXPORT char *n_iconv_norm_name(char const *cset, boole mime_name_norm);

/* Is it ASCII / UTF-8 indeed?  Note: cset MUST be a iconv_norm_name()! */
INLINE boole n_iconv_name_is_ascii(char const *cset) {return n__iconv_name_is(cset, n__ICONV_MIB_US);}
INLINE boole n_iconv_name_is_utf8(char const *cset) {return n__iconv_name_is(cset, n__ICONV_MIB_UTF8);}

#ifdef mx_HAVE_ICONV
/* This sets err(ERR_NONE) if tocode and fromcode are de-facto identical but iconv does not deal with them. */
EXPORT iconv_t n_iconv_open(char const *tocode, char const *fromcode);

/* If *cd* == *iconvd*, assigns -1 to the latter */
EXPORT void n_iconv_close(iconv_t cd);

/* Reset encoding state */
EXPORT void n_iconv_reset(iconv_t cd);

/* iconv(3), but return a su_err_number, or ERR_NONE upon success.
 * The err may be ERR_NOENT unless n_ICONV_IGN_NOREVERSE is set in icf.
 * iconv_str() auto-grows on ERR_2BIG errors; in and in_rest_or_nil may be the same object.
 * Note: ERR_INVAL (incomplete sequence at end of input) is NOT handled, so the replacement character must be added
 * manually if that happens at EOF!
 * TODO These must be contexts.  For now we duplicate errors into n_iconv_err in order to be able to access it
 * TODO when stuff happens "in between"!  su_err() is not set anyhow! */
EXPORT int n_iconv_buf(iconv_t cd, enum n_iconv_flags icf, char const **inb, uz *inbleft, char **outb, uz *outbleft);
EXPORT int n_iconv_str(iconv_t icp, enum n_iconv_flags icf, struct str *out, struct str const *in, struct str *in_rest_or_nil);

/* If tocode==NIL, uses *ttycharset*.  If fromcode==NIL, uses UTF-8.
 * Returns a AUTO_ALLOC()ed buffer or NIL */
EXPORT char *n_iconv_onetime_cp(enum n_iconv_flags icf, char const *tocode, char const *fromcode, char const *input);

/* Convert the entire file ifp.  If tocode==NIL, uses *ttycharset*.  If fromcode==NIL, uses UTF-8.
 * If *ofpp_or_nil is NIL a FS_O_RDWR|FS_O_UNLINK file is created and stored on ERR_NONE return.
 * No file positioning on ifp is done before or after it has been worked.
 * n_iconv_err may be used internally, but only return code shall be used!
 * The output file is fflush_rewind()ed on success */
EXPORT s32 n_iconv_onetime_fp(enum n_iconv_flags icf, FILE **ofpp_or_nil, FILE *ifp,
		char const *tocode, char const *fromcode);
#endif /* mx_HAVE_ICONV */

#include <su/code-ou.h>
#endif /* mx_ICONV_H */
/* s-itt-mode */
