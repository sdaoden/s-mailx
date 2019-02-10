/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ iconv(3) interface.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef mx_ICONV_H
#define mx_ICONV_H

#include <mx/nail.h>
#ifdef mx_HAVE_ICONV
# include <iconv.h>
#endif

/* TODO fake */
#include "su/code-in.h"

#ifdef mx_HAVE_ICONV
enum n_iconv_flags{
   n_ICONV_NONE,
   n_ICONV_IGN_ILSEQ = 1u<<0, /* Ignore input EILSEQ (replacement char) */
   n_ICONV_IGN_NOREVERSE = 1u<<1, /* .. non-reversible conversions in output */
   n_ICONV_UNIREPL = 1u<<2, /* Use Unicode replacement 0xFFFD=EF BF BD */
   n_ICONV_DEFAULT = n_ICONV_IGN_ILSEQ | n_ICONV_IGN_NOREVERSE,
   n_ICONV_UNIDEFAULT = n_ICONV_DEFAULT | n_ICONV_UNIREPL
};

VL s32 n_iconv_err_no; /* TODO HACK: part of CTX to not get lost */
VL iconv_t iconvd;
#endif /* mx_HAVE_ICONV */

/* Returns a newly n_autorec_alloc()ated thing if there were adjustments.
 * Return value is always smaller or of equal size.
 * NIL will be returned if cset is an invalid character set name */
FL char *n_iconv_normalize_name(char const *cset);

/* Is it ASCII indeed? */
FL boole n_iconv_name_is_ascii(char const *cset);

#ifdef mx_HAVE_ICONV
FL iconv_t n_iconv_open(char const *tocode, char const *fromcode);
/* If *cd* == *iconvd*, assigns -1 to the latter */
FL void n_iconv_close(iconv_t cd);

/* Reset encoding state */
FL void n_iconv_reset(iconv_t cd);

/* iconv(3), but return su_err_no() or 0 upon success.
 * The err_no may be ERR_NOENT unless n_ICONV_IGN_NOREVERSE is set in icf.
 * iconv_str() auto-grows on ERR_2BIG errors; in and in_rest_or_nil may be
 * the same object.
 * Note: ERR_INVAL (incomplete sequence at end of input) is NOT handled, so the
 * replacement character must be added manually if that happens at EOF!
 * TODO These must be contexts.  For now we duplicate su_err_no() into
 * TODO n_iconv_err_no in order to be able to access it when stuff happens
 * TODO "in between"! */
FL int n_iconv_buf(iconv_t cd, enum n_iconv_flags icf,
      char const **inb, size_t *inbleft, char **outb, size_t *outbleft);
FL int n_iconv_str(iconv_t icp, enum n_iconv_flags icf,
      struct str *out, struct str const *in, struct str *in_rest_or_nil);

/* If tocode==NIL, uses *ttycharset*.  If fromcode==NIL, uses UTF-8.
 * Returns a autorec_alloc()ed buffer or NIL */
FL char *n_iconv_onetime_cp(enum n_iconv_flags icf, char const *tocode,
      char const *fromcode, char const *input);
#endif /* mx_HAVE_ICONV */

#include "su/code-ou.h"
#endif /* mx_ICONV_H */
/* s-it-mode */
