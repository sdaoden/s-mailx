/*@ Atomic exchange and compare-and-swap operations.
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
#ifndef su_ATOMIC_H
#define su_ATOMIC_H

/*!
 * \file
 * \ingroup ATOMIC
 * \brief \r{ATOMIC}
 */

#include <su/code.h>

su_USECASE_MX_DISABLED

#include <su/x-atomic.h> /* 1. */
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* atomic {{{ */
/*!
 * \defgroup ATOMIC Atomic operations
 * \ingroup SMP
 * \brief Atomic operations (\r{su/atomic.h})
 *
 * An atomic operation is an operation which is executed by one thread at
 * a time only, no matter how many CPUs exist, and how many threads try to
 * perform the operation concurrently.
 * Atomic operations cannot be implemented in and through the C programming
 * language, but only on the hardware level.
 * Due to this detailed hardware (and compiler) knowledge is necessary to be
 * able to provide truly atomic operations.
 * Newer C standards also support atomic operations optionally: the functions
 * found here should be expected to be of a \c{memory_order_seq_cst} kind, in
 * standard terms.
 *
 * Due to these peculiarities the availability macro \r{su_ATOMIC_IS_REAL}
 * exists, and involved storage must use the \r{su_ATOMIC} qualifier.
 * Atomicity is ensured via a global mutex if no backing implementation exists.
 * Without \r{su_HAVE_MT} these act, non-atomically.
 *
 * \remarks{In general \SU expects that assigning a value to an integer type
 * smaller than or equal to \r{su_uz} (\r{su_UZ_BITS}) that has been qualified
 * as \r{su_ATOMIC} is atomic in that either the the storage contains the old
 * or the new value, but not a bitmix of both.}
 *
 * \head1{Busy atomic operations}
 *
 * If an atomic operation is performed in a loop until it succeeds the memory
 * storage will be beaten continuously, incurring cache noise and uselessly
 * consuming energy.
 * Hardware architectures exist which offer instructions to improve this
 * spinning behaviour, like introducing short delays, or pausing until some
 * memory cache effect is seen.
 * In order to make use of such improvements a \c{_busy_} interface series is
 * available.
 * @{
 */

/*! \def su_ATOMIC_IS_REAL
 * A boolean that indicates whether atomic operations are truly supported.
 * This means either there exists a special implementation, or at \SU compile
 * time there was ISO C11 (ISO9899:2011) available (and lack of
 * \c{__STDC_NO_ATOMICS__} announced availability). */

#if defined su_ATOMIC_IS_REAL || defined su_HAVE_MT
# define a_X(X,Y) \
EXPORT X CONCAT(su__atomic_g_xchg_,Y)(X ATOMIC *store, X nval);
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
# define a_X(X,Y) \
EXPORT X CONCAT(su__atomic_g_busy_xchg_,Y)(X ATOMIC *store, X nval);
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
# define a_X(X,Y) \
EXPORT boole CONCAT(su__atomic_g_cas_,Y)(X ATOMIC *store, X oval, X nval);
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
# define a_X(X,Y) \
EXPORT void CONCAT(su__atomic_g_busy_cas_,Y)(X ATOMIC *store, X oval, X nval);
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
#endif /* su_ATOMIC_IS_REAL || su_HAVE_MT */

#include <su/x-atomic.h> /* 2. */

#ifndef su_ATOMIC_IS_REAL
# define su_ATOMIC_IS_REAL 0
#endif

#ifndef su__ATOMIC_HAVE_FUNS /* {{{ */
# ifdef su_SOURCE_ATOMIC
#  error Combination will not work
# endif

# define a_X(X,Y) \
INLINE X CONCAT(su__atomic_xchg_,Y)(X ATOMIC *store, X nval){\
   return CONCAT(su__atomic_g_xchg_,Y)(store, nval);\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
# define a_X(X,Y) \
INLINE X CONCAT(su__atomic_busy_xchg_,Y)(X ATOMIC *store, X nval){\
   return CONCAT(su__atomic_g_busy_xchg_,Y)(store, nval);\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
# define a_X(X,Y) \
INLINE boole CONCAT(su__atomic_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   return CONCAT(su__atomic_g_cas_,Y)(store, oval, nval);\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
# define a_X(X,Y) \
INLINE void CONCAT(su__atomic_busy_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   CONCAT(su__atomic_g_busy_cas_,Y)(store, oval, nval);\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

# undef a_X
#endif /* su__ATOMIC_HAVE_FUNS }}} */

#undef su__ATOMIC_HAVE_FUNS

/*! Exchange \a{store} with \a{nval}, and return old value. */
INLINE u8 su_atomic_xchg_8(u8 ATOMIC *store, u8 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_xchg_8(store, nval);
}

/*! \copydoc{su_atomic_xchg_8()}. */
INLINE u16 su_atomic_xchg_16(u16 ATOMIC *store, u16 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_xchg_16(store, nval);
}

/*! \copydoc{su_atomic_xchg_8()}. */
INLINE u32 su_atomic_xchg_32(u32 ATOMIC *store, u32 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_xchg_32(store, nval);
}

/*! \copydoc{su_atomic_xchg_8()}. */
INLINE u64 su_atomic_xchg_64(u64 ATOMIC *store, u64 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_xchg_64(store, nval);
}

/*! \copydoc{su_atomic_xchg_8()}. */
INLINE uz su_atomic_xchg_z(uz ATOMIC *store, uz nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_xchg_z(store, nval);
}

/*! \copydoc{su_atomic_xchg_8()}. */
INLINE up su_atomic_xchg_p(up ATOMIC *store, up nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_xchg_p(store, nval);
}

/*! Loop as long as \a{store} equals \a{nval}, then return old value. */
INLINE u8 su_atomic_busy_xchg_8(u8 ATOMIC *store, u8 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_xchg_8(store, nval);
}

/*! \copydoc{su_atomic_busy_xchg_8()}. */
INLINE u16 su_atomic_busy_xchg_16(u16 ATOMIC *store, u16 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_busy_xchg_16(store, nval);
}

/*! \copydoc{su_atomic_busy_xchg_8()}. */
INLINE u32 su_atomic_busy_xchg_32(u32 ATOMIC *store, u32 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_busy_xchg_32(store, nval);
}

/*! \copydoc{su_atomic_xchg_8()}. */
INLINE u64 su_atomic_busy_xchg_64(u64 ATOMIC *store, u64 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_busy_xchg_64(store, nval);
}

/*! \copydoc{su_atomic_xchg_8()}. */
INLINE uz su_atomic_busy_xchg_z(uz ATOMIC *store, uz nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_busy_xchg_z(store, nval);
}

/*! \copydoc{su_atomic_busy_xchg_8()}. */
INLINE up su_atomic_busy_xchg_p(up ATOMIC *store, up nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_busy_xchg_p(store, nval);
}

/*! If \a{store} equals \a{oval} update it to \a{nval},
 * otherwise return \FAL0. */
INLINE boole su_atomic_cas_8(u8 ATOMIC *store, u8 oval, u8 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_cas_8(store, oval, nval);
}

/*! \copydoc{su_atomic_cas_8()}. */
INLINE boole su_atomic_cas_16(u16 ATOMIC *store, u16 oval, u16 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_cas_16(store, oval, nval);
}

/*! \copydoc{su_atomic_cas_8()}. */
INLINE boole su_atomic_cas_32(u32 ATOMIC *store, u32 oval, u32 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_cas_32(store, oval, nval);
}

/*! \copydoc{su_atomic_cas_8()}. */
INLINE boole su_atomic_cas_64(u64 ATOMIC *store, u64 oval, u64 nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_cas_64(store, oval, nval);
}

/*! \copydoc{su_atomic_cas_8()}. */
INLINE boole su_atomic_cas_z(uz ATOMIC *store, uz oval, uz nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_cas_z(store, oval, nval);
}

/*! \copydoc{su_atomic_cas_8()}. */
INLINE boole su_atomic_cas_p(up ATOMIC *store, up oval, up nval){
   ASSERT_RET(store != NIL, FAL0);
   return su__atomic_cas_p(store, oval, nval);
}

/*! Loop until \a{store} contains \a{oval}, then update it to \a{nval}. */
INLINE void su_atomic_busy_cas_8(u8 ATOMIC *store, u8 oval, u8 nval){
   ASSERT_RET_VOID(store != NIL);
   su__atomic_busy_cas_8(store, oval, nval);
}

/*! \copydoc{su_atomic_busy_cas_8()}. */
INLINE void su_atomic_busy_cas_16(u16 ATOMIC *store, u16 oval, u16 nval){
   ASSERT_RET_VOID(store != NIL);
   su__atomic_busy_cas_16(store, oval, nval);
}

/*! \copydoc{su_atomic_busy_cas_8()}. */
INLINE void su_atomic_busy_cas_32(u32 ATOMIC *store, u32 oval, u32 nval){
   ASSERT_RET_VOID(store != NIL);
   su__atomic_busy_cas_32(store, oval, nval);
}

/*! \copydoc{su_atomic_busy_cas_8()}. */
INLINE void su_atomic_busy_cas_64(u64 ATOMIC *store, u64 oval, u64 nval){
   ASSERT_RET_VOID(store != NIL);
   su__atomic_busy_cas_64(store, oval, nval);
}

/*! \copydoc{su_atomic_busy_cas_8()}. */
INLINE void su_atomic_busy_cas_z(uz ATOMIC *store, uz oval, uz nval){
   ASSERT_RET_VOID(store != NIL);
   su__atomic_busy_cas_z(store, oval, nval);
}

/*! \copydoc{su_atomic_busy_cas_8()}. */
INLINE void su_atomic_busy_cas_p(up ATOMIC *store, up oval, up nval){
   ASSERT_RET_VOID(store != NIL);
   su__atomic_busy_cas_p(store, oval, nval);
}
/*! @} *//* }}} */

#include <su/x-atomic.h> /* 3. */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class atomic;

/* atomic {{{ */
/*!
 * \ingroup ATOMIC
 * C++ variant of \r{ATOMIC} (\r{su/atomic.h})
 */
class atomic{
   su_CLASS_NO_COPY(atomic);
public:
   enum{
      is_real = su_ATOMIC_IS_REAL /*!< \copydoc{su_ATOMIC_IS_REAL} */
   };

   /*! \copydoc{su_atomic_xchg_8()} */
   static u8 xchg_8(u8 ATOMIC *store, u8 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_xchg_8(store, nval);
   }

   /*! \copydoc{su_atomic_xchg_16()} */
   static u16 xchg_16(u16 ATOMIC *store, u16 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_xchg_16(store, nval);
   }

   /*! \copydoc{su_atomic_xchg_32()} */
   static u32 xchg_32(u32 ATOMIC *store, u32 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_xchg_32(store, nval);
   }

   /*! \copydoc{su_atomic_xchg_64()} */
   static u64 xchg_64(u64 ATOMIC *store, u64 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_xchg_64(store, nval);
   }

   /*! \copydoc{su_atomic_xchg_z()} */
   static uz xchg_z(uz ATOMIC *store, uz nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_xchg_z(store, nval);
   }

   /*! \copydoc{su_atomic_xchg_p()} */
   static up xchg_p(up ATOMIC *store, up nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_xchg_p(store, nval);
   }

   /*! \copydoc{su_atomic_busy_xchg_8()} */
   static u8 busy_xchg_8(u8 ATOMIC *store, u8 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_busy_xchg_8(store, nval);
   }

   /*! \copydoc{su_atomic_busy_xchg_16()} */
   static u16 busy_xchg_16(u16 ATOMIC *store, u16 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_busy_xchg_16(store, nval);
   }

   /*! \copydoc{su_atomic_busy_xchg_32()} */
   static u32 busy_xchg_32(u32 ATOMIC *store, u32 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_busy_xchg_32(store, nval);
   }

   /*! \copydoc{su_atomic_busy_xchg_64()} */
   static u64 busy_xchg_64(u64 ATOMIC *store, u64 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_busy_xchg_64(store, nval);
   }

   /*! \copydoc{su_atomic_busy_xchg_z()} */
   static uz busy_xchg_z(uz ATOMIC *store, uz nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_busy_xchg_z(store, nval);
   }

   /*! \copydoc{su_atomic_busy_xchg_p()} */
   static up busy_xchg_p(up ATOMIC *store, up nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_busy_xchg_p(store, nval);
   }

   /*! \copydoc{su_atomic_cas_8()} */
   static boole cas_8(u8 ATOMIC *store, u8 oval, u8 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_cas_8(store, oval, nval);
   }

   /*! \copydoc{su_atomic_cas_16()} */
   static boole cas_16(u16 ATOMIC *store, u16 oval, u16 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_cas_16(store, oval, nval);
   }

   /*! \copydoc{su_atomic_cas_32()} */
   static boole cas_32(u32 ATOMIC *store, u32 oval, u32 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_cas_32(store, oval, nval);
   }

   /*! \copydoc{su_atomic_cas_64()} */
   static boole cas_64(u64 ATOMIC *store, u64 oval, u64 nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_cas_64(store, oval, nval);
   }

   /*! \copydoc{su_atomic_cas_z()} */
   static boole cas_z(uz ATOMIC *store, uz oval, uz nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_cas_z(store, oval, nval);
   }

   /*! \copydoc{su_atomic_cas_p()} */
   static boole cas_p(up ATOMIC *store, up oval, up nval){
      ASSERT_RET(store != NIL, FAL0);
      return su_atomic_cas_p(store, oval, nval);
   }

   /*! \copydoc{su_atomic_busy_cas_8()} */
   static void busy_cas_8(u8 ATOMIC *store, u8 oval, u8 nval){
      ASSERT_RET_VOID(store != NIL);
      su_atomic_busy_cas_8(store, oval, nval);
   }

   /*! \copydoc{su_atomic_busy_cas_16()} */
   static void busy_cas_16(u16 ATOMIC *store, u16 oval, u16 nval){
      ASSERT_RET_VOID(store != NIL);
      su_atomic_busy_cas_16(store, oval, nval);
   }

   /*! \copydoc{su_atomic_busy_cas_32()} */
   static void busy_cas_32(u32 ATOMIC *store, u32 oval, u32 nval){
      ASSERT_RET_VOID(store != NIL);
      su_atomic_busy_cas_32(store, oval, nval);
   }

   /*! \copydoc{su_atomic_busy_cas_64()} */
   static void busy_cas_64(u64 ATOMIC *store, u64 oval, u64 nval){
      ASSERT_RET_VOID(store != NIL);
      su_atomic_busy_cas_64(store, oval, nval);
   }

   /*! \copydoc{su_atomic_busy_cas_z()} */
   static void busy_cas_z(uz ATOMIC *store, uz oval, uz nval){
      ASSERT_RET_VOID(store != NIL);
      su_atomic_busy_cas_z(store, oval, nval);
   }

   /*! \copydoc{su_atomic_busy_cas_p()} */
   static void busy_cas_p(up ATOMIC *store, up oval, up nval){
      ASSERT_RET_VOID(store != NIL);
      su_atomic_busy_cas_p(store, oval, nval);
   }
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_ATOMIC_H */
/* s-it-mode */
