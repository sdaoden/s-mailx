/*@ Implementation of mem.h: utility funs.
 *@ The implementations are in ./y-mem-tools.h:
 *@ - a_memt_FUN().
 *@ - Data is asserted, length cannot be 0.
 *
 * Copyright (c) 2019 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_mem_tools
#define su_SOURCE
#define su_SOURCE_MEM_TOOLS

#include "su/code.h"

#include "su/mem.h"
#include "su/y-mem-tools.h" /* $(SU_SRCDIR) */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

#include <su/y-mem-tools.h> /* 2. */

void *
su_mem_find(void const *vp, s32 what, uz len){
   void *rv;
   NYD_IN;
   ASSERT_NYD_EXEC(len == 0 || vp != NIL, rv = NIL);

   rv = LIKELY(len != 0) ? a_memt_find(vp, what, len) : NIL;

   NYD_OU;
   return rv;
}

void *
su_mem_rfind(void const *vp, s32 what, uz len){
   void *rv;
   NYD_IN;
   ASSERT_NYD_EXEC(len == 0 || vp != NIL, rv = NIL);

   rv = LIKELY(len != 0) ? a_memt_rfind(vp, what, len) : NIL;

   NYD_OU;
   return rv;
}

sz
su_mem_cmp(void const *vpa, void const *vpb, uz len){
   sz rv;
   NYD_IN;
   ASSERT_NYD_EXEC(len == 0 || vpa != NIL, rv = (vpb == NIL) ? 0 : -1);
   ASSERT_NYD_EXEC(len == 0 || vpb != NIL, rv = 1);

   rv = UNLIKELY(vpa == vpb) ? 0
         : LIKELY(len != 0) ? a_memt_cmp(vpa, vpb, len) : 0;

   NYD_OU;
   return rv;
}

void *
su_mem_copy(void *vp, void const *src, uz len){
   NYD_IN;
   ASSERT_NYD(len == 0 || vp != NIL);
   ASSERT_NYD(len == 0 || src != NIL);

   if(LIKELY(vp != src && len > 0)){
      /* dalias: if((up)s-(up)d-n <= -2*n) return memcpy(d, s, n); */
      ASSERT_NYD_EXEC(
         (R(up,src) < R(up,vp) && R(up,src) + len <= R(up,vp)) ||
         (R(up,src) > R(up,vp) && R(up,src) - len >= R(up,vp)),
         su_mem_move(vp, src, len));
      a_memt_copy(vp, src, len);
   }

   NYD_OU;
   return vp;
}

void *
su_mem_move(void *vp, void const *src, uz len){
   NYD_IN;
   ASSERT_NYD(len == 0 || vp != NIL);
   ASSERT_NYD(len == 0 || src != NIL);

   if(LIKELY(vp != src && len > 0))
      a_memt_move(vp, src, len);

   NYD_OU;
   return vp;
}

void *
su_mem_set(void *vp, s32 what, uz len){
   NYD_IN;
   ASSERT_NYD(len == 0 || vp != NIL);

   if(LIKELY(len > 0))
      a_memt_set(vp, what, len);

   NYD_OU;
   return vp;
}

#include <su/y-mem-tools.h> /* 3. */

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_MEM_TOOLS
/* s-it-mode */
