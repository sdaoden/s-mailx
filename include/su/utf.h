/*@ Convert in between UnicodeTranformationFormats.
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

/* utf {{{ */
/*!
 * \defgroup UTF Unicode Transformation Formats
 * \ingroup TEXT
 * \brief Convert in between UnicodeTranformationFormats (\r{su/utf.h})
 * @{
 */

/* utf8 {{{ */
/*!
 * \defgroup UTF8 UTF-8
 * \ingroup UTF
 * \brief UTF-8 (\r{su/utf.h})
 * @{
 */

enum{
	su_UTF8_BUFFER_SIZE = 5u /*!< Maximum buffer size of an UTF-8 sequence including terminating \NUL. */
};

/*! Character set name. */
#define su_UTF8_NAME "UTF-8"

/*! Character set name, lowercase. */
#define su_UTF8_NAME_LOWER "utf-8"

/*! The Unicode replacement character \c{0xFFFD} as an UTF-8 literal. */
#define su_UTF8_REPLACER "\xEF\xBF\xBD"

/*! Compiled in version of \r{su_UTF8_NAME}. */
EXPORT_DATA char const su_utf8_name[sizeof su_UTF8_NAME];

/*! Compiled in version of \r{su_UTF8_NAME_LOWER}. */
EXPORT_DATA char const su_utf8_name_lower[sizeof su_UTF8_NAME_LOWER];

/*! Compiled in version of \r{su_UTF8_REPLACER}. */
EXPORT_DATA char const su_utf8_replacer[sizeof su_UTF8_REPLACER];

/*! Convert, and update arguments to point after range.
 * Returns \r{su_U32_MAX} on error, in which case the arguments will have been
 * stepped one byte. */
EXPORT u32 su_utf8_to_32(char const **bdat, uz *blen);
/*! @} *//* }}} */

/* utf32 {{{ */
/*!
 * \defgroup UTF32 UTF-32
 * \ingroup UTF
 * \brief UTF-32 (\r{su/utf.h})
 * @{
 */

/*! Character set name. */
#define su_UTF32_NAME "UTF-32"

/*! Character set name, lowercase. */
#define su_UTF32_NAME_LOWER "utf-32"

/*! The Unicode replacement character \c{0xFFFD} as an UTF-32 codepoint. */
#define su_UTF32_REPLACER 0xFFFDu

/*! Compiled in version of \r{su_UTF32_NAME}. */
EXPORT_DATA char const su_utf32_name[sizeof su_UTF32_NAME];

/*! Compiled in version of \r{su_UTF32_NAME_LOWER}. */
EXPORT_DATA char const su_utf32_name_lower[sizeof su_UTF32_NAME_LOWER];

/*! Convert an UTF-32 character to an UTF-8 sequence.
 * \a{bp} must be large enough also for the terminating \NUL (see
 * \r{su_UTF8_BUFFER_SIZE}), its length will * be returned. */
EXPORT uz su_utf32_to_8(u32 c, char *bp);
/*! @} *//* }}} */

/*! @} *//* }}}*/

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class utf8;
class utf32;

/* utf8 {{{ */
/*!
 * \ingroup UTF8
 * C++ variant of \r{UTF8} (\r{su/utf.h})
 */
class EXPORT utf8{
	su_CLASS_NO_COPY(utf8);
public:
	enum{
		buffer_size = su_UTF8_BUFFER_SIZE /*!< \copydoc{su_UTF8_BUFFER_SIZE} */
	};

	static char const name[sizeof su_UTF8_NAME]; /*!< \copydoc{su_utf8_name} */
	static char const name_lower[sizeof su_UTF8_NAME_LOWER]; /*!< \copydoc{su_utf8_name_lower} */
	static char const replacer[sizeof su_UTF8_REPLACER]; /*!< \copydoc{su_utf8_replacer} */

	/*! \copydoc{su_utf8_to_32()} */
	static u32 convert_to_32(char const **bdat, uz *blen) {return su_utf8_to_32(bdat, blen);}
};
/* }}} */

/* utf32 {{{ */
/*!
 * \ingroup UTF32
 * C++ variant of \r{UTF32} (\r{su/utf.h})
 */
class utf32{
	su_CLASS_NO_COPY(utf32);
public:
	static char const name[sizeof su_UTF32_NAME]; /*!< \copydoc{su_utf32_name} */
	static char const name_lower[sizeof su_UTF32_NAME_LOWER]; /*!< \copydoc{su_utf32_name_lower} */
	static u32 const replacer = su_UTF32_REPLACER; /*!< \copydoc{su_UTF32_REPLACER} */

	/*! \copydoc{su_utf32_to_8()} */
	static uz convert_to_8(u32 c, char *bp) {return su_utf32_to_8(c, bp);}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_UTF_H */
/* s-itt-mode */
