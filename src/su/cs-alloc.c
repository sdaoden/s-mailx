/*@ Implementation of cs.h: anything which performs allocations.
 *
 * Copyright (c) 2017 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_cs_alloc
#define su_SOURCE
#define su_SOURCE_CS_ALLOC

#include "su/code.h"
#include "su/mem.h"

#include "su/cs.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

char *
su_cs_dup_cbuf(char const *buf, uz len, u32 estate){
   char *rv;
   NYD_IN;
   ASSERT_EXEC(len == 0 || buf != NIL, len = 0);

   if(len == UZ_MAX)
      len = su_cs_len(buf);
   estate &= su_STATE_ERR_MASK;

   if(LIKELY(len != UZ_MAX)){
      rv = S(char*,su_ALLOCATE(1, len +1, estate));
      if(LIKELY(rv != NIL)){
         if(len > 0)
            su_mem_copy(rv, buf, len);
         rv[len] = '\0';
      }
   }else{
      su_state_err(su_STATE_ERR_OVERFLOW, estate,
            _("SU cs_dup_cbuf: buffer too large"));
      rv = NIL;
   }

   NYD_OU;
   return rv;
}

char *
su_cs_dup(char const *cp, u32 estate){
   char *rv;
   uz l;
   NYD_IN;
   ASSERT_EXEC(cp != NIL, cp = su_empty);

   estate &= su_STATE_ERR_MASK;
   l = su_cs_len(cp);

   if(LIKELY(l != UZ_MAX)){
      ++l;
      if(LIKELY((rv = S(char*,su_ALLOCATE(1, l, estate))) != NIL))
         su_mem_copy(rv, cp, l);
   }else{
      su_state_err(su_STATE_ERR_OVERFLOW, estate,
            _("SU cs_dup: string too long"));
      rv = NIL;
   }

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_ALLOC
/* s-it-mode */
