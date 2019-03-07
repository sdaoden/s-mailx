/*@ ATOI and ITOA: simple non-restartable integer conversion.
 *
 * Copyright (c) 2017 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_ICODEC_H
#define su_ICODEC_H
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
enum su_idec_mode{
   su_IDEC_MODE_NONE,
   su_IDEC_MODE_SIGNED_TYPE = 1u<<0,
   su_IDEC_MODE_POW2BASE_UNSIGNED = 1u<<1,
   su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN = 1u<<2,
#if 0
   su_IDEC_MODE_SIGN_FORCE_SIGNED_TYPE = 1u<<2,
#endif
   su_IDEC_MODE_LIMIT_8BIT = 1u<<3,
   su_IDEC_MODE_LIMIT_16BIT = 2u<<3,
   su_IDEC_MODE_LIMIT_32BIT = 3u<<3,
   su__IDEC_MODE_LIMIT_MASK = 3u<<3,
   su_IDEC_MODE_LIMIT_NOERROR = 1u<<5,
   /* These bits are duplicated in the _state result bits! */
   su__IDEC_MODE_MASK = (1u<<6) - 1
};
enum su_idec_state{
   su_IDEC_STATE_NONE,
   su_IDEC_STATE_EINVAL = 1u<<8,
   su_IDEC_STATE_EBASE = 2u<<8,
   su_IDEC_STATE_EOVERFLOW = 3u<<8,
   su_IDEC_STATE_EMASK = 3u<<8,
   su_IDEC_STATE_SEEN_MINUS = 1u<<16,
   su_IDEC_STATE_CONSUMED = 1u<<17,
   su__IDEC_PRIVATE_SHIFT1 = 24u
};
MCTA(su__IDEC_MODE_MASK <= (1u<<8) - 1, "Shared bit range overlaps")
EXPORT u32 su_idec(void *resp, char const *cbuf, uz clen, u8 base,
      u32 idec_mode, char const **endptr_or_nil);
INLINE u32 su_idec_cp(void *resp, char const *cp, u8 base, u32 idec_mode,
      char const **endptr_or_nil){
   uz len = UZ_MAX;
   ASSERT_EXEC(cp != NIL, len = 0);
   return su_idec(resp, cp, len, base, idec_mode, endptr_or_nil);
}
#define su_idec_u8(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_8BIT), CLP)
#define su_idec_u8_cp(RP,CBP,B,CLP) su_idec_u8(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_s8(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_8BIT), CLP)
#define su_idec_s8_cp(RP,CBP,B,CLP) su_idec_s8(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_u16(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_16BIT), CLP)
#define su_idec_u16_cp(RP,CBP,B,CLP) su_idec_u16(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_s16(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_16BIT), CLP)
#define su_idec_s16_cp(RP,CBP,B,CLP) su_idec_s16(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_u32(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_32BIT), CLP)
#define su_idec_u32_cp(RP,CBP,B,CLP) su_idec_u32(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_s32(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_32BIT), CLP)
#define su_idec_s32_cp(RP,CBP,B,CLP) su_idec_s32(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_u64(RP,CBP,CL,B,CLP) su_idec(RP, CBP, CL, B, 0, CLP)
#define su_idec_u64_cp(RP,CBP,B,CLP) su_idec_u64(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_s64(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_SIGNED_TYPE), CLP)
#define su_idec_s64_cp(RP,CBP,B,CLP) su_idec_s64(RP,CBP,su_UZ_MAX,B,CLP)
#if UZ_BITS == 32
# define su_idec_uz(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_32BIT), CLP)
# define su_idec_sz(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_32BIT), CLP)
#else
# define su_idec_uz(RP,CBP,CL,B,CLP) su_idec(RP, CBP, CL, B, 0, CLP)
# define su_idec_sz(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_SIGNED_TYPE), CLP)
#endif
#define su_idec_uz_cp(RP,CBP,B,CLP) su_idec_uz(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_sz_cp(RP,CBP,B,CLP) su_idec_sz(RP,CBP,su_UZ_MAX,B,CLP)
#if UZ_BITS == 32
# define su_idec_up(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_32BIT), CLP)
# define su_idec_sp(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_32BIT), CLP)
#else
# define su_idec_up(RP,CBP,CL,B,CLP) su_idec(RP, CBP, CL, B, 0, CLP)
# define su_idec_sp(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_SIGNED_TYPE), CLP)
#endif
#define su_idec_up_cp(RP,CBP,B,CLP) su_idec_up(RP,CBP,su_UZ_MAX,B,CLP)
#define su_idec_sp_cp(RP,CBP,B,CLP) su_idec_sp(RP,CBP,su_UZ_MAX,B,CLP)
enum{
   su_IENC_BUFFER_SIZE = 80u
};
enum su_ienc_mode{
   su_IENC_MODE_NONE,
   su_IENC_MODE_SIGNED_TYPE = 1u<<1,
   su_IENC_MODE_SIGNED_PLUS = 1u<<2,
   su_IENC_MODE_SIGNED_SPACE = 1u<<3,
   su_IENC_MODE_NO_PREFIX = 1u<<4,
   su_IENC_MODE_LOWERCASE = 1u<<5,
   su__IENC_MODE_SHIFT = 6u,
   su__IENC_MODE_MASK = (1u<<su__IENC_MODE_SHIFT) - 1
};
EXPORT char *su_ienc(char cbuf[su_IENC_BUFFER_SIZE], u64 value, u8 base,
      u32 ienc_mode);
#define su_ienc_u8(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u8,VAL), B, su_IENC_MODE_NONE)
#define su_ienc_s8(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s8,VAL), B, su_IENC_MODE_SIGNED_TYPE)
#define su_ienc_u16(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u16,VAL), B, su_IENC_MODE_NONE)
#define su_ienc_s16(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s16,VAL), B, su_IENC_MODE_SIGNED_TYPE)
#define su_ienc_u32(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u32,VAL), B, su_IENC_MODE_NONE)
#define su_ienc_s32(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s32,VAL), B, su_IENC_MODE_SIGNED_TYPE)
#define su_ienc_u64(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u64,VAL), B, su_IENC_MODE_NONE)
#define su_ienc_s64(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s64,VAL), B, su_IENC_MODE_SIGNED_TYPE)
#define su_ienc_uz(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_uz,VAL), B, su_IENC_MODE_NONE)
#define su_ienc_sz(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_sz,VAL), B, su_IENC_MODE_SIGNED_TYPE)
#define su_ienc_up(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_up,VAL), B, su_IENC_MODE_NONE)
#define su_ienc_sp(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_sp,VAL), B, su_IENC_MODE_SIGNED_TYPE)
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)
class idec;
class ienc;
class idec{
public:
   enum mode{
      mode_none = su_IDEC_MODE_NONE,
      mode_signed_type = su_IDEC_MODE_SIGNED_TYPE,
      mode_pow2base_unsigned = su_IDEC_MODE_POW2BASE_UNSIGNED,
      mode_base0_number_sign_rescan = su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN,
      mode_limit_8bit = su_IDEC_MODE_LIMIT_8BIT,
      mode_limit_16bit = su_IDEC_MODE_LIMIT_16BIT,
      mode_limit_32bit = su_IDEC_MODE_LIMIT_32BIT,
      mode_limit_noerror = su_IDEC_MODE_LIMIT_NOERROR
   };
   enum state{
      state_none = su_IDEC_STATE_NONE,
      state_einval = su_IDEC_STATE_EINVAL,
      state_ebase = su_IDEC_STATE_EBASE,
      state_eoverflow = su_IDEC_STATE_EOVERFLOW,
      state_emask = su_IDEC_STATE_EMASK,
      state_seen_minus = su_IDEC_STATE_SEEN_MINUS,
      state_consumed = su_IDEC_STATE_CONSUMED
   };
   static u32 convert(void *resp, char const *cbuf, uz clen, u8 base, u32 mode,
         char const **endptr_or_nil=NIL){
      return su_idec(resp, cbuf, clen, base, mode, endptr_or_nil);
   }
   static u32 convert(void *resp, char const *cbuf, u8 base, u32 mode,
         char const **endptr_or_nil=NIL){
      return su_idec_cp(resp, cbuf, base, mode, endptr_or_nil);
   }
   static u32 convert(u8 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u8(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(u8 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u8_cp(&resr, cbuf, base, endptr_or_nil);
   }
   static u32 convert(s8 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s8(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(s8 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s8_cp(&resr, cbuf, base, endptr_or_nil);
   }
   static u32 convert(u16 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u16(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(u16 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u16_cp(&resr, cbuf, base, endptr_or_nil);
   }
   static u32 convert(s16 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s16(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(s16 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s16_cp(&resr, cbuf, base, endptr_or_nil);
   }
   static u32 convert(u32 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u32(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(u32 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u32_cp(&resr, cbuf, base, endptr_or_nil);
   }
   static u32 convert(s32 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s32(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(s32 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s32_cp(&resr, cbuf, base, endptr_or_nil);
   }
   static u32 convert(u64 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u64(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(u64 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u64_cp(&resr, cbuf, base, endptr_or_nil);
   }
   static u32 convert(s64 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s64(&resr, cbuf, clen, base, endptr_or_nil);
   }
   static u32 convert(s64 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s64_cp(&resr, cbuf, base, endptr_or_nil);
   }
};
class ienc{
public:
   enum{
      buffer_size = su_IENC_BUFFER_SIZE
   };
   enum mode{
      mode_none = su_IENC_MODE_NONE,
      mode_signed_type = su_IENC_MODE_SIGNED_TYPE,
      mode_signed_plus = su_IENC_MODE_SIGNED_PLUS,
      mode_signed_space = su_IENC_MODE_SIGNED_SPACE,
      mode_no_prefix = su_IENC_MODE_NO_PREFIX,
      mode_lowercase = su_IENC_MODE_LOWERCASE
   };
   static char *convert(char *cbuf, u64 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   static char *convert(char *cbuf, s64 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   static char *convert(char *cbuf, u32 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   static char *convert(char *cbuf, s32 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   static char *convert(char *cbuf, u16 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   static char *convert(char *cbuf, s16 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   static char *convert(char *cbuf, u8 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   static char *convert(char *cbuf, s8 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
};
NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_ICODEC_H */
/* s-it-mode */
