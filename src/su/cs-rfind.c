/*@ Implementation of cs.h: reverse finding related things.
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
#define su_FILE su_cs_rfind
#define su_SOURCE
#define su_SOURCE_CS_RFIND

#include "su/code.h"

#include "su/cs.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

boole
su_cs_ends_with_case(char const *cp, char const *xp){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(cp != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(xp != NIL, rv = FAL0);

   rv = FAL0;

   if(LIKELY(*xp != '\0')){
      uz cl, xl;

      cl = su_cs_len(cp);
      xl = su_cs_len(xp);

      if(cl < xl)
         goto jleave;

      for(cp += cl - xl;; ++cp, ++xp){
         char c, xc;

         if((c = su_cs_to_lower(*cp)) != (xc = su_cs_to_lower(*xp)))
            break;

         if(xc == '\0'){
            rv = TRU1;
            break;
         }
      }
   }

jleave:
   NYD_OU;
   return rv;
}

char *
su_cs_rfind_c(char const *cp, char x){
   char const *match, *tail;
   NYD_IN;
   ASSERT_NYD_EXEC(cp != NIL, match = NIL);

   for(match = NIL, tail = cp;; ++tail){
      char c;

      if((c = *tail) == x)
         match = tail;
      if(c == '\0')
         break;
   }

   NYD_OU;
   return UNCONST(char*,match);
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_RFIND
/* s-it-mode */
