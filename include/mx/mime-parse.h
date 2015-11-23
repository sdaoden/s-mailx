/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Parse a message into a tree of struct mimepart objects. TODO
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
#ifndef mx_MIME_PARSE_H
#define mx_MIME_PARSE_H

#include <su/code.h>

#define mx_HEADER
#include <su/code-in.h>

struct message;

enum mx_mime_parse_flags{
   mx_MIME_PARSE_NONE,
   mx_MIME_PARSE_DECRYPT = 1u<<0,
   mx_MIME_PARSE_PARTS = 1u<<1,
   mx_MIME_PARSE_SHALLOW = 1u<<2,
   /* In effect we parse this message for user display or quoting purposes,
    * so relaxed rules regarding content inspection may be applicable */
   mx_MIME_PARSE_FOR_USER_CONTEXT = 1u<<3
};

/* Create MIME part object tree for and of mp */
EXPORT struct mimepart *mx_mime_parse_msg(struct message *mp,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf);

#ifdef mx_HAVE_TLS
#if 0 /* TODO mime_parse_pkcs7 */
EXPORT struct mimepart *mx_mime_parse_pkcs7(struct message *mp,
      struct mimepart *unencmp, BITENUM_IS(u32,mx_mime_parse_flags) mpf,
      int level);
#endif
#endif

#include <su/code-ou.h>
#endif /* mx_MIME_PARSE_H */
/* s-it-mode */
