/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of random.h.
 *
 * Copyright (c) 2015 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE random
#define mx_SOURCE
#define mx_SOURCE_RANDOM

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/boswap.h>
#include <su/mem.h>
#include <su/random.h>
#include <su/time.h>

#include "mx/compat.h"
#include "mx/mime-enc.h"

#include "mx/random.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

static struct su_random a_random; /* xxx dbg onexit dtor */

char *
mx_random_create_buf(char *dat, uz len, u32 *reprocnt_or_nil){
   struct str b64;
   char *indat, *cp, *oudat;
   uz i, inlen, oulen;
   boole really;
   NYD_IN;

   really = (!su_state_has(su_STATE_REPRODUCIBLE) || reprocnt_or_nil == NIL);

   if(really && a_random.rm_type == su_RANDOM_TYPE_NONE){
      /* (su_state_create() ok..) */
      (void)su_random_create(&a_random, su_RANDOM_TYPE_P, su_STATE_ERR_NOPASS);
      (void)su_random_seed(&a_random, NIL);
   }

   /* We use our base64 encoder with _NOPAD set, so ensure the encoded result
    * with PAD stripped is still longer than what the user requests, easy way.
    * The relation of base64 is fixed 3 in = 4 out; give some pad */
   i = len;
   if((inlen = i % 3) != 0)
      i += 3 - inlen;
   for(;;){
      inlen = i >> 2;
      oulen = inlen << 2;
      if(oulen >= len)
         break;
      i += 3;
   }
   inlen = inlen + (inlen << 1);

   indat = su_LOFI_ALLOC(inlen +1);

   if(really)
      su_random_generate(&a_random, indat, inlen);
   else{
      for(cp = indat, i = inlen; i > 0;){
         uz j;
         union {u32 i4; char c[4];} r;

         r.i4 = su_boswap_little_32(++*reprocnt_or_nil);
         switch((j = i & 3)){
         case 0: cp[3] = r.c[3]; j = 4; /* FALLTHRU */
         case 3: cp[2] = r.c[2]; /* FALLTHRU */
         case 2: cp[1] = r.c[1]; /* FALLTHRU */
         default: cp[0] = r.c[0]; break;
         }
         cp += j;
         i -= j;
      }
   }

   oudat = (len >= oulen) ? dat : su_LOFI_ALLOC(oulen +1);

   b64.s = oudat;
   mx_b64_enc_buf(&b64, indat, inlen,
      mx_B64_BUF | mx_B64_RFC4648URL | mx_B64_NOPAD);
   ASSERT(b64.l >= len);
   su_mem_move(dat, b64.s, len);
   dat[len] = '\0';

   if(oudat != dat)
      su_LOFI_FREE(oudat);

   su_LOFI_FREE(indat);

   NYD_OU;
   return dat;
}

char *
mx_random_create_cp(uz len, u32 *reprocnt_or_nil){
   char *dat;
   NYD2_IN;

   dat = su_AUTO_ALLOC(len +1);
   dat = mx_random_create_buf(dat, len, reprocnt_or_nil);

   NYD2_OU;
   return dat;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_RANDOM
/* s-it-mode */
