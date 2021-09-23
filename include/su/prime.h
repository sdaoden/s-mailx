/*@ Prime numbers.
 *
 * Copyright (c) 2001 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_PRIME_H
#define su_PRIME_H

/*!
 * \file
 * \ingroup PRIME
 * \brief \r{PRIME}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* prime {{{ */
/*!
 * \defgroup PRIME Prime numbers
 * \ingroup MISC
 * \brief Prime numbers (\r{su/prime.h})
 * @{
 */

/*! Minimum pre-calculated lookup prime (a \r{su_u32}). */
#define su_PRIME_LOOKUP_MIN 0x2u

/*! Maximum pre-calculated lookup prime (a \r{su_u32}). */
#define su_PRIME_LOOKUP_MAX 0x18000005u

/*! \_
 * \remarks{Very unacademical (brute force).} */
EXPORT boole su_prime_is_prime(u64 no, boole allowpseudo);

/*! \_ */
EXPORT u64 su_prime_get_former(u64 no, boole allowpseudo);

/*! \_ */
EXPORT u64 su_prime_get_next(u64 no, boole allowpseudo);

/*! Former precalculated, minimum is \r{su_PRIME_LOOKUP_MIN}. */
EXPORT u32 su_prime_lookup_former(u32 no);

/*! Next precalculated, maximum is \r{su_PRIME_LOOKUP_MAX}. */
EXPORT u32 su_prime_lookup_next(u32 no);

/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class prime;

/* prime {{{ */
/*!
 * \ingroup PRIME
 * C++ variant of \r{PRIME} (\r{su/prime.h})
 */
class prime{
   su_CLASS_NO_COPY(prime);
public:
   /*! \copydoc{su_PRIME_LOOKUP_MIN} */
   static u32 const lookup_min = su_PRIME_LOOKUP_MIN;

   /*! \copydoc{su_PRIME_LOOKUP_MAX} */
   static u32 const lookup_max = su_PRIME_LOOKUP_MAX;

   /*! \copydoc{su_prime_is_prime()} */
   static boole is_prime(u64 no, boole allowpseudo=TRU1){
      return su_prime_is_prime(no, allowpseudo);
   }

   /*! \copydoc{su_prime_get_former()} */
   static u64 get_former(u64 no, boole allowpseudo=TRU1){
      return su_prime_get_former(no, allowpseudo);
   }

   /*! \copydoc{su_prime_get_next()} */
   static u64 get_next(u64 no, boole allowpseudo=TRU1){
      return su_prime_get_next(no, allowpseudo);
   }

   /*! \copydoc{su_prime_lookup_former()} */
   static u32 lookup_former(u32 no) {return su_prime_lookup_former(no);}

   /*! \copydoc{su_prime_lookup_next()} */
   static u32 lookup_next(u32 no) {return su_prime_lookup_next(no);}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_PRIME_H */
/* s-it-mode */
