/*@ Implementation of cs.h: basic tools, like copy etc.
 *@ TODO ASM optimization hook (like mem tools).
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
#define su_FILE su_cs_tools
#define su_SOURCE
#define su_SOURCE_CS_TOOLS

#include "su/code.h"

#include "su/cs.h"
#include "su/code-in.h"

sz
su_cs_cmp(char const *cp1, char const *cp2){
   sz rv;
   NYD_IN;
   ASSERT_NYD_EXEC(cp1 != NIL, rv = (cp2 == NIL) ? 0 : -1);
   ASSERT_NYD_EXEC(cp2 != NIL, rv = 1);

   for(;;){
      u8 c1, c2;

      c1 = *cp1++;
      c2 = *cp2++;
      if((rv = c1 - c2) != 0 || c1 == '\0')
         break;
   }
   NYD_OU;
   return rv;
}

sz
su_cs_cmp_n(char const *cp1, char const *cp2, uz n){
   sz rv;
   NYD_IN;
   ASSERT_NYD_EXEC(cp1 != NIL, rv = (cp2 == NIL) ? 0 : -1);
   ASSERT_NYD_EXEC(cp2 != NIL, rv = 1);

   for(rv = 0; n != 0; --n){
      u8 c1, c2;

      c1 = *cp1++;
      c2 = *cp2++;
      if((rv = c1 - c2) != 0 || c1 == '\0')
         break;
   }
   NYD_OU;
   return rv;
}

char *
su_cs_copy_n(char *dst, char const *src, uz n){
   NYD_IN;
   ASSERT_NYD(n == 0 || dst != NIL);
   ASSERT_NYD_EXEC(src != NIL, *dst = '\0');

   if(LIKELY(n > 0)){
      char *cp;

      cp = dst;
      do if((*cp++ = *src++) == '\0')
         goto jleave;
      while(--n > 0);
      *--cp = '\0';
   }
   dst = NIL;
jleave:
   NYD_OU;
   return dst;
}

uz
su_cs_len(char const *cp){
   char const *cp_base;
   NYD_IN;
   ASSERT_NYD_EXEC(cp != NIL, cp_base = cp);

   for(cp_base = cp; *cp != '\0'; ++cp)
      ;
   NYD_OU;
   return P2UZ(cp - cp_base);
}

char *
su_cs_pcopy(char *dst, char const *src){
   NYD_IN;
   ASSERT_NYD(dst != NIL);
   ASSERT_NYD_EXEC(src != NIL, *dst = '\0');

   while((*dst = *src++) != '\0')
      ++dst;
   NYD_OU;
   return dst;
}

char *
su_cs_pcopy_n(char *dst, char const *src, uz n){
   NYD_IN;
   ASSERT_NYD(n == 0 || dst != NIL);
   ASSERT_NYD_EXEC(src != NIL, *dst = '\0');

   if(LIKELY(n > 0)){
      do{
         if((*dst = *src++) == '\0')
            goto jleave;
         ++dst;
      }while(--n > 0);
      *--dst = '\0';
   }
   dst = NIL;
jleave:
   NYD_OU;
   return dst;
}

char *
su_cs_sep_c(char **iolist, char sep, boole ignore_empty){
   char *base, c, *cp;
   NYD_IN;
   ASSERT_NYD_EXEC(iolist != NIL, base = NIL);

   for(base = *iolist; base != NIL; base = *iolist){
      /* Skip WS */
      while((c = *base) != '\0' && su_cs_is_space(c))
         ++base;

      if((cp = su_cs_find_c(base, sep)) != NIL)
         *iolist = &cp[1];
      else{
         *iolist = NIL;
         cp = &base[su_cs_len(base)];
      }

      /* Chop WS */
      while(cp > base && su_cs_is_space(cp[-1]))
         --cp;
      *cp = '\0';

      if(*base != '\0' || !ignore_empty)
         break;
   }
   NYD_OU;
   return base;
}

char *
su_cs_sep_escable_c(char **iolist, char sep, boole ignore_empty){
   char *cp, c, *base;
   boole isesc, anyesc;
   NYD_IN;
   ASSERT_NYD_EXEC(iolist != NIL, base = NIL);

   for(base = *iolist; base != NIL; base = *iolist){
      /* Skip WS */
      while((c = *base) != '\0' && su_cs_is_space(c))
         ++base;

      /* Do not recognize escaped sep characters, keep track of whether we
       * have seen any such tuple along the way */
      for(isesc = anyesc = FAL0, cp = base;; ++cp){
         if(UNLIKELY((c = *cp) == '\0')){
            *iolist = NIL;
            break;
         }else if(!isesc){
            if(c == sep){
               *iolist = &cp[1];
               break;
            }
            isesc = (c == '\\');
         }else{
            isesc = FAL0;
            anyesc |= (c == sep);
         }
      }

      /* Chop WS */
      while(cp > base && su_cs_is_space(cp[-1]))
         --cp;
      *cp = '\0';

      /* Need to strip reverse solidus escaping sep's? */
      if(*base != '\0' && anyesc){
         char *ins;

         for(ins = cp = base;; ++ins)
            if((c = *cp) == '\\' && cp[1] == sep){
               *ins = sep;
               cp += 2;
            }else if((*ins = c) == '\0')
               break;
            else
               ++cp;
      }

      if(*base != '\0' || !ignore_empty)
         break;
   }

   NYD_OU;
   return base;
}

#include "su/code-ou.h"
/* s-it-mode */
