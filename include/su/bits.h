/*@ Bit operations. TODO asm optimizations
 *
 * Copyright (c) 2001 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
#define su_BITS_ROUNDUP(BITS) (((BITS) + (su_UZ_BITS - 1)) & ~(su_UZ_BITS - 1))
#define su_BITS_TO_UZ(BITS) (su_BITS_ROUNDUP(BITS) / su_UZ_BITS)
#define su_BITS_WHICH_OFF(BIT) ((BIT) / su_UZ_BITS)
#define su_BITS_WHICH_BIT(BIT) ((BIT) & (su_UZ_BITS - 1))
#define su_BITS_TOP_OFF(BITS) (su_BITS_TO_UZ(BITS) - 1)
#define su_BITS_TOP_BITNO(BITS) (su_UZ_BITS - (su_BITS_ROUNDUP(BITS) - (BITS)))
#define su_BITS_TOP_MASK(BITS) (su_UZ_MAX >> (su_BITS_ROUNDUP(BITS) - (BITS)))
#define su_BITS_RANGE_MASK(LO,HI) su_BITENUM_MASK(LO, HI)
INLINE boole su_bits_test(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, FAL0);
   return ((x & (1lu << bit)) != 0);
}
INLINE uz su_bits_set(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, x);
   return (x | (1lu << bit));
}
INLINE uz su_bits_flip(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, x);
   return (x ^ (1lu << bit));
}
INLINE uz su_bits_clear(uz x, uz bit){
   ASSERT_RET(bit < UZ_BITS, x);
   return (x & ~(1lu << bit));
}
INLINE boole su_bits_test_and_set(uz *xp, uz bit){
   boole rv;
   ASSERT_RET(xp != NIL, FAL0);
   ASSERT_RET(bit < UZ_BITS, FAL0);
   bit = 1lu << bit;
   rv = ((*xp & bit) != 0);
   *xp |= bit;
   return rv;
}
INLINE boole su_bits_test_and_flip(uz *xp, uz bit){
   boole rv;
   ASSERT_RET(xp != NIL, FAL0);
   ASSERT_RET(bit < UZ_BITS, FAL0);
   bit = 1lu << bit;
   rv = ((*xp & bit) != 0);
   *xp ^= bit;
   return rv;
}
INLINE boole su_bits_test_and_clear(uz *xp, uz bit){
   boole rv;
   ASSERT_RET(xp != NIL, FAL0);
   ASSERT_RET(bit < UZ_BITS, FAL0);
   bit = 1lu << bit;
   rv = ((*xp & bit) != 0);
   *xp &= ~bit;
   return rv;
}
INLINE uz su_bits_find_first_set(uz x){
   uz i = 0;
   if(x != 0)
      do if(x & 1)
         return i;
      while((++i, x >>= 1));
   return UZ_MAX;
}
INLINE uz su_bits_find_last_set(uz x){
   if(x != 0){
      uz i = UZ_BITS - 1;
      do if(x & (1lu << i))
         return i;
      while(i--);
   }
   return UZ_MAX;
}
INLINE uz su_bits_rotate_left(uz x, uz bits){
   ASSERT_RET(bits < UZ_BITS, x);
   return ((x << bits) | (x >> (UZ_BITS - bits)));
}
INLINE uz su_bits_rotate_right(uz x, uz bits){
   ASSERT_RET(bits < UZ_BITS, x);
   return ((x >> bits) | (x << (UZ_BITS - bits)));
}
INLINE boole su_bits_array_test(uz const *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   return su_bits_test(xap[su_BITS_WHICH_OFF(bit)], su_BITS_WHICH_BIT(bit));
}
INLINE void su_bits_array_set(uz *xap, uz bit){
   ASSERT_RET_VOID(xap != NIL);
   xap += su_BITS_WHICH_OFF(bit);
   *xap = su_bits_set(*xap, su_BITS_WHICH_BIT(bit));
}
INLINE void su_bits_array_flip(uz *xap, uz bit){
   ASSERT_RET_VOID(xap != NIL);
   xap += su_BITS_WHICH_OFF(bit);
   *xap = su_bits_flip(*xap, su_BITS_WHICH_BIT(bit));
}
INLINE void su_bits_array_clear(uz *xap, uz bit){
   ASSERT_RET_VOID(xap != NIL);
   xap += su_BITS_WHICH_OFF(bit);
   *xap = su_bits_clear(*xap, su_BITS_WHICH_BIT(bit));
}
INLINE boole su_bits_array_test_and_set(uz *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   xap += su_BITS_WHICH_OFF(bit);
   return su_bits_test_and_set(xap, su_BITS_WHICH_BIT(bit));
}
INLINE boole su_bits_array_test_and_flip(uz *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   xap += su_BITS_WHICH_OFF(bit);
   return su_bits_test_and_flip(xap, su_BITS_WHICH_BIT(bit));
}
INLINE boole su_bits_array_test_and_clear(uz *xap, uz bit){
   ASSERT_RET(xap != NIL, FAL0);
   xap += su_BITS_WHICH_OFF(bit);
   return su_bits_test_and_clear(xap, su_BITS_WHICH_BIT(bit));
}
#if 0 /* TODO port array_find_first() */
EXTERN uz su_bits_array_find_first_set(uz const *xap, uz xaplen);
EXTERN uz su_bits_array_find_last_set(uz const *xap, uz xaplen);
EXTERN uz su_bits_array_find_first_set_after(uz const *xap, uz xaplen,
      uz startbit);
#endif
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)
class bits;
class bits{
public:
   static boole test(uz x, uz bit) {return su_bits_test(x, bit);}
   static uz set(uz x, uz bit) {return su_bits_set(x, bit);}
   static uz flip(uz x, uz bit) {return su_bits_flip(x, bit);}
   static uz clear(uz x, uz bit) {return su_bits_clear(x, bit);}
   static boole test_and_set(uz *xp, uz bit){
      return su_bits_test_and_set(xp, bit);
   }
   static boole test_and_flip(uz *xp, uz bit){
      return su_bits_test_and_flip(xp, bit);
   }
   static boole test_and_clear(uz *xp, uz bit){
      return su_bits_test_and_clear(xp, bit);
   }
   static uz find_first_set(uz x) {return su_bits_find_first_set(x);}
   static uz find_last_set(uz x) {return su_bits_find_last_set(x);}
   static uz rotate_left(uz x, uz bits) {return su_bits_rotate_left(x, bits);}
   static uz rotate_right(uz x, uz bits){
      return su_bits_rotate_right(x, bits);
   }
   static boole array_test(uz const *xap, uz bit){
      return su_bits_array_test(xap, bit);
   }
   static void array_set(uz *xap, uz bit) {su_bits_array_set(xap, bit);}
   static void array_flip(uz *xap, uz bit) {su_bits_array_flip(xap, bit);}
   static void array_clear(uz *xap, uz bit) {su_bits_array_clear(xap, bit);}
   static boole array_test_and_set(uz *xap, uz bit){
      return su_bits_array_test_and_set(xap, bit);
   }
   static boole array_test_and_flip(uz *xap, uz bit){
      return su_bits_array_test_and_flip(xap, bit);
   }
   static boole array_test_and_clear(uz *xap, uz bit){
      return su_bits_array_test_and_clear(xap, bit);
   }
#if 0 /* TODO port array_find_first() */
   static uz array_find_first_set(uz const *xap, uz xaplen){
      return su_bits_array_find_first_set(xap, xaplen);
   }
   static uz array_find_last_set(uz const *xap, uz xaplen){
      return su_bits_array_find_last_set(xap, xaplen);
   }
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
