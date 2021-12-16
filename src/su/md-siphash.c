/*@ Implementation of md-siphash.h.
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
#define su_FILE su_md_siphash
#define su_SOURCE
#define su_SOURCE_MD_SIPHASH

#include "su/code.h"

su_EMPTY_FILE()
#ifdef su_HAVE_MD

#include "su/mem.h"

#include "su/md-siphash.h"
#include "su/y-md-siphash.h" /* $(SU_SRCDIR) */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#include <su/y-md-siphash.h> /* 2. */

s32
su_siphash_setup_custom(struct su_siphash *self, void const *key,
      enum su_siphash_digest digest_size, u8 crounds, u8 drounds){
   s32 rv;
   NYD_IN;
   ASSERT(self);

   if(crounds == 0)
      crounds = su__SIPHASH_DEFAULT_CROUNDS;
   if(drounds == 0)
      drounds = su__SIPHASH_DEFAULT_DROUNDS;

   FIELD_RANGE_ZERO(struct su_siphash,self, sh_carry_size,sh_v3);
   self->sh_digest = digest_size;
   self->sh_compress_rounds = crounds;
   self->sh_finalize_rounds = drounds;

   ASSERT_NYD_EXEC(key != NIL, rv = -su_ERR_FAULT);

   rv = a_md_siphash_setup(self, key);

   NYD_OU;
   return rv;
}

void
su_siphash_update(struct su_siphash *self, void const *dat, uz dat_len){
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD(dat_len == 0 || dat != NIL);

   if(dat_len > 0)
      a_md_siphash_update(self, dat, dat_len);

   NYD_OU;
}

void
su_siphash_end(struct su_siphash *self, void *digest_store){
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD(digest_store != NIL);

   a_md_siphash_end(self, S(u8*,digest_store));

   NYD_OU;
}

void
su_siphash_once(void *digest_store, enum su_siphash_digest digest_type,
      void const *key, void const *dat, uz dat_len, u8 crounds, u8 drounds){
   NYD_IN;
   ASSERT_NYD(digest_store != NIL);
   DVLDBG( su_mem_set(digest_store, 0,
      (digest_type == su_SIPHASH_DIGEST_64 ? 8 : 16)); )
   ASSERT_NYD(key != NIL);
   ASSERT_NYD(dat_len == 0 || dat != NIL);

   if(crounds == 0)
      crounds = su__SIPHASH_DEFAULT_CROUNDS;
   if(drounds == 0)
      drounds = su__SIPHASH_DEFAULT_DROUNDS;

   a_md_siphash(dat, dat_len, key,
      digest_store,
      (digest_type == su_SIPHASH_DIGEST_64 ? su_SIPHASH_DIGEST_SIZE_64
         : su_SIPHASH_DIGEST_SIZE_128),
      crounds, drounds);

   NYD_OU;
}

#include <su/y-md-siphash.h> /* 3. */

#include "su/code-ou.h"
#endif /* su_HAVE_MD */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_MD_SIPHASH
/* s-it-mode */
