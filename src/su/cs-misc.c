/*@ Implementation of cs.h: anything non-specific, like hashing.
 *
 * Copyright (c) 2017 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_cs_misc
#define su_SOURCE
#define su_SOURCE_CS_MISC

#include "su/code.h"

#include "su/cs.h"
#include "su/code-in.h"

uz
su_cs_hash_cbuf(char const *buf, uz len){
   char c;
   uz h;
   NYD_IN;
   ASSERT_NYD_RET(len == 0 || buf != NIL, h = 0);

   h = 0;
   if(len == UZ_MAX)
      for(; (c = *buf++) != '\0';)
         h = (h * 33) + c;
   else
      while(len-- != 0) /* XXX Duff's device, unroll 8? */
         h = (h * 33) + *buf++;
   NYD_OU;
   return h;
}

uz
su_cs_hash_case_cbuf(char const *buf, uz len){
   char c;
   uz h;
   NYD_IN;
   ASSERT_NYD_RET(len == 0 || buf != NIL, h = 0);

   h = 0;
   if(len == UZ_MAX)
      for(; (c = *buf++) != '\0';){
         c = su_cs_to_lower(c);
         h = (h * 33) + c;
      }
   else
      while(len-- != 0){
         c = su_cs_to_lower(*buf++);
         h = (h * 33) + c;
      }
   NYD_OU;
   return h;
}

#include "su/code-ou.h"
/* s-it-mode */
