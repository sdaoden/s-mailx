/*@ ATOI and ITOA: simple non-restartable integer conversion.
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
#ifndef su_ICODEC_H
#define su_ICODEC_H

/*!
 * \file
 * \ingroup ICODEC
 * \brief \r{ICODEC}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/*!
 * \defgroup ICODEC Integer/String conversion
 * \ingroup TEXT
 * \brief Convert integers from and to strings (\r{su/icodec.h})
 * @{
 */

/*!
 * \defgroup IDEC Integers from Strings
 * \ingroup ICODEC
 * \brief Parsing integers out of string buffers (\r{su/icodec.h})
 * @{
 */

/*! \_ */
enum su_idec_mode{
   su_IDEC_MODE_NONE, /*!< \_ */
   /*! Will be used to choose limits, error constants, etc. */
   su_IDEC_MODE_SIGNED_TYPE = 1u<<0,
   /*! If a power-of-two is used explicitly, or if a \a{base} of 0 is used
    * and a known standard prefix is seen, enforce interpretation as unsigned.
    * This only makes a difference in conjunction with
    * \r{su_IDEC_MODE_SIGNED_TYPE}. */
   su_IDEC_MODE_POW2BASE_UNSIGNED = 1u<<1,
   /*! Relaxed \a{base} 0 convenience: if the input used \c{BASE#number} number
    * sign syntax, then the scan will be restarted anew with the base given.
    * Like this an UI can be permissive and support \c{s='  -008'; eval 10#$s}
    * out of the box (it would require a lot of logic otherwise). */
   su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN = 1u<<2,
#if 0
   su_IDEC_MODE_SIGN_FORCE_SIGNED_TYPE = 1u<<2,
#endif
   /*! Assume input is an 8-bit integer (limits, saturation, etc.). */
   su_IDEC_MODE_LIMIT_8BIT = 1u<<3,
   /*! Assume input is an 16-bit integer (limits, saturation, etc.). */
   su_IDEC_MODE_LIMIT_16BIT = 2u<<3,
   /*! Assume input is an 32-bit integer (limits, saturation, etc.). */
   su_IDEC_MODE_LIMIT_32BIT = 3u<<3,
   su__IDEC_MODE_LIMIT_MASK = 3u<<3,
   /*! Do not treat it as an error if the limit is excessed!
    * Like this saturated (and bit-limited) results can be created
    * (in that \r{su_IDEC_STATE_EOVERFLOW is suppressed). */
   su_IDEC_MODE_LIMIT_NOERROR = 1u<<5,
   /* These bits are duplicated in the _state result bits! */
   su__IDEC_MODE_MASK = (1u<<6) - 1
};

/*! \_ */
enum su_idec_state{
   su_IDEC_STATE_NONE, /*!< \_*/
   /*! Malformed input, no usable result has been stored. */
   su_IDEC_STATE_EINVAL = 1u<<8,
   /*! Bad character according to base, but we have seen some good ones
    * before, otherwise \r{su_IDEC_STATE_EINVAL} would have been used. */
   su_IDEC_STATE_EBASE = 2u<<8,
   su_IDEC_STATE_EOVERFLOW = 3u<<8, /*!< Result too large. */
   su_IDEC_STATE_EMASK = 3u<<8, /*!< All errors, that is. */
   su_IDEC_STATE_SEEN_MINUS = 1u<<16, /*!< Seen hyphen-minus in the input? */
   su_IDEC_STATE_CONSUMED = 1u<<17, /*!< All the input has been consumed. */
   su__IDEC_PRIVATE_SHIFT1 = 24u
};
MCTA(su__IDEC_MODE_MASK <= (1u<<8) - 1, "Shared bit range overlaps")

/*! Decode \a{clen} (or \r{su_cs_len()} if \r{su_UZ_MAX}) bytes of \a{cbuf}
 * into an integer according to the \r{su_idec_mode} \a{idec_mode},
 * store a/the result in \a{*resp} (in the \r{su_IDEC_STATE_EINVAL} case an
 * overflow constant is used, for signed types it depends on parse state
 * whether MIN/MAX are used), which must point to storage of the correct type,
 * return the resulting \r{su_idec_state} (which includes \a{idec_mode}).
 * If \a{endptr_or_nil} is will be pointed to the last parsed byte.
 * Base auto-detection can be enfored by setting \a{base} to 0. */
EXPORT u32 su_idec(void *resp, char const *cbuf, uz clen, u8 base,
      u32 idec_mode, char const **endptr_or_nil);

/*! \_ */
INLINE u32 su_idec_cp(void *resp, char const *cp, u8 base, u32 idec_mode,
      char const **endptr_or_nil){
   uz len = UZ_MAX;
   ASSERT_EXEC(cp != NIL, len = 0);
   return su_idec(resp, cp, len, base, idec_mode, endptr_or_nil);
}

/*! \_ */
#define su_idec_u8(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_8BIT), CLP)
/*! \_ */
#define su_idec_u8_cp(RP,CBP,B,CLP) su_idec_u8(RP,CBP,su_UZ_MAX,B,CLP)

/*! \_ */
#define su_idec_s8(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_8BIT), CLP)
/*! \_ */
#define su_idec_s8_cp(RP,CBP,B,CLP) su_idec_s8(RP,CBP,su_UZ_MAX,B,CLP)

/*! \_ */
#define su_idec_u16(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_16BIT), CLP)
/*! \_ */
#define su_idec_u16_cp(RP,CBP,B,CLP) su_idec_u16(RP,CBP,su_UZ_MAX,B,CLP)

/*! \_ */
#define su_idec_s16(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_16BIT), CLP)
/*! \_ */
#define su_idec_s16_cp(RP,CBP,B,CLP) su_idec_s16(RP,CBP,su_UZ_MAX,B,CLP)

/*! \_ */
#define su_idec_u32(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_32BIT), CLP)
/*! \_ */
#define su_idec_u32_cp(RP,CBP,B,CLP) su_idec_u32(RP,CBP,su_UZ_MAX,B,CLP)

/*! \_ */
#define su_idec_s32(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_32BIT), CLP)
/*! \_ */
#define su_idec_s32_cp(RP,CBP,B,CLP) su_idec_s32(RP,CBP,su_UZ_MAX,B,CLP)

/*! \_ */
#define su_idec_u64(RP,CBP,CL,B,CLP) su_idec(RP, CBP, CL, B, 0, CLP)
/*! \_ */
#define su_idec_u64_cp(RP,CBP,B,CLP) su_idec_u64(RP,CBP,su_UZ_MAX,B,CLP)

/*! \_ */
#define su_idec_s64(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_SIGNED_TYPE), CLP)
/*! \_ */
#define su_idec_s64_cp(RP,CBP,B,CLP) su_idec_s64(RP,CBP,su_UZ_MAX,B,CLP)

#if UZ_BITS == 32
   /*! \_ */
# define su_idec_uz(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_32BIT), CLP)
   /*! \_ */
# define su_idec_sz(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_32BIT), CLP)
#else
# define su_idec_uz(RP,CBP,CL,B,CLP) su_idec(RP, CBP, CL, B, 0, CLP)
# define su_idec_sz(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_SIGNED_TYPE), CLP)
#endif
/*! \_ */
#define su_idec_uz_cp(RP,CBP,B,CLP) su_idec_uz(RP,CBP,su_UZ_MAX,B,CLP)
/*! \_ */
#define su_idec_sz_cp(RP,CBP,B,CLP) su_idec_sz(RP,CBP,su_UZ_MAX,B,CLP)

#if UZ_BITS == 32
   /*! \_ */
# define su_idec_up(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_LIMIT_32BIT), CLP)
   /*! \_ */
# define su_idec_sp(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B,\
      (su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_LIMIT_32BIT), CLP)
#else
# define su_idec_up(RP,CBP,CL,B,CLP) su_idec(RP, CBP, CL, B, 0, CLP)
# define su_idec_sp(RP,CBP,CL,B,CLP) \
   su_idec(RP, CBP, CL, B, (su_IDEC_MODE_SIGNED_TYPE), CLP)
#endif
/*! \_ */
#define su_idec_up_cp(RP,CBP,B,CLP) su_idec_up(RP,CBP,su_UZ_MAX,B,CLP)
/*! \_ */
#define su_idec_sp_cp(RP,CBP,B,CLP) su_idec_sp(RP,CBP,su_UZ_MAX,B,CLP)

/*! @} */
/*!
 * \defgroup IENC Integers to Strings
 * \ingroup ICODEC
 * \brief Creating textual integer representations (\r{su/icodec.h})
 *
 * \remarks{The support macros simply cast the type to the given type,
 * therefore care for correct signedness extension etc. has to be taken.}
 * @{
 */

enum{
   /*! Maximum buffer size needed by \r{su_ienc()},
    * including \c{NUL} and base prefixes. */
   su_IENC_BUFFER_SIZE = 80u
};

/*! \_ */
enum su_ienc_mode{
   su_IENC_MODE_NONE, /*!< \_ */
   /*! Whether signedness correction shall be applied. */
   su_IENC_MODE_SIGNED_TYPE = 1u<<1,
   /*! Positive nubers shall have a plus-sign \c{+} prefix. */
   su_IENC_MODE_SIGNED_PLUS = 1u<<2,
   /*! Positive nubers shall have a space prefix.
    * Has a lower priority than \r{su_IENC_MODE_SIGNED_PLUS}. */
   su_IENC_MODE_SIGNED_SPACE = 1u<<3,
   /*! No base prefixes shall prepend the number, even if the conversion base
    * would normally place one. */
   su_IENC_MODE_NO_PREFIX = 1u<<4,
   /*! For bases greater ten (10), use lowercase letters instead of the default
    * uppercase.
    * This does not cover the base. */
   su_IENC_MODE_LOWERCASE = 1u<<5,

   su__IENC_MODE_SHIFT = 6u,
   su__IENC_MODE_MASK = (1u<<su__IENC_MODE_SHIFT) - 1
};

/*! Encode an integer value according to base (2-36) and \r{su_ienc_mode}
 * \a{ienc_mode}, return pointer to starting byte or \NIL on error.
 * An error only happens for an invalid base. */
EXPORT char *su_ienc(char cbuf[su_IENC_BUFFER_SIZE], u64 value, u8 base,
      u32 ienc_mode);

/*! \_ */
#define su_ienc_u8(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u8,VAL), B, su_IENC_MODE_NONE)
/*! \_ */
#define su_ienc_s8(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s8,VAL), B, su_IENC_MODE_SIGNED_TYPE)

/*! \_ */
#define su_ienc_u16(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u16,VAL), B, su_IENC_MODE_NONE)
/*! \_ */
#define su_ienc_s16(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s16,VAL), B, su_IENC_MODE_SIGNED_TYPE)

/*! \_ */
#define su_ienc_u32(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u32,VAL), B, su_IENC_MODE_NONE)
/*! \_ */
#define su_ienc_s32(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s32,VAL), B, su_IENC_MODE_SIGNED_TYPE)

/*! \_ */
#define su_ienc_u64(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_u64,VAL), B, su_IENC_MODE_NONE)
/*! \_ */
#define su_ienc_s64(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_s64,VAL), B, su_IENC_MODE_SIGNED_TYPE)

/*! \_ */
#define su_ienc_uz(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_uz,VAL), B, su_IENC_MODE_NONE)
/*! \_ */
#define su_ienc_sz(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_sz,VAL), B, su_IENC_MODE_SIGNED_TYPE)

/*! \_ */
#define su_ienc_up(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_up,VAL), B, su_IENC_MODE_NONE)
/*! \_ */
#define su_ienc_sp(CBP,VAL,B) \
   su_ienc(CBP, su_S(su_sp,VAL), B, su_IENC_MODE_SIGNED_TYPE)

/*! @} */
/*! @} */
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class idec;
class ienc;

/*!
 * \ingroup IDEC
 * C++ variant of \r{IDEC} (\r{su/icodec.h})
 */
class idec{
public:
   /*! \copydoc{su_idec_mode} */
   enum mode{
      /*! \copydoc{su_IDEC_MODE_NONE} */
      mode_none = su_IDEC_MODE_NONE,
      /*! \copydoc{su_IDEC_MODE_SIGNED_TYPE} */
      mode_signed_type = su_IDEC_MODE_SIGNED_TYPE,
      /*! \copydoc{su_IDEC_MODE_POW2BASE_UNSIGNED} */
      mode_pow2base_unsigned = su_IDEC_MODE_POW2BASE_UNSIGNED,
      /*! \copydoc{su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN} */
      mode_base0_number_sign_rescan = su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN,
      /*! \copydoc{su_IDEC_MODE_LIMIT_8BIT} */
      mode_limit_8bit = su_IDEC_MODE_LIMIT_8BIT,
      /*! \copydoc{su_IDEC_MODE_LIMIT_16BIT} */
      mode_limit_16bit = su_IDEC_MODE_LIMIT_16BIT,
      /*! \copydoc{su_IDEC_MODE_LIMIT_32BIT} */
      mode_limit_32bit = su_IDEC_MODE_LIMIT_32BIT,
      /*! \copydoc{su_IDEC_MODE_LIMIT_NOERROR} */
      mode_limit_noerror = su_IDEC_MODE_LIMIT_NOERROR
   };

   /*! \copydoc{su_idec_state} */
   enum state{
      /*! \copydoc{su_IDEC_STATE_NONE} */
      state_none = su_IDEC_STATE_NONE,
      /*! \copydoc{su_IDEC_STATE_EINVAL} */
      state_einval = su_IDEC_STATE_EINVAL,
      /*! \copydoc{su_IDEC_STATE_EBASE} */
      state_ebase = su_IDEC_STATE_EBASE,
      /*! \copydoc{su_IDEC_STATE_EOVERFLOW} */
      state_eoverflow = su_IDEC_STATE_EOVERFLOW,
      /*! \copydoc{su_IDEC_STATE_EMASK} */
      state_emask = su_IDEC_STATE_EMASK,
      /*! \copydoc{su_IDEC_STATE_SEEN_MINUS} */
      state_seen_minus = su_IDEC_STATE_SEEN_MINUS,
      /*! \copydoc{su_IDEC_STATE_CONSUMED} */
      state_consumed = su_IDEC_STATE_CONSUMED
   };

   /*! \copydoc{su_idec()} */
   static u32 convert(void *resp, char const *cbuf, uz clen, u8 base, u32 mode,
         char const **endptr_or_nil=NIL){
      return su_idec(resp, cbuf, clen, base, mode, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(void *resp, char const *cbuf, u8 base, u32 mode,
         char const **endptr_or_nil=NIL){
      return su_idec_cp(resp, cbuf, base, mode, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(u8 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u8(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(u8 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u8_cp(&resr, cbuf, base, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(s8 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s8(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(s8 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s8_cp(&resr, cbuf, base, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(u16 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u16(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(u16 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u16_cp(&resr, cbuf, base, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(s16 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s16(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(s16 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s16_cp(&resr, cbuf, base, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(u32 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u32(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(u32 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u32_cp(&resr, cbuf, base, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(s32 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s32(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(s32 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s32_cp(&resr, cbuf, base, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(u64 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u64(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(u64 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_u64_cp(&resr, cbuf, base, endptr_or_nil);
   }

   /*! \r{su_idec()} */
   static u32 convert(s64 &resr, char const *cbuf, uz clen, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s64(&resr, cbuf, clen, base, endptr_or_nil);
   }
   /*! \r{su_idec()} */
   static u32 convert(s64 &resr, char const *cbuf, u8 base,
         char const **endptr_or_nil=NIL){
      return su_idec_s64_cp(&resr, cbuf, base, endptr_or_nil);
   }
};

/*!
 * \ingroup IENC
 * C++ variant of \r{IENC} (\r{su/icodec.h})
 */
class ienc{
public:
   enum{
      /*! \copydoc{su_IENC_BUFFER_SIZE} */
      buffer_size = su_IENC_BUFFER_SIZE
   };

   /*! \copydoc{su_ienc_mode} */
   enum mode{
      /*! \copydoc{su_IENC_MODE_NONE} */
      mode_none = su_IENC_MODE_NONE,
      /*! \copydoc{su_IENC_MODE_SIGNED_TYPE} */
      mode_signed_type = su_IENC_MODE_SIGNED_TYPE,
      /*! \copydoc{su_IENC_MODE_SIGNED_PLUS} */
      mode_signed_plus = su_IENC_MODE_SIGNED_PLUS,
      /*! \copydoc{su_IENC_MODE_SIGNED_SPACE} */
      mode_signed_space = su_IENC_MODE_SIGNED_SPACE,
      /*! \copydoc{su_IENC_MODE_NO_PREFIX} */
      mode_no_prefix = su_IENC_MODE_NO_PREFIX,
      /*! \copydoc{su_IENC_MODE_LOWERCASE} */
      mode_lowercase = su_IENC_MODE_LOWERCASE
   };

   /*! \copydoc{su_ienc()} */
   static char *convert(char *cbuf, u64 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   /*! \r{su_ienc()} */
   static char *convert(char *cbuf, s64 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }

   /*! \r{su_ienc()} */
   static char *convert(char *cbuf, u32 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   /*! \r{su_ienc()} */
   static char *convert(char *cbuf, s32 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }

   /*! \r{su_ienc()} */
   static char *convert(char *cbuf, u16 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   /*! \r{su_ienc()} */
   static char *convert(char *cbuf, s16 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }

   /*! \r{su_ienc()} */
   static char *convert(char *cbuf, u8 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
   /*! \r{su_ienc()} */
   static char *convert(char *cbuf, s8 value, u8 base=10, u32 mode=mode_none){
      return su_ienc(cbuf, value, base, mode);
   }
};

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_ICODEC_H */
/* s-it-mode */
