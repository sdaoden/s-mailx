/*@ Implementation of mem.h: utility funs: generic implementations.
 *
 * Copyright (c) 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#include <string.h> /* TODO unroll them in C */

#include "su/code-in.h"

/**/
su_SINLINE void *a_memt_find(void const *vp, s32 what, uz len);
su_SINLINE void *a_memt_rfind(void const *vp, s32 what, uz len);
su_SINLINE sz a_memt_cmp(void const *vpa, void const *vpb, uz len);
su_SINLINE void *a_memt_copy(void *vp, void const *src, uz len);
su_SINLINE void *a_memt_move(void *vp, void const *src, uz len);
su_SINLINE void *a_memt_set(void *vp, s32 what, uz len);

su_SINLINE void *
a_memt_find(void const *vp, s32 what, uz len){
   return memchr(vp, what, len);
}

su_SINLINE void *
a_memt_rfind(void const *vp, s32 what, uz len){
   u8 *rv;

   for(rv = &C(u8*,S(u8 const*,vp))[len];;){
      ASSERT(&rv[-1] >= S(u8 const*,vp));
      if(*--rv == S(u8,what))
         break;
      if(UNLIKELY(rv == vp)){
         rv = NIL;
         break;
      }
   }
   return rv;
}

su_SINLINE sz
a_memt_cmp(void const *vpa, void const *vpb, uz len){
   return memcmp(vpa, vpb, len);
}

su_SINLINE void *
a_memt_copy(void *vp, void const *src, uz len){
   memcpy(vp, src, len);
   return vp;
}

su_SINLINE void *
a_memt_move(void *vp, void const *src, uz len){
   memmove(vp, src, len);
   return vp;
}

su_SINLINE void *
a_memt_set(void *vp, s32 what, uz len){
   memset(vp, what, len);
   return vp;
}

#include "su/code-ou.h"
/* s-it-mode */
