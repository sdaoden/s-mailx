/*@ Convert in between UnicodeTranformationFormats.
 *
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

/*!
 * \file
 * \ingroup UTF
 * \brief \r{UTF}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/*!
 * \defgroup UTF Unicode Transformation Formats
 * \ingroup TEXT
 * \brief Convert in between UnicodeTranformationFormats (\r{su/utf.h})
 * @{
 */

/*! The Unicode replacement character \c{0xFFFD} as an UTF-8 literal. */
#define su_UTF_REPLACEMENT_8 "\xEF\xBF\xBD"

/*! Compiled in version of \r{su_UTF_REPLACEMENT_8}. */
EXPORT_DATA char const su_utf_replacement_8[sizeof su_UTF_REPLACEMENT_8];

/*! Convert, and update arguments to point after range.
 * Returns \r{su_U32_MAX} on error, in which case the arguments will have been
 * stepped one byte. */
EXPORT u32 su_utf_8_to_32(char const **bdat, uz *blen);

/*! Convert an UTF-32 character to an UTF-8 sequence.
 * \a{bp} must be large enough also for the terminating NUL, it's length will
 * be returned. */
EXPORT uz su_utf_32_to_8(u32 c, char *bp);

/*! @} */
C_DECL_END
#if !C_LANG || defined DOXYGEN_CXX
NSPC_BEGIN(su)

/*! C++ variant of \r{UTF} (\r{su/utf.h}) */
class utf{
public:
   /*! \r{su_UTF_REPLACEMENT_8} */
   static char const replacement_8[] = su_UTF_REPLACEMENT_8;

   /*! \r{su_utf_8_to_32()} */
   static u32 convert_8_to_32(char const **bdat, uz *blen){
      return su_utf_8_to_32(bdat, blen);
   }

   /*! \r{su_utf_32_to_8()} */
   static uz convert_32_to_8(u32 c, char *bp) {return su_utf_32_to_8(c, bp);}
};

NSPC_END(su)
#endif /* !C_LANG || DOXYGEN_CXX */
#include <su/code-ou.h>
#endif /* su_UTF_H */
/* s-it-mode */
