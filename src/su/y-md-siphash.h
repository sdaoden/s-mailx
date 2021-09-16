/*@ SipHash pseudorandom function.
 *@ Taken over from the reference implementation, copyright as below.
 *@
 *@ From https://github.com/veorq/SipHash.git,
 *@ git show 13c0c2c2de60a3652cca02b5c18b5ea8958dce21:siphash.c, modified via:
 *@  sed -E -e \
 *@    's/uint8_t/u8/g
 *@     s/uint32_t/u32/g
 *@     s/uint64_t/u64/g
 *@     s/UINT64_C/U64_C/g
 *@     s/size_t/uz/g
 *@     s/const unsigned char/u8 const/g
 *@     s/return 0/return/g
 *@     s/int siphash/SINLINE void\na_md_siphash/
 *@     s/[[:space:]]+\\/ \\/
 *@     s/assert/ASSERT/g
 *@     /^#ifdef DEBUG/,/^#endif/c #define TRACE'
 *@
 *@ The rest manually:
 *@ - Append ", uz crounds, uz drounds" arguments.
 *@ - Use crounds,drounds not cROUNDS,dROUNDS in the function (and remove test
 *@   for defaults since they _are_ set).
 *@ - Then provide the rest via copy+paste.
 *@ Note: If default != 2-4, adjust!
 *
 * SPDX-License-Identifier: CC0-1.0
 */
/*
   SipHash reference C implementation

   Copyright (c) 2012-2021 Jean-Philippe Aumasson
   <jeanphilippe.aumasson@gmail.com>
   Copyright (c) 2012-2014 Daniel J. Bernstein <djb@cr.yp.to>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along
   with
   this software. If not, see
   <http://creativecommons.org/publicdomain/zero/1.0/>.
 */
#ifndef su__MD_SIPHASH_Y
# define su__MD_SIPHASH_Y 1

#elif su__MD_SIPHASH_Y == 1
# undef su__MD_SIPHASH_Y
# define su__MD_SIPHASH_Y 2

/* >>>PASTE */
/* default: SipHash-2-4 */
#ifndef cROUNDS
#define cROUNDS 2
#endif
#ifndef dROUNDS
#define dROUNDS 4
#endif

#define ROTL(x, b) (u64)(((x) << (b)) | ((x) >> (64 - (b)))) /* XXX bits_.. */

#define U32TO8_LE(p, v) \
    (p)[0] = (u8)((v)); \
    (p)[1] = (u8)((v) >> 8); \
    (p)[2] = (u8)((v) >> 16); \
    (p)[3] = (u8)((v) >> 24);

#define U64TO8_LE(p, v) \
    U32TO8_LE((p), (u32)((v))); \
    U32TO8_LE((p) + 4, (u32)((v) >> 32));

#define U8TO64_LE(p) \
    (((u64)((p)[0])) | ((u64)((p)[1]) << 8) | \
     ((u64)((p)[2]) << 16) | ((u64)((p)[3]) << 24) | \
     ((u64)((p)[4]) << 32) | ((u64)((p)[5]) << 40) | \
     ((u64)((p)[6]) << 48) | ((u64)((p)[7]) << 56))

#define SIPROUND \
    do { \
        v0 += v1; \
        v1 = ROTL(v1, 13); \
        v1 ^= v0; \
        v0 = ROTL(v0, 32); \
        v2 += v3; \
        v3 = ROTL(v3, 16); \
        v3 ^= v2; \
        v0 += v3; \
        v3 = ROTL(v3, 21); \
        v3 ^= v0; \
        v2 += v1; \
        v1 = ROTL(v1, 17); \
        v1 ^= v2; \
        v2 = ROTL(v2, 32); \
    } while (0)

#define TRACE

SINLINE void
a_md_siphash(const void *in, const uz inlen, const void *k, u8 *out,
            const uz outlen, uz crounds, uz drounds) {

    u8 const *ni = (u8 const *)in;
    u8 const *kk = (u8 const *)k;

    ASSERT((outlen == 8) || (outlen == 16));
    u64 v0 = U64_C(0x736f6d6570736575);
    u64 v1 = U64_C(0x646f72616e646f6d);
    u64 v2 = U64_C(0x6c7967656e657261);
    u64 v3 = U64_C(0x7465646279746573);
    u64 k0 = U8TO64_LE(kk);
    u64 k1 = U8TO64_LE(kk + 8);
    u64 m;
    int i;
    u8 const *end = ni + inlen - (inlen % sizeof(u64));
    const int left = inlen & 7;
    u64 b = ((u64)inlen) << 56;
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    if (outlen == 16)
        v1 ^= 0xee;

    for (; ni != end; ni += 8) {
        m = U8TO64_LE(ni);
        v3 ^= m;

        TRACE;
        for (i = 0; i < crounds; ++i)
            SIPROUND;

        v0 ^= m;
    }

    switch (left) {
    case 7:
        b |= ((u64)ni[6]) << 48;
    case 6:
        b |= ((u64)ni[5]) << 40;
    case 5:
        b |= ((u64)ni[4]) << 32;
    case 4:
        b |= ((u64)ni[3]) << 24;
    case 3:
        b |= ((u64)ni[2]) << 16;
    case 2:
        b |= ((u64)ni[1]) << 8;
    case 1:
        b |= ((u64)ni[0]);
        break;
    case 0:
        break;
    }

    v3 ^= b;

    TRACE;
    for (i = 0; i < crounds; ++i)
        SIPROUND;

    v0 ^= b;

    if (outlen == 16)
        v2 ^= 0xee;
    else
        v2 ^= 0xff;

    TRACE;
    for (i = 0; i < drounds; ++i)
        SIPROUND;

    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out, b);

    if (outlen == 8)
        return;

    v1 ^= 0xdd;

    TRACE;
    for (i = 0; i < drounds; ++i)
        SIPROUND;

    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out + 8, b);

    return;
}
/* <<<PASTE */

/*
 * From the above via copy+paste, with minimal effort
 */

SINLINE s32
a_md_siphash_setup(struct su_siphash *self, const void *k){
    u8 const *kk = (u8 const *)k;

    u64 v0 = U64_C(0x736f6d6570736575);
    u64 v1 = U64_C(0x646f72616e646f6d);
    u64 v2 = U64_C(0x6c7967656e657261);
    u64 v3 = U64_C(0x7465646279746573);
    u64 k0 = U8TO64_LE(kk);
    u64 k1 = U8TO64_LE(kk + 8);
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    if(self->sh_digest == su_SIPHASH_DIGEST_128)
        v1 ^= 0xee;

   self->sh_v0 = v0;
   self->sh_v1 = v1;
   self->sh_v2 = v2;
   self->sh_v3 = v3;

    return su_STATE_NONE;
}

SINLINE void
a_md_siphash_update(struct su_siphash *self, const void *in, uz inlen){
    u8 const *ni = (u8 const *)in;

    u64 v0 = self->sh_v0;
    u64 v1 = self->sh_v1;
    u64 v2 = self->sh_v2;
    u64 v3 = self->sh_v3;
    u64 m;
    u32 i;

   self->sh_bytes += inlen;

   if((i = self->sh_carry_size) != 0){
      for(;;){
         --inlen;
         self->sh_carry[i++] = *ni++;
         if(i == su_SIPHASH_BLOCK_SIZE){
            self->sh_carry_size = 0;
            m = U8TO64_LE(self->sh_carry);
            goto jalgo;
         }else if(inlen == 0){
            self->sh_carry_size = i;
            goto jleave;
         }
      }
   }

   for(; inlen >= su_SIPHASH_BLOCK_SIZE;){
        m = U8TO64_LE(ni);
      inlen -= su_SIPHASH_BLOCK_SIZE;
      ni += su_SIPHASH_BLOCK_SIZE;
jalgo:
        v3 ^= m;

        for (i = 0; i < self->sh_compress_rounds; ++i)
            SIPROUND;

        v0 ^= m;
    }

   self->sh_v0 = v0;
   self->sh_v1 = v1;
   self->sh_v2 = v2;
   self->sh_v3 = v3;

   if(inlen > 0){
      self->sh_carry_size = S(u32,inlen);
      su_mem_copy(self->sh_carry, ni, inlen);
   }

jleave:;
}

SINLINE void
a_md_siphash_end(struct su_siphash *self, u8 *out){
    u64 v0 = self->sh_v0;
    u64 v1 = self->sh_v1;
    u64 v2 = self->sh_v2;
    u64 v3 = self->sh_v3;
    u64 b = self->sh_bytes << 56;
    u32 i;

    switch (self->sh_carry_size) {
    case 7:
        b |= ((u64)self->sh_carry[6]) << 48;
    case 6:
        b |= ((u64)self->sh_carry[5]) << 40;
    case 5:
        b |= ((u64)self->sh_carry[4]) << 32;
    case 4:
        b |= ((u64)self->sh_carry[3]) << 24;
    case 3:
        b |= ((u64)self->sh_carry[2]) << 16;
    case 2:
        b |= ((u64)self->sh_carry[1]) << 8;
    case 1:
        b |= ((u64)self->sh_carry[0]);
        break;
    case 0:
        break;
    }

    v3 ^= b;

    TRACE;
    for (i = 0; i < self->sh_compress_rounds; ++i)
        SIPROUND;

    v0 ^= b;

    if(self->sh_digest == su_SIPHASH_DIGEST_128)
        v2 ^= 0xee;
    else
        v2 ^= 0xff;

    TRACE;
    for (i = 0; i < self->sh_finalize_rounds; ++i)
        SIPROUND;

    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out, b);

    if(self->sh_digest == su_SIPHASH_DIGEST_64)
      goto jleave;

    v1 ^= 0xdd;

    TRACE;
    for (i = 0; i < self->sh_finalize_rounds; ++i)
        SIPROUND;

    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out + 8, b);

jleave:;
}

# undef cROUNDS
# undef dROUNDS
# undef ROTL
# undef U32TO8_LE
# undef U64TO8_LE
# undef U8TO64_LE
# undef SIPROUND
# undef TRACE

#elif su__MD_SIPHASH_Y == 2
# undef su__MD_SIPHASH_Y
# define su__MD_SIPHASH_Y 3

#else
# error .
#endif
/* s-it-mode */
