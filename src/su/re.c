/*@ Implementation of re.h.
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
#undef su_FILE
#define su_FILE su_re
#define su_SOURCE
#define su_SOURCE_RE

#include "su/code.h"

su_EMPTY_FILE()
#ifdef su_HAVE_RE

#include <regex.h>

#include "su/cs.h"
#include "su/mem.h"

#include "su/re.h"
#include "su/code-in.h"

/* Reuse our sufficiently spaced match buffer for interaction with underlaying
 * engine: either pass it directly on equal size of match structure, otherwise
 * pass it at &[>base] and copy result backwards */
#define a_RE_MATCH_SIZE \
   Z_ALIGN_OVER(MAX(sizeof(struct su_re_match), sizeof(regmatch_t)))

struct su_re *
su__re_reset(struct su_re *self){
   NYD2_IN;
   ASSERT(self->re_super != NIL);

   regfree(S(regex_t*,self->re_super));

   su_FREE(self->re_super);

   DBG( su_mem_set(self, 0xAA, sizeof *self); )
   self->re_super = NIL;

   NYD2_OU;
   return self;
}

char const *
su__re_error_doc(u8 error){
   char const *rv;
   NYD_IN;

   switch(error){
#undef a_X
#define a_X(E,S) case E: rv = S; break;
   a_X(su_RE_ERROR_NONE, "none")
   a_X(su_RE_ERROR_BADBR, "Invalid use of back reference")
   a_X(su_RE_ERROR_BADPAT, "Invalid use of pattern such as group or list")
   a_X(su_RE_ERROR_BADRPT,
      "Invalid use of repetition (like using \"*\" as first character)")
   a_X(su_RE_ERROR_BRACE, "Brace imbalanced")
   a_X(su_RE_ERROR_BRACK, "Bracket list imbalanced")
   a_X(su_RE_ERROR_COLLATE, "Invalid collating element")
   a_X(su_RE_ERROR_CTYPE, "Unknown character class name")
   a_X(su_RE_ERROR_ESCAPE, "Trailing backslash")
   a_X(su_RE_ERROR_PAREN, "Parenthesis group imbalanced")
   a_X(su_RE_ERROR_RANGE, "Invalid use of range (invalid endpoint)")
   a_X(su_RE_ERROR_SPACE, "Regular expression routines ran out of memory")
   a_X(su_RE_ERROR_SUBREG, "Invalid reference to subexpression group")
   default:
   a_X(su_RE_ERROR_MISC, "Non-specific failure, like pattern space excess")
#undef a_X
   }

   NYD_OU;
   return rv;
}

BITENUM_IS(u8,su_re_errors)
su_re_setup_cp(struct su_re *self, char const *expr,
      BITENUM_IS(u8,su_re_setup_flags) flags){
   regex_t re;
   uz i;
   int ef;
   NYD_IN;
   ASSERT(self);

   if(self->re_super != NIL)
      self = su__re_reset(self);

   self->re_setup_flags = flags;

   ASSERT_INJ( self->re_error = su_RE_ERROR_MISC; )
   ASSERT_NYD(expr != NIL);

   ef = 0;
   if(flags & su_RE_SETUP_EXT)
      ef |= REG_EXTENDED;
   if(flags & su_RE_SETUP_ICASE)
      ef |= REG_ICASE;
   if(flags & su_RE_SETUP_NONL)
      ef |= REG_NEWLINE;
   if(flags & su_RE_SETUP_TEST_ONLY)
      ef |= REG_NOSUB;

   if((ef = regcomp(&re, expr, ef)) != 0){
      u8 y;

      switch(ef){
#undef a_X
#define a_X(E,XE) case E: y = XE; break;
      a_X(REG_BADBR, su_RE_ERROR_BADBR)
      a_X(REG_BADPAT, su_RE_ERROR_BADPAT)
      a_X(REG_BADRPT, su_RE_ERROR_BADRPT)
      a_X(REG_EBRACE, su_RE_ERROR_BRACE)
      a_X(REG_EBRACK, su_RE_ERROR_BRACK)
      a_X(REG_ECOLLATE, su_RE_ERROR_COLLATE)
      a_X(REG_ECTYPE, su_RE_ERROR_CTYPE)
      a_X(REG_EESCAPE, su_RE_ERROR_ESCAPE)
      a_X(REG_EPAREN, su_RE_ERROR_PAREN)
      a_X(REG_ERANGE, su_RE_ERROR_RANGE)
      a_X(REG_ESPACE, su_RE_ERROR_SPACE)
      a_X(REG_ESUBREG, su_RE_ERROR_SUBREG)
#undef a_X
      default: y = su_RE_ERROR_MISC; break;
      }

      self->re_error = y;
      goto jleave;
   }
   self->re_error = su_RE_ERROR_NONE;

   if(flags & su_RE_SETUP_TEST_ONLY){
      self->re_group_count = 0;
      self->re_match = NIL;
      i = 0;
   }else{
      /* One for [0], one for copying, see a_RE_MATCH_SIZE */
      i = (self->re_group_count = re.re_nsub) + 1 + 1;
      i *= a_RE_MATCH_SIZE;
   }

   i += Z_ALIGN_OVER(sizeof(regex_t));
   if((self->re_super = su_ALLOCATE(i, 1, su_MEM_ALLOC_MAYFAIL)) != NIL){
      self->re_match = S(struct su_re_match*,S(void*,
            &S(u8*,self->re_super)[Z_ALIGN_OVER(sizeof(regex_t))]));
      /* xxx It seems hacky to think copying works everywhere */
      su_mem_copy(self->re_super, &re, sizeof(re));
   }else{
      regfree(&re);
      self->re_error = su_RE_ERROR_SPACE;
   }

jleave:
   NYD_OU;
   return self->re_error;
}

boole
su_re_eval_cp(struct su_re *self, char const *input,
      BITENUM_IS(u8,su_re_eval_flags) flags){
   boole const direct_use = sizeof(struct su_re_match) == sizeof(regmatch_t) &&
         FIELD_SIZEOF(struct su_re_match,rem_start) ==
            FIELD_SIZEOF(regmatch_t,rm_so) &&
         FIELD_SIZEOF(struct su_re_match,rem_end) ==
            FIELD_SIZEOF(regmatch_t,rm_eo);
   regmatch_t *remtp;
   int ef;
   boole rv;
   NYD_IN;
   ASSERT(self);
   /* .. */

   rv = FAL0;
   self->re_eval_flags = flags;
   self->re_eval_ok = FAL0;
   self->re_input = input;

   ASSERT_NYD(su_re_is_setup(self));
   ASSERT_NYD(input != NIL);

   ef = 0;
   if(flags & su_RE_EVAL_NOTBOL)
      ef |= REG_NOTBOL;
   if(flags & su_RE_EVAL_NOTEOL)
      ef |= REG_NOTEOL;

   /* If we can use su_re_match and regmatch_t interchangeably, do it */
   if(direct_use)
      remtp = R(regmatch_t*,self->re_match);
   else{
      uz i;
      u8 *p;

      p = S(u8*,self->re_match);
      i = self->re_group_count + 1 + 1;
      p += a_RE_MATCH_SIZE * i;
      p -= sizeof(regmatch_t) * --i;
      remtp = R(regmatch_t*,S(void*,p));
   }

   if((rv = (regexec(S(regex_t*,self->re_super), input,
         ((self->re_setup_flags & su_RE_SETUP_TEST_ONLY)
            ? 0 : self->re_group_count + 1), remtp, ef) == 0))){
      self->re_eval_ok = TRU1;

      if(!direct_use && !(self->re_setup_flags & su_RE_SETUP_TEST_ONLY)){
         uz i;
         struct su_re_match *remp;

         for(i = self->re_group_count, remp = self->re_match;;
               ++remp, ++remtp){
            remp->rem_start = remtp->rm_so;
            remp->rem_end = remtp->rm_eo;
            if(i-- == 0)
               break;
         }
      }
   }

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* su_HAVE_RE */
/* s-it-mode */
