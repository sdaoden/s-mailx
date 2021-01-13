/*@ Implementation of sort.h.
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
#define su_FILE su_sort
#define su_SOURCE
#define su_SOURCE_SORT

#include "su/code.h"

#include "su/sort.h"
#include "su/code-in.h"

void
su_sort_shell_vpp(void const **arr, uz entries, su_compare_fun cmp_or_nil){
   void const **vpp, *vpa, *vpb;
   sz j, tmp;
   uz gap, i;
   NYD_IN;
   ASSERT_NYD(entries == 0 || arr != NIL);

   for(gap = entries; (gap >>= 1) != 0;){
      for(i = gap; i < entries; ++i){
         for(j = S(sz,i - gap); j >= 0; j -= gap){
            vpp = &arr[j];
            vpb = vpp[0];
            vpa = vpp[gap];
            tmp = S(sz,P2UZ(vpa) - P2UZ(vpb));

            if(tmp == 0)
               break;
            if(cmp_or_nil != NIL && vpa != NIL && vpb != NIL)
               tmp = (*cmp_or_nil)(vpa, vpb);
            if(tmp >= 0)
               break;

            vpp[0] = vpa;
            vpp[gap] = vpb;
         }
      }
   }

   NYD_OU;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_SORT
/* s-it-mode */
