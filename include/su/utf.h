/*@ Convert in between UnicodeTranformationFormats.
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)
class utf8;
class utf32;
class EXPORT utf8{
public:
   enum{
      buffer_size = su_UTF8_BUFFER_SIZE
   };
   static char const replacer[sizeof su_UTF8_REPLACER];
   static u32 convert_to_32(char const **bdat, uz *blen){
      return su_utf8_to_32(bdat, blen);
   }
};
class utf32{
public:
   static u32 const replacer = su_UTF32_REPLACER;
   static uz convert_to_8(u32 c, char *bp) {return su_utf32_to_8(c, bp);}
};
NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_UTF_H */
/* s-it-mode */
