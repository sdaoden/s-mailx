/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Visual strings, string classification/preparation for the user interface.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO rewrite, ISC
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

struct mx_visual_info_ctx;

enum mx_visual_info_flags{
   mx_VISUAL_INFO_NONE,
   mx_VISUAL_INFO_ONE_CHAR = 1u<<0, /* Step only one char, then return */
   mx_VISUAL_INFO_SKIP_ERRORS = 1u<<1, /* Treat via replacement, step byte */
   mx_VISUAL_INFO_WIDTH_QUERY = 1u<<2, /* Detect visual character widths */

   /* Rest only with mx_HAVE_C90AMEND1, mutual with _ONE_CHAR */
   mx_VISUAL_INFO_WOUT_CREATE = 1u<<8, /* Use/create .vic_woudat */
   mx_VISUAL_INFO_WOUT_AUTO_ALLOC = 1u<<9, /* ..AUTO_ALLOC() it first */
   /* Only visuals into .vic_woudat - implies _WIDTH_QUERY */
   mx_VISUAL_INFO_WOUT_PRINTABLE = 1u<<10,

   n__VISUAL_INFO_FLAGS = mx_VISUAL_INFO_WOUT_CREATE |
         mx_VISUAL_INFO_WOUT_AUTO_ALLOC | mx_VISUAL_INFO_WOUT_PRINTABLE
};

#ifdef mx_HAVE_C90AMEND1
typedef wchar_t wc_t;
# define mx_WC_C(X) L ## X
#else
typedef char wc_t; /* Yep: really 8-bit char */
# define mx_WC_C(X) X
#endif

struct mx_visual_info_ctx{
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
   BITENUM_IS(u32,mx_visual_info_flags) vic_flags; /*O Copy of parse flags */
   /* TODO bidi */
#ifdef mx_HAVE_C90AMEND1
   mbstate_t *vic_mbstate; /*IO .vic_mbs_def used if NULL */
   mbstate_t vic_mbs_def;
#endif
};

/* setlocale(3), *ttycharset* etc. */
EXPORT void mx_locale_init(void);

/* Parse (onechar of) a given buffer, and generate infos along the way.
 * If _WOUT_CREATE is set in vif, .vic_woudat will be NUL terminated!
 * vicp must be zeroed before first use */
EXPORT boole mx_visual_info(struct mx_visual_info_ctx *vicp,
      BITENUM_IS(u32,mx_visual_info_flags) vif);

/* Check (multibyte-safe) how many bytes of buf (which is blen byts) can be
 * safely placed in a buffer (field width) of maxlen bytes */
EXPORT uz mx_field_detect_clip(uz maxlen, char const *buf, uz blen);

/* Place cp in a AUTO_ALLOC()ed buffer, column-aligned.
 * For header display only */
EXPORT char *mx_colalign(char const *cp, int col, int fill,
      int *cols_decr_used_or_nil);

/* Convert a string to a displayable one;
 * _cp() returns the result savestr()d, _write_cp() writes it */
EXPORT void mx_makeprint(struct str const *in, struct str *out);
EXPORT char *mx_makeprint_cp(char const *cp);
EXPORT int mx_makeprint_write_fp(char const *s, uz sz, FILE *fp);

/* Only remove (remaining) control characters, reterminate, return length */
EXPORT uz mx_del_cntrl(char *cp, uz len);

#include <su/code-ou.h>
#endif /* mx_UI_STR_H */
/* s-it-mode */
