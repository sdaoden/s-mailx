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
#undef su_FILE
#define su_FILE random
#define mx_SOURCE
#define mx_SOURCE_RANDOM

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include "mx/random.h"
#if mx_HAVE_RANDOM != mx_RANDOM_IMPL_ARC4 &&\
      mx_HAVE_RANDOM != mx_RANDOM_IMPL_TLS
# define a_RAND_USE_BUILTIN
# if mx_HAVE_RANDOM == mx_RANDOM_IMPL_GETENTROPY
#  include <unistd.h>
# elif mx_HAVE_RANDOM == mx_RANDOM_IMPL_GETRANDOM
#  include mx_RANDOM_GETRANDOM_H
# endif
# ifdef mx_HAVE_SCHED_YIELD
#  include <sched.h>
# endif
#endif

#include <su/mem.h>

#ifdef a_RAND_USE_BUILTIN
# include <su/prime.h>
#endif

#include "mx/compat.h"
#include "mx/mime-enc.h"

/* Already: #include "mx/random.h" */
#include "su/code-in.h"

#ifdef a_RAND_USE_BUILTIN
union a_rand_state{
   struct a_rand_arc4{
      u8 _dat[256];
      u8 _i;
      u8 _j;
      u8 __pad[6];
   } a;
   u8 b8[sizeof(struct a_rand_arc4)];
   u32 b32[sizeof(struct a_rand_arc4) / sizeof(u32)];
};
#endif

#ifdef a_RAND_USE_BUILTIN
static union a_rand_state *a_rand;
#endif

/* Our ARC4 random generator with its completely unacademical pseudo
 * initialization (shall /dev/urandom fail) */
#ifdef a_RAND_USE_BUILTIN
static void a_rand_init(void);
su_SINLINE u8 a_rand_get8(void);
static u32 a_rand_weak(u32 seed);
#endif

#ifdef a_RAND_USE_BUILTIN
static void
a_rand_init(void){
   union {int fd; uz i;} u;
   NYD2_IN;

   a_rand = n_alloc(sizeof *a_rand);

# if mx_HAVE_RANDOM == mx_RANDOM_IMPL_GETENTROPY ||\
      mx_HAVE_RANDOM == mx_RANDOM_IMPL_GETRANDOM
   /* getentropy(3)/getrandom(2) guarantee 256 without su_ERR_INTR..
    * However, support sequential reading to avoid possible hangs that have
    * been reported on the ML (2017-08-22, s-nail/s-mailx freezes when
    * mx_HAVE_GETRANDOM is #defined) */
   LCTA(sizeof(a_rand->a._dat) <= 256,
      "Buffer too large to be served without su_ERR_INTR error");
   LCTA(sizeof(a_rand->a._dat) >= 256,
      "Buffer too small to serve used array indices");
   /* C99 */{
      uz o, i;

      for(o = 0, i = sizeof a_rand->a._dat;;){
         sz gr;

#  if mx_HAVE_RANDOM == mx_RANDOM_IMPL_GETENTROPY
         if((gr = getentropy(&a_rand->a._dat[o], i)) != -1)
            gr = i;
#  else
         gr = mx_RANDOM_GETRANDOM_FUN(&a_rand->a._dat[o], i);
#  endif
         if(gr == -1 && su_err_no() == su_ERR_NOSYS)
            break;
         a_rand->a._i = (a_rand->a._dat[a_rand->a._dat[1] ^
               a_rand->a._dat[84]]);
         a_rand->a._j = (a_rand->a._dat[a_rand->a._dat[65] ^
               a_rand->a._dat[42]]);
         /* ..but be on the safe side */
         if(gr > 0){
            i -= S(uz,gr);
            if(i == 0)
               goto jleave;
            o += S(uz,gr);
         }
         n_err(_("Not enough entropy for the "
            "P(seudo)R(andom)N(umber)G(enerator), waiting a bit\n"));
         n_msleep(250, FAL0);
      }
   }

# elif mx_HAVE_RANDOM == mx_RANDOM_IMPL_URANDOM
   if((u.fd = open("/dev/urandom", O_RDONLY)) != -1){
      boole ok;

      ok = (sizeof(a_rand->a._dat) == S(uz,read(u.fd,
            a_rand->a._dat, sizeof(a_rand->a._dat))));
      close(u.fd);

      a_rand->a._i = (a_rand->a._dat[a_rand->a._dat[1] ^ a_rand->a._dat[84]]);
      a_rand->a._j = (a_rand->a._dat[a_rand->a._dat[65] ^ a_rand->a._dat[42]]);
      if(ok)
         goto jleave;
   }
# elif mx_HAVE_RANDOM != mx_RANDOM_IMPL_BUILTIN
#  error a_rand_init(): the value of mx_HAVE_RANDOM is not supported
# endif

   /* As a fallback, a homebrew seed */
   if(n_poption & n_PO_D_V)
      n_err(_("P(seudo)R(andom)N(umber)G(enerator): "
         "creating homebrew seed\n"));
   /* C99 */{
# ifdef mx_HAVE_CLOCK_GETTIME
      struct timespec ts;
# else
      struct timeval ts;
# endif
      boole slept;
      u32 seed, rnd, t, k;

      /* We first do three rounds, and then add onto that a (cramped) random
       * number of rounds; in between we give up our timeslice once (from our
       * point of view) */
      seed = R(up,a_rand) & U32_MAX;
      rnd = 3;
      slept = FAL0;

      for(;;){
         /* Stir the entire pool once */
         for(u.i = NELEM(a_rand->b32); u.i-- != 0;){

# ifdef mx_HAVE_CLOCK_GETTIME
            clock_gettime(CLOCK_REALTIME, &ts);
            t = S(u32,ts.tv_nsec);
# else
            gettimeofday(&ts, NIL);
            t = S(u32,ts.tv_usec);
# endif
            if(rnd & 1)
               t = (t >> 16) | (t << 16);
            a_rand->b32[u.i] ^= a_rand_weak(seed ^ t);
            a_rand->b32[t % NELEM(a_rand->b32)] ^= seed;
            if(rnd == 7 || rnd == 17)
               a_rand->b32[u.i] ^= a_rand_weak(seed ^ S(u32,ts.tv_sec));
            k = a_rand->b32[u.i] % NELEM(a_rand->b32);
            a_rand->b32[k] ^= a_rand->b32[u.i];
            seed ^= a_rand_weak(a_rand->b32[k]);
            if((rnd & 3) == 3)
               seed ^= su_prime_lookup_next(seed);
         }

         if(--rnd == 0){
            if(slept)
               break;
            rnd = (a_rand_get8() % 5) + 3;
# ifdef mx_HAVE_SCHED_YIELD
            sched_yield();
# elif defined mx_HAVE_NANOSLEEP
            ts.tv_sec = 0, ts.tv_nsec = 0;
            nanosleep(&ts, NIL);
# else
            rnd += 10;
# endif
            slept = TRU1;
         }
      }

      for(u.i = sizeof(a_rand->b8) * ((a_rand_get8() % 5)  + 1);
            u.i != 0; --u.i)
         a_rand_get8();
      goto jleave; /* (avoid unused warning) */
   }
jleave:
   NYD2_OU;
}

su_SINLINE u8
a_rand_get8(void){
   u8 i, j;

   i = a_rand->a._dat[++a_rand->a._i];
   j = a_rand->a._dat[a_rand->a._j += i];
   a_rand->a._dat[a_rand->a._i] = j;
   a_rand->a._dat[a_rand->a._j] = i;
   return a_rand->a._dat[S(u8,i + j)];
}

static u32
a_rand_weak(u32 seed){
   /* From "Random number generators: good ones are hard to find",
    * Park and Miller, Communications of the ACM, vol. 31, no. 10,
    * October 1988, p. 1195.
    * (In fact: FreeBSD 4.7, /usr/src/lib/libc/stdlib/random.c.) */
   u32 hi;

   if(seed == 0)
      seed = 123459876;
   hi =  seed /  127773;
         seed %= 127773;
   seed = (seed * 16807) - (hi * 2836);
   if(S(s32,seed) < 0)
      seed += S32_MAX;
   return seed;
}
#endif /* a_RAND_USE_BUILTIN */

char *
mx_random_create_buf(char *dat, uz len, u32 *reprocnt_or_nil){
   struct str b64;
   char *indat, *cp, *oudat;
   uz i, inlen, oulen;
   NYD_IN;

   if(!(n_psonce & n_PSO_RANDOM_INIT)){
      n_psonce |= n_PSO_RANDOM_INIT;

      if(n_poption & n_PO_D_V){
         char const *prngn;

#if mx_HAVE_RANDOM == mx_RANDOM_IMPL_ARC4
         prngn = "arc4random";
#elif mx_HAVE_RANDOM == mx_RANDOM_IMPL_TLS
         prngn = "*TLS RAND_*";
#elif mx_HAVE_RANDOM == mx_RANDOM_IMPL_GETENTROPY
         prngn = "getentropy(3) + builtin ARC4";
#elif mx_HAVE_RANDOM == mx_RANDOM_IMPL_GETRANDOM
         prngn = "getrandom(2/3) + builtin ARC4";
#elif mx_HAVE_RANDOM == mx_RANDOM_IMPL_URANDOM
         prngn = "/dev/urandom + builtin ARC4";
#elif mx_HAVE_RANDOM == mx_RANDOM_IMPL_BUILTIN
         prngn = "builtin ARC4";
#else
# error mx_random_create_buf(): the value of mx_HAVE_RANDOM is not supported
#endif
         n_err(_("P(seudo)R(andom)N(umber)G(enerator): %s\n"), prngn);
      }

#ifdef a_RAND_USE_BUILTIN
      a_rand_init();
#endif
   }

   /* We use our base64 encoder with _NOPAD set, so ensure the encoded result
    * with PAD stripped is still longer than what the user requests, easy way.
    * The relation of base64 is fixed 3 in = 4 out, and we do not want to
    * include the base64 PAD characters in our random string: give some pad */
   i = len;
   if((inlen = i % 3) != 0)
      i += 3 - inlen;
jinc1:
   inlen = i >> 2;
   oulen = inlen << 2;
   if(oulen < len){
      i += 3;
      goto jinc1;
   }
   inlen = inlen + (inlen << 1);

   indat = n_lofi_alloc(inlen +1);

   if(!su_state_has(su_STATE_REPRODUCIBLE) || reprocnt_or_nil == NIL){
#if mx_HAVE_RANDOM == mx_RANDOM_IMPL_TLS
      mx_tls_rand_bytes(indat, inlen);
#elif mx_HAVE_RANDOM != mx_RANDOM_IMPL_ARC4
      for(i = inlen; i-- > 0;)
         indat[i] = S(char,a_rand_get8());
#else
      for(cp = indat, i = inlen; i > 0;){
         union {u32 i4; char c[4];} r;
         uz j;

         r.i4 = S(u32,arc4random());
         switch((j = i & 3)){
         case 0: cp[3] = r.c[3]; j = 4; /* FALLTHRU */
         case 3: cp[2] = r.c[2]; /* FALLTHRU */
         case 2: cp[1] = r.c[1]; /* FALLTHRU */
         default: cp[0] = r.c[0]; break;
         }
         cp += j;
         i -= j;
      }
#endif
   }else{
      for(cp = indat, i = inlen; i > 0;){
         union {u32 i4; char c[4];} r;
         uz j;

         r.i4 = ++*reprocnt_or_nil;
         if(su_BOM_IS_BIG()){ /* TODO BSWAP */
            char x;

            x = r.c[0];
            r.c[0] = r.c[3];
            r.c[3] = x;
            x = r.c[1];
            r.c[1] = r.c[2];
            r.c[2] = x;
         }
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

   oudat = (len >= oulen) ? dat : n_lofi_alloc(oulen +1);
   b64.s = oudat;
   mx_b64_enc_buf(&b64, indat, inlen,
      mx_B64_BUF | mx_B64_RFC4648URL | mx_B64_NOPAD);
   ASSERT(b64.l >= len);
   su_mem_copy(dat, b64.s, len);
   dat[len] = '\0';
   if(oudat != dat)
      n_lofi_free(oudat);

   n_lofi_free(indat);

   NYD_OU;
   return dat;
}

char *
mx_random_create_cp(uz len, u32 *reprocnt_or_nil){
   char *dat;
   NYD_IN;

   dat = n_autorec_alloc(len +1);
   dat = mx_random_create_buf(dat, len, reprocnt_or_nil);
   NYD_OU;
   return dat;
}

#include "su/code-ou.h"
/* s-it-mode */
