/*@ Implementation of cs.h: character type and case conversion tables, tools.
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
#define su_FILE su_cs_ctype
#define su_SOURCE
#define su_SOURCE_CS_CTYPE

#include "su/code.h"

#include "su/cs.h"
#include "su/code-in.h"

/* Include the constant su-make-cs-ctype.sh output */
#include "su/gen-cs-ctype.h" /* $(SU_SRCDIR) */

sz
su_cs_cmp_case(char const *cp1, char const *cp2){
   sz rv;
   NYD_IN;
   ASSERT_NYD_EXEC(cp1 != NIL, rv = (cp2 == NIL) ? 0 : -1);
   ASSERT_NYD_EXEC(cp2 != NIL, rv = 1);

   for(;;){
      u8 c1, c2;

      c1 = su_cs_to_lower(*cp1++);
      c2 = su_cs_to_lower(*cp2++);
      if((rv = c1 - c2) != 0 || c1 == '\0')
         break;
   }

   NYD_OU;
   return rv;
}

sz
su_cs_cmp_case_n(char const *cp1, char const *cp2, uz n){
   sz rv;
   NYD_IN;
   ASSERT_NYD_EXEC(cp1 != NIL, rv = (cp2 == NIL) ? 0 : -1);
   ASSERT_NYD_EXEC(cp2 != NIL, rv = 1);

   for(rv = 0; n != 0; --n){
      u8 c1, c2;

      c1 = su_cs_to_lower(*cp1++);
      c2 = su_cs_to_lower(*cp2++);
      if((rv = c1 - c2) != 0 || c1 == '\0')
         break;
   }

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_CTYPE
/* s-it-mode */
