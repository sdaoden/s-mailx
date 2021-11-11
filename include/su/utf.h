/*@ Convert in between UnicodeTranformationFormats.
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
#ifndef su_UTF_H
#define su_UTF_H
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
enum{
   su_UTF8_BUFFER_SIZE = 5u
};
#define su_UTF8_REPLACER "\xEF\xBF\xBD"
EXPORT_DATA char const su_utf8_replacer[sizeof su_UTF8_REPLACER];
EXPORT u32 su_utf8_to_32(char const **bdat, uz *blen);
#define su_UTF32_REPLACER 0xFFFDu
EXPORT uz su_utf32_to_8(u32 c, char *bp);
C_DECL_END
#include <su/code-ou.h>
#endif /* su_UTF_H */
/* s-it-mode */
