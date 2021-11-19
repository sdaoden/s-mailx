/*@ Implementation of code.h: su_state_create().
 *
 * Copyright (c) 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_core_create
#define su_SOURCE
#define su_SOURCE_CORE_CREATE

#include <su/random.h>

#include "su/code.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

s32
su_state_create(BITENUM_IS(u32,su_state_create_flags) create_flags,
      char const *name_or_nil, uz flags, u32 estate){
   s32 rv;

   if((rv = su_state_create_core(name_or_nil, flags, estate)) != su_STATE_NONE)
      goto jleave;

#undef a_V1
#define a_V1(X) ((X) | su_STATE_CREATE_V1 | su_STATE_CREATE_ALL)

   if((create_flags & a_V1(su_STATE_CREATE_RANDOM)) &&
         (rv = su_random_vsp_install(NIL, estate)) != su_STATE_NONE)
      goto jleave;

#if defined su_HAVE_MD && !defined su_USECASE_MX
   /* C99 */{
      extern s32 su__md_init(u32 estate);

      if((create_flags & a_V1(su_STATE_CREATE_MD)) &&
            (rv = su__md_init(estate)) != su_STATE_NONE)
         goto jleave;
   }
#endif

#undef a_V1

jleave:
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CORE_CREATE
/* s-it-mode */
