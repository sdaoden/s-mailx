/*@ Byte order swapping.
 *
 * Copyright (c) 2001 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_BOSWAP_H
#define su_BOSWAP_H

/*!
 * \file
 * \ingroup BOSWAP
 * \brief \r{BOSWAP}
 */

#include <su/code.h>

#include <su/x-boswap.h> /* 1. */
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* boswap {{{ */
/*!
 * \defgroup BOSWAP Byte order swapping
 * \ingroup MISC
 * \brief Convertion in between byte orders (\r{su/boswap.h})
 * @{
 */

#include <su/x-boswap.h> /* 2. */

/*! Swap byte order of 16-bit integer. */
INLINE u16 su_boswap_16(u16 v) {return su__boswap_16(v);}

/*! Swap byte order of 32-bit integer. */
INLINE u32 su_boswap_32(u32 v) {return su__boswap_32(v);}

/*! Swap byte order of 64-bit integer. */
INLINE u64 su_boswap_64(u64 v) {return su__boswap_64(v);}

/*! Swap byte order of register-width integer. */
INLINE uz su_boswap_z(uz v) {return su_6432(su__boswap_64,su__boswap_32)(v);}

#undef a_X
#define a_X(X) if(su_BOM_IS_LITTLE()) v = su_CONCAT(su_boswap_,X)(v); return v

/*! Host and big endian byte order, 16-bit. */
INLINE u16 su_boswap_big_16(u16 v) {a_X(16);}

/*! Host and big endian byte order, 32-bit. */
INLINE u32 su_boswap_big_32(u32 v) {a_X(32);}

/*! Host and big endian byte order, 64-bit. */
INLINE u64 su_boswap_big_64(u64 v) {a_X(64);}

/*! Host and big endian byte order, register-width. */
INLINE uz su_boswap_big_z(uz v) {a_X(z);}

#undef a_X
#define a_X(X) if(su_BOM_IS_BIG()) v = su_CONCAT(su_boswap_,X)(v); return v

/*! Host and little endian byte order, 16-bit. */
INLINE u16 su_boswap_little_16(u16 v) {a_X(16);}

/*! Host and little endian byte order, 32-bit. */
INLINE u32 su_boswap_little_32(u32 v) {a_X(32);}

/*! Host and little endian byte order, 64-bit. */
INLINE u64 su_boswap_little_64(u64 v) {a_X(64);}

/*! Host and little endian byte order, register-width. */
INLINE uz su_boswap_little_z(uz v) {a_X(z);}

#undef a_X

/*! Host and network byte order, 16-bit. */
INLINE u16 su_boswap_net_16(u16 v) {return su_boswap_big_16(v);}

/*! Host and network byte order, 32-bit. */
INLINE u32 su_boswap_net_32(u32 v) {return su_boswap_big_32(v);}

/*! Host and network byte order, 64-bit. */
INLINE u64 su_boswap_net_64(u64 v) {return su_boswap_big_64(v);}

/*! Host and network byte order, register-width. */
INLINE uz su_boswap_net_z(uz v) {return su_boswap_big_z(v);}
/*! @} *//* }}} */

#include <su/x-boswap.h> /* 3. */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class boswap;

/* boswap {{{ */
/*!
 * \ingroup BOSWAP
 * C++ variant of \r{BOSWAP} (\r{su/boswap.h})
 */
class EXPORT boswap{
   su_CLASS_NO_COPY(boswap);
public:
   /*! \copydoc{su_boswap_16()} */
   static u16 swap_16(u16 v) {return su_boswap_16(v);}
   /*! \copydoc{su_boswap_32()} */
   static u32 swap_32(u32 v) {return su_boswap_32(v);}
   /*! \copydoc{su_boswap_64()} */
   static u64 swap_64(u64 v) {return su_boswap_64(v);}
   /*! \copydoc{su_boswap_z()} */
   static uz swap_z(uz v) {return su_boswap_z(v);}

   /*! \copydoc{su_boswap_big_16()} */
   static u16 big_16(u16 v) {return su_boswap_big_16(v);}
   /*! \copydoc{su_boswap_big_32()} */
   static u32 big_32(u32 v) {return su_boswap_big_32(v);}
   /*! \copydoc{su_boswap_big_64()} */
   static u64 big_64(u64 v) {return su_boswap_big_64(v);}
   /*! \copydoc{su_boswap_big_z()} */
   static uz big_z(uz v) {return su_boswap_big_z(v);}

   /*! \copydoc{su_boswap_little_16()} */
   static u16 little_16(u16 v) {return su_boswap_little_16(v);}
   /*! \copydoc{su_boswap_little_32()} */
   static u32 little_32(u32 v) {return su_boswap_little_32(v);}
   /*! \copydoc{su_boswap_little_64()} */
   static u64 little_64(u64 v) {return su_boswap_little_64(v);}
   /*! \copydoc{su_boswap_little_z()} */
   static uz little_z(uz v) {return su_boswap_little_z(v);}

   /*! \copydoc{su_boswap_net_16()} */
   static u16 net_16(u16 v) {return su_boswap_net_16(v);}
   /*! \copydoc{su_boswap_net_32()} */
   static u32 net_32(u32 v) {return su_boswap_net_32(v);}
   /*! \copydoc{su_boswap_net_64()} */
   static u64 net_64(u64 v) {return su_boswap_net_64(v);}
   /*! \copydoc{su_boswap_net_z()} */
   static uz net_z(uz v) {return su_boswap_net_z(v);}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_BOSWAP_H */
/* s-it-mode */
