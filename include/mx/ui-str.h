/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Visual strings, string classification/preparation for the user interface.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
 */
/*
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
#ifndef mx_UI_STR_H
#define mx_UI_STR_H

#include <mx/nail.h>

#ifdef mx_HAVE_C90AMEND1
# include <wchar.h>
# include <wctype.h>
#endif

#define mx_HEADER
#include <su/code-in.h>

#ifdef mx_HAVE_C90AMEND1
typedef wchar_t wc_t;
# define n_WC_C(X) L ## X
#else
typedef char wc_t; /* Yep: really 8-bit char */
# define n_WC_C(X) X
#endif

struct n_visual_info_ctx;

struct n_visual_info_ctx{
   char const *vic_indat; /*I Input data */
   uz vic_inlen; /*I If UZ_MAX, su_cs_len(.vic_indat) */
   char const *vic_oudat; /*O remains */
   uz vic_oulen;
   uz vic_chars_seen; /*O number of characters processed */
   uz vic_bytes_seen; /*O number of bytes passed */
   uz vic_vi_width; /*[O] visual width of the entire range */
   wc_t *vic_woudat; /*[O] if so requested */
   uz vic_woulen; /*[O] entries in .vic_woudat, if used */
   wc_t vic_waccu; /*O The last wchar_t/char processed (if any) */
   enum n_visual_info_flags vic_flags; /*O Copy of parse flags */
   /* TODO bidi */
#ifdef mx_HAVE_C90AMEND1
   mbstate_t *vic_mbstate; /*IO .vic_mbs_def used if NULL */
   mbstate_t vic_mbs_def;
#endif
};

/* setlocale(3), *ttycharset* etc. */
EXPORT void n_locale_init(void);

/* Parse (onechar of) a given buffer, and generate infos along the way.
 * If _WOUT_CREATE is set in vif, .vic_woudat will be NUL terminated!
 * vicp must be zeroed before first use */
EXPORT boole n_visual_info(struct n_visual_info_ctx *vicp,
      enum n_visual_info_flags vif);

/* Check (multibyte-safe) how many bytes of buf (which is blen byts) can be
 * safely placed in a buffer (field width) of maxlen bytes */
EXPORT uz field_detect_clip(uz maxlen, char const *buf, uz blen);

/* Place cp in a autorec_alloc()ed buffer, column-aligned.
 * For header display only */
EXPORT char *colalign(char const *cp, int col, int fill,
      int *cols_decr_used_or_nil);

/* Convert a string to a displayable one;
 * prstr() returns the result savestr()d, prout() writes it */
EXPORT void makeprint(struct str const *in, struct str *out);
EXPORT uz delctrl(char *cp, uz len);
EXPORT char *prstr(char const *s);
EXPORT int prout(char const *s, uz sz, FILE *fp);

#include <su/code-ou.h>
#endif /* mx_UI_STR_H */
/* s-it-mode */
