/*@ Implementation of icodec.h: idec.
 *
 * Copyright (c) 2017 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_icodec_dec
#define su_SOURCE
#define su_SOURCE_ICODEC_DEC

#include "su/code.h"

#include "su/cs.h"

#include "su/icodec.h"
#include "su/code-in.h"

static u8 const a_icod_atoi[256] = {
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x01,
   0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,
   0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
   0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,
   0x23,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0A,0x0B,0x0C,
   0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,
   0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,
   0x21,0x22,0x23,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

#define a_X(X) (U64_MAX / (X))
static u64 const a_icod_cutlimit[35] = {
   a_X( 2), a_X( 3), a_X( 4), a_X( 5), a_X( 6), a_X( 7), a_X( 8),
   a_X( 9), a_X(10), a_X(11), a_X(12), a_X(13), a_X(14), a_X(15),
   a_X(16), a_X(17), a_X(18), a_X(19), a_X(20), a_X(21), a_X(22),
   a_X(23), a_X(24), a_X(25), a_X(26), a_X(27), a_X(28), a_X(29),
   a_X(30), a_X(31), a_X(32), a_X(33), a_X(34), a_X(35), a_X(36)
};
#undef a_X

u32
su_idec(void *resp, char const *cbuf, uz clen, u8 base, u32 idec_mode,
      char const **endptr_or_nil){
   /* XXX Brute simple and */
   u8 currc;
   u64 res, cut;
   u32 rv;
   NYD_IN;
   ASSERT(resp != NIL);
   ASSERT_EXEC(cbuf != NIL || clen == 0, clen = 0);

   idec_mode &= su__IDEC_MODE_MASK;
   rv = su_IDEC_STATE_NONE | idec_mode;
   res = 0;

   if(clen == UZ_MAX){
      if(*cbuf == '\0')
         goto jeinval;
   }else if(clen == 0)
      goto jeinval;

   ASSERT(base != 1 && base <= 36); /* XXX _RET_VAL! */
   /*if(base == 1 || base > 36)
    *   goto jeinval;*/

jnumber_sign_rescan:
   /* Leading WS */
   while(su_cs_is_space(*cbuf))
      if(*++cbuf == '\0' || --clen == 0)
         goto jeinval;

   /* Check sign */
   switch(*cbuf){
   case '-':
      rv |= su_IDEC_STATE_SEEN_MINUS;
      /* FALLTHROUGH */
   case '+':
      if(*++cbuf == '\0' || --clen == 0)
         goto jeinval;
      break;
   }

   /* Base detection/skip */
   if(*cbuf != '0'){
      if(base == 0){
         base = 10;

         /* Support BASE#number prefix, where BASE is decimal 2-36 XXX ASCII */
         if(clen > 1){
            char c1, c2, c3;

            if(((c1 = cbuf[0]) >= '0' && c1 <= '9') &&
                  (((c2 = cbuf[1]) == '#') ||
                   (c2 >= '0' && c2 <= '9' && clen > 2 && cbuf[2] == '#'))){
               base = a_icod_atoi[S(u8,c1)];
               if(c2 == '#')
                  c3 = cbuf[2];
               else{
                  c3 = cbuf[3];
                  base *= 10; /* xxx Inline atoi decimal base */
                  base += a_icod_atoi[S(u8,c2)];
               }

               /* We do not interpret this as BASE#number at all if either we
                * did not get a valid base or if the first char is not valid
                * according to base, to comply to the latest interpretion of
                * "prefix", see comment for standard prefixes below */
               if(base < 2 || base > 36 ||
                     (a_icod_atoi[S(u8,c3)] >= base &&
                        !(rv & su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN)))
                  base = 10;
               else{
                  if(c2 == '#')
                     clen -= 2, cbuf += 2;
                  else
                     clen -= 3, cbuf += 3;

                  if(rv & su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN)
                     goto jnumber_sign_rescan;
               }
            }
         }
      }

      /* Character must be valid for base */
      currc = a_icod_atoi[S(u8,*cbuf)];
      if(currc >= base)
         goto jeinval;
   }else{
      /* 0 always valid as is, fallback base 10 */
      if(*++cbuf == '\0' || --clen == 0)
         goto jleave;

      /* Base "detection" */
      if(base == 0 || base == 2 || base == 16){
         switch(*cbuf){
         case 'x':
         case 'X':
            if((base & 2) == 0){
               base = 0x10;
               goto jprefix_skip;
            }
            break;
         case 'b':
         case 'B':
            if((base & 16) == 0){
               base = 2; /* 0b10 */
               /* Char after prefix must be valid.  However, after some error
                * in the tor software all libraries (which had to) turned to
                * an interpretation of the C standard which says that the
                * prefix may optionally precede an otherwise valid sequence,
                * which means that "0x" is not a STATE_INVAL error but gives
                * a "0" result with a "STATE_BASE" error and a rest of "x" */
jprefix_skip:
#if 1
               if(clen > 1 && a_icod_atoi[S(u8,cbuf[1])] < base)
                  --clen, ++cbuf;
#else
               if(*++cbuf == '\0' || --clen == 0)
                  goto jeinval;

               /* Character must be valid for base, invalid otherwise */
               currc = a_icod_atoi[S(u8,*cbuf)];
               if(currc >= base)
                  goto jeinval;
#endif
            }
            break;
         default:
            if(base == 0)
               base = 010;
            break;
         }
      }

      /* Character must be valid for base, _EBASE otherwise */
      currc = a_icod_atoi[S(u8,*cbuf)];
      if(currc >= base)
         goto jebase;
   }

   for(cut = a_icod_cutlimit[base - 2];;){
      if(res >= cut){
         if(res == cut){
            res *= base;
            if(res > U64_MAX - currc)
               goto jeover;
            res += currc;
         }else
            goto jeover;
      }else{
         res *= base;
         res += currc;
      }

      if(*++cbuf == '\0' || --clen == 0)
         break;

      currc = a_icod_atoi[S(u8,*cbuf)];
      if(currc >= base)
         goto jebase;
   }

jleave:
   do{
      u64 umask;

      switch(rv & su__IDEC_MODE_LIMIT_MASK){
      case su_IDEC_MODE_LIMIT_8BIT: umask = U8_MAX; break;
      case su_IDEC_MODE_LIMIT_16BIT: umask = U16_MAX; break;
      case su_IDEC_MODE_LIMIT_32BIT: umask = U32_MAX; break;
      default: umask = U64_MAX; break;
      }
      if((rv & su_IDEC_MODE_SIGNED_TYPE) &&
            (!(rv & su_IDEC_MODE_POW2BASE_UNSIGNED) || !IS_POW2(base)))
         umask >>= 1;

      if(res & ~umask){
         if((rv & (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_STATE_SEEN_MINUS)
               ) == (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_STATE_SEEN_MINUS)){
            if(res > umask + 1){
               res = umask << 1;
               res &= ~umask;
            }else{
               res = -res;
               break;
            }
         }else
            res = umask;
         rv |= su_IDEC_STATE_EOVERFLOW;
      }else if(rv & su_IDEC_STATE_SEEN_MINUS)
         res = -res;
   }while(0);

   switch(rv & su__IDEC_MODE_LIMIT_MASK){
   case su_IDEC_MODE_LIMIT_8BIT:
      if(rv & su_IDEC_MODE_SIGNED_TYPE)
         *S(s8*,resp) = S(s8,res);
      else
         *S(u8*,resp) = S(u8,res);
      break;
   case su_IDEC_MODE_LIMIT_16BIT:
      if(rv & su_IDEC_MODE_SIGNED_TYPE)
         *S(s16*,resp) = S(s16,res);
      else
         *S(u16*,resp) = S(u16,res);
      break;
   case su_IDEC_MODE_LIMIT_32BIT:
      if(rv & su_IDEC_MODE_SIGNED_TYPE)
         *S(s32*,resp) = S(s32,res);
      else
         *S(u32*,resp) = S(u32,res);
      break;
   default:
      if(rv & su_IDEC_MODE_SIGNED_TYPE)
         *S(s64*,resp) = S(s64,res);
      else
         *S(u64*,resp) = S(u64,res);
      break;
   }
   if(rv & su_IDEC_MODE_LIMIT_NOERROR)
      rv &= ~su_IDEC_STATE_EOVERFLOW;

   if(endptr_or_nil != NIL)
      *endptr_or_nil = cbuf;
   if(*cbuf == '\0' || clen == 0)
      rv |= su_IDEC_STATE_CONSUMED;
   NYD_OU;
   return rv;

jeinval:
   rv |= su_IDEC_STATE_EINVAL;
   goto j_maxval;
jebase:
   /* Not a base error for terminator and whitespace! */
   if(*cbuf != '\0' && !su_cs_is_space(*cbuf))
      rv |= su_IDEC_STATE_EBASE;
   goto jleave;

jeover:
   /* Overflow error: consume input until bad character or length out */
   for(;;){
      if(*++cbuf == '\0' || --clen == 0)
         break;
      currc = a_icod_atoi[S(u8,*cbuf)];
      if(currc >= base)
         break;
   }

   rv |= su_IDEC_STATE_EOVERFLOW;
j_maxval:
   if(rv & su_IDEC_MODE_SIGNED_TYPE)
      res = (rv & su_IDEC_STATE_SEEN_MINUS) ? S(u64,S64_MIN) : S(u64,S64_MAX);
   else
      res = U64_MAX;
   rv &= ~su_IDEC_STATE_SEEN_MINUS;
   goto jleave;
}

#include "su/code-ou.h"
/* s-it-mode */
