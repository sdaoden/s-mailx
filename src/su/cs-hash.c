/*@ Implementation of cs.h: ("attackable") hashing.
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
#define su_FILE su_cs_hash
#define su_SOURCE
#define su_SOURCE_CS_HASH

#include "su/code.h"

#include "su/cs.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#define a_CSHASH_HASH(C) \
do{\
   u64 xh = 0;\
\
   if(len == UZ_MAX)\
      for(; (c = *buf++) != '\0';)\
         xh = (xh * 33) + S(u8,C);\
   else\
      while(len-- != 0){ /* XXX Duff's device, unroll 8? */\
         c = *buf++;\
         xh = (xh * 33) + S(u8,C);\
      }\
\
   /* Since mixing matters mostly for pow2 spaced maps, mixing the \
    * lower 32-bit seems to be sufficient (? in practice) */\
   if(xh != 0){\
      xh += xh << 13;\
      xh ^= xh >> 7;\
      xh += xh << 3;\
      xh ^= xh >> 17;\
      xh += xh << 5;\
   }\
   h = S(uz,xh);\
}while(0)

uz
su_cs_hash_cbuf(char const *buf, uz len){
   char c;
   uz h;
   NYD_IN;
   ASSERT_NYD_EXEC(len == 0 || buf != NIL, h = 0);

   a_CSHASH_HASH(c);

   NYD_OU;
   return h;
}

uz
su_cs_hash_case_cbuf(char const *buf, uz len){
   char c;
   uz h;
   NYD_IN;
   ASSERT_NYD_EXEC(len == 0 || buf != NIL, h = 0);

   a_CSHASH_HASH(su_cs_to_lower(c));

   NYD_OU;
   return h;
}

#undef a_CSHASH_HASH

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_HASH
/* s-it-mode */
