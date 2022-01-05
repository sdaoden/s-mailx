/*@ Implementation of utf.h.
 *
 * Copyright (c) 2015 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_utf
#define su_SOURCE
#define su_SOURCE_UTF

#include "su/code.h"

#include "su/utf.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

char const su_utf8_replacer[sizeof su_UTF8_REPLACER] = su_UTF8_REPLACER;

u32
su_utf8_to_32(char const **bdat, uz *blen){
   u32 c, x, x1;
   char const *cp, *cpx;
   uz l, lx;
   NYD_IN;

   lx = l = *blen - 1;
   x = S(u8,*(cp = *bdat));
   cpx = ++cp;

   if(LIKELY(x <= 0x7Fu))
      c = x;
   /* 0xF8, but Unicode guarantees maximum of 0x10FFFFu -> F4 8F BF BF.
    * Unicode 9.0, 3.9, UTF-8, Table 3-7. Well-Formed UTF-8 Byte Sequences */
   else if(LIKELY(x > 0xC0u && x <= 0xF4u)){
      if(LIKELY(x < 0xE0u)){
         if(UNLIKELY(l < 1))
            goto jenobuf;
         --l;

         c = (x &= 0x1Fu);
      }else if(LIKELY(x < 0xF0u)){
         if(UNLIKELY(l < 2))
            goto jenobuf;
         l -= 2;

         x1 = x;
         c = (x &= 0x0Fu);

         /* Second byte constraints */
         x = S(u8,*cp++);
         switch(x1){
         case 0xE0u:
            if(UNLIKELY(x < 0xA0u || x > 0xBFu))
               goto jerr;
            break;
         case 0xEDu:
            if(UNLIKELY(x < 0x80u || x > 0x9Fu))
               goto jerr;
            break;
         default:
            if(UNLIKELY((x & 0xC0u) != 0x80u))
               goto jerr;
            break;
         }
         c <<= 6;
         c |= (x &= 0x3Fu);
      }else{
         if(UNLIKELY(l < 3))
            goto jenobuf;
         l -= 3;

         x1 = x;
         c = (x &= 0x07u);

         /* Third byte constraints */
         x = S(u8,*cp++);
         switch(x1){
         case 0xF0u:
            if(UNLIKELY(x < 0x90u || x > 0xBFu))
               goto jerr;
            break;
         case 0xF4u:
            if(UNLIKELY((x & 0xF0u) != 0x80u)) /* 80..8F */
               goto jerr;
            break;
         default:
            if(UNLIKELY((x & 0xC0u) != 0x80u))
               goto jerr;
            break;
         }
         c <<= 6;
         c |= (x &= 0x3Fu);

         x = S(u8,*cp++);
         if(UNLIKELY((x & 0xC0u) != 0x80u))
            goto jerr;
         c <<= 6;
         c |= (x &= 0x3Fu);
      }

      x = S(u8,*cp++);
      if(UNLIKELY((x & 0xC0u) != 0x80u))
         goto jerr;
      c <<= 6;
      c |= x & 0x3Fu;
   }else
      goto jerr;

   cpx = cp;
   lx = l;
jleave:
   *bdat = cpx;
   *blen = lx;
   NYD_OU;
   return c;
jenobuf:
jerr:
   c = U32_MAX;
   goto jleave;
}

uz
su_utf32_to_8(u32 c, char *bp){
   struct{
      u32 lower_bound;
      u32 upper_bound;
      u8 enc_leader;
      u8 enc_lval;
      u8 dec_leader_mask;
      u8 dec_leader_val_mask;
      u8 dec_bytes_togo;
      u8 cat_index;
      u8 d__ummy[2];
   } const a_cat[] = {
      {0x00000000, 0x00000000, 0x00, 0, 0x00,      0x00,   0, 0, {0,}},
      {0x00000000, 0x0000007F, 0x00, 1, 0x80,      0x7F, 1-1, 1, {0,}},
      {0x00000080, 0x000007FF, 0xC0, 2, 0xE0, 0xFF-0xE0, 2-1, 2, {0,}},
      /* We assume surrogates are U+D800 - U+DFFF, a_cat index 3 */
      /* xxx _from_utf32() simply assumes magic code points for surrogates!
       * xxx (However, should we ever get yet another surrogate range we
       * xxx need to deal with that all over the place anyway? */
      {0x00000800, 0x0000FFFF, 0xE0, 3, 0xF0, 0xFF-0xF0, 3-1, 3, {0,}},
      {0x00010000, 0x0010FFFF, 0xF0, 4, 0xF8, 0xFF-0xF8, 4-1, 4, {0,}},
   }, *catp;
   NYD_IN;

   catp = &a_cat[0];
   if(LIKELY(c <= a_cat[0].upper_bound)) {catp += 0; goto j0;}
   if(LIKELY(c <= a_cat[1].upper_bound)) {catp += 1; goto j1;}
   if(LIKELY(c <= a_cat[2].upper_bound)) {catp += 2; goto j2;}
   if(LIKELY(c <= a_cat[3].upper_bound)){
      /* Surrogates may not be converted (Compatibility rule C10) */
      if(UNLIKELY(c >= 0xD800u && c <= 0xDFFFu))
         goto jerr;
      catp += 3;
      goto j3;
   }
   if(LIKELY(c <= a_cat[4].upper_bound)) {catp += 4; goto j4;}
jerr:
   c = su_UTF32_REPLACER;
   catp += 3;
   goto j3;
j4:
   bp[3] = S(char,0x80u) | S(char,c & 0x3Fu); c >>= 6;
j3:
   bp[2] = S(char,0x80u) | S(char,c & 0x3Fu); c >>= 6;
j2:
   bp[1] = S(char,0x80u) | S(char,c & 0x3Fu); c >>= 6;
j1:
   bp[0] = S(char,catp->enc_leader) | S(char,c);
j0:
   bp[catp->enc_lval] = '\0';

   NYD_OU;
   return catp->enc_lval;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_UTF
/* s-it-mode */
