/*@ Implementation of cs.h: the toolboxes.
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
#define su_FILE su_cs_tbox
#define su_SOURCE
#define su_SOURCE_CS_TBOX

#include "su/code.h"

#include "su/mem.h"

#include "su/cs.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/**/
#if DVLOR(1, 0)
static void a_cstbox_free(void *t);
#else
# define a_cstbox_free su_mem_free
#endif

/**/
static void *a_cstbox_assign(void *self, void const *t, u32 estate);
static uz a_cstbox_hash(void const *self);
static uz a_cstbox_hash_case(void const *self);

#if DVLOR(1, 0)
static void
a_cstbox_free(void *t){
   NYD2_IN;

   su_FREE(t);

   NYD2_OU;
}
#endif

static void *
a_cstbox_assign(void *self, void const *t, u32 estate){
   char *rv;
   NYD2_IN;

   if((rv = su_cs_dup(S(char const*,t), estate)) != NIL)
      su_FREE(self);

   NYD2_OU;
   return rv;
}

static uz
a_cstbox_hash(void const *self){
   uz rv;
   NYD2_IN;

   rv = su_cs_hash(S(char const*,self));

   NYD2_OU;
   return rv;
}

static uz
a_cstbox_hash_case(void const *self){
   uz rv;
   NYD2_IN;

   rv = su_cs_hash_case(S(char const*,self));

   NYD2_OU;
   return rv;
}

struct su_toolbox const su_cs_toolbox = su_TOOLBOX_I9R(
   &su_cs_dup, &a_cstbox_free, &a_cstbox_assign,
   &su_cs_cmp, &a_cstbox_hash);

struct su_toolbox const su_cs_toolbox_case = su_TOOLBOX_I9R(
   &su_cs_dup, &a_cstbox_free, &a_cstbox_assign,
   &su_cs_cmp_case, &a_cstbox_hash_case);

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_TBOX
/* s-it-mode */
