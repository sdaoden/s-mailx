/*@ Implementation of cs.h: strong hashing.
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
#define su_FILE su_cs_hash_strong
#define su_SOURCE
#define su_SOURCE_CS_HASH_STRONG

#include "su/code.h"

su_EMPTY_FILE()
#ifdef su_HAVE_MD

#include "su/boswap.h"
#include "su/md-siphash.h"
#include "su/random.h"

#include "su/cs.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

static struct su_siphash a_cshashstrong_t;
static struct su_siphash const *a_cshashstrong_tp; /* XXX dbg exit */

s32
su_cs_hash_strong_setup(struct su_siphash const *tp, u32 estate){
   /* No locking here me thinks */
   u8 buf[su_SIPHASH_KEY_SIZE];
   s32 rv;
   NYD_IN;

   rv = su_STATE_NONE;

   if(tp == NIL){
      if(a_cshashstrong_t.sh_compress_rounds == 0){
         su__glck_gi9r();
         if(a_cshashstrong_t.sh_compress_rounds == 0 &&
               (rv = su_random_builtin_generate(buf, sizeof buf, estate)
                     ) == su_STATE_NONE)
            rv = su_siphash_setup(&a_cshashstrong_t, buf);
         su__gnlck_gi9r();
      }

      tp = &a_cshashstrong_t;
   }

   if(rv == su_STATE_NONE)
      a_cshashstrong_tp = tp;

   NYD_OU;
   return rv;
}

uz
su_cs_hash_strong_cbuf(char const *buf, uz len){
   union {u8 b[su_SIPHASH_DIGEST_SIZE_64]; u64 i64; uz z;} h;
   struct su_siphash sh;
   NYD_IN;
   ASSERT_NYD_EXEC(len == 0 || buf != NIL, h.z = 0);

   if(len == UZ_MAX)
      len = su_cs_len(buf);

   if((h.z = len) > 0){
      if(UNLIKELY(a_cshashstrong_tp == NIL))
         su_cs_hash_strong_setup(NIL, su_STATE_ERR_NOPASS);

      sh = *a_cshashstrong_tp;
      su_siphash_update(&sh, buf, len);
      su_siphash_end(&sh, h.b);

      h.i64 = su_boswap_little_64(h.i64);
      su_32( h.z = ((h.i64 >> 32) & 0xFFFFFFFFul) ^ (h.i64 & 0xFFFFFFFFul); )
   }

   NYD_OU;
   return h.z;
}

uz
su_cs_hash_strong_case_cbuf(char const *buf, uz len){
   char b_base[80], *cp;
   union {u8 b[su_SIPHASH_DIGEST_SIZE_64]; u64 i64; uz z;} h;
   struct su_siphash sh;
   NYD_IN;
   ASSERT_NYD_EXEC(len == 0 || buf != NIL, h.z = 0);

   LCTAV(sizeof(b_base) % su_SIPHASH_BLOCK_SIZE == 0);

   if(len == UZ_MAX)
      len = su_cs_len(buf);

   if((h.z = len) > 0){
      if(UNLIKELY(a_cshashstrong_tp == NIL))
         su_cs_hash_strong_setup(NIL, su_STATE_ERR_NOPASS);

      sh = *a_cshashstrong_tp;
      while(len > 0){
         for(cp = b_base;;){
            *cp++ = su_cs_to_lower(*buf++);
            if(--len == 0 || cp == &b_base[NELEM(b_base)])
               break;
         }
         su_siphash_update(&sh, b_base, S(uz,cp - b_base));
      }
      su_siphash_end(&sh, h.b);

      h.i64 = su_boswap_little_64(h.i64);
      su_32( h.z = ((h.i64 >> 32) & 0xFFFFFFFFul) ^ (h.i64 & 0xFFFFFFFFul); )
   }

   NYD_OU;
   return h.z;
}

#include "su/code-ou.h"
#endif /* su_HAVE_MD */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_HASH_STRONG
/* s-it-mode */
