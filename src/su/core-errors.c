/*@ Implementation of code.h: error (number) handling.
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
#define su_FILE su_core_errors
#define su_SOURCE
#define su_SOURCE_CORE_ERRORS

#include "su/cs.h"
#include "su/icodec.h"

#include "su/code.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct a_corerr_map{
   u32 cem_hash;     /* Hash of name */
   u32 cem_nameoff;  /* Into a_corerr_names[] */
   u32 cem_docoff;   /* Into a_corerr_docs[] */
   s32 cem_errno;    /* OS errno value for this one */
};

/* Include the constant su-make-errors.sh output */
#include "su/gen-errors.h" /* $(SU_SRCDIR) */

/* And these come in via su/config.h (config-time su-make-errors.sh output) */
static su__ERR_NUMBER_TYPE const a_corerr_no2mapoff[][2] = {
#undef a_X
#define a_X(N,I) {N,I},
su__ERR_NUMBER_TO_MAPOFF
#undef a_X
};

/* Find the descriptive mapping of an error number, or _ERR_INVAL */
static struct a_corerr_map const *a_corerr_map_from_no(s32 eno);

static struct a_corerr_map const *
a_corerr_map_from_no(s32 eno){
   s32 ecmp;
   uz asz;
   su__ERR_NUMBER_TYPE const (*adat)[2], (*tmp)[2];
   struct a_corerr_map const *cemp;
   NYD2_IN;

   cemp = &a_corerr_map[su__ERR_NUMBER_VOIDOFF];

   if(UCMP(z, ABS(eno), <=, S(su__ERR_NUMBER_TYPE,-1))){
      for(adat = a_corerr_no2mapoff, asz = NELEM(a_corerr_no2mapoff);
            asz != 0; asz >>= 1){
         tmp = &adat[asz >> 1];
         if((ecmp = S(s32,S(su__ERR_NUMBER_TYPE,eno) - (*tmp)[0])) == 0){
            cemp = &a_corerr_map[(*tmp)[1]];
            break;
         }
         if(ecmp > 0){
            adat = &tmp[1];
            --asz;
         }
      }
   }

   NYD2_OU;
   return cemp;
}

char const *
su_err_doc(s32 eno){
   char const *rv;
   struct a_corerr_map const *cemp;
   NYD2_IN;

   if(eno == -1)
      eno = su_err_no();

   cemp = a_corerr_map_from_no(eno);
   rv = (
#ifdef su_HAVE_DOCSTRINGS
         !su_state_has(su_STATE_REPRODUCIBLE)
         ? &a_corerr_docs[cemp->cem_docoff] :
#endif
         &a_corerr_names[cemp->cem_nameoff]);

   NYD2_OU;
   return rv;
}

char const *
su_err_name(s32 eno){
   char const *rv;
   struct a_corerr_map const *cemp;
   NYD2_IN;

   if(eno == -1)
      eno = su_err_no();

   cemp = a_corerr_map_from_no(eno);
   rv = &a_corerr_names[cemp->cem_nameoff];

   NYD2_OU;
   return rv;
}

s32
su_err_by_name(char const *name){
   struct a_corerr_map const *cemp;
   u32 hash, i, j, x;
   s32 rv;
   NYD2_IN;

   hash = su_cs_hash_case(name) & U32_MAX;

   for(i = hash % a_CORERR_REV_PRIME, j = 0; j <= a_CORERR_REV_LONGEST; ++j){
      if((x = a_corerr_revmap[i]) == a_CORERR_REV_ILL)
         break;

      cemp = &a_corerr_map[x];
      if(cemp->cem_hash == hash &&
            !su_cs_cmp_case(&a_corerr_names[cemp->cem_nameoff], name)){
         rv = cemp->cem_errno;
         goto jleave;
      }

      if(++i == a_CORERR_REV_PRIME){
#ifdef a_CORERR_REV_WRAPAROUND
         i = 0;
#else
         break;
#endif
      }
   }

   /* Have not found it.  But wait, it could be that the user did, e.g.,
    *    eval echo \$^ERR-$: \$^ERRDOC-$!: \$^ERRNAME-$! */
   if((su_idec_s32_cp(&rv, name, 0, NIL) &
            (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) == su_IDEC_STATE_CONSUMED){
      cemp = a_corerr_map_from_no(rv);
      rv = cemp->cem_errno;
      goto jleave;
   }

   rv = a_corerr_map[su__ERR_NUMBER_VOIDOFF].cem_errno;
jleave:
   NYD2_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CORE_ERRORS
/* s-it-mode */
