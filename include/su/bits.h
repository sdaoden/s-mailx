/*@ Bit operations. TODO asm optimizations
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
#ifndef su_BITS_H
#define su_BITS_H

/*!
 * \file
 * \ingroup BITS
 * \brief \r{BITS}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/*!
 * \defgroup BITS Bit operations
 * \ingroup MISC
 * \brief Bit operations (\r{su/bits.h})
 * @{
 */

/*! Round up \a{BITS} to the next \r{su_UZ_BITS} multiple.
 * \remarks{\a{BITS} may not be 0.} */
#define su_BITS_ROUNDUP(BITS) (((BITS) + (su_UZ_BITS - 1)) & ~(su_UZ_BITS - 1))

/*! Calculate needed number of \r{su_uz} (array entries) to store \a{BITS}.
 * \remarks{\a{BITS} may not be 0.} */
#define su_BITS_TO_UZ(BITS) (su_BITS_ROUNDUP(BITS) / su_UZ_BITS)

/*! Array offset for \a{BIT}. */
#define su_BITS_WHICH_OFF(BIT) ((BIT) / su_UZ_BITS)

/*! Bit to test in slot indexed via \r{su_BITS_WHICH_OFF()}. */
#define su_BITS_WHICH_BIT(BIT) ((BIT) & (su_UZ_BITS - 1))

/*! Maximum useful offset in a \r{su_uz} array for \a{BITS}.
 * \remarks{\a{BITS} may not be 0.} */
#define su_BITS_TOP_OFF(BITS) (su_BITS_TO_UZ(BITS) - 1)

/*! Number of useful bits in the topmost offset of a \r{su_uz} array used to
 * store \a{BITS}.
 * \remarks{\a{BITS} may not be 0.} */
#define su_BITS_TOP_BITNO(BITS) (su_UZ_BITS - (su_BITS_ROUNDUP(BITS) - (BITS)))

/*! The mask for the topmost offset of a \r{su_uz} array used to store
 * store \a{BITS}.
 * \remarks{\a{BITS} may not be 0.} */
#define su_BITS_TOP_MASK(BITS) (su_UZ_MAX >> (su_BITS_ROUNDUP(BITS) - (BITS)))

/*! Create a bit mask for the inclusive bit range \a{LO} to \a{HI}.
 * \remarks{\a{HI} cannot use highest bit!}
 * \remarks{Identical to \r{su_BITENUM_MASK()}.} */
#define su_BITS_RANGE_MASK(LO,HI) su_BITENUM_MASK(LO, HI)

/*! \_ */
INLINE boole su_bits_test(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, FAL0);
   return ((x & (1lu << bit)) != 0);
}

/*! \_ */
INLINE uz su_bits_set(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, x);
   return (x | (1lu << bit));
}

/*! \_ */
INLINE uz su_bits_flip(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, x);
   return (x ^ (1lu << bit));
}

/*! \_ */
INLINE uz su_bits_clear(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, x);
   return (x & ~(1lu << bit));
}

/*! \_ */
INLINE boole su_bits_test_and_set(uz *xp, uz bit){
   boole rv;
   ASSERT_RET(xp != NIL, FAL0);
   ASSERT_RET(bit < UZ_BITS, FAL0);
   bit = 1lu << bit;
   rv = ((*xp & bit) != 0);
   *xp |= bit;
   return rv;
}

/*! \_ */
INLINE boole su_bits_test_and_flip(uz *xp, uz bit){
   boole rv;
   ASSERT_RET(xp != NIL, FAL0);
   ASSERT_RET(bit < UZ_BITS, FAL0);
   bit = 1lu << bit;
   rv = ((*xp & bit) != 0);
   *xp ^= bit;
   return rv;
}

/*! \_ */
INLINE boole su_bits_test_and_clear(uz *xp, uz bit){
   boole rv;
   ASSERT_RET(xp != NIL, FAL0);
   ASSERT_RET(bit < UZ_BITS, FAL0);
   bit = 1lu << bit;
   rv = ((*xp & bit) != 0);
   *xp &= ~bit;
   return rv;
}

/*! \r{su_UZ_MAX} if none found. */
INLINE uz su_bits_find_first_set(uz x){
   uz i = 0;
   if(x != 0)
      do if(x & 1)
         return i;
      while((++i, x >>= 1));
   return UZ_MAX;
}

/*! \r{su_UZ_MAX} if none found. */
INLINE uz su_bits_find_last_set(uz x){
   if(x != 0){
      uz i = UZ_BITS - 1;

      do if(x & (1lu << i))
         return i;
      while(i--);
   }
   return UZ_MAX;
}

/*! \_ */
INLINE uz su_bits_rotate_left(uz x, uz bits){
   ASSERT_RET(bits < UZ_BITS, x);
   return ((x << bits) | (x >> (UZ_BITS - bits)));
}

/*! \_ */
INLINE uz su_bits_rotate_right(uz x, uz bits){
   ASSERT_RET(bits < UZ_BITS, x);
   return ((x >> bits) | (x << (UZ_BITS - bits)));
}

/*! \_ */
INLINE boole su_bits_array_test(uz const *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   return su_bits_test(xap[su_BITS_WHICH_OFF(bit)], su_BITS_WHICH_BIT(bit));
}

/*! \_ */
INLINE void su_bits_array_set(uz *xap, uz bit){
   ASSERT_RET_VOID(xap != NIL);
   xap += su_BITS_WHICH_OFF(bit);
   *xap = su_bits_set(*xap, su_BITS_WHICH_BIT(bit));
}

/*! \_ */
INLINE void su_bits_array_flip(uz *xap, uz bit){
   ASSERT_RET_VOID(xap != NIL);
   xap += su_BITS_WHICH_OFF(bit);
   *xap = su_bits_flip(*xap, su_BITS_WHICH_BIT(bit));
}

/*! \_ */
INLINE void su_bits_array_clear(uz *xap, uz bit){
   ASSERT_RET_VOID(xap != NIL);
   xap += su_BITS_WHICH_OFF(bit);
   *xap = su_bits_clear(*xap, su_BITS_WHICH_BIT(bit));
}

/*! \_ */
INLINE boole su_bits_array_test_and_set(uz *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   xap += su_BITS_WHICH_OFF(bit);
   return su_bits_test_and_set(xap, su_BITS_WHICH_BIT(bit));
}

/*! \_ */
INLINE boole su_bits_array_test_and_flip(uz *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   xap += su_BITS_WHICH_OFF(bit);
   return su_bits_test_and_flip(xap, su_BITS_WHICH_BIT(bit));
}

/*! \_ */
INLINE boole su_bits_array_test_and_clear(uz *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   xap += su_BITS_WHICH_OFF(bit);
   return su_bits_test_and_clear(xap, su_BITS_WHICH_BIT(bit));
}

#if 0 /* TODO port array_find_first() */
/*! \_ */
EXTERN uz su_bits_array_find_first_set(uz const *xap, uz xaplen);

/*! \_ */
EXTERN uz su_bits_array_find_last_set(uz const *xap, uz xaplen);

/*! \_ */
EXTERN uz su_bits_array_find_first_set_after(uz const *xap, uz xaplen,
      uz startbit);
#endif

/*! @} */
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class bits;

/*!
 * \ingroup BITS
 * C++ variant of \r{BITS} (\r{su/bits.h})
 */
class bits{
   su_CLASS_NO_COPY(bits);
public:
   /*! \copydoc{su_bits_test()} */
   static boole test(uz x, uz bit) {return su_bits_test(x, bit);}

   /*! \copydoc{su_bits_set()} */
   static uz set(uz x, uz bit) {return su_bits_set(x, bit);}

   /*! \copydoc{su_bits_flip()} */
   static uz flip(uz x, uz bit) {return su_bits_flip(x, bit);}

   /*! \copydoc{su_bits_clear()} */
   static uz clear(uz x, uz bit) {return su_bits_clear(x, bit);}

   /*! \copydoc{su_bits_test_and_set()} */
   static boole test_and_set(uz *xp, uz bit){
      return su_bits_test_and_set(xp, bit);
   }

   /*! \copydoc{su_bits_test_and_flip()} */
   static boole test_and_flip(uz *xp, uz bit){
      return su_bits_test_and_flip(xp, bit);
   }

   /*! \copydoc{su_bits_test_and_clear()} */
   static boole test_and_clear(uz *xp, uz bit){
      return su_bits_test_and_clear(xp, bit);
   }

   /*! \copydoc{su_bits_find_first_set()} */
   static uz find_first_set(uz x) {return su_bits_find_first_set(x);}

   /*! \copydoc{su_bits_find_last_set()} */
   static uz find_last_set(uz x) {return su_bits_find_last_set(x);}

   /*! \copydoc{su_bits_rotate_left()} */
   static uz rotate_left(uz x, uz bits) {return su_bits_rotate_left(x, bits);}

   /*! \copydoc{su_bits_rotate_right()} */
   static uz rotate_right(uz x, uz bits){
      return su_bits_rotate_right(x, bits);
   }

   /*! \copydoc{su_bits_array_test()} */
   static boole array_test(uz const *xap, uz bit){
      return su_bits_array_test(xap, bit);
   }

   /*! \copydoc{su_bits_array_set()} */
   static void array_set(uz *xap, uz bit) {su_bits_array_set(xap, bit);}

   /*! \copydoc{su_bits_array_flip()} */
   static void array_flip(uz *xap, uz bit) {su_bits_array_flip(xap, bit);}

   /*! \copydoc{su_bits_array_clear()} */
   static void array_clear(uz *xap, uz bit) {su_bits_array_clear(xap, bit);}

   /*! \copydoc{su_bits_array_test_and_set()} */
   static boole array_test_and_set(uz *xap, uz bit){
      return su_bits_array_test_and_set(xap, bit);
   }

   /*! \copydoc{su_bits_array_test_and_flip()} */
   static boole array_test_and_flip(uz *xap, uz bit){
      return su_bits_array_test_and_flip(xap, bit);
   }

   /*! \copydoc{su_bits_array_test_and_clear()} */
   static boole array_test_and_clear(uz *xap, uz bit){
      return su_bits_array_test_and_clear(xap, bit);
   }

#if 0 /* TODO port array_find_first() */
   /*! \copydoc{su_bits_array_find_first_set()} */
   static uz array_find_first_set(uz const *xap, uz xaplen){
      return su_bits_array_find_first_set(xap, xaplen);
   }

   /*! \copydoc{su_bits_array_find_last_set()} */
   static uz array_find_last_set(uz const *xap, uz xaplen){
      return su_bits_array_find_last_set(xap, xaplen);
   }

   /*! \copydoc{su_bits_array_find_first_set_after()} */
   static uz array_find_first_set_after(uz const *xap, uz xaplen, uz startbit){
      return su_bits_array_find_first_set_after(xap, xaplen, startbit);
   }
#endif
};

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_BITS_H */
/* s-it-mode */
