/*@ Implementation of cs.h: finding related things.
 *@ TODO Optimize (even asm hooks?)
 *
 * Copyright (c) 2017 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE su_cs_find
#define su_SOURCE
#define su_SOURCE_CS_FIND

#include "su/code.h"

#include "su/bits.h"
#include "su/mem.h"

#include "su/cs.h"
#include "su/code-in.h"

char *
su_cs_find(char const *cp, char const *xp){
   char xc, c;
   NYD_IN;
   ASSERT_NYD(cp != NIL);
   ASSERT_NYD_EXEC(xp != NIL, cp = NIL);

   /* Return cp if x is empty */
   if(LIKELY((xc = *xp) != '\0')){
      for(; (c = *cp) != '\0'; ++cp){
         if(c == xc && su_cs_starts_with(cp, xp))
            goto jleave;
      }
      cp = NIL;
   }
jleave:
   NYD_OU;
   return UNCONST(char*,cp);
}

char *
su_cs_find_c(char const *cp, char xc){
   NYD_IN;
   ASSERT_NYD(cp != NIL);

   for(;; ++cp){
      char c;

      if((c = *cp) == xc)
         break;
      if(c == '\0'){
         cp = NIL;
         break;
      }
   }
   NYD_OU;
   return UNCONST(char*,cp);
}

char *
su_cs_find_case(char const *cp, char const *xp){
   char xc, c;
   NYD_IN;
   ASSERT_NYD(cp != NIL);
   ASSERT_NYD_EXEC(xp != NIL, cp = NIL);

   /* Return cp if xp is empty */
   if(LIKELY((xc = *xp) != '\0')){
      xc = su_cs_to_lower(xc);
      for(; (c = *cp) != '\0'; ++cp){
         c = su_cs_to_lower(c);
         if(c == xc && su_cs_starts_with_case(cp, xp))
            goto jleave;
      }
      cp = NIL;
   }
jleave:
   NYD_OU;
   return UNCONST(char*,cp);
}

uz
su_cs_first_of_cbuf_cbuf(char const *cp, uz cplen, char const *xp, uz xlen){
   /* TODO (first|last)_(not_)?of: */
   uz rv, bs[su_BITS_TO_UZ(U8_MAX + 1)];
   char c;
   NYD_IN;
   ASSERT_NYD_EXEC(cplen == 0 || cp != NIL, rv = UZ_MAX);
   ASSERT_NYD_EXEC(xlen == 0 || xp != NIL, rv = UZ_MAX);

   su_mem_set(bs, 0, sizeof bs);

   /* For all bytes in x, set the bit of value */
   for(rv = P2UZ(xp);; ++xp){
      if(xlen-- == 0 || (c = *xp) == '\0')
         break;
      su_bits_array_set(bs, S(u8,c));
   }
   if(UNLIKELY(rv == P2UZ(xp)))
      goto jnope;

   /* For all bytes in cp, test whether the value bit is set */
   for(xp = cp;; ++cp){
      if(cplen-- == 0 || (c = *cp) == '\0')
         break;
      if(su_bits_array_test(bs, S(u8,c))){
         rv = P2UZ(cp - xp);
         goto jleave;
      }
   }

jnope:
   rv = UZ_MAX;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with(char const *cp, char const *xp){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(cp != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(xp != NIL, rv = FAL0);

   if(LIKELY(*xp != '\0'))
      for(rv = TRU1;; ++cp, ++xp){
         char xc, c;

         if((xc = *xp) == '\0')
            goto jleave;
         if((c = *cp) != xc)
            break;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with_n(char const *cp, char const *xp, uz n){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(n == 0 || cp != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(n == 0 || xp != NIL, rv = FAL0);

   if(LIKELY(n > 0 && *xp != '\0'))
      for(rv = TRU1;; ++cp, ++xp){
         char xc, c;

         if((xc = *xp) == '\0')
            goto jleave;
         if((c = *cp) != xc)
            break;
         if(--n == 0)
            goto jleave;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with_case(char const *cp, char const *xp){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(cp != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(xp != NIL, rv = FAL0);

   if(LIKELY(*xp != '\0'))
      for(rv = TRU1;; ++cp, ++xp){
         char xc, c;

         if((xc = *xp) == '\0')
            goto jleave;
         xc = su_cs_to_lower(*xp);
         if((c = su_cs_to_lower(*cp)) != xc)
            break;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with_case_n(char const *cp, char const *xp, uz n){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(n == 0 || cp != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(n == 0 || xp != NIL, rv = FAL0);

   if(LIKELY(n > 0 && *xp != '\0'))
      for(rv = TRU1;; ++cp, ++xp){
         char xc, c;

         if((xc = *xp) == '\0')
            goto jleave;
         xc = su_cs_to_lower(*xp);
         if((c = su_cs_to_lower(*cp)) != xc)
            break;
         if(--n == 0)
            goto jleave;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
