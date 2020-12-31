/*@ Implementation of icodec.h: enc.
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
#undef su_FILE
#define su_FILE su_icodec_enc
#define su_SOURCE
#define su_SOURCE_ICODEC_ENC

#include "su/code.h"

#include "su/cs.h"

#include "su/icodec.h"
#include "su/code-in.h"

/* "Is power-of-two" table, and if, shift (indexed by base-2) */
static u8 const a_icoe_shifts[35] = {
         1, 0, 2, 0, 0, 0, 3, 0,   /*  2 ..  9 */
   0, 0, 0, 0, 0, 0, 4, 0, 0, 0,   /* 10 .. 19 */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* 20 .. 29 */
   0, 0, 5, 0, 0, 0, 0             /* 30 .. 36 */
};

/* XXX itoa byte maps not locale aware.. */
static char const a_icoe_upper[36 +1] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static char const a_icoe_lower[36 +1] = "0123456789abcdefghijklmnopqrstuvwxyz";

char *
su_ienc(char cbuf[su_IENC_BUFFER_SIZE], u64 value, u8 base, u32 ienc_mode){
   enum{a_ISNEG = 1u<<su__IENC_MODE_SHIFT};

   u8 shiftmodu;
   char const *itoa;
   char *rv;
   NYD_IN;

   if(UNLIKELY(base < 2 || base > 36)){
      rv = NIL;
      goto jleave;
   }

   ienc_mode &= su__IENC_MODE_MASK;
   *(rv = &cbuf[su_IENC_BUFFER_SIZE -1]) = '\0';
   itoa = (ienc_mode & su_IENC_MODE_LOWERCASE) ? a_icoe_lower : a_icoe_upper;

   if(S(s64,value) < 0){
      ienc_mode |= a_ISNEG;
      if(ienc_mode & su_IENC_MODE_SIGNED_TYPE){
         /* self->is_negative = TRU1; */
         value = -value;
      }
   }

   if((shiftmodu = a_icoe_shifts[base - 2]) != 0){
      --base; /* convert to mask */
      do{
         *--rv = itoa[value & base];
         value >>= shiftmodu;
      }while(value != 0);

      if(!(ienc_mode & su_IENC_MODE_NO_PREFIX)){
         /* self->before_prefix = cp; */
         if(shiftmodu == 4)
            *--rv = 'x';
         else if(shiftmodu == 1)
            *--rv = 'b';
         else if(shiftmodu != 3){
            ++base; /* Reconvert from mask */
            goto jnumber_sign_prefix;
         }
         *--rv = '0';
      }
   }else{
      do{
         shiftmodu = value % base;
         value /= base;
         *--rv = itoa[shiftmodu];
      }while(value != 0);

      if(!(ienc_mode & su_IENC_MODE_NO_PREFIX) && base != 10){
jnumber_sign_prefix:
         value = base;
         base = 10;
         *--rv = '#';
         do{
            shiftmodu = value % base;
            value /= base;
            *--rv = itoa[shiftmodu];
         }while(value != 0);
      }

      if(ienc_mode & su_IENC_MODE_SIGNED_TYPE){
         char c;

         if(ienc_mode & a_ISNEG)
            c = '-';
         else if(ienc_mode & su_IENC_MODE_SIGNED_PLUS)
            c = '+';
         else if(ienc_mode & su_IENC_MODE_SIGNED_SPACE)
            c = ' ';
         else
            c = '\0';

         if(c != '\0')
            *--rv = c;
      }
   }

jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
