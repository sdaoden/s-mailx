/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ struct mx_srch_ctx.
 *
 * Copyright (c) 2014 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_SRCH_CTX_H
#define mx_SRCH_CTX_H

#include <mx/nail.h>

#ifdef mx_HAVE_REGEX
# include <su/re.h>
#endif

#define mx_HEADER
#include <su/code-in.h>

struct mx_srch_ctx;

struct mx_srch_ctx{
   /* XXX Type of search should not be evaluated but be enum */
   boole sc_field_exists; /* Only check whether field spec. exists */
   boole sc_skin; /* Shall work on (skin()ned) addresses */
   u8 sc__pad[6];
   char const *sc_field; /* Field spec. where to search (not always used) */
   char const *sc_body; /* Field body search expression */
#ifdef mx_HAVE_REGEX
   struct su_re *sc_fieldre; /* Could be instead of .sc_field */
   struct su_re *sc_bodyre; /* Ditto, .sc_body */
   struct su_re sc_fieldre__buf;
   struct su_re sc_bodyre__buf;
#endif
};

#include <su/code-ou.h>
#endif /* mx_SRCH_CTX_H */
/* s-it-mode */
